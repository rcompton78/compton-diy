"""Headless preview renders with matplotlib (no OpenGL needed)."""

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from mpl_toolkits.mplot3d.art3d import Poly3DCollection  # noqa: E402

LIGHT = np.array([0.4, -0.6, 0.7])
LIGHT = LIGHT / np.linalg.norm(LIGHT)


def _mesh(shape, tol=0.25):
    verts, tris = shape.tessellate(tol, 0.3)
    v = np.array([[q.X, q.Y, q.Z] for q in verts])
    t = np.array(tris)
    return _subdivide(v[t])


def _subdivide(tri, max_edge=6.0):
    """Split long triangles so per-triangle depth sorting stays accurate (painter's algorithm)."""
    out = []
    todo = tri
    for _ in range(6):
        if not len(todo):
            break
        e = np.max(np.linalg.norm(todo - np.roll(todo, 1, axis=1), axis=2), axis=1)
        small, big = todo[e <= max_edge], todo[e > max_edge]
        out.append(small)
        if not len(big):
            todo = big
            break
        a, b, c = big[:, 0], big[:, 1], big[:, 2]
        ab, bc, ca = (a + b) / 2, (b + c) / 2, (c + a) / 2
        todo = np.concatenate([np.stack(q, axis=1) for q in ((a, ab, ca), (ab, b, bc), (ca, bc, c), (ab, bc, ca))])
    out.append(todo)
    return np.concatenate([o for o in out if len(o)]) if out else tri


def render(items, path, view=(20, -60), title=None, lines=(), size=(9, 7), ortho=True, zshade=False):
    """items: [(shape, (r, g, b), alpha)]; lines: [(Nx3 array, colour, width)].

    All triangles of all items go into one collection so matplotlib depth-sorts them
    individually; per-item collections are only sorted as whole objects, which paints
    hidden parts (e.g. bosses behind the glass) over nearer ones.
    """
    fig = plt.figure(figsize=size, dpi=150)
    # Depth-sort parts unless a line (the cable) must be drawn on top of them.
    ax = fig.add_subplot(111, projection="3d", computed_zorder=not lines)
    allpts, all_tri, all_fc = [], [], []
    for shape, colour, alpha in items:
        tri = _mesh(shape)
        if not len(tri):
            continue
        n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
        n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
        shade = 0.45 + 0.55 * np.abs(n @ LIGHT)
        if zshade:   # plan views: darker = lower, so pockets and floors read clearly
            zc = tri[:, :, 2].mean(1)
            shade = 0.35 + 0.65 * (zc - zc.min()) / (np.ptp(zc) + 1e-9)
        fc = np.clip(np.array(colour)[None, :] * shade[:, None], 0, 1)
        all_tri.append(tri)
        all_fc.append(np.c_[fc, np.full(len(fc), alpha)])
        allpts.append(tri.reshape(-1, 3))
    if all_tri:
        fc = np.concatenate(all_fc)
        # Edges in the face colour close the anti-aliasing seams between neighbouring
        # triangles, which would otherwise let parts behind show through as hairlines.
        coll = Poly3DCollection(np.concatenate(all_tri), facecolors=fc, edgecolors=fc, linewidths=0.35)
        ax.add_collection3d(coll)
    for pts, colour, width in lines:
        pts = np.asarray(pts)
        ax.plot(pts[:, 0], pts[:, 1], pts[:, 2], color=colour, linewidth=width, zorder=10)
        allpts.append(pts)
    pts = np.vstack(allpts)
    lo, hi = pts.min(0), pts.max(0)
    ax.set_xlim(lo[0], hi[0])
    ax.set_ylim(lo[1], hi[1])
    ax.set_zlim(lo[2], hi[2])
    ax.set_box_aspect(hi - lo + 1e-6)
    if ortho:
        ax.set_proj_type("ortho")
    ax.view_init(elev=view[0], azim=view[1])
    ax.set_axis_off()
    if title:
        ax.set_title(title, fontsize=11)
    fig.tight_layout()
    fig.savefig(path, facecolor="white")
    plt.close(fig)
