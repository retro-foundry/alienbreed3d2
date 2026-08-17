#!/usr/bin/env python3
"""Build renderer-native DXR material textures from project-authored sheets."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError as error:  # pragma: no cover - exercised by build hosts without Pillow
    raise SystemExit(
        "build_dxr_materials requires Pillow; install the Python 'Pillow' package"
    ) from error


CHANNELS = ("base_color", "normal", "metalness", "roughness")
FOREGROUND_THRESHOLD = 12
ROW_COVERAGE = 0.35
COLUMN_COVERAGE = 0.30
MINIMUM_PANEL_SIZE = 16


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def largest_true_run(values: list[bool], description: str) -> tuple[int, int]:
    best: tuple[int, int] | None = None
    start: int | None = None
    for index, value in enumerate(values + [False]):
        if value and start is None:
            start = index
        elif not value and start is not None:
            candidate = (start, index)
            if best is None or candidate[1] - candidate[0] > best[1] - best[0]:
                best = candidate
            start = None
    if best is None:
        raise ValueError(f"unable to locate {description}")
    return best


def row_band(image: Image.Image, box: tuple[int, int, int, int]) -> tuple[int, int]:
    pixels = image.load()
    left, top, right, bottom = box
    minimum = (right - left) * ROW_COVERAGE
    rows = [
        sum(max(pixels[x, y]) > FOREGROUND_THRESHOLD for x in range(left, right))
        >= minimum
        for y in range(top, bottom)
    ]
    first, last = largest_true_run(rows, "material panel rows")
    return top + first, top + last


def column_range(
    image: Image.Image, left: int, right: int, top: int, bottom: int
) -> tuple[int, int]:
    pixels = image.load()
    minimum = (bottom - top) * COLUMN_COVERAGE
    columns = [
        sum(max(pixels[x, y]) > FOREGROUND_THRESHOLD for y in range(top, bottom))
        >= minimum
        for x in range(left, right)
    ]
    occupied = [index for index, value in enumerate(columns) if value]
    if not occupied:
        raise ValueError("unable to locate material panel columns")
    return left + occupied[0], left + occupied[-1] + 1


def grid_panel_boxes(image: Image.Image) -> dict[str, tuple[int, int, int, int]]:
    width, height = image.size
    middle_x = width // 2
    middle_y = height // 2
    top = row_band(image, (middle_x, 0, width, middle_y))
    bottom = row_band(image, (middle_x, middle_y, width, height))
    left = column_range(image, 0, middle_x, *top)
    top_right = column_range(image, middle_x, width, *top)
    bottom_right = column_range(image, middle_x, width, *bottom)
    return {
        "base_color": (left[0], top[0], left[1], top[1]),
        "normal": (top_right[0], top[0], top_right[1], top[1]),
        "metalness": (left[0], bottom[0], left[1], bottom[1]),
        "roughness": (bottom_right[0], bottom[0], bottom_right[1], bottom[1]),
    }


def vertical_panel_boxes(image: Image.Image) -> dict[str, tuple[int, int, int, int]]:
    width, height = image.size
    bands = [
        row_band(image, (0, index * height // 4, width, (index + 1) * height // 4))
        for index in range(4)
    ]
    normal_columns = column_range(image, 0, width, *bands[1])
    return {
        channel: (normal_columns[0], band[0], normal_columns[1], band[1])
        for channel, band in zip(CHANNELS, bands, strict=True)
    }


def validate_spec(spec: object, source_dir: Path) -> dict:
    if not isinstance(spec, dict) or spec.get("schema_version") != 1:
        raise ValueError("DXR material source manifest must use schema_version 1")
    materials = spec.get("materials")
    defaults = spec.get("material_defaults")
    if not isinstance(materials, list) or not materials or not isinstance(defaults, dict):
        raise ValueError("DXR material source manifest is incomplete")
    names: set[str] = set()
    sheets: set[str] = set()
    bindings: set[tuple[str, int]] = set()
    for material in materials:
        if not isinstance(material, dict):
            raise ValueError("DXR material entry is not an object")
        name = material.get("name")
        sheet = material.get("sheet")
        layout = material.get("layout")
        if not isinstance(name, str) or not re.fullmatch(r"[a-z0-9_]+", name):
            raise ValueError("DXR material name is invalid")
        if name in names:
            raise ValueError(f"duplicate DXR material name: {name}")
        if not isinstance(sheet, str) or Path(sheet).name != sheet or not sheet.endswith(".png"):
            raise ValueError(f"DXR material {name} has an invalid source sheet")
        if sheet in sheets:
            raise ValueError(f"duplicate DXR material source sheet: {sheet}")
        if layout not in ("grid", "vertical"):
            raise ValueError(f"DXR material {name} has an invalid sheet layout")
        if not (source_dir / sheet).is_file():
            raise ValueError(f"DXR material source sheet is missing: {sheet}")
        binding = material.get("binding")
        if binding is not None:
            if (
                not isinstance(binding, dict)
                or binding.get("source") != "shared_wall"
                or not isinstance(binding.get("source_asset_id"), int)
                or binding["source_asset_id"] < 0
            ):
                raise ValueError(f"DXR material {name} has an invalid source binding")
            key = (binding["source"], binding["source_asset_id"])
            if key in bindings:
                raise ValueError(f"duplicate DXR material source binding: {key}")
            bindings.add(key)
        names.add(name)
        sheets.add(sheet)
    discovered = {path.name for path in source_dir.glob("*.png")}
    if discovered != sheets:
        missing = sorted(discovered - sheets)
        stale = sorted(sheets - discovered)
        raise ValueError(
            f"DXR material manifest/source mismatch; unlisted={missing}, missing={stale}"
        )
    return spec


def save_png(image: Image.Image, path: Path) -> dict[str, object]:
    image.save(path, format="PNG", optimize=False, compress_level=9)
    data = path.read_bytes()
    return {
        "file": path.name,
        "sha256": sha256_bytes(data),
        "pixel_sha256": sha256_bytes(image.tobytes()),
        "mode": image.mode,
    }


def build_materials(source_dir: Path, spec_path: Path, output_dir: Path) -> Path:
    spec = validate_spec(json.loads(spec_path.read_text(encoding="utf-8")), source_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    expected_files = {"material_manifest.json"}
    output_materials = []
    defaults = spec["material_defaults"]

    for material in sorted(spec["materials"], key=lambda entry: entry["name"]):
        source_path = source_dir / material["sheet"]
        source_bytes = source_path.read_bytes()
        with Image.open(source_path) as opened:
            source_image = opened.convert("RGB")
        boxes = (
            grid_panel_boxes(source_image)
            if material["layout"] == "grid"
            else vertical_panel_boxes(source_image)
        )
        for channel, box in boxes.items():
            if box[2] - box[0] < MINIMUM_PANEL_SIZE or box[3] - box[1] < MINIMUM_PANEL_SIZE:
                raise ValueError(f"DXR material {material['name']} {channel} panel is too small")

        base_box = boxes["base_color"]
        output_size = (base_box[2] - base_box[0], base_box[3] - base_box[1])
        channels = {}
        for channel in CHANNELS:
            panel = source_image.crop(boxes[channel])
            if panel.size != output_size:
                panel = panel.resize(output_size, Image.Resampling.LANCZOS)
            filename = f"{material['name']}_{channel}.png"
            expected_files.add(filename)
            channels[channel] = save_png(panel, output_dir / filename)

        output_entry = {
            "name": material["name"],
            "source_sheet": material["sheet"],
            "source_sheet_sha256": sha256_bytes(source_bytes),
            "source_sheet_size": list(source_image.size),
            "sheet_layout": material["layout"],
            "panel_boxes": {channel: list(boxes[channel]) for channel in CHANNELS},
            "width": output_size[0],
            "height": output_size[1],
            "channels": channels,
            **defaults,
        }
        if "binding" in material:
            output_entry["binding"] = material["binding"]
        output_materials.append(output_entry)

    unexpected = sorted(
        existing.name
        for existing in output_dir.iterdir()
        if existing.is_file() and existing.name not in expected_files
    )
    if unexpected:
        raise ValueError(f"DXR material output contains unexpected files: {unexpected}")
    manifest = {
        "schema_version": 1,
        "generator": "tools/build_dxr_materials.py",
        "materials": output_materials,
        "missing_material": {
            "base_color": "decoded_source_albedo",
            "roughness": 1.0,
            "metalness": 0.0,
            "emissive_factor": [0.0, 0.0, 0.0],
        },
    }
    manifest_path = output_dir / "material_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n"
    )
    return manifest_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--spec", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        manifest = build_materials(
            arguments.source_dir.resolve(), arguments.spec.resolve(), arguments.output_dir.resolve()
        )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"DXR material build failed: {error}", file=sys.stderr)
        return 1
    print(manifest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
