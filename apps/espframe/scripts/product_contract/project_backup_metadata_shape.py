from __future__ import annotations

from product_contract.common import ROOT, check_relative_path, read
from product_config import web_manual_entities, web_static_entities

def check_project_backup_metadata_shape(product: dict, errors: list[str]) -> None:
    project = product["project"]
    backup_config_version = project.get("backup_config_version")
    if not isinstance(backup_config_version, int) or isinstance(backup_config_version, bool) or backup_config_version < 1:
        errors.append("project.backup_config_version must be a positive integer")
    backup_import_photo_id_limit = project.get("backup_import_photo_id_limit")
    if (
        not isinstance(backup_import_photo_id_limit, int)
        or isinstance(backup_import_photo_id_limit, bool)
        or backup_import_photo_id_limit < 1
    ):
        errors.append("project.backup_import_photo_id_limit must be a positive integer")
    backup_excluded_values = project.get("backup_excluded_runtime_values", [])
    if not isinstance(backup_excluded_values, list) or not backup_excluded_values:
        errors.append("project.backup_excluded_runtime_values must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in backup_excluded_values):
        errors.append("project.backup_excluded_runtime_values must only contain non-empty strings")
    elif len({str(value).strip() for value in backup_excluded_values}) != len(backup_excluded_values):
        errors.append("project.backup_excluded_runtime_values must not contain duplicate values")
    backup_export_groups = project.get("backup_export_groups", [])
    if not isinstance(backup_export_groups, list) or not backup_export_groups:
        errors.append("project.backup_export_groups must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in backup_export_groups):
        errors.append("project.backup_export_groups must only contain non-empty strings")
    elif len({str(value).strip() for value in backup_export_groups}) != len(backup_export_groups):
        errors.append("project.backup_export_groups must not contain duplicate groups")
    backup_export_fields = project.get("backup_export_fields", {})
    if not isinstance(backup_export_fields, dict) or not backup_export_fields:
        errors.append("project.backup_export_fields must be a non-empty object")
    else:
        expected_groups = {str(group).strip() for group in backup_export_groups if str(group).strip()}
        configured_groups = {str(group).strip() for group in backup_export_fields}
        missing_groups = sorted(expected_groups - configured_groups)
        extra_groups = sorted(configured_groups - expected_groups)
        if missing_groups:
            errors.append(f"project.backup_export_fields is missing groups: {', '.join(missing_groups)}")
        if extra_groups:
            errors.append(f"project.backup_export_fields contains unknown groups: {', '.join(extra_groups)}")

        all_fields: set[str] = set()
        field_count = 0
        for raw_group, raw_fields in backup_export_fields.items():
            group = str(raw_group).strip()
            if not group:
                errors.append("project.backup_export_fields keys must be non-empty strings")
            if not isinstance(raw_fields, list) or not raw_fields:
                errors.append(f"project.backup_export_fields.{group or '<missing>'} must be a non-empty list")
                continue
            fields = [str(field).strip() for field in raw_fields]
            if any(not field for field in fields):
                errors.append(f"project.backup_export_fields.{group or '<missing>'} must only contain non-empty strings")
            if len(fields) != len(set(fields)):
                errors.append(f"project.backup_export_fields.{group or '<missing>'} must not contain duplicate fields")
            all_fields.update(fields)
            field_count += len(fields)
        if len(all_fields) != field_count:
            errors.append("project.backup_export_fields field names must be unique across groups")
    backup_state_mappings = project.get("backup_field_state_keys", {})
    if not isinstance(backup_state_mappings, dict) or not backup_state_mappings:
        errors.append("project.backup_field_state_keys must be a non-empty object")
    else:
        expected_group_fields = {
            (str(group).strip(), str(field).strip())
            for group, fields in project.get("backup_export_fields", {}).items()
            if isinstance(fields, list)
            for field in fields
            if str(group).strip() and str(field).strip()
        }
        configured_group_fields: set[tuple[str, str]] = set()
        valid_state_keys = (
            {str(setting.get("key", "")).strip() for setting in product.get("settings", [])}
            | set(web_static_entities(product))
            | set(web_manual_entities(product))
        )
        for raw_group, raw_fields in backup_state_mappings.items():
            group = str(raw_group).strip()
            if not group:
                errors.append("project.backup_field_state_keys keys must be non-empty strings")
                continue
            if not isinstance(raw_fields, dict) or not raw_fields:
                errors.append(f"project.backup_field_state_keys.{group} must be a non-empty object")
                continue
            for raw_field, raw_state_keys in raw_fields.items():
                field = str(raw_field).strip()
                if not field:
                    errors.append(f"project.backup_field_state_keys.{group} field keys must be non-empty strings")
                    continue
                configured_group_fields.add((group, field))
                if isinstance(raw_state_keys, list):
                    state_keys = [str(value).strip() for value in raw_state_keys]
                    if not state_keys:
                        errors.append(f"project.backup_field_state_keys.{group}.{field} must list at least one state key")
                    elif any(not value for value in state_keys):
                        errors.append(f"project.backup_field_state_keys.{group}.{field} must only contain non-empty strings")
                    elif len(state_keys) != len(set(state_keys)):
                        errors.append(f"project.backup_field_state_keys.{group}.{field} must not contain duplicate state keys")
                else:
                    state_key = str(raw_state_keys).strip()
                    state_keys = [state_key] if state_key else []
                    if not state_key:
                        errors.append(f"project.backup_field_state_keys.{group}.{field} must be a non-empty string or list")
                for state_key in state_keys:
                    if state_key not in valid_state_keys:
                        errors.append(f"Backup field {group}.{field} maps to unknown state key {state_key}")
        missing_mappings = sorted(expected_group_fields - configured_group_fields)
        extra_mappings = sorted(configured_group_fields - expected_group_fields)
        if missing_mappings:
            errors.append(
                "project.backup_field_state_keys is missing fields: "
                + ", ".join(f"{group}.{field}" for group, field in missing_mappings)
            )
        if extra_mappings:
            errors.append(
                "project.backup_field_state_keys contains unknown fields: "
                + ", ".join(f"{group}.{field}" for group, field in extra_mappings)
            )
    backup_fixture_files = project.get("backup_fixture_files", [])
    if not isinstance(backup_fixture_files, list) or not backup_fixture_files:
        errors.append("project.backup_fixture_files must be a non-empty list")
    elif len({str(fixture_file).strip() for fixture_file in backup_fixture_files}) != len(backup_fixture_files):
        errors.append("project.backup_fixture_files must not contain duplicate files")
    else:
        for fixture_file in backup_fixture_files:
            path = check_relative_path(fixture_file, "project.backup_fixture_files entry", errors)
            if path:
                read(ROOT / path, errors)
    compatibility_fixture_files = project.get("compatibility_fixture_files", {})
    if not isinstance(compatibility_fixture_files, dict) or not compatibility_fixture_files:
        errors.append("project.compatibility_fixture_files must be a non-empty object")
    else:
        accepted = compatibility_fixture_files.get("accepted", [])
        rejected_fields = compatibility_fixture_files.get("rejected_fields", [])
        if not isinstance(accepted, list) or not accepted:
            errors.append("project.compatibility_fixture_files.accepted must be a non-empty list")
        else:
            for fixture_file in accepted:
                path = check_relative_path(fixture_file, "project.compatibility_fixture_files.accepted entry", errors)
                if path:
                    read(ROOT / path, errors)
        if not isinstance(rejected_fields, list) or not rejected_fields:
            errors.append("project.compatibility_fixture_files.rejected_fields must be a non-empty list")
        else:
            for item in rejected_fields:
                if not isinstance(item, dict):
                    errors.append("project.compatibility_fixture_files.rejected_fields entries must be objects")
                    continue
                path = check_relative_path(item.get("path"), "project.compatibility_fixture_files.rejected_fields path", errors)
                if path:
                    read(ROOT / path, errors)
                messages = item.get("messages", [])
                if not isinstance(messages, list) or not messages:
                    errors.append(f"project.compatibility_fixture_files.rejected_fields {path or '<missing>'} messages must be a non-empty list")
                elif any(not isinstance(message, str) or not message.strip() for message in messages):
                    errors.append(f"project.compatibility_fixture_files.rejected_fields {path or '<missing>'} messages must be non-empty strings")
