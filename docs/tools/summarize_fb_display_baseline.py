#!/usr/bin/env python3
"""Summarize RV1106 framebuffer display baseline capture directories."""

from __future__ import annotations

import csv
import math
import re
import statistics
import sys
from collections import Counter
from pathlib import Path


DISPLAY_RATE_RE = re.compile(r"Display update:\s+(\d+) kB/s, fps=(\d+)")
KEY_VALUE_RE = re.compile(r"^([^=]+)=(.*)$")


def read_key_values(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for raw_line in stream:
            match = KEY_VALUE_RE.match(raw_line.rstrip("\n"))
            if match:
                values[match.group(1)] = match.group(2)
    return values


def numeric(row: dict[str, str], key: str) -> float:
    value = row.get(key, "NA")
    if value in {"", "NA"}:
        return math.nan
    return float(value)


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(value for value in values if math.isfinite(value))
    if not ordered:
        return math.nan
    index = max(0, math.ceil(len(ordered) * fraction) - 1)
    return ordered[index]


def display_geometry(capture: Path) -> tuple[int, int, int] | None:
    text = (capture / "fb_sysfs.txt").read_text(encoding="utf-8", errors="replace")
    size_match = re.search(r"^virtual_size=(\d+),(\d+)", text, re.MULTILINE)
    bpp_match = re.search(r"^bits_per_pixel=(\d+)", text, re.MULTILINE)
    if not size_match or not bpp_match:
        return None
    return int(size_match.group(1)), int(size_match.group(2)), int(bpp_match.group(1))


def display_rates(capture: Path) -> list[float]:
    before_path = capture / "dmesg_before.txt"
    after_path = capture / "dmesg_after.txt"
    if not after_path.exists():
        return []
    after_lines = after_path.read_text(encoding="utf-8", errors="replace").splitlines()
    if before_path.exists():
        before_lines = Counter(
            before_path.read_text(encoding="utf-8", errors="replace").splitlines()
        )
        delta_lines = []
        for line in after_lines:
            if before_lines[line] > 0:
                before_lines[line] -= 1
            else:
                delta_lines.append(line)
    else:
        delta_lines = after_lines
    return [
        float(match.group(1))
        for line in delta_lines
        if (match := DISPLAY_RATE_RE.search(line))
    ]


def summarize_capture(capture: Path) -> dict[str, object]:
    manifest = read_key_values(capture / "manifest.txt")
    with (capture / "process_samples.tsv").open(
        "r", encoding="utf-8", errors="replace", newline=""
    ) as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    if len(rows) < 2:
        raise ValueError("process_samples.tsv needs at least two rows")

    interval_cpu: list[float] = []
    interval_busy: list[float] = []
    pids: set[str] = set()
    for previous, current in zip(rows, rows[1:]):
        previous_pid = previous.get("pid", "NA")
        current_pid = current.get("pid", "NA")
        if current_pid != "NA":
            pids.add(current_pid)

        previous_cpu = [
            numeric(previous, name)
            for name in (
                "cpu_user",
                "cpu_nice",
                "cpu_system",
                "cpu_idle",
                "cpu_iowait",
                "cpu_irq",
                "cpu_softirq",
                "cpu_steal",
            )
        ]
        current_cpu = [
            numeric(current, name)
            for name in (
                "cpu_user",
                "cpu_nice",
                "cpu_system",
                "cpu_idle",
                "cpu_iowait",
                "cpu_irq",
                "cpu_softirq",
                "cpu_steal",
            )
        ]
        if not all(math.isfinite(value) for value in previous_cpu + current_cpu):
            continue
        deltas = [end - start for start, end in zip(previous_cpu, current_cpu)]
        total_delta = sum(deltas)
        if total_delta <= 0:
            continue
        idle_delta = deltas[3] + deltas[4]
        interval_busy.append(100.0 * (total_delta - idle_delta) / total_delta)

        if previous_pid != current_pid or current_pid == "NA":
            continue
        previous_proc = numeric(previous, "proc_utime") + numeric(previous, "proc_stime")
        current_proc = numeric(current, "proc_utime") + numeric(current, "proc_stime")
        if math.isfinite(previous_proc) and math.isfinite(current_proc):
            interval_cpu.append(100.0 * (current_proc - previous_proc) / total_delta)

    rss_values = [numeric(row, "vmrss_kb") for row in rows]
    hwm_values = [numeric(row, "vmhwm_kb") for row in rows]
    rss_values = [value for value in rss_values if math.isfinite(value)]
    hwm_values = [value for value in hwm_values if math.isfinite(value)]

    geometry = display_geometry(capture)
    rates = display_rates(capture)
    full_frame_fps: list[float] = []
    if geometry:
        width, height, bpp = geometry
        frame_bytes = width * height * bpp / 8
        full_frame_fps = [rate * 1024 / frame_bytes for rate in rates]

    return {
        "capture": capture,
        "scenario": manifest.get("scenario", "unknown"),
        "samples": len(rows),
        "duration_s": numeric(rows[-1], "elapsed_s") - numeric(rows[0], "elapsed_s"),
        "pids": sorted(pids),
        "process_cpu_avg": statistics.fmean(interval_cpu) if interval_cpu else math.nan,
        "process_cpu_p95": percentile(interval_cpu, 0.95),
        "system_busy_avg": statistics.fmean(interval_busy) if interval_busy else math.nan,
        "rss_avg_kb": statistics.fmean(rss_values) if rss_values else math.nan,
        "rss_max_kb": max(rss_values) if rss_values else math.nan,
        "hwm_max_kb": max(hwm_values) if hwm_values else math.nan,
        "display_rate_avg_kbps": statistics.fmean(rates) if rates else math.nan,
        "full_frame_fps_avg": statistics.fmean(full_frame_fps) if full_frame_fps else math.nan,
        "geometry": geometry,
        "rate_samples": len(rates),
    }


def formatted(value: object, digits: int = 2) -> str:
    if isinstance(value, float):
        if not math.isfinite(value):
            return "N/A"
        return f"{value:.{digits}f}"
    return str(value)


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(f"usage: {argv[0]} CAPTURE_DIR [CAPTURE_DIR ...]", file=sys.stderr)
        return 2

    summaries = []
    for name in argv[1:]:
        capture = Path(name)
        try:
            summaries.append(summarize_capture(capture))
        except (OSError, ValueError) as error:
            print(f"{capture}: {error}", file=sys.stderr)
            return 1

    print(
        "| capture | scenario | duration s | process CPU avg % | process CPU p95 % | "
        "system busy avg % | RSS avg/max KiB | HWM max KiB | display kB/s | "
        "implied full-screen FPS |"
    )
    print(
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
    )
    for summary in summaries:
        rss = f"{formatted(summary['rss_avg_kb'], 1)}/{formatted(summary['rss_max_kb'], 1)}"
        print(
            f"| {summary['capture']} | {summary['scenario']} | "
            f"{formatted(summary['duration_s'], 0)} | "
            f"{formatted(summary['process_cpu_avg'])} | "
            f"{formatted(summary['process_cpu_p95'])} | "
            f"{formatted(summary['system_busy_avg'])} | {rss} | "
            f"{formatted(summary['hwm_max_kb'], 1)} | "
            f"{formatted(summary['display_rate_avg_kbps'], 1)} | "
            f"{formatted(summary['full_frame_fps_avg'])} |"
        )

    warnings = []
    for summary in summaries:
        if len(summary["pids"]) != 1:
            warnings.append(
                f"{summary['capture']}: observed DeskBot PIDs={summary['pids']}; "
                "process was absent or restarted"
            )
        if summary["rate_samples"]:
            warnings.append(
                f"{summary['capture']}: fbtft throughput came from kernel debug logs; "
                "debug output can perturb the measurement"
            )
        else:
            warnings.append(
                f"{summary['capture']}: no 'Display update' records; SPI throughput is N/A"
            )

    if warnings:
        print("\nData-quality notes:", file=sys.stderr)
        for warning in warnings:
            print(f"- {warning}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
