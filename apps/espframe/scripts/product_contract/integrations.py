from __future__ import annotations

import re

from product_contract.common import ROOT, read, require_contains


def check_immich_api_key_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    mode = str(project.get("immich_api_key_mode", "")).strip()
    privacy_promise = str(project.get("immich_api_key_privacy_promise", "")).strip()
    permissions = project.get("immich_api_key_permissions", [])

    api_key_docs = read(ROOT / "docs" / "api-key.md", errors)
    troubleshooting_docs = read(ROOT / "docs" / "troubleshooting.md", errors)
    immich_photo_frame_docs = read(ROOT / "docs" / "immich-photo-frame.md", errors)

    if mode:
        require_contains(api_key_docs, f"{mode} API key", "docs/api-key.md", errors)
        require_contains(troubleshooting_docs, f"{mode} Immich API key", "docs/troubleshooting.md", errors)
        require_contains(immich_photo_frame_docs, f"{mode.capitalize()} permissions are recommended", "docs/immich-photo-frame.md", errors)
    if privacy_promise:
        require_contains(api_key_docs, privacy_promise, "docs/api-key.md", errors)

    if not isinstance(permissions, list):
        return

    expected_names: list[str] = []
    for permission in permissions:
        if not isinstance(permission, dict):
            continue
        name = str(permission.get("name", "")).strip()
        purpose = str(permission.get("purpose", "")).strip()
        if not name or not purpose:
            continue
        expected_names.append(name)
        require_contains(api_key_docs, f"| `{name}` | {purpose} |", "docs/api-key.md", errors)

    documented_names = re.findall(r"^\| `([^`]+)` \|", api_key_docs, flags=re.MULTILINE)
    if documented_names != expected_names:
        errors.append(
            "docs/api-key.md permission rows must match project.immich_api_key_permissions "
            f"(expected {expected_names}, found {documented_names})"
        )


def check_immich_connection_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    schemes = project.get("immich_server_url_schemes", [])
    targets = project.get("immich_server_url_targets", [])
    examples = project.get("immich_server_url_examples", [])

    install_docs = read(ROOT / "docs" / "install.md", errors)
    api_key_docs = read(ROOT / "docs" / "api-key.md", errors)
    troubleshooting_docs = read(ROOT / "docs" / "troubleshooting.md", errors)

    docs_to_check = (
        ("docs/install.md", install_docs),
        ("docs/api-key.md", api_key_docs),
        ("docs/troubleshooting.md", troubleshooting_docs),
    )
    for field_name, values in (
        ("immich_server_url_schemes", schemes),
        ("immich_server_url_targets", targets),
        ("immich_server_url_examples", examples),
    ):
        if not isinstance(values, list):
            continue
        for label, text in docs_to_check:
            for value in values:
                if isinstance(value, str) and value.strip():
                    require_contains(text, value.strip(), f"{label} {field_name}", errors)


def check_home_assistant_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    name = str(project.get("home_assistant_name", "")).strip()
    url = str(project.get("home_assistant_url", "")).strip()
    requirement = str(project.get("home_assistant_requirement", "")).strip()
    platform = str(project.get("home_assistant_integration_platform", "")).strip()
    features = project.get("home_assistant_integration_features", [])
    network_entities = project.get("home_assistant_network_entities", [])
    diagnostic_entities = project.get("home_assistant_diagnostic_entities", [])
    debug_update_interval = str(project.get("device_debug_update_interval", "")).strip()
    wifi_strength_source = str(project.get("network_wifi_strength_source", "")).strip()
    wifi_strength_update_interval = str(project.get("network_wifi_strength_update_interval", "")).strip()

    readme = read(ROOT / "README.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    immich_photo_frame_docs = read(ROOT / "docs" / "immich-photo-frame.md", errors)
    home_assistant_docs = read(ROOT / "docs" / "home-assistant.md", errors)
    network_yaml = read(ROOT / "common" / "addon" / "network.yaml", errors)
    device_yaml_path = "devices/guition-esp32-p4-jc8012p4a1/device/device.yaml"
    device_yaml = read(ROOT / device_yaml_path, errors)

    docs_to_check = (
        ("README.md", readme),
        ("docs/index.md", index_docs),
        ("docs/immich-photo-frame.md", immich_photo_frame_docs),
        ("docs/home-assistant.md", home_assistant_docs),
    )
    if name:
        for label, text in docs_to_check:
            require_contains(text, name, label, errors)
    if requirement:
        require_contains(readme, requirement, "README.md", errors)
        require_contains(home_assistant_docs, requirement, "docs/home-assistant.md", errors)
    if url:
        require_contains(home_assistant_docs, url, "docs/home-assistant.md", errors)
    if platform:
        for label, text in (
            ("README.md", readme),
            ("docs/immich-photo-frame.md", immich_photo_frame_docs),
            ("docs/home-assistant.md", home_assistant_docs),
        ):
            require_contains(text, platform, label, errors)
    if isinstance(features, list):
        for feature in features:
            if not isinstance(feature, str) or not feature.strip():
                continue
            require_contains(home_assistant_docs, feature.strip(), "docs/home-assistant.md", errors)
    if isinstance(network_entities, list):
        for entity in network_entities:
            if not isinstance(entity, dict):
                continue
            entity_name = str(entity.get("name", "")).strip()
            entity_type = str(entity.get("type", "")).strip()
            entity_description = str(entity.get("description", "")).strip()
            for value in (entity_name, entity_type, entity_description):
                if value:
                    require_contains(home_assistant_docs, value, "docs/home-assistant.md", errors)
            if entity_name:
                require_contains(network_yaml, f'name: "{entity_name}"', "common/addon/network.yaml", errors)
    if isinstance(diagnostic_entities, list):
        for entity in diagnostic_entities:
            if not isinstance(entity, dict):
                continue
            entity_name = str(entity.get("name", "")).strip()
            entity_type = str(entity.get("type", "")).strip()
            entity_description = str(entity.get("description", "")).strip()
            for value in (entity_name, entity_type, entity_description):
                if value:
                    require_contains(home_assistant_docs, value, "docs/home-assistant.md", errors)
            if entity_name:
                require_contains(device_yaml, f'name: "{entity_name}"', device_yaml_path, errors)
    if debug_update_interval:
        require_contains(device_yaml, "debug:", device_yaml_path, errors)
        require_contains(device_yaml, f"update_interval: {debug_update_interval}", device_yaml_path, errors)
    for needle in (
        "text_sensor:",
        "platform: debug",
        "reset_reason:",
        "entity_category: diagnostic",
    ):
        require_contains(device_yaml, needle, device_yaml_path, errors)
    if wifi_strength_source:
        require_contains(network_yaml, f'name: "{wifi_strength_source}"', "common/addon/network.yaml", errors)
        require_contains(network_yaml, "source_id: wifi_signal_db", "common/addon/network.yaml", errors)
    if wifi_strength_update_interval:
        require_contains(network_yaml, f"update_interval: {wifi_strength_update_interval}", "common/addon/network.yaml", errors)
    for needle in (
        "platform: status",
        "platform: wifi_signal",
        "platform: copy",
        "unit_of_measurement: \"%\"",
        "platform: wifi_info",
        "ip_address:",
        "entity_category: diagnostic",
        "return min(max(2 * (x + 100.0), 0.0), 100.0);",
    ):
        require_contains(network_yaml, needle, "common/addon/network.yaml", errors)
    require_contains(home_assistant_docs, "notify when **Network: Online** goes unavailable", "docs/home-assistant.md", errors)
