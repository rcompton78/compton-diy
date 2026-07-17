from __future__ import annotations

from product_contract.project_backup_metadata_shape import check_project_backup_metadata_shape
from product_contract.project_core_metadata import (
    check_project_package_firmware_metadata,
    check_project_required_metadata,
    check_project_url_metadata,
)
from product_contract.project_feature_metadata_shape import (
    check_project_api_key_feature_metadata_shape,
    check_project_integration_feature_metadata_shape,
    check_project_photo_display_feature_metadata_shape,
    check_project_screen_feature_metadata_shape,
    check_project_setup_feature_metadata_shape,
)
from product_contract.project_release_metadata import check_project_release_metadata
from product_contract.project_web_metadata_shape import (
    check_project_generated_web_metadata_shape,
    check_project_web_server_metadata_shape,
    check_project_web_ui_metadata_shape,
)


def check_project_metadata(product: dict, errors: list[str]) -> None:
    check_project_required_metadata(product, errors)
    check_project_release_metadata(product, errors)
    check_project_generated_web_metadata_shape(product, errors)
    check_project_url_metadata(product, errors)
    check_project_integration_feature_metadata_shape(product, errors)
    check_project_backup_metadata_shape(product, errors)
    check_project_screen_feature_metadata_shape(product, errors)
    check_project_web_server_metadata_shape(product, errors)
    check_project_setup_feature_metadata_shape(product, errors)
    check_project_web_ui_metadata_shape(product, errors)
    check_project_photo_display_feature_metadata_shape(product, errors)
    check_project_api_key_feature_metadata_shape(product, errors)
    check_project_package_firmware_metadata(product, errors)
