#!/usr/bin/env python3
"""Summarize LOADTIME_* records emitted by the Wii U profiling build."""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path
import re
import statistics


# PROFILE10_* keeps the analyzer compatible with captures made before the
# instrumentation was promoted to the repository-wide build flag.
EVENT_RE = re.compile(r"\b(LOADTIME_[A-Z_]+|PROFILE10_[A-Z_]+)(?:\s+(.*))?$")
FIELD_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)")


def parse_log(path: Path) -> dict:
    result = {
        "path": path,
        "modes": [],
        "startups": [],
        "saves": [],
        "save_io": [],
        "post_save": [],
        "post_save_textures": [],
        "runtime_textures": [],
        "vcutm_windows": [],
        "levels": {},
    }
    levels: dict[int, dict] = result["levels"]

    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            match = EVENT_RE.search(line.rstrip())
            if not match:
                continue
            event, payload = match.groups()
            fields = dict(FIELD_RE.findall(payload or ""))

            if event == "LOADTIME_SESSION":
                result["modes"].append(fields.get("precache", "unknown"))
                continue
            if event == "LOADTIME_STARTUP_END":
                result["startups"].append(fields)
                continue
            if event == "LOADTIME_SAVE_END":
                result["saves"].append(fields)
                continue
            if event == "LOADTIME_SAVE_IO":
                result["save_io"].append(fields)
                continue
            if event == "LOADTIME_POST_SAVE_END":
                result["post_save"].append(fields)
                continue
            if event == "LOADTIME_POST_SAVE_TEXTURE":
                result["post_save_textures"].append(fields)
                continue
            if event == "LOADTIME_RUNTIME_TEXTURE":
                result["runtime_textures"].append(fields)
                continue
            if event in {
                "LOADTIME_VCUTM_TIMING",
                "LOADTIME_VCUTM_SCENE",
                "PROFILE10_VCUTM_TIMING",
                "PROFILE10_VCUTM_SCENE",
            }:
                window = fields.get("window")
                record = next(
                    (item for item in result["vcutm_windows"] if item.get("window") == window),
                    None,
                )
                if record is None:
                    record = {"window": window}
                    result["vcutm_windows"].append(record)
                record.update(fields)
                continue

            try:
                sequence = int(fields["sequence"])
            except (KeyError, ValueError):
                continue
            record = levels.setdefault(sequence, {"sequence": sequence})
            record.update({"level": fields.get("level"), "name": fields.get("name", "UNKNOWN")})
            if event == "LOADTIME_PRELOAD_END":
                record["preload_ms"] = number(fields.get("total_us"), divisor=1000)
                record["requested"] = integer(fields.get("requested"))
                record["loaded"] = integer(fields.get("loaded"))
            elif event == "LOADTIME_LEVEL_READY":
                record["ready_ms"] = number(fields.get("transition_to_ready_us"), divisor=1000)
                record["init_ms"] = number(fields.get("init_us"), divisor=1000)
                record["precache"] = fields.get("precache")
            elif event == "LOADTIME_FIRST_FRAME":
                record["first_frame_ms"] = number(fields.get("total_us"), divisor=1000)
                record["ready_to_frame_ms"] = number(fields.get("ready_to_frame_us"), divisor=1000)
                record["precache"] = fields.get("precache")
            elif event == "LOADTIME_LEVEL_ABORT":
                record["aborted"] = True

    return result


def integer(value: str | None) -> int | None:
    try:
        return int(value) if value is not None else None
    except ValueError:
        return None


def number(value: str | None, divisor: float = 1) -> float | None:
    parsed = integer(value)
    return parsed / divisor if parsed is not None else None


def median(values: list[float | None]) -> float | None:
    present = [value for value in values if value is not None]
    return statistics.median(present) if present else None


def fmt(value: float | None) -> str:
    return "-" if value is None else f"{value:.1f}"


def mode_of(result: dict) -> str:
    modes = set(result["modes"])
    if len(modes) == 1:
        return modes.pop()
    return "mixed" if modes else "unknown"


def grouped_levels(result: dict) -> dict[str, list[dict]]:
    grouped: dict[str, list[dict]] = defaultdict(list)
    for record in result["levels"].values():
        if not record.get("aborted"):
            grouped[record.get("name", "UNKNOWN")].append(record)
    return grouped


def print_log(result: dict) -> None:
    mode = mode_of(result)
    print(f"\n## {result['path'].name} - precache {mode}")
    startup = median([number(item.get("total_us"), 1000) for item in result["startups"]])
    print(f"\nStartup texture work: {fmt(startup)} ms")
    saves = [number(item.get("total_us"), 1000) for item in result["saves"]]
    if saves:
        first_save = saves[0]
        later_saves = median(saves[1:])
        print(
            f"Save confirmation: first {fmt(first_save)} ms; "
            f"later median {fmt(later_saves)} ms ({len(saves)} total)"
        )
    if result["save_io"]:
        print(
            "Save I/O median: "
            f"open {fmt(median([number(i.get('open_us'), 1000) for i in result['save_io']]))} ms; "
            f"write {fmt(median([number(i.get('write_us'), 1000) for i in result['save_io']]))} ms; "
            f"close {fmt(median([number(i.get('close_us'), 1000) for i in result['save_io']]))} ms"
        )
    if result["post_save"]:
        texture_counts = [number(item.get("textures")) for item in result["post_save"]]
        texture_times = [number(item.get("texture_us"), 1000) for item in result["post_save"]]
        print(
            f"Post-save texture misses: median {fmt(median(texture_counts))}; "
            f"texture time {fmt(median(texture_times))} ms"
        )
    if result["post_save_textures"]:
        slowest = sorted(
            result["post_save_textures"],
            key=lambda item: integer(item.get("total_us")) or 0,
            reverse=True,
        )[:10]
        print("\nSlowest post-save texture misses:")
        for item in slowest:
            print(
                f"- {fmt(number(item.get('total_us'), 1000))} ms: "
                f"{item.get('path', 'unknown')}"
            )
    if result["runtime_textures"]:
        by_level: dict[str, list[dict]] = defaultdict(list)
        for item in result["runtime_textures"]:
            label = f"level {item.get('level', '?')} area {item.get('area', '?')}"
            by_level[label].append(item)
        total_ms = sum(number(item.get("total_us"), 1000) or 0 for item in result["runtime_textures"])
        print(f"\nRuntime texture misses: {len(result['runtime_textures'])} ({total_ms:.1f} ms total)")
        for label, items in sorted(by_level.items()):
            print(f"- {label}: {len(items)}")
        slowest = sorted(
            result["runtime_textures"],
            key=lambda item: integer(item.get("total_us")) or 0,
            reverse=True,
        )[:15]
        print("\nSlowest runtime texture misses:")
        for item in slowest:
            print(
                f"- {fmt(number(item.get('total_us'), 1000))} ms "
                f"(level {item.get('level', '?')}): {item.get('path', 'unknown')}"
            )
    if result["vcutm_windows"]:
        windows = result["vcutm_windows"]
        worst = max(windows, key=lambda item: integer(item.get("max_total_us")) or 0)
        frame_counts = sorted({item.get("frames", "?") for item in windows})
        print(f"\nVCutM frame profile: {len(windows)} windows ({'/'.join(frame_counts)} frames each)")
        print(
            f"- median FPS: {fmt(median([number(i.get('fps_x1000'), 1000) for i in windows]))}"
        )
        print(
            "- median stage times: "
            f"pre-submit {fmt(median([number(i.get('avg_pre_submit_us'), 1000) for i in windows]))} ms; "
            f"render setup {fmt(median([number(i.get('avg_setup_us'), 1000) for i in windows]))} ms; "
            f"display list {fmt(median([number(i.get('avg_dl_us'), 1000) for i in windows]))} ms; "
            f"audio/rumble {fmt(median([number(i.get('avg_audio_us'), 1000) for i in windows]))} ms; "
            f"GX2 finish {fmt(median([number(i.get('avg_finish_us'), 1000) for i in windows]))} ms; "
            f"present {fmt(median([number(i.get('avg_present_us'), 1000) for i in windows]))} ms"
        )
        print(
            f"- worst frame: {fmt(number(worst.get('max_total_us'), 1000))} ms; "
            f"Mario {worst.get('mario', '?')}; camera {worst.get('camera', '?')}; "
            f"focus {worst.get('focus', '?')}; yaw {worst.get('yaw', '?')}"
        )
    print("\n| Level | Runs | Preload ms | Ready ms | First frame ms |")
    print("|---|---:|---:|---:|---:|")
    for name, records in sorted(grouped_levels(result).items()):
        print(
            f"| {name} | {len(records)} | "
            f"{fmt(median([r.get('preload_ms') for r in records]))} | "
            f"{fmt(median([r.get('ready_ms') for r in records]))} | "
            f"{fmt(median([r.get('first_frame_ms') for r in records]))} |"
        )


def print_comparison(results: list[dict]) -> None:
    by_mode = {mode_of(result): result for result in results}
    if "false" not in by_mode or "true" not in by_mode:
        return
    false_levels = grouped_levels(by_mode["false"])
    true_levels = grouped_levels(by_mode["true"])
    shared = sorted(set(false_levels) & set(true_levels))
    if not shared:
        return

    print("\n## Comparison - first visible frame")
    print("\nNegative delta means `precache true` reached the level sooner.")
    print("\n| Level | False ms | True ms | True − false ms |")
    print("|---|---:|---:|---:|")
    for name in shared:
        false_ms = median([r.get("first_frame_ms") for r in false_levels[name]])
        true_ms = median([r.get("first_frame_ms") for r in true_levels[name]])
        delta = true_ms - false_ms if false_ms is not None and true_ms is not None else None
        print(f"| {name} | {fmt(false_ms)} | {fmt(true_ms)} | {fmt(delta)} |")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path, help="One or more captured Wii U logs")
    args = parser.parse_args()
    results = [parse_log(path) for path in args.logs]
    print("# Wii U level load-time report")
    for result in results:
        print_log(result)
    print_comparison(results)


if __name__ == "__main__":
    main()
