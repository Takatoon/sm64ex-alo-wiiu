#!/usr/bin/env python3
"""Build deterministic PNG texture packs for SM64EX-ALO on Wii U."""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import io
import json
import os
import re
import sys
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Iterable

try:
    import numpy as np
    from PIL import Image, UnidentifiedImageError, __version__ as PILLOW_VERSION
except ImportError as exc:  # pragma: no cover - exercised only without dependencies
    raise SystemExit(
        "Missing image dependencies. Install them with: "
        f"{sys.executable} -m pip install Pillow numpy"
    ) from exc


TOOL_VERSION = "1.9.0"
PROFILE_FACTORS: dict[str, int | None] = {
    "full-720": 3,
    "full-720-hd-hud": 3,
    "max-6x": 6,
    "half-source": None,
    "quarter-source": None,
    "clean-ex-alo": None,
}
ALLOWED_PREFIXES = ("gfx/actors/", "gfx/levels/", "gfx/textures/")
BUILD_ONLY_PREFIXES = ("gfx/textures/skyboxes/",)
MAX_6X_HALF_SOURCE_PREFIXES = (
    "gfx/levels/castle_grounds/",
    "gfx/textures/outside/",
)
ZIP_EPOCH = (1980, 1, 1, 0, 0, 0)
DEFAULT_MAX_ENTRY_BYTES = 256 * 1024 * 1024
DEFAULT_MAX_TOTAL_BYTES = 4 * 1024 * 1024 * 1024
DEFAULT_MAX_PIXELS = 64 * 1024 * 1024


class PackError(Exception):
    """A user-correctable input pack or manifest error."""


@dataclass(frozen=True)
class Dimensions:
    width: int
    height: int
    source: str


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_zip_path(raw_name: str) -> str:
    """Validate a ZIP member name and return its canonical POSIX form."""
    if not raw_name or "\x00" in raw_name:
        raise PackError("empty path or NUL byte")
    if "\\" in raw_name:
        raise PackError("backslash path separator is not allowed")
    if raw_name.startswith("/") or raw_name.startswith("//"):
        raise PackError("absolute path is not allowed")
    if re.match(r"^[A-Za-z]:", raw_name):
        raise PackError("drive-qualified path is not allowed")

    path = PurePosixPath(raw_name)
    if any(part in ("", ".", "..") for part in path.parts):
        raise PackError("empty, '.' or '..' path component is not allowed")
    canonical = path.as_posix()
    if canonical != raw_name.rstrip("/"):
        raise PackError("path is not canonical")
    return canonical


def is_allowed_png(path: str) -> bool:
    return (
        path.lower().endswith(".png")
        and path.startswith(ALLOWED_PREFIXES)
        and not path.startswith(BUILD_ONLY_PREFIXES)
    )


def family_for(path: str) -> str:
    return path.split("/", 2)[1]


def load_ui_patterns(path: Path) -> list[str]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise PackError(f"cannot read HUD path list {path}: {exc}") from exc
    patterns: list[str] = []
    for line_number, raw_line in enumerate(lines, 1):
        pattern = raw_line.strip()
        if not pattern or pattern.startswith("#"):
            continue
        if (
            "\\" in pattern
            or pattern.startswith("/")
            or ".." in PurePosixPath(pattern).parts
            or not pattern.startswith(ALLOWED_PREFIXES)
        ):
            raise PackError(f"unsafe HUD pattern at {path}:{line_number}: {pattern}")
        patterns.append(pattern)
    if not patterns:
        raise PackError(f"HUD path list contains no patterns: {path}")
    return patterns


def matches_any(path: str, patterns: list[str]) -> bool:
    return any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)


def fit_without_upscale(
    input_size: tuple[int, int], maximum_size: tuple[int, int]
) -> tuple[int, int]:
    """Fit inside maximum_size, retaining aspect ratio and never enlarging."""
    width, height = input_size
    maximum_width, maximum_height = maximum_size
    scale = min(1.0, maximum_width / width, maximum_height / height)
    if scale == 1.0:
        return input_size
    return max(1, round(width * scale)), max(1, round(height * scale))


def half_source_with_minimum(
    input_size: tuple[int, int], minimum_size: tuple[int, int]
) -> tuple[int, int]:
    """Halve both axes while retaining aspect ratio and a minimum 3x detail level."""
    width, height = input_size
    minimum_width, minimum_height = minimum_size
    scale = min(
        1.0,
        max(0.5, minimum_width / width, minimum_height / height),
    )
    if scale == 1.0:
        return input_size
    return max(1, round(width * scale)), max(1, round(height * scale))


def quarter_source_with_minimum(
    input_size: tuple[int, int], minimum_size: tuple[int, int]
) -> tuple[int, int]:
    """Quarter both axes while retaining aspect ratio and a minimum 3x detail level."""
    width, height = input_size
    minimum_width, minimum_height = minimum_size
    scale = min(
        1.0,
        max(0.25, minimum_width / width, minimum_height / height),
    )
    if scale == 1.0:
        return input_size
    return max(1, round(width * scale)), max(1, round(height * scale))


def _parse_dimension_value(path: str, value: Any, source: str) -> Dimensions:
    if isinstance(value, list) and len(value) >= 2:
        width, height = value[0], value[1]
    elif isinstance(value, dict):
        width, height = value.get("width"), value.get("height")
    else:
        raise PackError(f"invalid dimensions for {path!r} in {source}")
    if type(width) is not int or type(height) is not int or width <= 0 or height <= 0:
        raise PackError(f"dimensions for {path!r} in {source} must be positive integers")
    return Dimensions(width, height, source)


def load_dimensions(assets_json: Path) -> dict[str, Dimensions]:
    try:
        assets = json.loads(assets_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise PackError(f"cannot read assets JSON {assets_json}: {exc}") from exc
    if not isinstance(assets, dict):
        raise PackError("assets JSON root must be an object")

    dimensions: dict[str, Dimensions] = {}
    for asset_path, value in assets.items():
        if not isinstance(asset_path, str):
            raise PackError("assets JSON contains a non-string path")
        canonical = "gfx/" + PurePosixPath(asset_path).as_posix().lstrip("/")
        if canonical.startswith(ALLOWED_PREFIXES):
            # Most records begin with width and height. A few raw/special assets
            # only expose their byte length and ROM offsets; those deliberately
            # remain unresolved until supplied through --dimensions.
            if (
                isinstance(value, list)
                and len(value) >= 2
                and type(value[0]) is int
                and type(value[1]) is int
                and value[0] > 0
                and value[1] > 0
            ):
                dimensions[canonical] = Dimensions(value[0], value[1], "assets.json")

    return dimensions


def apply_dimension_overrides(
    dimensions: dict[str, Dimensions], overrides_json: Path | None
) -> None:
    if overrides_json is None:
        return
    try:
        overrides = json.loads(overrides_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise PackError(f"cannot read dimensions JSON {overrides_json}: {exc}") from exc
    if isinstance(overrides, dict) and "dimensions" in overrides:
        overrides = overrides["dimensions"]
    if not isinstance(overrides, dict):
        raise PackError("dimensions JSON root (or 'dimensions') must be an object")
    for raw_path, value in overrides.items():
        if not isinstance(raw_path, str):
            raise PackError("dimensions JSON contains a non-string path")
        canonical = canonical_zip_path(raw_path)
        if not is_allowed_png(canonical):
            raise PackError(f"override path is outside the supported PNG trees: {canonical}")
        dimensions[canonical] = _parse_dimension_value(canonical, value, "dimensions override")


def load_base_zip_dimensions(
    base_zip: Path,
    max_entry_bytes: int,
    max_total_bytes: int,
    max_pixels: int,
) -> dict[str, Dimensions]:
    """Read authoritative dimensions and supported paths from a local base.zip."""
    if not base_zip.is_file():
        raise PackError(f"base ZIP does not exist: {base_zip}")
    dimensions: dict[str, Dimensions] = {}
    try:
        with zipfile.ZipFile(base_zip, "r") as archive:
            selected, _ = inspect_zip(archive, max_entry_bytes, max_total_bytes)
            for canonical, info in selected:
                try:
                    with archive.open(info) as stream:
                        with Image.open(stream) as image:
                            if image.width * image.height > max_pixels:
                                raise PackError(
                                    f"base image exceeds --max-pixels ({max_pixels}): {canonical}"
                                )
                            dimensions[canonical] = Dimensions(
                                image.width, image.height, "base.zip"
                            )
                except (UnidentifiedImageError, OSError) as exc:
                    raise PackError(f"invalid PNG in base ZIP {canonical}: {exc}") from exc
    except (zipfile.BadZipFile, OSError) as exc:
        raise PackError(f"cannot read base ZIP {base_zip}: {exc}") from exc
    return dimensions


def inspect_zip(
    archive: zipfile.ZipFile,
    max_entry_bytes: int,
    max_total_bytes: int,
) -> tuple[list[tuple[str, zipfile.ZipInfo]], list[str]]:
    selected: list[tuple[str, zipfile.ZipInfo]] = []
    skipped: list[str] = []
    exact: set[str] = set()
    folded: dict[str, str] = {}
    total = 0
    problems: list[str] = []

    for info in archive.infolist():
        try:
            canonical = canonical_zip_path(info.filename)
        except PackError as exc:
            problems.append(f"{info.filename!r}: {exc}")
            continue
        if info.is_dir():
            continue
        if canonical in exact:
            problems.append(f"duplicate ZIP path: {canonical}")
            continue
        exact.add(canonical)
        folded_path = canonical.casefold()
        if folded_path in folded:
            problems.append(
                f"case-insensitive path collision: {folded[folded_path]} and {canonical}"
            )
            continue
        folded[folded_path] = canonical
        if info.file_size < 0 or info.file_size > max_entry_bytes:
            problems.append(
                f"entry exceeds --max-entry-bytes ({max_entry_bytes}): {canonical}"
            )
            continue
        total += info.file_size
        if total > max_total_bytes:
            problems.append(f"archive exceeds --max-total-bytes ({max_total_bytes})")
            break
        if is_allowed_png(canonical):
            selected.append((canonical, info))
        else:
            skipped.append(canonical)

    if problems:
        raise PackError("invalid input ZIP:\n  - " + "\n  - ".join(problems))
    if not selected:
        raise PackError("input ZIP has no PNG files under gfx/actors, gfx/levels or gfx/textures")
    return sorted(selected, key=lambda item: item[0]), sorted(skipped)


def resize_linear_premultiplied(image: Image.Image, size: tuple[int, int]) -> Image.Image:
    """Resize RGBA in linear light with premultiplied alpha."""
    rgba = np.asarray(image.convert("RGBA"), dtype=np.float32) / np.float32(255.0)
    rgb = rgba[..., :3]
    alpha = rgba[..., 3]
    linear = np.where(
        rgb <= np.float32(0.04045),
        rgb / np.float32(12.92),
        ((rgb + np.float32(0.055)) / np.float32(1.055)) ** np.float32(2.4),
    )
    premultiplied = linear * alpha[..., None]
    planes = [premultiplied[..., index] for index in range(3)] + [alpha]
    resized = [
        np.asarray(
            Image.fromarray(plane, mode="F").resize(size, Image.Resampling.LANCZOS),
            dtype=np.float32,
        )
        for plane in planes
    ]
    resized_premultiplied = np.stack(resized[:3], axis=-1)
    resized_alpha = np.clip(resized[3], 0.0, 1.0)
    resized_linear = np.zeros_like(resized_premultiplied)
    np.divide(
        resized_premultiplied,
        resized_alpha[..., None],
        out=resized_linear,
        where=resized_alpha[..., None] > np.float32(1.0 / 65535.0),
    )
    resized_linear = np.clip(resized_linear, 0.0, 1.0)
    srgb = np.where(
        resized_linear <= np.float32(0.0031308),
        resized_linear * np.float32(12.92),
        np.float32(1.055) * (resized_linear ** np.float32(1.0 / 2.4)) - np.float32(0.055),
    )
    output = np.empty((*size[::-1], 4), dtype=np.uint8)
    output[..., :3] = np.floor(np.clip(srgb, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    output[..., 3] = np.floor(resized_alpha * 255.0 + 0.5).astype(np.uint8)
    return Image.fromarray(output, mode="RGBA")


def encode_png(image: Image.Image) -> bytes:
    output = io.BytesIO()
    image.save(output, format="PNG", optimize=False, compress_level=9)
    return output.getvalue()


def deterministic_zip_info(path: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(path, ZIP_EPOCH)
    info.compress_type = zipfile.ZIP_STORED
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    return info


def _atomic_target(path: Path) -> tuple[Path, int]:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    return Path(temporary), descriptor


def build_pack(
    input_zip: Path,
    assets_json: Path,
    output_zip: Path,
    report_json: Path,
    profile: str = "full-720",
    dimensions_json: Path | None = None,
    base_zip: Path | None = None,
    ui_paths: Path | None = None,
    max_entry_bytes: int = DEFAULT_MAX_ENTRY_BYTES,
    max_total_bytes: int = DEFAULT_MAX_TOTAL_BYTES,
    max_pixels: int = DEFAULT_MAX_PIXELS,
) -> dict[str, Any]:
    factor = PROFILE_FACTORS[profile]
    clean_only = profile == "clean-ex-alo"
    half_source = profile == "half-source"
    quarter_source = profile == "quarter-source"
    resolved_input = input_zip.resolve()
    resolved_output = output_zip.resolve()
    resolved_report = report_json.resolve()
    if len({resolved_input, resolved_output, resolved_report}) != 3:
        raise PackError("input, output and report must be three different files")
    protected_inputs = {resolved_input, assets_json.resolve()}
    if dimensions_json is not None:
        protected_inputs.add(dimensions_json.resolve())
    if base_zip is not None:
        protected_inputs.add(base_zip.resolve())
    if ui_paths is not None:
        protected_inputs.add(ui_paths.resolve())
    if resolved_output in protected_inputs or resolved_report in protected_inputs:
        raise PackError("output and report must not overwrite an input or manifest")
    if not input_zip.is_file():
        raise PackError(f"input ZIP does not exist: {input_zip}")
    if clean_only and base_zip is None:
        raise PackError("profile clean-ex-alo requires --base-zip")

    dimensions = load_dimensions(assets_json)
    supported_paths: set[str] | None = None
    if base_zip is not None:
        base_dimensions = load_base_zip_dimensions(
            base_zip, max_entry_bytes, max_total_bytes, max_pixels
        )
        dimensions.update(base_dimensions)
        supported_paths = set(base_dimensions)
    apply_dimension_overrides(dimensions, dimensions_json)
    ui_patterns: list[str] = []
    if profile == "full-720-hd-hud":
        if ui_paths is None:
            raise PackError("profile full-720-hd-hud requires --ui-paths")
        ui_patterns = load_ui_patterns(ui_paths)
    try:
        with zipfile.ZipFile(input_zip, "r") as source:
            selected, skipped = inspect_zip(source, max_entry_bytes, max_total_bytes)
            unsupported: list[str] = []
            if supported_paths is not None:
                unsupported = [path for path, _ in selected if path not in supported_paths]
                selected = [item for item in selected if item[0] in supported_paths]
                if not selected:
                    raise PackError("input ZIP has no PNG paths supported by the supplied base ZIP")
            unresolved = [path for path, _ in selected if path not in dimensions]
            if unresolved:
                preview = "\n  - ".join(unresolved[:50])
                suffix = "\n  - ..." if len(unresolved) > 50 else ""
                raise PackError(
                    f"{len(unresolved)} PNG path(s) have no original dimensions; "
                    "add them with --dimensions:\n  - " + preview + suffix
                )

            temp_output, descriptor = _atomic_target(output_zip)
            os.close(descriptor)
            entries: list[dict[str, Any]] = []
            try:
                with zipfile.ZipFile(temp_output, "w", allowZip64=True) as destination:
                    for canonical, info in selected:
                        raw = source.read(info)
                        try:
                            with Image.open(io.BytesIO(raw)) as opened:
                                if getattr(opened, "n_frames", 1) != 1:
                                    raise PackError(f"animated PNG is unsupported: {canonical}")
                                opened.load()
                                input_size = [opened.width, opened.height]
                                if opened.width * opened.height > max_pixels:
                                    raise PackError(
                                        f"decoded image exceeds --max-pixels ({max_pixels}): {canonical}"
                                    )
                                input_mode = opened.mode
                                original = dimensions[canonical]
                                preserve_hd = matches_any(canonical, ui_patterns)
                                optimize_castle_grounds = (
                                    profile == "max-6x"
                                    and canonical.startswith(MAX_6X_HALF_SOURCE_PREFIXES)
                                )
                                castle_grounds_half_applied = False
                                if clean_only:
                                    target = opened.size
                                elif half_source:
                                    target = half_source_with_minimum(
                                        opened.size,
                                        (original.width * 3, original.height * 3),
                                    )
                                elif quarter_source:
                                    target = quarter_source_with_minimum(
                                        opened.size,
                                        (original.width * 3, original.height * 3),
                                    )
                                elif optimize_castle_grounds:
                                    half_target = half_source_with_minimum(
                                        opened.size,
                                        (original.width * 3, original.height * 3),
                                    )
                                    max_6x_target = fit_without_upscale(
                                        opened.size,
                                        (original.width * 6, original.height * 6),
                                    )
                                    if half_target[0] * half_target[1] <= (
                                        max_6x_target[0] * max_6x_target[1]
                                    ):
                                        target = half_target
                                        castle_grounds_half_applied = True
                                    else:
                                        target = max_6x_target
                                else:
                                    assert factor is not None
                                    maximum_target = (
                                        original.width * factor, original.height * factor
                                    )
                                    target = (
                                        opened.size
                                        if preserve_hd
                                        else fit_without_upscale(opened.size, maximum_target)
                                    )
                                if target[0] * target[1] > max_pixels:
                                    raise PackError(
                                        f"target image exceeds --max-pixels ({max_pixels}): {canonical}"
                                    )
                                preserve_below_target = (
                                    not clean_only and not preserve_hd and target == opened.size
                                )
                                converted = None
                                if not clean_only:
                                    converted = (
                                        opened.convert("RGBA")
                                        if preserve_hd or preserve_below_target
                                        else resize_linear_premultiplied(opened, target)
                                    )
                        except (UnidentifiedImageError, OSError) as exc:
                            raise PackError(f"invalid PNG {canonical}: {exc}") from exc

                        encoded = raw if clean_only else encode_png(converted)
                        destination.writestr(deterministic_zip_info(canonical), encoded)
                        entries.append(
                            {
                                "path": canonical,
                                "family": family_for(canonical),
                                "dimensions_source": original.source,
                                "original_size": [original.width, original.height],
                                "input_size": input_size,
                                "output_size": list(target),
                                "input_mode": input_mode,
                                "output_mode": input_mode if clean_only else "RGBA",
                                "profile_action": (
                                    "copy-source-clean"
                                    if clean_only
                                    else (
                                        "preserve-source-hd"
                                        if preserve_hd
                                        else (
                                            "preserve-source-below-target"
                                            if preserve_below_target
                                            else (
                                                "resize-half-source"
                                                if half_source
                                                else (
                                                    "resize-quarter-source"
                                                    if quarter_source
                                                    else (
                                                        "resize-half-source-castle-grounds"
                                                        if castle_grounds_half_applied
                                                        else "resize-linear-premultiplied"
                                                    )
                                                )
                                            )
                                        )
                                    )
                                ),
                                "input_png_bytes": len(raw),
                                "output_png_bytes": len(encoded),
                                "estimated_rgba8_bytes": target[0] * target[1] * 4,
                                "sha256": sha256_bytes(encoded),
                            }
                        )
                os.replace(temp_output, output_zip)
            except Exception:
                temp_output.unlink(missing_ok=True)
                raise
    except (zipfile.BadZipFile, OSError) as exc:
        raise PackError(f"cannot read input ZIP {input_zip}: {exc}") from exc

    selected_paths = {entry["path"] for entry in entries}
    expected_paths = sorted(
        supported_paths
        if supported_paths is not None
        else (path for path in dimensions if path.startswith(ALLOWED_PREFIXES))
    )
    missing = [path for path in expected_paths if path not in selected_paths]
    report: dict[str, Any] = {
        "schema_version": 2,
        "tool": "wiiu_texture_pack/build_pack.py",
        "tool_version": TOOL_VERSION,
        "runtime": {
            "python": sys.version.split()[0],
            "pillow": PILLOW_VERSION,
            "numpy": np.__version__,
        },
        "profile": profile,
        "scale_factor": factor,
        "source_dimension_multiplier": (
            0.5 if half_source else (0.25 if quarter_source else None)
        ),
        "minimum_scale_factor": 3 if half_source or quarter_source else None,
        "input": {
            "name": input_zip.name,
            "bytes": input_zip.stat().st_size,
            "sha256": sha256_file(input_zip),
        },
        "base": (
            {
                "name": base_zip.name,
                "bytes": base_zip.stat().st_size,
                "sha256": sha256_file(base_zip),
            }
            if base_zip is not None
            else None
        ),
        "output": {
            "name": output_zip.name,
            "bytes": output_zip.stat().st_size,
            "sha256": sha256_file(output_zip),
            "zip_compression": "store",
        },
        "summary": {
            "converted_png_count": len(entries),
            "copied_clean_png_count": sum(
                entry["profile_action"] == "copy-source-clean" for entry in entries
            ),
            "removed_input_entry_count": len(skipped) + len(unsupported),
            "preserved_hd_hud_count": sum(
                entry["profile_action"] == "preserve-source-hd" for entry in entries
            ),
            "preserved_below_target_count": sum(
                entry["profile_action"] == "preserve-source-below-target"
                for entry in entries
            ),
            "skipped_entry_count": len(skipped),
            "unsupported_pack_entry_count": len(unsupported),
            "missing_known_asset_count": len(missing),
            "base_only_asset_count": len(missing),
            "output_png_bytes": sum(entry["output_png_bytes"] for entry in entries),
            "estimated_rgba8_bytes": sum(entry["estimated_rgba8_bytes"] for entry in entries),
        },
        "entries": entries,
        "skipped_entries": skipped,
        "unsupported_pack_entries": unsupported,
        "missing_known_assets": missing,
        "inventory": {
            "pack_replacement_assets": sorted(selected_paths),
            "base_only_assets": missing,
            "unsupported_pack_assets": unsupported,
            "skipped_auxiliary_entries": skipped,
        },
        "hud_patterns": ui_patterns,
    }
    encoded_report = (json.dumps(report, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    temp_report, descriptor = _atomic_target(report_json)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(encoded_report)
        os.replace(temp_report, report_json)
    except Exception:
        temp_report.unlink(missing_ok=True)
        raise
    return report


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return parsed


def parse_args(argv: Iterable[str] | None = None) -> argparse.Namespace:
    repository_root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(
        description=(
            "Convert an SM64EX HD texture ZIP to a deterministic Wii U 720p "
            "PNG pack, or clean it for an exact EX-ALO release."
        )
    )
    parser.add_argument("--input", required=True, type=Path, help="source SM64EX HD ZIP")
    parser.add_argument(
        "--assets-json",
        type=Path,
        default=repository_root / "assets.json",
        help="SM64EX-ALO assets.json (defaults to the repository copy)",
    )
    parser.add_argument(
        "--dimensions",
        type=Path,
        help="optional JSON overrides for canonical paths missing from assets.json",
    )
    parser.add_argument(
        "--base-zip",
        type=Path,
        help=(
            "optional local base.zip used as the authoritative path allowlist and "
            "dimension source for this exact build"
        ),
    )
    parser.add_argument(
        "--ui-paths",
        type=Path,
        default=repository_root / "tools" / "wiiu_texture_pack" / "ui_paths.txt",
        help="HUD glob allowlist used by the full-720-hd-hud profile",
    )
    parser.add_argument("--profile", choices=sorted(PROFILE_FACTORS), default="full-720")
    parser.add_argument("--output", required=True, type=Path, help="destination ZIP")
    parser.add_argument("--report", required=True, type=Path, help="destination JSON report")
    parser.add_argument("--max-entry-bytes", type=positive_int, default=DEFAULT_MAX_ENTRY_BYTES)
    parser.add_argument("--max-total-bytes", type=positive_int, default=DEFAULT_MAX_TOTAL_BYTES)
    parser.add_argument("--max-pixels", type=positive_int, default=DEFAULT_MAX_PIXELS)
    return parser.parse_args(argv)


def main(argv: Iterable[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        report = build_pack(
            input_zip=args.input,
            assets_json=args.assets_json,
            dimensions_json=args.dimensions,
            base_zip=args.base_zip,
            ui_paths=args.ui_paths,
            profile=args.profile,
            output_zip=args.output,
            report_json=args.report,
            max_entry_bytes=args.max_entry_bytes,
            max_total_bytes=args.max_total_bytes,
            max_pixels=args.max_pixels,
        )
    except (PackError, KeyError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    summary = report["summary"]
    print(
        f"Converted {summary['converted_png_count']} PNG files to {args.output} "
        f"({report['output']['sha256']})."
    )
    print(f"Report: {args.report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
