#!/usr/bin/env python3
"""Validate ESPFrame timezone data against Python's IANA zoneinfo database."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import datetime as dt
import importlib.util
import os
from pathlib import Path
import re
import time
import zoneinfo
from zoneinfo import ZoneInfo


ROOT = Path(__file__).resolve().parents[1]
TIMEZONES_PATH = ROOT / "components" / "espframe" / "timezones.py"
REQUIREMENTS_PATH = Path(__file__).resolve().with_name("requirements-timezones.txt")
INSTALL_HINT = (
    f"run scripts/setup.sh (installs it into apps/espframe/.venv, which "
    f"npm run test:timezones uses), or pip install -r {REQUIREMENTS_PATH} in a venv"
)


@dataclass(frozen=True)
class PosixException:
    """A one-time transition a POSIX rule can't express, accepted until it ends.

    POSIX samples for `zone` whose local date falls in [start, end) are skipped;
    every other sample, and every other zone, is still checked. Once `end` has
    passed the zone's normal rule applies again, and the run prints the entry as
    EXPIRED so it gets removed.
    """

    zone: str
    start: dt.date  # local date, inclusive
    end: dt.date  # local date, exclusive
    reason: str

    def covers(self, tz: str, instant: dt.datetime) -> bool:
        return tz == self.zone and self.start <= instant.astimezone(ZoneInfo(tz)).date() < self.end


POSIX_EXCEPTIONS = [
    PosixException(
        zone="America/Havana",
        start=dt.date(2026, 11, 1),
        end=dt.date(2027, 11, 7),
        reason=(
            "tzdata 2026f: Cuba skips the Nov 2026 fall-back and stays on UTC-4 until "
            "2027-11-07, so frames run an hour behind until then (COM-590)"
        ),
    ),
]


def load_timezones():
    spec = importlib.util.spec_from_file_location("espframe_timezones", TIMEZONES_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {TIMEZONES_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def use_pinned_tzdata() -> str:
    """Validate against the pip tzdata package, never the host's zoneinfo.

    The host database's version depends on the machine, so the same table could
    pass on one and fail on another. Returns the IANA version in use.
    """
    pinned = re.search(r"^tzdata==([\d.]+)", REQUIREMENTS_PATH.read_text(), re.MULTILINE)
    if pinned is None:
        raise RuntimeError(f"No tzdata pin in {REQUIREMENTS_PATH}")
    try:
        import tzdata
    except ImportError as exc:
        raise SystemExit(f"tzdata is not installed: {INSTALL_HINT}") from exc
    installed = tuple(int(part) for part in tzdata.__version__.split("."))
    if installed < tuple(int(part) for part in pinned.group(1).split(".")):
        raise SystemExit(
            f"tzdata {tzdata.__version__} is older than the pinned {pinned.group(1)}: {INSTALL_HINT}"
        )
    zoneinfo.reset_tzpath(to=[])
    return tzdata.IANA_VERSION


def posix_offset_hours(posix: str, instant: dt.datetime) -> float:
    old_tz = os.environ.get("TZ")
    os.environ["TZ"] = posix
    time.tzset()
    try:
        local = time.localtime(instant.timestamp())
        if hasattr(local, "tm_gmtoff"):
            return local.tm_gmtoff / 3600.0
        return (time.mktime(local) - time.mktime(time.gmtime(instant.timestamp()))) / 3600.0
    finally:
        if old_tz is None:
            os.environ.pop("TZ", None)
        else:
            os.environ["TZ"] = old_tz
        time.tzset()


def iana_offset_hours(tz: str, instant: dt.datetime) -> float:
    return instant.astimezone(ZoneInfo(tz)).utcoffset().total_seconds() / 3600.0


def iana_local_noon_offset_hours(tz: str, year: int, month: int, day: int) -> float:
    local_noon = dt.datetime(year, month, day, 12, 0, 0, tzinfo=ZoneInfo(tz))
    return local_noon.utcoffset().total_seconds() / 3600.0


def assert_equal(actual, expected, label: str) -> None:
    if actual != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")


def assert_close(actual: float, expected: float, label: str) -> None:
    if abs(actual - expected) > 0.0001:
        raise AssertionError(f"{label}: expected {expected}, got {actual}")


def rule_sample_years(rolling: bool) -> list[int]:
    """Years whose offsets each zone's POSIX rule must match.

    The fixed years keep the PR check deterministic. The weekly drift job adds
    years relative to today so it keeps catching newly announced changes once
    the fixed years are in the past.
    """
    years = {2027, 2030}
    if rolling:
        this_year = dt.date.today().year
        years |= {this_year + 1, this_year + 4}
    return sorted(years)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--rolling-years",
        action="store_true",
        help="also check the rules a year and four years from today (the weekly tzdata drift job)",
    )
    args = parser.parse_args()

    iana_version = use_pinned_tzdata()
    module = load_timezones()
    rows = module.TIMEZONES
    row_by_tz = {tz: (gmt, posix) for tz, gmt, _lat, _lon, posix in rows}

    assert_equal(row_by_tz["Asia/Almaty"][0], "GMT+5", "Asia/Almaty label")

    # GMT labels are part of the saved option value, so a zone whose rules
    # changed (e.g. Vancouver, Edmonton, Winnipeg and Casablanca in 2026) keeps
    # its old label. A label only has to match an offset the zone used in 2026.
    label_instants = [
        dt.datetime(2026, 1, 15, 12, tzinfo=dt.timezone.utc),
        dt.datetime(2026, 2, 20, 12, tzinfo=dt.timezone.utc),
        dt.datetime(2026, 4, 22, 12, tzinfo=dt.timezone.utc),
        dt.datetime(2026, 7, 15, 12, tzinfo=dt.timezone.utc),
        dt.datetime(2026, 11, 15, 12, tzinfo=dt.timezone.utc),
    ]
    # A POSIX string can't express a one-time transition, so the firmware's
    # rule has to match the zone's current rules: sample years after the 2026
    # one-time changes, so a newer tzdata announcing a future change fails too.
    def sample_instants(rolling: bool) -> list[dt.datetime]:
        return [instant.replace(year=year) for year in rule_sample_years(rolling) for instant in label_instants]

    posix_instants = sample_instants(args.rolling_years)
    # Whether an exception still covers a sample is judged against the drift
    # job's samples (fixed + rolling years) in either mode, so an entry only a
    # rolling year needs doesn't fail the PR check.
    coverage_instants = sample_instants(rolling=True)

    zones = {tz for tz, *_ in rows}
    for exception in POSIX_EXCEPTIONS:
        if exception.zone not in zones:
            raise AssertionError(f"POSIX exception for {exception.zone}, which isn't in the timezone table")
        if exception.end <= exception.start:
            raise AssertionError(f"POSIX exception for {exception.zone} doesn't end after it starts")
    skipped = {exception: 0 for exception in POSIX_EXCEPTIONS}

    for tz, gmt, _lat, _lon, posix in rows:
        label_offset = module.parse_gmt_label(gmt)
        iana_offsets = {iana_offset_hours(tz, instant) for instant in label_instants}
        if label_offset not in iana_offsets:
            raise AssertionError(f"{tz} label {gmt} is not one of the sampled IANA offsets {sorted(iana_offsets)}")

        for instant in posix_instants:
            exception = next((e for e in POSIX_EXCEPTIONS if e.covers(tz, instant)), None)
            if exception is not None:
                skipped[exception] += 1
                continue
            assert_close(
                posix_offset_hours(posix, instant),
                iana_offset_hours(tz, instant),
                f"{tz} POSIX offset on {instant.date()} (tzdata {iana_version})",
            )

    expected_noon_offsets = [
        ("Europe/London", 2026, 1, 15, 0.0),
        ("Europe/London", 2026, 3, 29, 1.0),
        ("Europe/London", 2026, 4, 22, 1.0),
        ("America/New_York", 2026, 7, 15, -4.0),
        ("Australia/Sydney", 2026, 1, 15, 11.0),
        ("Australia/Sydney", 2026, 7, 15, 10.0),
        ("UTC", 2026, 4, 22, 0.0),
        ("America/Phoenix", 2026, 7, 15, -7.0),
        ("Asia/Kolkata", 2026, 7, 15, 5.5),
        ("America/St_Johns", 2026, 7, 15, -2.5),
        ("Australia/Adelaide", 2026, 1, 15, 10.5),
    ]
    for tz, year, month, day, expected in expected_noon_offsets:
        assert_close(
            iana_local_noon_offset_hours(tz, year, month, day),
            expected,
            f"{tz} local noon offset on {year:04d}-{month:02d}-{day:02d}",
        )

    labels = module.generate_web_timezone_labels()
    assert "daylight GMT+1" in labels["Europe/London (GMT+0)"]
    assert "daylight GMT-4" in labels["America/New_York (GMT-5)"]
    assert labels["Asia/Almaty (GMT+5)"] == "Asia/Almaty (GMT+5)"

    today = dt.date.today()
    for exception, count in skipped.items():
        window = f"{exception.zone} POSIX samples {exception.start}..{exception.end} (local)"
        if today >= exception.end:
            # Fixed sample years inside the window are skipped forever, so removing
            # the entry alone would make them fail against the zone's history.
            print(
                f"EXPIRED: remove this entry and, in the same change, move rule_sample_years' "
                f"fixed years past {exception.end}: {window}: {exception.reason}"
            )
            continue
        if not any(exception.covers(exception.zone, instant) for instant in coverage_instants):
            raise AssertionError(f"POSIX exception for {window} covers no sample; remove or fix it")
        print(f"exception: {window}, {count} skipped: {exception.reason}")

    print(f"timezone validation passed (tzdata {iana_version})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
