#!/usr/bin/env python3
"""Summarize the last cumulative ai_camera_metrics record in each log."""

from __future__ import annotations

import re
import statistics
import sys
from collections import defaultdict
from pathlib import Path


METRIC_PREFIX = "[ai_camera_metrics]"
PAIR_RE = re.compile(r"([A-Za-z0-9_]+)=([^\s]+)")
REPORT_FIELDS = (
    "backend",
    "uptime_s",
    "frames",
    "published",
    "fps",
    "e2e_p95_ms",
    "infer_p95_ms",
    "queue_p95_ms",
    "queue_drop",
    "timeouts",
    "recoveries",
    "failures",
    "media_pts",
    "software_start",
)


def last_record(path: Path) -> dict[str, str]:
    records = []
    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if METRIC_PREFIX in line:
                records.append(dict(PAIR_RE.findall(line)))
    if not records:
        raise ValueError("no ai_camera_metrics record")
    return records[-1]


def numeric(record: dict[str, str], field: str) -> float:
    try:
        return float(record[field])
    except (KeyError, ValueError) as error:
        raise ValueError(f"invalid or missing {field}") from error


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(f"usage: {argv[0]} LOG [LOG ...]", file=sys.stderr)
        return 2

    rows = []
    for name in argv[1:]:
        path = Path(name)
        try:
            record = last_record(path)
            for field in REPORT_FIELDS:
                if field not in record:
                    raise ValueError(f"missing {field}")
        except (OSError, ValueError) as error:
            print(f"{path}: {error}", file=sys.stderr)
            return 1
        rows.append((path, record))

    print("| log | " + " | ".join(REPORT_FIELDS) + " |")
    print("| --- | " + " | ".join("---:" for _ in REPORT_FIELDS) + " |")
    for path, record in rows:
        values = [record[field] for field in REPORT_FIELDS]
        print(f"| {path} | " + " | ".join(values) + " |")

    grouped: dict[str, list[dict[str, str]]] = defaultdict(list)
    for _, record in rows:
        grouped[record["backend"]].append(record)

    print("\nRun-level aggregate (median [min, max]); p95 values are not pooled):")
    print("| backend | runs | fps | e2e p95 ms | inference p95 ms | min uptime s | total failures |")
    print("| --- | ---: | ---: | ---: | ---: | ---: | ---: |")
    for backend, records in sorted(grouped.items()):
        def summary(field: str) -> str:
            values = [numeric(record, field) for record in records]
            return f"{statistics.median(values):.3f} [{min(values):.3f}, {max(values):.3f}]"

        min_uptime = min(numeric(record, "uptime_s") for record in records)
        failures = sum(int(numeric(record, "failures")) for record in records)
        print(
            f"| {backend} | {len(records)} | {summary('fps')} | "
            f"{summary('e2e_p95_ms')} | {summary('infer_p95_ms')} | "
            f"{min_uptime:.3f} | {failures} |"
        )

    quality_errors = []
    for path, record in rows:
        frames = int(numeric(record, "frames"))
        media_pts = int(numeric(record, "media_pts"))
        software_start = int(numeric(record, "software_start"))
        if frames <= 0:
            quality_errors.append(f"{path}: no measured frames")
        if record["backend"] == "rkmpi" and media_pts != frames:
            quality_errors.append(
                f"{path}: rkmpi media_pts={media_pts}, frames={frames}; "
                "e2e timing origin is mixed"
            )
        if media_pts + software_start != frames:
            quality_errors.append(
                f"{path}: timestamp counters do not sum to measured frames"
            )

    if quality_errors:
        print("\nData-quality warnings:", file=sys.stderr)
        for error in quality_errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
