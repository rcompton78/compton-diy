"""Check GitHub workflow contract metadata and the tool versions CI installs.

check_esphome_version and check_node_version run as part of check:product. The
remaining workflow helpers parse workflow YAML and are covered by
tests/workflow_contract_tests.py.
"""

from __future__ import annotations

from collections.abc import Callable
import re
from pathlib import Path

from product_contract.common import ROOT, require_contains


WORKFLOW_ACTION_TARGETS = {
    "cache": (".github/workflows/release.yml", ".github/workflows/compile.yml"),
    "checkout": (".github/workflows/release.yml", ".github/workflows/docs.yml", ".github/workflows/compile.yml"),
    "deploy_pages": (".github/workflows/docs.yml",),
    "download_artifact": (".github/workflows/release.yml", ".github/workflows/docs.yml"),
    "setup_node": (".github/workflows/docs.yml", ".github/workflows/compile.yml"),
    "upload_artifact": (".github/workflows/release.yml", ".github/workflows/docs.yml", ".github/workflows/compile.yml"),
    "upload_pages_artifact": (".github/workflows/docs.yml",),
}


WORKFLOW_PATH_FILTER_TARGETS = {
    "compile_pull_request": (".github/workflows/compile.yml", "pull_request"),
    "docs_push": (".github/workflows/docs.yml", "push"),
}


WORKFLOW_SPARSE_CHECKOUT_TARGETS = {
    "compile": ("metadata",),
    "docs": ("release",),
    "release": ("metadata", "release"),
}


WORKFLOW_FIRMWARE_TIMEOUT_TARGETS = {
    "compile.compile",
    "release.build-firmware",
}


def append_list_drift_errors(
    expected_values: list[str],
    actual_values: list[str],
    missing_prefix: str,
    extra_prefix: str,
    errors: list[str],
) -> None:
    missing_values = [value for value in expected_values if value not in actual_values]
    extra_values = [value for value in actual_values if value not in expected_values]
    if missing_values:
        errors.append(f"{missing_prefix}: {', '.join(missing_values)}")
    if extra_values:
        errors.append(f"{extra_prefix}: {', '.join(extra_values)}")


def workflow_event_names(text: str) -> list[str]:
    match = re.search(r"^on:\n(.*?)(?=^[A-Za-z0-9_-]+:|\Z)", text, re.DOTALL | re.MULTILINE)
    if not match:
        return []

    events: list[str] = []
    for line in match.group(1).splitlines():
        event_match = re.match(r"^  ([A-Za-z0-9_-]+):", line)
        if event_match:
            events.append(event_match.group(1))
    return events


def check_workflow_events(
    workflow_events: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    if not isinstance(workflow_events, dict):
        return

    for workflow, (label, text) in workflow_texts.items():
        raw_events = workflow_events.get(workflow)
        if not isinstance(raw_events, list):
            continue
        expected_events = [str(event).strip() for event in raw_events if str(event).strip()]
        actual_events = workflow_event_names(text)
        append_list_drift_errors(
            expected_events,
            actual_events,
            f"{label} events are missing product metadata events",
            f"{label} events contain triggers missing from product metadata",
            errors,
        )


def workflow_event_block(text: str, event_name: str) -> str:
    match = re.search(rf"^  {re.escape(event_name)}:\n(.*?)(?=^  [A-Za-z0-9_-]+:|\Z)", text, re.DOTALL | re.MULTILINE)
    return match.group(1) if match else ""


def unquote_workflow_value(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in {'"', "'"}:
        return value[1:-1]
    return value


def workflow_inline_list_values(value: str) -> list[str]:
    value = value.strip()
    if not value:
        return []
    if value.startswith("[") and value.endswith("]"):
        return [unquote_workflow_value(item) for item in value[1:-1].split(",") if item.strip()]
    return [unquote_workflow_value(value)]


def workflow_event_list_field_values(text: str, event_name: str, field_name: str) -> list[str]:
    block = workflow_event_block(text, event_name)
    if not block:
        return []

    values: list[str] = []
    in_field = False
    field_prefix = f"    {field_name}:"
    for line in block.splitlines():
        if line.startswith(field_prefix):
            inline_values = workflow_inline_list_values(line.removeprefix(field_prefix))
            if inline_values:
                return inline_values
            in_field = True
            continue
        if not in_field:
            continue
        if line.startswith("      - "):
            values.append(unquote_workflow_value(line.removeprefix("      - ")))
            continue
        if line.strip() and not line.startswith("      "):
            break
    return values


def workflow_event_path_filters(text: str, event_name: str) -> list[str]:
    return workflow_event_list_field_values(text, event_name, "paths")


def workflow_event_branch_filters(text: str, event_name: str) -> list[str]:
    return workflow_event_list_field_values(text, event_name, "branches")


def workflow_event_type_filters(text: str, event_name: str) -> list[str]:
    return workflow_event_list_field_values(text, event_name, "types")


def workflow_event_workflow_filters(text: str, event_name: str) -> list[str]:
    return workflow_event_list_field_values(text, event_name, "workflows")


def check_workflow_event_type_usage(
    workflow_event_types: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    if not isinstance(workflow_event_types, dict):
        return

    configured_keys = {str(key).strip() for key in workflow_event_types if str(key).strip()}
    for raw_key, raw_types in workflow_event_types.items():
        key = str(raw_key).strip()
        workflow_name, _, event_name = key.partition(".")
        if workflow_name not in workflow_texts or not event_name or not isinstance(raw_types, list):
            continue
        label, text = workflow_texts[workflow_name]
        expected_types = [str(event_type).strip() for event_type in raw_types if str(event_type).strip()]
        actual_types = workflow_event_type_filters(text, event_name)
        append_list_drift_errors(
            expected_types,
            actual_types,
            f"{label} {event_name} types are missing product metadata types",
            f"{label} {event_name} types contain values missing from product metadata",
            errors,
        )

    for workflow_name, (label, text) in workflow_texts.items():
        for event_name in workflow_event_names(text):
            key = f"{workflow_name}.{event_name}"
            if key in configured_keys:
                continue
            actual_types = workflow_event_type_filters(text, event_name)
            if actual_types:
                errors.append(f"{label} {event_name} types are missing from product metadata: {', '.join(actual_types)}")


def check_workflow_run_targets(
    expected_workflows: list[str],
    label: str,
    text: str,
    errors: list[str],
) -> None:
    expected_targets = [workflow.strip() for workflow in expected_workflows if workflow.strip()]
    if not expected_targets:
        return

    actual_targets = workflow_event_workflow_filters(text, "workflow_run")
    append_list_drift_errors(
        expected_targets,
        actual_targets,
        f"{label} workflow_run workflows are missing targets",
        f"{label} workflow_run workflows contain targets missing from product metadata",
        errors,
    )


def check_workflow_default_branch(
    expected_branch: str,
    label: str,
    text: str,
    errors: list[str],
) -> None:
    expected_branch = expected_branch.strip()
    if not expected_branch:
        return

    expected_branches = [expected_branch]
    actual_branches = workflow_event_branch_filters(text, "push")
    append_list_drift_errors(
        expected_branches,
        actual_branches,
        f"{label} push branches are missing default branch",
        f"{label} push branches contain branches missing from product metadata",
        errors,
    )


def check_workflow_path_filters(
    workflow_path_filters: object,
    workflow_texts: dict[str, str],
    errors: list[str],
) -> None:
    if not isinstance(workflow_path_filters, dict):
        return

    for filter_set, (label, event_name) in WORKFLOW_PATH_FILTER_TARGETS.items():
        raw_paths = workflow_path_filters.get(filter_set)
        if not isinstance(raw_paths, list):
            actual_paths = workflow_event_path_filters(workflow_texts.get(label, ""), event_name)
            if actual_paths:
                errors.append(
                    f"{label} {event_name} paths are missing from product metadata: "
                    + ", ".join(actual_paths)
                )
            continue
        expected_paths = [str(path).strip() for path in raw_paths if str(path).strip()]
        actual_paths = workflow_event_path_filters(workflow_texts.get(label, ""), event_name)
        append_list_drift_errors(
            expected_paths,
            actual_paths,
            f"{label} {event_name} paths are missing product filters",
            f"{label} {event_name} paths contain filters missing from product metadata",
            errors,
        )


def check_workflow_action_usage(
    release_actions: object,
    workflow_texts: dict[str, str],
    errors: list[str],
) -> None:
    if not isinstance(release_actions, dict):
        return

    expected_actions_by_label: dict[str, list[str]] = {}
    for action_key, labels in WORKFLOW_ACTION_TARGETS.items():
        action = release_actions.get(action_key)
        if not isinstance(action, str) or not action.strip():
            continue
        action_name = action.strip()
        for label in labels:
            expected_actions_by_label.setdefault(label, []).append(action_name)

    for label, text in workflow_texts.items():
        expected_actions = expected_actions_by_label.get(label, [])
        if not expected_actions:
            continue
        actual_actions = workflow_action_references(text)
        append_list_drift_errors(
            expected_actions,
            actual_actions,
            f"{label} uses are missing product metadata actions",
            f"{label} uses contain actions missing from product metadata",
            errors,
        )


def workflow_action_references(text: str) -> list[str]:
    actions: list[str] = []
    for match in re.finditer(r"^\s*(?:-\s*)?uses:\s*(.*?)\s*$", text, re.MULTILINE):
        action = unquote_workflow_value(match.group(1))
        if action and action not in actions:
            actions.append(action)
    return actions


def workflow_top_level_value(text: str, field_name: str) -> str:
    match = re.search(rf"^{re.escape(field_name)}:\s*(.*?)\s*$", text, re.MULTILINE)
    return unquote_workflow_value(match.group(1)) if match else ""


def workflow_display_name(text: str) -> str:
    return workflow_top_level_value(text, "name")


def check_workflow_names(
    workflow_names_metadata: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    if not isinstance(workflow_names_metadata, dict):
        return

    for workflow_name, (label, text) in workflow_texts.items():
        expected_name = str(workflow_names_metadata.get(workflow_name, "")).strip()
        if not expected_name:
            continue
        actual_name = workflow_display_name(text)
        if not actual_name:
            errors.append(f"{label} is missing top-level workflow name")
        elif actual_name != expected_name:
            errors.append(f"{label} name must be {expected_name!r}, found {actual_name!r}")


def workflow_top_level_mapping(text: str, section_name: str) -> dict[str, str]:
    match = re.search(rf"^{re.escape(section_name)}:\n(.*?)(?=^[A-Za-z0-9_-]+:|\Z)", text, re.DOTALL | re.MULTILINE)
    if not match:
        return {}

    values: dict[str, str] = {}
    for line in match.group(1).splitlines():
        item_match = re.match(r"^  ([A-Za-z0-9_-]+):\s*(.*?)\s*$", line)
        if item_match:
            values[item_match.group(1)] = unquote_workflow_value(item_match.group(2))
    return values


def workflow_permissions(text: str) -> dict[str, str]:
    return workflow_top_level_mapping(text, "permissions")


def workflow_env(text: str) -> dict[str, str]:
    return workflow_top_level_mapping(text, "env")


def workflow_concurrency(text: str) -> dict[str, str]:
    return workflow_top_level_mapping(text, "concurrency")


def check_workflow_concurrency(
    expected_group: str,
    expected_cancel_in_progress: object,
    label: str,
    text: str,
    errors: list[str],
) -> None:
    expected_group = expected_group.strip()
    expected_cancel = (
        str(expected_cancel_in_progress).lower()
        if isinstance(expected_cancel_in_progress, bool)
        else ""
    )
    if not expected_group and not expected_cancel:
        return

    actual_concurrency = workflow_concurrency(text)
    if not actual_concurrency:
        errors.append(f"{label} is missing top-level concurrency")
        return

    if expected_group:
        actual_group = actual_concurrency.get("group")
        if actual_group is None:
            errors.append(f"{label} concurrency is missing group")
        elif actual_group != expected_group:
            errors.append(
                f"{label} concurrency.group must be {expected_group!r}, "
                f"found {actual_group!r}"
            )
    if expected_cancel:
        actual_cancel = actual_concurrency.get("cancel-in-progress")
        if actual_cancel is None:
            errors.append(f"{label} concurrency is missing cancel-in-progress")
        elif actual_cancel != expected_cancel:
            errors.append(
                f"{label} concurrency.cancel-in-progress must be "
                f"{expected_cancel!r}, found {actual_cancel!r}"
            )


def check_workflow_top_level_env(
    env_name: str,
    expected_value: str,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    env_name = env_name.strip()
    expected_value = expected_value.strip()
    if not env_name or not expected_value:
        return

    for label, text in workflow_texts.values():
        actual_env = workflow_env(text)
        actual_value = actual_env.get(env_name)
        if actual_value is None:
            errors.append(f"{label} top-level env is missing {env_name}")
        elif actual_value != expected_value:
            errors.append(
                f"{label} top-level env {env_name} must be {expected_value!r}, found {actual_value!r}"
            )


def check_workflow_permissions(
    workflow_permissions_metadata: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    if not isinstance(workflow_permissions_metadata, dict):
        return

    for workflow_name, (label, text) in workflow_texts.items():
        raw_permissions = workflow_permissions_metadata.get(workflow_name)
        if not isinstance(raw_permissions, dict):
            continue
        expected_permissions = {
            str(scope).strip(): str(access).strip()
            for scope, access in raw_permissions.items()
            if str(scope).strip() and str(access).strip()
        }
        actual_permissions = workflow_permissions(text)
        append_list_drift_errors(
            list(expected_permissions),
            list(actual_permissions),
            f"{label} permissions are missing product metadata scopes",
            f"{label} permissions contain scopes missing from product metadata",
            errors,
        )
        for scope, expected_access in expected_permissions.items():
            actual_access = actual_permissions.get(scope)
            if actual_access is not None and actual_access != expected_access:
                errors.append(
                    f"{label} permissions.{scope} must be {expected_access!r}, found {actual_access!r}"
                )


def workflow_job_ids(text: str) -> list[str]:
    match = re.search(r"^jobs:\n(.*?)(?=^[A-Za-z0-9_-]+:|\Z)", text, re.DOTALL | re.MULTILINE)
    if not match:
        return []
    return re.findall(r"^  ([A-Za-z0-9_-]+):", match.group(1), re.MULTILINE)


def workflow_job_block(text: str, job_id: str, label: str, errors: list[str]) -> str:
    match = re.search(rf"^  {re.escape(job_id)}:\n(.*?)(?=^  [A-Za-z0-9_-]+:|\Z)", text, re.DOTALL | re.MULTILINE)
    if not match:
        errors.append(f"{label} is missing job {job_id}")
        return ""
    return match.group(1)


def workflow_job_field_value(job_block: str, field_name: str) -> str:
    match = re.search(rf"^    {re.escape(field_name)}:\s*(.*?)\s*$", job_block, re.MULTILINE)
    return unquote_workflow_value(match.group(1)) if match else ""


def workflow_job_nested_field_value(job_block: str, section_name: str, field_name: str) -> str:
    match = re.search(
        rf"^    {re.escape(section_name)}:\n(.*?)(?=^    [A-Za-z0-9_-]+:|\Z)",
        job_block,
        re.DOTALL | re.MULTILINE,
    )
    if not match:
        return ""

    field_match = re.search(
        rf"^      {re.escape(field_name)}:\s*(.*?)\s*$",
        match.group(1),
        re.MULTILINE,
    )
    return unquote_workflow_value(field_match.group(1)) if field_match else ""


def workflow_job_display_name(job_block: str) -> str:
    return workflow_job_field_value(job_block, "name")


def check_workflow_jobs(
    workflow_jobs: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    if not isinstance(workflow_jobs, dict):
        return

    for workflow_name, (label, text) in workflow_texts.items():
        raw_jobs = workflow_jobs.get(workflow_name)
        if not isinstance(raw_jobs, dict):
            continue
        expected_jobs = [str(job_id).strip() for job_id in raw_jobs if str(job_id).strip()]
        actual_jobs = workflow_job_ids(text)
        append_list_drift_errors(
            expected_jobs,
            actual_jobs,
            f"{label} jobs are missing product metadata jobs",
            f"{label} jobs contain jobs missing from product metadata",
            errors,
        )

        for raw_job_id, raw_job_name in raw_jobs.items():
            job_id = str(raw_job_id).strip()
            job_name = str(raw_job_name).strip()
            if not job_id or not job_name:
                continue
            job_block = workflow_job_block(text, job_id, label, errors)
            if job_block:
                actual_name = workflow_job_display_name(job_block)
                if not actual_name:
                    errors.append(f"{label} job {job_id} is missing name")
                elif actual_name != job_name:
                    errors.append(f"{label} job {job_id} name must be {job_name!r}, found {actual_name!r}")


def workflow_job_runs_on(job_block: str) -> str:
    return workflow_job_field_value(job_block, "runs-on")


def workflow_job_timeout_minutes(job_block: str) -> str:
    return workflow_job_field_value(job_block, "timeout-minutes")


def workflow_job_strategy_fail_fast(job_block: str) -> str:
    return workflow_job_nested_field_value(job_block, "strategy", "fail-fast")


def workflow_job_strategy_matrix(job_block: str) -> str:
    return workflow_job_nested_field_value(job_block, "strategy", "matrix")


def workflow_job_mapping(job_block: str, section_name: str) -> dict[str, str]:
    values: dict[str, str] = {}
    lines = job_block.splitlines()
    for index, line in enumerate(lines):
        if not line.startswith(f"    {section_name}:"):
            continue
        for continuation in lines[index + 1:]:
            if continuation.strip() and not continuation.startswith("      "):
                break
            item_match = re.match(r"^      ([A-Za-z_][A-Za-z0-9_-]*):\s*(.*?)\s*$", continuation)
            if item_match:
                values[item_match.group(1)] = unquote_workflow_value(item_match.group(2))
        return values
    return values


def workflow_job_outputs(job_block: str) -> dict[str, str]:
    return workflow_job_mapping(job_block, "outputs")


def workflow_job_env(job_block: str) -> dict[str, str]:
    return workflow_job_mapping(job_block, "env")


def workflow_job_environment(job_block: str) -> dict[str, str]:
    return workflow_job_mapping(job_block, "environment")


def normalized_workflow_mapping(values: dict[str, str]) -> dict[str, str]:
    return {
        str(name).strip(): str(value).strip()
        for name, value in values.items()
        if str(name).strip() and str(value).strip()
    }


def append_mapping_value_errors(
    expected_values: dict[str, str],
    actual_values: dict[str, str],
    missing_prefix: str,
    value_prefix: str,
    errors: list[str],
) -> None:
    for name, expected_value in expected_values.items():
        actual_value = actual_values.get(name)
        if actual_value is None:
            errors.append(f"{missing_prefix} is missing {name}")
        elif actual_value != expected_value:
            errors.append(
                f"{value_prefix}.{name} must be {expected_value!r}, "
                f"found {actual_value!r}"
            )


def append_scalar_value_error(
    actual_value: str,
    expected_value: str,
    missing_message: str,
    value_prefix: str,
    errors: list[str],
) -> None:
    if not actual_value:
        errors.append(missing_message)
    elif actual_value != expected_value:
        errors.append(f"{value_prefix} must be {expected_value!r}, found {actual_value!r}")


def normalized_workflow_strings(values: list[str]) -> list[str]:
    return [str(value).strip() for value in values if str(value).strip()]


def workflow_target_job_block(
    target: str,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> tuple[str, str, str] | None:
    workflow_name, _, job_id = target.partition(".")
    if workflow_name not in workflow_texts or not job_id:
        return None

    label, text = workflow_texts[workflow_name]
    job_block = workflow_job_block(text, job_id, label, errors)
    if not job_block:
        return None
    return label, job_id, job_block


def workflow_job_blocks(
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> list[tuple[str, str, str, str]]:
    blocks: list[tuple[str, str, str, str]] = []
    for workflow_name, (label, text) in workflow_texts.items():
        for job_id in workflow_job_ids(text):
            job_block = workflow_job_block(text, job_id, label, errors)
            if job_block:
                blocks.append((workflow_name, label, job_id, job_block))
    return blocks


def check_workflow_job_mapping(
    target: str,
    expected_values: dict[str, str],
    section_name: str,
    read_actual_values: Callable[[str], dict[str, str]],
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
    missing_section_message: str = "",
) -> None:
    expected_values = normalized_workflow_mapping(expected_values)
    if not expected_values:
        return

    target_job = workflow_target_job_block(target, workflow_texts, errors)
    if target_job is None:
        return

    label, job_id, job_block = target_job
    actual_values = read_actual_values(job_block)
    if missing_section_message and not actual_values:
        errors.append(f"{label} job {job_id} {missing_section_message}")
        return

    message_prefix = f"{label} job {job_id} {section_name}"
    append_mapping_value_errors(expected_values, actual_values, message_prefix, message_prefix, errors)


def check_workflow_job_outputs(
    target: str,
    expected_outputs: dict[str, str],
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    check_workflow_job_mapping(
        target,
        expected_outputs,
        "outputs",
        workflow_job_outputs,
        workflow_texts,
        errors,
    )


def check_workflow_job_environment(
    target: str,
    expected_environment: dict[str, str],
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    check_workflow_job_mapping(
        target,
        expected_environment,
        "environment",
        workflow_job_environment,
        workflow_texts,
        errors,
        "is missing environment",
    )


def check_workflow_job_env(
    target: str,
    expected_env: dict[str, str],
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    check_workflow_job_mapping(
        target,
        expected_env,
        "env",
        workflow_job_env,
        workflow_texts,
        errors,
    )


def workflow_job_step_blocks(job_block: str) -> list[str]:
    blocks: list[str] = []
    current: list[str] = []
    in_steps = False
    for line in job_block.splitlines():
        if line.startswith("    steps:"):
            in_steps = True
            continue
        if in_steps and re.match(r"^    [A-Za-z0-9_-]+:", line):
            break
        if not in_steps:
            continue
        if line.startswith("      - "):
            if current:
                blocks.append("\n".join(current))
            current = [line]
        elif current:
            current.append(line)
    if current:
        blocks.append("\n".join(current))
    return blocks


def workflow_step_display_name(step_block: str) -> str:
    for line in step_block.splitlines():
        match = re.match(r"^      - name:\s*(.*?)\s*$", line)
        if not match:
            match = re.match(r"^        name:\s*(.*?)\s*$", line)
        if match:
            return unquote_workflow_value(match.group(1))
    return ""


def workflow_step_uses(step_block: str) -> str:
    for line in step_block.splitlines():
        match = re.match(r"^      - uses:\s*(.*?)\s*$", line)
        if not match:
            match = re.match(r"^        uses:\s*(.*?)\s*$", line)
        if match:
            return unquote_workflow_value(match.group(1))
    return ""


def workflow_step_id(step_block: str) -> str:
    for line in step_block.splitlines():
        match = re.match(r"^      - id:\s*(.*?)\s*$", line)
        if not match:
            match = re.match(r"^        id:\s*(.*?)\s*$", line)
        if match:
            return unquote_workflow_value(match.group(1))
    return ""


def workflow_step_mapping(step_block: str, section_name: str) -> dict[str, str]:
    values: dict[str, str] = {}
    lines = step_block.splitlines()
    for index, line in enumerate(lines):
        if not line.startswith(f"        {section_name}:"):
            continue
        for continuation in lines[index + 1:]:
            if continuation.strip() and not continuation.startswith("          "):
                break
            item_match = re.match(r"^          ([A-Za-z_][A-Za-z0-9_-]*):\s*(.*?)\s*$", continuation)
            if item_match:
                values[item_match.group(1)] = unquote_workflow_value(item_match.group(2))
        return values
    return values


def workflow_step_env(step_block: str) -> dict[str, str]:
    return workflow_step_mapping(step_block, "env")


def workflow_step_with(step_block: str) -> dict[str, str]:
    return workflow_step_mapping(step_block, "with")


def workflow_step_run(step_block: str) -> str:
    lines = step_block.splitlines()
    for index, line in enumerate(lines):
        if not line.startswith("        run:"):
            continue
        command = line.removeprefix("        run:").strip()
        if command in {">", ">-", "|", "|-"}:
            run_lines: list[str] = []
            for continuation in lines[index + 1:]:
                if continuation.startswith("          "):
                    run_lines.append(continuation.removeprefix("          "))
                    continue
                if continuation.strip():
                    break
            return "\n".join(run_lines)
        return unquote_workflow_value(command)
    return ""


def workflow_step_text(step_block: str) -> str:
    return step_block


def workflow_step_uses_gh_cli(step_block: str) -> bool:
    return re.search(r"(^|[\s;&|])gh\s+", workflow_step_run(step_block)) is not None


def check_workflow_action_step_inputs(
    target: str,
    action_ref: str,
    match_input: str,
    match_value: str,
    expected_inputs: dict[str, str],
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    action_ref = action_ref.strip()
    match_input = match_input.strip()
    match_value = match_value.strip()
    expected_inputs = normalized_workflow_mapping(expected_inputs)
    if not action_ref or not match_input or not match_value or not expected_inputs:
        return

    target_job = workflow_target_job_block(target, workflow_texts, errors)
    if target_job is None:
        return
    label, job_id, job_block = target_job

    matching_step = ""
    for step_block in workflow_job_step_blocks(job_block):
        if workflow_step_uses(step_block) != action_ref:
            continue
        step_inputs = workflow_step_with(step_block)
        if step_inputs.get(match_input) == match_value:
            matching_step = step_block
            break

    if not matching_step:
        errors.append(
            f"{label} job {job_id} is missing {action_ref} step with "
            f"{match_input} {match_value!r}"
        )
        return

    step_name = workflow_step_display_name(matching_step) or "<unnamed>"
    actual_inputs = workflow_step_with(matching_step)
    message_prefix = f"{label} job {job_id} step {step_name!r} with"
    append_mapping_value_errors(expected_inputs, actual_inputs, message_prefix, message_prefix, errors)


def workflow_named_step_block(
    target: str,
    step_name: str,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> tuple[str, str, str]:
    step_name = step_name.strip()
    workflow_name, _, job_id = target.partition(".")
    if not step_name or workflow_name not in workflow_texts or not job_id:
        return "", "", ""

    label, text = workflow_texts[workflow_name]
    job_block = workflow_job_block(text, job_id, label, errors)
    if not job_block:
        return label, job_id, ""

    for step_block in workflow_job_step_blocks(job_block):
        if workflow_step_display_name(step_block) == step_name:
            return label, job_id, step_block

    errors.append(f"{label} job {job_id} is missing step {step_name!r}")
    return label, job_id, ""


def check_workflow_named_step_mapping(
    target: str,
    step_name: str,
    expected_values: dict[str, str],
    section_name: str,
    read_actual_values: Callable[[str], dict[str, str]],
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    step_name = step_name.strip()
    expected_values = normalized_workflow_mapping(expected_values)
    if not step_name or not expected_values:
        return

    label, job_id, step_block = workflow_named_step_block(target, step_name, workflow_texts, errors)
    if not step_block:
        return

    actual_values = read_actual_values(step_block)
    message_prefix = f"{label} job {job_id} step {step_name!r} {section_name}"
    append_mapping_value_errors(expected_values, actual_values, message_prefix, message_prefix, errors)


def check_workflow_named_step_value(
    target: str,
    step_name: str,
    expected_value: str,
    field_name: str,
    read_actual_value: Callable[[str], str],
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    step_name = step_name.strip()
    expected_value = expected_value.strip()
    if not step_name or not expected_value:
        return

    label, job_id, step_block = workflow_named_step_block(target, step_name, workflow_texts, errors)
    if not step_block:
        return

    actual_value = read_actual_value(step_block)
    append_scalar_value_error(
        actual_value,
        expected_value,
        f"{label} job {job_id} step {step_name!r} is missing {field_name}",
        f"{label} job {job_id} step {step_name!r} {field_name}",
        errors,
    )


def check_workflow_named_step_env(
    target: str,
    step_name: str,
    expected_env: dict[str, str],
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    check_workflow_named_step_mapping(
        target,
        step_name,
        expected_env,
        "env",
        workflow_step_env,
        workflow_texts,
        errors,
    )


def check_workflow_named_step_uses(
    target: str,
    step_name: str,
    expected_action: str,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    check_workflow_named_step_value(
        target,
        step_name,
        expected_action,
        "uses",
        workflow_step_uses,
        workflow_texts,
        errors,
    )


def check_workflow_named_step_id(
    target: str,
    step_name: str,
    expected_id: str,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    check_workflow_named_step_value(
        target,
        step_name,
        expected_id,
        "id",
        workflow_step_id,
        workflow_texts,
        errors,
    )


def check_workflow_named_step_with(
    target: str,
    step_name: str,
    expected_inputs: dict[str, str],
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    check_workflow_named_step_mapping(
        target,
        step_name,
        expected_inputs,
        "with",
        workflow_step_with,
        workflow_texts,
        errors,
    )


def check_workflow_named_step_fragments(
    target: str,
    step_name: str,
    expected_fragments: list[str],
    source_label: str,
    read_source_text: Callable[[str], str],
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
    missing_source_label: str = "",
) -> None:
    step_name = step_name.strip()
    expected_fragments = normalized_workflow_strings(expected_fragments)
    if not step_name or not expected_fragments:
        return

    label, job_id, step_block = workflow_named_step_block(target, step_name, workflow_texts, errors)
    if not step_block:
        return

    source_text = read_source_text(step_block)
    if missing_source_label and not source_text:
        errors.append(f"{label} job {job_id} step {step_name!r} is missing {missing_source_label}")
        return

    for fragment in expected_fragments:
        if fragment in source_text:
            continue
        if source_label:
            errors.append(f"{label} job {job_id} step {step_name!r} {source_label} is missing {fragment!r}")
        else:
            errors.append(f"{label} job {job_id} step {step_name!r} is missing {fragment!r}")


def check_workflow_named_step_contains(
    target: str,
    step_name: str,
    expected_fragments: list[str],
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    check_workflow_named_step_fragments(
        target,
        step_name,
        expected_fragments,
        "",
        workflow_step_text,
        workflow_texts,
        errors,
    )


def check_workflow_named_step_run_contains(
    target: str,
    step_name: str,
    expected_fragments: list[str],
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    check_workflow_named_step_fragments(
        target,
        step_name,
        expected_fragments,
        "run",
        workflow_step_run,
        workflow_texts,
        errors,
        "run",
    )


def check_workflow_job_run_command(
    target: str,
    expected_run: str,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    expected_run = expected_run.strip()
    if not expected_run:
        return

    target_job = workflow_target_job_block(target, workflow_texts, errors)
    if target_job is None:
        return
    label, job_id, job_block = target_job

    for step_block in workflow_job_step_blocks(job_block):
        if workflow_step_run(step_block).strip() == expected_run:
            return
    errors.append(f"{label} job {job_id} is missing run command {expected_run!r}")


def check_workflow_gh_cli_env(
    github_cli_env: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    if not isinstance(github_cli_env, dict):
        return

    expected_env = {
        str(raw_name).strip(): str(raw_value).strip()
        for raw_name, raw_value in github_cli_env.items()
        if str(raw_name).strip() and str(raw_value).strip()
    }
    if not expected_env:
        return

    for label, text in workflow_texts.values():
        for job_id in workflow_job_ids(text):
            job_block = workflow_job_block(text, job_id, label, errors)
            if not job_block:
                continue
            for step_block in workflow_job_step_blocks(job_block):
                if not workflow_step_uses_gh_cli(step_block):
                    continue
                step_name = workflow_step_display_name(step_block) or "<unnamed>"
                actual_env = workflow_step_env(step_block)
                message_prefix = f"{label} job {job_id} step {step_name!r} env"
                append_mapping_value_errors(expected_env, actual_env, message_prefix, message_prefix, errors)


def check_workflow_release_build_fail_fast(
    expected_fail_fast: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    if not isinstance(expected_fail_fast, bool):
        return

    expected_fail_fast_value = str(expected_fail_fast).lower()
    target_job = workflow_target_job_block("release.build-firmware", workflow_texts, errors)
    if target_job is None:
        return
    label, job_id, job_block = target_job
    actual_fail_fast = workflow_job_strategy_fail_fast(job_block)
    append_scalar_value_error(
        actual_fail_fast,
        expected_fail_fast_value,
        f"{label} job {job_id} strategy is missing fail-fast",
        f"{label} job {job_id} strategy.fail-fast",
        errors,
    )


def check_workflow_job_strategy_matrix(
    target: str,
    expected_matrix: str,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    expected_matrix = expected_matrix.strip()
    if not expected_matrix:
        return

    target_job = workflow_target_job_block(target, workflow_texts, errors)
    if target_job is None:
        return
    label, job_id, job_block = target_job

    actual_matrix = workflow_job_strategy_matrix(job_block)
    append_scalar_value_error(
        actual_matrix,
        expected_matrix,
        f"{label} job {job_id} strategy is missing matrix",
        f"{label} job {job_id} strategy.matrix",
        errors,
    )


def check_workflow_job_timeout_usage(
    expected_timeout: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    if not isinstance(expected_timeout, int) or isinstance(expected_timeout, bool):
        return
    expected_timeout_value = str(expected_timeout)

    for target in sorted(WORKFLOW_FIRMWARE_TIMEOUT_TARGETS):
        target_job = workflow_target_job_block(target, workflow_texts, errors)
        if target_job is None:
            continue
        label, job_id, job_block = target_job
        actual_timeout = workflow_job_timeout_minutes(job_block)
        append_scalar_value_error(
            actual_timeout,
            expected_timeout_value,
            f"{label} job {job_id} is missing timeout-minutes",
            f"{label} job {job_id} timeout-minutes",
            errors,
        )


def check_workflow_job_runner_usage(
    expected_runner: str,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    expected_runner = expected_runner.strip()
    if not expected_runner:
        return

    for _, label, job_id, job_block in workflow_job_blocks(workflow_texts, errors):
        actual_runner = workflow_job_runs_on(job_block)
        append_scalar_value_error(
            actual_runner,
            expected_runner,
            f"{label} job {job_id} is missing runs-on",
            f"{label} job {job_id} runs-on",
            errors,
        )


def workflow_job_needs(job_block: str) -> list[str]:
    lines = job_block.splitlines()
    for index, line in enumerate(lines):
        if not line.startswith("    needs:"):
            continue
        inline_needs = workflow_inline_list_values(line.removeprefix("    needs:"))
        if inline_needs:
            return inline_needs
        dependencies: list[str] = []
        for continuation in lines[index + 1:]:
            if continuation.startswith("      - "):
                dependencies.append(unquote_workflow_value(continuation.removeprefix("      - ")))
                continue
            if continuation.strip() and not continuation.startswith("      "):
                break
        return dependencies
    return []


def check_workflow_job_dependency_usage(
    workflow_job_dependencies: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    if not isinstance(workflow_job_dependencies, dict):
        return

    configured_keys = {str(key).strip() for key in workflow_job_dependencies if str(key).strip()}
    for raw_key, raw_dependencies in workflow_job_dependencies.items():
        key = str(raw_key).strip()
        if not isinstance(raw_dependencies, list):
            continue
        expected_dependencies = [str(dependency).strip() for dependency in raw_dependencies if str(dependency).strip()]
        target_job = workflow_target_job_block(key, workflow_texts, errors)
        if target_job is None:
            continue
        label, job_id, job_block = target_job
        actual_dependencies = workflow_job_needs(job_block)
        append_list_drift_errors(
            expected_dependencies,
            actual_dependencies,
            f"{label} job {job_id} needs are missing dependencies",
            f"{label} job {job_id} needs contain dependencies missing from product metadata",
            errors,
        )

    for workflow_name, label, job_id, job_block in workflow_job_blocks(workflow_texts, errors):
        key = f"{workflow_name}.{job_id}"
        if key in configured_keys:
            continue
        actual_dependencies = workflow_job_needs(job_block)
        if actual_dependencies:
            errors.append(f"{label} job {job_id} needs are missing from product metadata: {', '.join(actual_dependencies)}")


def workflow_sparse_checkout_entries(text: str) -> list[tuple[list[str], str]]:
    entries: list[tuple[list[str], str]] = []
    lines = text.splitlines()
    for index, line in enumerate(lines):
        match = re.match(r"^(\s*)sparse-checkout:\s*\|\s*$", line)
        if not match:
            continue
        indent = match.group(1)
        value_prefix = f"{indent}  "
        paths: list[str] = []
        next_index = index + 1
        while next_index < len(lines):
            continuation = lines[next_index]
            if continuation.startswith(value_prefix):
                paths.append(unquote_workflow_value(continuation.removeprefix(value_prefix)))
                next_index += 1
                continue
            if continuation.strip():
                break
            next_index += 1
        cone_mode = ""
        if next_index < len(lines):
            cone_mode_prefix = f"{indent}sparse-checkout-cone-mode:"
            if lines[next_index].startswith(cone_mode_prefix):
                cone_mode = unquote_workflow_value(lines[next_index].removeprefix(cone_mode_prefix))
        entries.append((paths, cone_mode))
    return entries


def workflow_sparse_checkout_blocks(text: str) -> list[list[str]]:
    blocks: list[list[str]] = []
    for paths, _ in workflow_sparse_checkout_entries(text):
        blocks.append(paths)
    return blocks


def check_workflow_sparse_checkout_usage(
    release_checkout_files: list[str],
    metadata_checkout_files: list[str],
    expected_cone_mode: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    expected_checkout_groups = {
        "metadata": [path.strip() for path in metadata_checkout_files if path.strip()],
        "release": [path.strip() for path in release_checkout_files if path.strip()],
    }
    if not expected_checkout_groups["metadata"] or not expected_checkout_groups["release"]:
        return
    expected_cone_mode_value = (
        str(expected_cone_mode).lower() if isinstance(expected_cone_mode, bool) else ""
    )

    for workflow_name, (label, text) in workflow_texts.items():
        expected_groups = set(WORKFLOW_SPARSE_CHECKOUT_TARGETS.get(workflow_name, ()))
        if not expected_groups:
            continue

        matched_groups: list[str] = []
        for block, cone_mode in workflow_sparse_checkout_entries(text):
            matching_groups = [name for name, paths in expected_checkout_groups.items() if block == paths]
            if matching_groups:
                matched_group = matching_groups[0]
                matched_groups.append(matched_group)
            else:
                matched_group = "unknown"
                errors.append(f"{label} sparse-checkout block must match product metadata, found: {', '.join(block)}")
            if expected_cone_mode_value and cone_mode != expected_cone_mode_value:
                found = cone_mode or "<missing>"
                errors.append(
                    f"{label} sparse-checkout block {matched_group} cone mode must be "
                    f"{expected_cone_mode_value!r}, found {found!r}"
                )

        for expected_group in sorted(expected_groups):
            if expected_group not in matched_groups:
                errors.append(
                    f"{label} is missing {expected_group} sparse-checkout block: "
                    + ", ".join(expected_checkout_groups[expected_group])
                )
        for matched_group in matched_groups:
            if matched_group not in expected_groups:
                errors.append(f"{label} sparse-checkout block {matched_group} is not expected")


def normalize_workflow_condition(condition: str) -> str:
    return re.sub(r"\s+", " ", condition).strip()


def workflow_job_condition(job_block: str) -> str:
    lines = job_block.splitlines()
    for index, line in enumerate(lines):
        if not line.startswith("    if:"):
            continue
        condition = line.removeprefix("    if:").strip()
        if condition in {">", ">-", "|", "|-"}:
            folded_lines = []
            for continuation in lines[index + 1:]:
                if not continuation.startswith("      "):
                    break
                folded_lines.append(continuation.strip())
            return normalize_workflow_condition(" ".join(folded_lines))
        return normalize_workflow_condition(condition)
    return ""


def check_workflow_job_condition_usage(
    workflow_job_conditions: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    if not isinstance(workflow_job_conditions, dict):
        return

    for raw_key, raw_condition in workflow_job_conditions.items():
        key = str(raw_key).strip()
        target_job = workflow_target_job_block(key, workflow_texts, errors)
        if target_job is None:
            continue
        label, job_id, job_block = target_job
        expected_condition = normalize_workflow_condition(str(raw_condition)) if raw_condition is not None else ""
        actual_condition = workflow_job_condition(job_block)
        if expected_condition and actual_condition != expected_condition:
            errors.append(
                f"{label} job {job_id} if condition must be {expected_condition!r}, found {actual_condition!r}"
            )
        elif not expected_condition and actual_condition:
            errors.append(f"{label} job {job_id} must not define an if condition")


# This repo has two workflows: build.yml (pull requests, manual runs, and called
# from release.yml) and release.yml (pushes to main). The contract only pins the
# tool versions they install; the workflow layout itself is not mirrored in
# product metadata.
FIRMWARE_WORKFLOWS = (".github/workflows/build.yml", ".github/workflows/release.yml")
ESPHOME_PIN_READ = "['esphome_version']"
# The pin as the workflows install it; extra pip flags around it are fine.
ESPHOME_PINNED_INSTALL = "esphome==$version"


def _read_rel(root: Path, rel_path: str, errors: list[str]) -> str:
    path = root / rel_path
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError:
        errors.append(f"Missing file: {rel_path}")
        return ""


def workflow_files(root: Path) -> list[Path]:
    """Return every GitHub workflow file (.yml and .yaml), sorted by name."""
    workflow_dir = root / ".github" / "workflows"
    return sorted([*workflow_dir.glob("*.yml"), *workflow_dir.glob("*.yaml")])


def check_esphome_version(product: dict, errors: list[str], root: Path = ROOT) -> None:
    """Check that local builds and CI install the contract's ESPHome pin and the docs state it.

    Each firmware workflow must read the pin from the contract and install exactly
    that version; any other ``esphome`` install in a workflow is reported.
    """
    version = str(product["project"].get("esphome_version", "")).strip()
    if not version:
        errors.append("project.esphome_version is required")
        return
    if not re.match(r"^\d+\.\d+\.\d+$", version):
        errors.append("project.esphome_version must be an exact X.Y.Z version")

    require_contains(_read_rel(root, "scripts/firmware.sh", errors), '["esphome_version"]', "scripts/firmware.sh", errors)
    for rel_path in FIRMWARE_WORKFLOWS:
        text = _read_rel(root, rel_path, errors)
        require_contains(text, ESPHOME_PIN_READ, rel_path, errors)
        require_contains(text, ESPHOME_PINNED_INSTALL, rel_path, errors)
    for path in workflow_files(root):
        rel_path = path.relative_to(root).as_posix()
        for line in path.read_text(encoding="utf-8").splitlines():
            if re.search(r"\bpip3? install\b.*\besphome\b", line) and ESPHOME_PINNED_INSTALL not in line:
                errors.append(f"{rel_path} installs ESPHome without the contract pin: {line.strip()}")

    install_docs = _read_rel(root, "docs/install.md", errors)
    require_contains(install_docs, f"`{version}`", "docs/install.md", errors)
    require_contains(_read_rel(root, "docs/manual-setup.md", errors), f"`{version}`", "docs/manual-setup.md", errors)
    # The install notes head the bullet with the X.Y series, e.g. "**ESPHome 2026.6:**".
    series = version.rsplit(".", 1)[0]
    for found in re.findall(r"\*\*ESPHome (\d+\.\d+):\*\*", install_docs):
        if found != series:
            errors.append(f"docs/install.md heading says ESPHome {found}, expected {series}")


def check_node_version(product: dict, errors: list[str], root: Path = ROOT) -> None:
    """Check that workflow Node versions and the README match project.node_version.

    At least one workflow must set ``node-version``. Only the leading major
    version is compared, so ``22``, ``22.x`` and ``'22.12.0'`` all match 22.
    Expressions such as ``${{ matrix.node }}`` and ``node-version-file`` can't be
    checked statically and are reported so the check never silently passes.
    """
    version = str(product["project"].get("node_version", "")).strip()
    if not version:
        errors.append("project.node_version is required")
        return
    if not re.match(r"^\d+$", version):
        errors.append("project.node_version must be a major version number")

    found_any = False
    for path in workflow_files(root):
        rel_path = path.relative_to(root).as_posix()
        text = path.read_text(encoding="utf-8")
        if re.search(r"^[ \t]*node-version-file:", text, re.MULTILINE):
            errors.append(f"{rel_path} uses node-version-file; set node-version: {version} instead")
        for raw in re.findall(r"^[ \t]*node-version:[ \t]*(.*?)[ \t]*(?:#.*)?$", text, re.MULTILINE):
            found_any = True
            value = raw.strip().strip("'\"")
            major = re.match(r"(\d+)(?:\.|$)", value)
            if not major:
                errors.append(f"{rel_path} node-version {raw.strip() or '(empty)'} can't be checked; use {version}")
            elif major.group(1) != version:
                errors.append(f"{rel_path} uses node-version {value}, expected project.node_version {version}")
    if not found_any:
        errors.append(f"No workflow sets node-version; expected {version}")

    readme = _read_rel(root, "README.md", errors)
    mentioned = re.findall(r"Node\.js (\d+)\b", readme)
    if version not in mentioned:
        errors.append(f"README.md is missing 'Node.js {version}'")
    for other in sorted(set(mentioned) - {version}):
        errors.append(f"README.md mentions Node.js {other}, expected {version}")
