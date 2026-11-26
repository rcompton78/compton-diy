from __future__ import annotations

from pathlib import Path

from asset_generation.paths import ROOT
from product_config import load_product


def render_device_packages_yaml(device: dict[str, object], project_name: str) -> str:
    """Render a device's packages.yaml.

    project_name is project.package_name from the contract. It becomes the
    project_name substitution that common/addon/firmware_update.yaml uses for
    esphome.project.name, so every device build reports the same project.
    """
    name = str(device.get("name", "")).strip()
    project_name = project_name.strip()
    if not project_name:
        raise RuntimeError("project.package_name is required to render device packages")
    substitutions = device.get("package_substitutions", {})
    package_includes = device.get("package_includes", [])
    if not isinstance(substitutions, dict):
        raise RuntimeError(f"Device {name or '<missing>'} package_substitutions must be an object")
    if not isinstance(package_includes, list) or not package_includes:
        raise RuntimeError(f"Device {name or '<missing>'} package_includes must be a non-empty list")

    lines = [
        "# ESPFRAME: generated from product/contract; run `npm run generate` to update.",
        "",
        "substitutions:",
        f'  project_name: "{project_name}"',
    ]
    for key, value in substitutions.items():
        key_text = str(key).strip()
        if not key_text:
            raise RuntimeError(f"Device {name or '<missing>'} has a blank package substitution key")
        if key_text == "project_name":
            raise RuntimeError(f"Device {name or '<missing>'} must not set project_name; it comes from project.package_name")
        lines.append(f'  {key_text}: "{value}"')

    lines.extend(["", "packages:"])
    for item in package_includes:
        if not isinstance(item, dict):
            raise RuntimeError(f"Device {name or '<missing>'} package_includes entries must be objects")
        alias = str(item.get("alias", "")).strip()
        path = str(item.get("path", "")).strip()
        if not alias or not path:
            raise RuntimeError(f"Device {name or '<missing>'} package_includes entries need alias and path")
        if Path(path).is_absolute():
            raise RuntimeError(f"Device {name or '<missing>'} package include {alias} must be relative")
        lines.append(f"  {alias}: !include {path}")

    return "\n".join(lines) + "\n"


def generated_device_package_files(product: dict[str, object] | None = None) -> dict[Path, str]:
    data = product if product is not None else load_product()
    project_name = str(data["project"].get("package_name", ""))
    result: dict[Path, str] = {}
    for device in data["devices"]:
        package_yaml = str(device.get("package_yaml", "")).strip()
        if not package_yaml:
            raise RuntimeError(f"Device {device.get('slug', '<missing>')} is missing package_yaml")
        result[ROOT / package_yaml] = render_device_packages_yaml(device, project_name)
    return result
