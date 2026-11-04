"""Reference bodies (not printed): the CrowPanel in its acrylic case, from the STEP numbers in params,
plus the keep-out zones the checks test against."""

from build123d import Axis, fillet

from . import params as p
from .geom import box, dcyl, step_box, step_d, step_x, step_z


def case_outline():
    """The acrylic case's outline block, R4.5 plan corners."""
    body = box(-p.CASE_W / 2, p.CASE_W / 2, p.D_CASE_BACK, p.D_CASE_FRONT, p.Z_CASE0, p.Z_CASE1)
    return fillet(body.edges().filter_by(Axis.Y), p.CASE_R)


def heads():
    """The four M2.5 pan heads standing proud of the back plate."""
    out = None
    for xs, ys in p.STEP_HEADS:
        h = dcyl(step_x(ys), step_z(xs), p.D_CASE_BACK - p.HEAD_H, p.D_CASE_BACK, p.HEAD_D)
        out = h if out is None else out + h
    return out


def flex_tab():
    """The LCD flex tab that pokes 0.35 mm out of the case's left side."""
    return step_box(*p.STEP_FLEX_TAB)


def tf_card():
    """A TF card in its socket (it stands 0.08 mm proud of the case's right side)."""
    return step_box(*p.STEP_TF_CARD)


def case():
    """Everything of the case and board that the cradle must clear."""
    return case_outline() + heads() + flex_tab() + tf_card()


def glass():
    """The glass, 1.93 mm behind the case face, filling the window (for renders)."""
    return step_box(p.STEP_WINDOW_X, p.STEP_WINDOW_Y, (p.STEP_GLASS_FRONT_Z - 0.3, p.STEP_GLASS_FRONT_Z))


def window_column(margin=0.0):
    """The case window grown by `margin` and run from the glass out into the room."""
    x0, x1 = sorted((step_x(p.STEP_WINDOW_Y[0]), step_x(p.STEP_WINDOW_Y[1])))
    z0, z1 = step_z(p.STEP_WINDOW_X[0]), step_z(p.STEP_WINDOW_X[1])
    return box(x0 - margin, x1 + margin, step_d(p.STEP_GLASS_FRONT_Z), p.D_FRONT + 20, z0 - margin, z1 + margin)


def active_column():
    """The LCD's active area, centred on the window, run out into the room."""
    xc = (step_x(p.STEP_WINDOW_Y[0]) + step_x(p.STEP_WINDOW_Y[1])) / 2
    zc = (step_z(p.STEP_WINDOW_X[0]) + step_z(p.STEP_WINDOW_X[1])) / 2
    return box(xc - p.ACTIVE_H / 2, xc + p.ACTIVE_H / 2, step_d(p.STEP_GLASS_FRONT_Z), p.D_FRONT + 20,
               zc - p.ACTIVE_W / 2, zc + p.ACTIVE_W / 2)


# --- USB-C plug ------------------------------------------------------------------------------------
def usb_axis():
    """(X, d) of the USB-C receptacle's axis."""
    (_, _), (y0, y1), (z0, z1) = p.STEP_USB
    return step_x((y0 + y1) / 2), step_d((z0 + z1) / 2)


def plug():
    """The reserved overmold, from the case's bottom edge down PLUG_L."""
    x, d = usb_axis()
    return box(x - p.PLUG_W / 2, x + p.PLUG_W / 2, d - p.PLUG_T / 2, d + p.PLUG_T / 2,
               p.Z_CASE0 - p.PLUG_L, p.Z_CASE0)


def plug_keepout():
    """What must stay clear for the plug: the overmold plus PLUG_CLEAR through the floor, then a
    zone PLUG_BEND_MARGIN wider down to PLUG_BEND_CLEAR below the case, plus room for the cable to
    bend back to the wall below the overmold's end."""
    x, d = usb_axis()
    hw, ht = p.PLUG_W / 2, p.PLUG_T / 2
    through = box(x - hw - p.PLUG_CLEAR, x + hw + p.PLUG_CLEAR, d - ht - p.PLUG_CLEAR, d + ht + p.PLUG_CLEAR,
                  0.0, p.Z_CASE0)
    m = p.PLUG_CLEAR + p.PLUG_BEND_MARGIN
    below = box(x - hw - m, x + hw + m, d - ht - m, d + ht + m, p.Z_CASE0 - p.PLUG_BEND_CLEAR, 0.0)
    bend = box(x - hw - m, x + hw + m, 0.0, d + ht + m, p.Z_CASE0 - p.PLUG_BEND_CLEAR, p.Z_CASE0 - p.PLUG_L)
    return through + below + bend


# --- TF slot, buttons, antenna --------------------------------------------------------------------
def tf_slot_column():
    """The case's TF slot opening, run out through the right wall and 10 mm beyond."""
    x_edge = p.CASE_W / 2
    return box(x_edge - 0.5, p.X_OUT + 10, step_d(p.STEP_SPACER_Z[0]), step_d(p.STEP_SPACER_Z[1]),
               step_z(p.STEP_TF_SLOT_X[0]), step_z(p.STEP_TF_SLOT_X[1]))


def antenna_zone():
    """The module's antenna end (its outer ANTENNA_L mm), through the case's full depth."""
    (x0, x1), (y0, y1), _ = p.STEP_MODULE
    return box(step_x(y1), step_x(y0), p.D_CASE_BACK, p.D_CASE_FRONT, step_z(x1 - p.ANTENNA_L), step_z(x1))
