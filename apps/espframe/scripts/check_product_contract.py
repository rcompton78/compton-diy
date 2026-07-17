#!/usr/bin/env python3
"""Validate the shared product metadata against the checked-in project.

This is the first release gate for the reset architecture. It catches drift
between product metadata, firmware YAML, the custom web UI, docs, and CI before
we start generating larger parts of the project from the product schema.
"""

from __future__ import annotations

import sys

from product_contract.backup_metadata import check_backup_metadata
from product_contract.build_outputs import (
    check_external_components_metadata,
    check_factory_firmware_metadata,
    check_generated_asset_metadata,
    check_web_server_metadata,
)
from product_contract.devices import check_devices
from product_contract.firmware_updates import (
    check_device_logging_metadata,
    check_firmware_update_metadata,
    check_ota_update_metadata,
)
from product_contract.integrations import (
    check_home_assistant_metadata,
    check_immich_api_key_metadata,
    check_immich_connection_metadata,
)
from product_contract.package_metadata import check_license_metadata, check_npm_package_metadata
from product_contract.photo_features import (
    check_connection_resilience_metadata,
    check_photo_display_metadata,
    check_photo_source_metadata,
    check_setup_flow_metadata,
)
from product_contract.privacy import check_privacy_metadata
from product_contract.project_metadata import check_project_metadata
from product_contract.public_site import (
    check_docs_site_config,
    check_public_manifest_urls,
    check_public_site_references,
)
from product_contract.screen_features import (
    check_clock_time_metadata,
    check_developer_features_metadata,
    check_screen_rotation_metadata,
    check_screen_schedule_metadata,
    check_screen_tone_metadata,
    check_touch_controls_metadata,
)
from product_contract.settings import check_settings
from product_contract.workflows import (
    check_device_workflow_contract,
    check_esphome_version,
    check_node_version,
    check_workflows,
)
from product_config import load_product


def main() -> int:
    errors: list[str] = []
    product = load_product()
    check_project_metadata(product, errors)
    check_npm_package_metadata(product, errors)
    check_license_metadata(product, errors)
    check_immich_api_key_metadata(product, errors)
    check_immich_connection_metadata(product, errors)
    check_home_assistant_metadata(product, errors)
    check_device_logging_metadata(product, errors)
    check_firmware_update_metadata(product, errors)
    check_ota_update_metadata(product, errors)
    check_backup_metadata(product, errors)
    check_privacy_metadata(product, errors)
    check_touch_controls_metadata(product, errors)
    check_screen_schedule_metadata(product, errors)
    check_screen_rotation_metadata(product, errors)
    check_developer_features_metadata(product, errors)
    check_screen_tone_metadata(product, errors)
    check_clock_time_metadata(product, errors)
    check_photo_source_metadata(product, errors)
    check_connection_resilience_metadata(product, errors)
    check_setup_flow_metadata(product, errors)
    check_photo_display_metadata(product, errors)
    check_devices(product, errors)
    check_public_manifest_urls(product, errors)
    check_public_site_references(product, errors)
    check_docs_site_config(product, errors)
    check_device_workflow_contract(product, errors)
    check_generated_asset_metadata(product, errors)
    check_factory_firmware_metadata(product, errors)
    check_web_server_metadata(product, errors)
    check_external_components_metadata(product, errors)
    check_esphome_version(product, errors)
    check_node_version(product, errors)
    check_workflows(product, errors)
    check_settings(product, errors)

    if errors:
        for error in errors:
            print(f"product contract error: {error}", file=sys.stderr)
        return 1

    print("product contract validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
