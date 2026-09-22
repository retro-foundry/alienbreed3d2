#!/usr/bin/env python3
"""Build renderer-native DXR material textures from project-authored sheets."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError as error:  # pragma: no cover - exercised by build hosts without Pillow
    raise SystemExit(
        "build_dxr_materials requires Pillow; install the Python 'Pillow' package"
    ) from error


CHANNELS = ("base_color", "normal", "metalness", "roughness", "emissive")
SHEET_CHANNELS = CHANNELS[:4]
RUNTIME_MAGIC = b"AB3PBR2\0"
RUNTIME_VERSION = 2
RUNTIME_SOURCE_NONE = 0
RUNTIME_SOURCE_SHARED_WALL = 1
RUNTIME_SOURCE_SHARED_FLOOR = 2
RUNTIME_EMISSIVE_NONE = 0
RUNTIME_EMISSIVE_TEXTURE = 1
RUNTIME_HEADER = struct.Struct("<8sIIII")
RUNTIME_RECORD = struct.Struct("<IIIIffffII")
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
        for channel, band in zip(SHEET_CHANNELS, bands, strict=True)
    }


def emissive_from_albedo(name: str, albedo: Image.Image) -> Image.Image:
    """Reproduce the old renderer's explicit per-pixel light masks.

    This is a build-time conversion into a conventional emissive texture. The
    runtime never infers emission from base-color brightness.

    It applies only to authored PBR art. It must never be pointed at a
    source-decoded tile: source_floor_0101 decodes through the remap table's
    identity row, so every texel comes back at full brightness and asking which
    of them are bright keeps the whole texture. That is how the entire floor
    became a 164-radiance area light and the level's only illumination. The
    game has no emissive surfaces at all -- no palette entry survives the
    darkest shading row -- so a source tile's emission is always none.
    """
    pixels = []
    for red, green, blue in albedo.convert("RGB").getdata():
        brightest = max(red, green, blue)
        if name == "technolights":
            keep = brightest > 56 and (
                blue > red * 1.05
                or red > green * 1.15
                or green > max(red, blue) * 1.05
                or (red > 190 and green > 190 and blue > 190)
            )
        else:
            raise ValueError(f"DXR material {name} has no emissive-mask profile")
        pixels.append(
            (
                min(255, red * 2) if keep else 0,
                min(255, green * 2) if keep else 0,
                min(255, blue * 2) if keep else 0,
            )
        )
    result = Image.new("RGB", albedo.size)
    result.putdata(pixels)
    return result


def require_unpacked_source(path: Path) -> bytes:
    data = path.read_bytes()
    if data.startswith(b"=SB="):
        raise ValueError(f"DXR material input must be unpacked: {path}")
    return data


def read_display_palette(path: Path) -> list[tuple[int, int, int]]:
    data = require_unpacked_source(path)
    if len(data) < 256 * 6:
        raise ValueError(f"source display palette is malformed: {path}")
    return [
        tuple(
            min(int.from_bytes(data[index * 6 + channel:index * 6 + channel + 2], "big"), 255)
            for channel in (0, 2, 4)
        )
        for index in range(256)
    ]


def source_floor_0101(
    floor_path: Path, remap_path: Path, display_palette_path: Path
) -> Image.Image:
    """Decode the 64x64 floor_0101 tile through its authored bright palette row."""
    floor_data = require_unpacked_source(floor_path)
    remap = require_unpacked_source(remap_path)
    palette = read_display_palette(display_palette_path)
    tile_size = 64
    tile_row_stride = 1024
    tile_offset = 0x0101
    bright_row_offset = 32 * 256
    if len(floor_data) < tile_size * tile_row_stride:
        raise ValueError(f"source floor atlas is malformed: {floor_path}")
    if len(remap) < bright_row_offset + 256:
        raise ValueError(f"source floor palette is malformed: {remap_path}")
    result = Image.new("RGB", (tile_size, tile_size))
    pixels = []
    for y in range(tile_size):
        for x in range(tile_size):
            source_offset = (tile_offset + y * tile_row_stride + x * 4) % len(floor_data)
            pixels.append(palette[remap[bright_row_offset + floor_data[source_offset]]])
    result.putdata(pixels)
    return result


def validate_spec(spec: object, source_dir: Path) -> dict:
    if not isinstance(spec, dict) or spec.get("schema_version") != 2:
        raise ValueError("DXR material source manifest must use schema_version 2")
    materials = spec.get("materials")
    source_materials = spec.get("source_materials")
    defaults = spec.get("material_defaults")
    if (
        not isinstance(materials, list)
        or not materials
        or not isinstance(source_materials, list)
        or not source_materials
        or not isinstance(defaults, dict)
        or defaults.get("emissive_space") != "srgb"
    ):
        raise ValueError("DXR material source manifest is incomplete")
    names: set[str] = set()
    sheets: set[str] = set()
    bindings: set[tuple[str, int]] = set()

    def validate_common(material: object) -> None:
        if not isinstance(material, dict):
            raise ValueError("DXR material entry is not an object")
        name = material.get("name")
        if not isinstance(name, str) or not re.fullmatch(r"[a-z0-9_]+", name):
            raise ValueError("DXR material name is invalid")
        if name in names:
            raise ValueError(f"duplicate DXR material name: {name}")
        binding = material.get("binding")
        if binding is not None:
            if (
                not isinstance(binding, dict)
                or binding.get("source") not in ("shared_wall", "shared_floor")
                or not isinstance(binding.get("source_asset_id"), int)
                or binding["source_asset_id"] < 0
            ):
                raise ValueError(f"DXR material {name} has an invalid source binding")
            key = (binding["source"], binding["source_asset_id"])
            if key in bindings:
                raise ValueError(f"duplicate DXR material source binding: {key}")
            bindings.add(key)
        emissive_source = material.get(
            "emissive_source", defaults.get("emissive_source", "none")
        )
        emissive_factor = material.get(
            "emissive_factor", defaults.get("emissive_factor")
        )
        if emissive_source not in ("none", "albedo_mask"):
            raise ValueError(f"DXR material {name} has an invalid emissive source")
        if (
            not isinstance(emissive_factor, list)
            or len(emissive_factor) != 3
            or any(
                not isinstance(value, (int, float))
                or not math.isfinite(value)
                or value < 0.0
                for value in emissive_factor
            )
        ):
            raise ValueError(f"DXR material {name} has an invalid emissive factor")
        if emissive_source == "none" and any(value != 0.0 for value in emissive_factor):
            raise ValueError(
                f"DXR material {name} has emissive radiance without an emissive source"
            )
        if emissive_source == "albedo_mask" and not any(
            value > 0.0 for value in emissive_factor
        ):
            raise ValueError(
                f"DXR material {name} selects texture emission with zero radiance"
            )
        if emissive_source == "albedo_mask" and name not in (
            "technolights",
        ):
            raise ValueError(f"DXR material {name} has no approved emissive mask")
        names.add(name)

    for material in materials:
        validate_common(material)
        name = material["name"]
        sheet = material.get("sheet")
        layout = material.get("layout")
        if not isinstance(sheet, str) or Path(sheet).name != sheet or not sheet.endswith(".png"):
            raise ValueError(f"DXR material {name} has an invalid source sheet")
        if sheet in sheets:
            raise ValueError(f"duplicate DXR material source sheet: {sheet}")
        if layout not in ("grid", "vertical"):
            raise ValueError(f"DXR material {name} has an invalid sheet layout")
        if not (source_dir / sheet).is_file():
            raise ValueError(f"DXR material source sheet is missing: {sheet}")
        sheets.add(sheet)
    for material in source_materials:
        validate_common(material)
        if material.get("source_generator") != "floor_0101":
            raise ValueError(
                f"DXR material {material.get('name')} has an invalid source generator"
            )
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


def build_runtime_package(materials: list[dict[str, object]]) -> bytes:
    package = bytearray(
        RUNTIME_HEADER.pack(
            RUNTIME_MAGIC,
            RUNTIME_VERSION,
            len(materials),
            len(CHANNELS),
            RUNTIME_RECORD.size,
        )
    )
    pixels = bytearray()
    for material in materials:
        binding = material.get("binding")
        source_kind = RUNTIME_SOURCE_NONE
        source_asset_id = 0xFFFFFFFF
        if binding is not None:
            source_kind = (
                RUNTIME_SOURCE_SHARED_WALL
                if binding["source"] == "shared_wall"
                else RUNTIME_SOURCE_SHARED_FLOOR
            )
            source_asset_id = binding["source_asset_id"]
        emissive_source = (
            RUNTIME_EMISSIVE_TEXTURE
            if material["emissive_source"] == "texture"
            else RUNTIME_EMISSIVE_NONE
        )
        emissive = material["emissive_factor"]
        package.extend(
            RUNTIME_RECORD.pack(
                source_kind,
                source_asset_id,
                material["width"],
                material["height"],
                material["normal_strength"],
                emissive[0],
                emissive[1],
                emissive[2],
                emissive_source,
                0,
            )
        )
        for channel in CHANNELS:
            pixels.extend(material["runtime_pixels"][channel])
    package.extend(pixels)
    return bytes(package)


def build_materials(
    source_dir: Path,
    spec_path: Path,
    output_dir: Path,
    floor_source: Path,
    floor_remap: Path,
    display_palette: Path,
) -> Path:
    spec = validate_spec(json.loads(spec_path.read_text(encoding="utf-8")), source_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    expected_files = {"material_manifest.json", "material_runtime.bin"}
    output_materials = []
    defaults = spec["material_defaults"]

    def append_material(
        material: dict,
        images: dict[str, Image.Image],
        provenance: dict[str, object],
    ) -> None:
        output_size = images["base_color"].size
        if any(image.size != output_size for image in images.values()):
            raise ValueError(f"DXR material {material['name']} channel extents disagree")
        channels = {}
        runtime_pixels = {}
        for channel in CHANNELS:
            image = images[channel].convert("RGB")
            filename = f"{material['name']}_{channel}.png"
            expected_files.add(filename)
            channels[channel] = save_png(image, output_dir / filename)
            runtime_pixels[channel] = image.convert("RGBA").tobytes()
        source_mode = material.get(
            "emissive_source", defaults.get("emissive_source", "none")
        )
        output_entry = {
            "name": material["name"],
            "width": output_size[0],
            "height": output_size[1],
            "channels": channels,
            **defaults,
            **provenance,
            "emissive_source": "texture" if source_mode == "albedo_mask" else "none",
            "emissive_factor": material.get(
                "emissive_factor", defaults["emissive_factor"]
            ),
            "runtime_pixels": runtime_pixels,
        }
        if source_mode == "albedo_mask":
            output_entry["emissive_provenance"] = (
                "old_renderer_build_time_albedo_mask"
            )
        if "binding" in material:
            output_entry["binding"] = material["binding"]
        output_materials.append(output_entry)

    for material in spec["materials"]:
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
        images = {}
        for channel in SHEET_CHANNELS:
            panel = source_image.crop(boxes[channel])
            if panel.size != output_size:
                panel = panel.resize(output_size, Image.Resampling.LANCZOS)
            images[channel] = panel
        source_mode = material.get("emissive_source", defaults["emissive_source"])
        images["emissive"] = (
            emissive_from_albedo(material["name"], images["base_color"])
            if source_mode == "albedo_mask"
            else Image.new("RGB", output_size, (0, 0, 0))
        )
        append_material(
            material,
            images,
            {
                "source_sheet": material["sheet"],
                "source_sheet_sha256": sha256_bytes(source_bytes),
                "source_sheet_size": list(source_image.size),
                "sheet_layout": material["layout"],
                "panel_boxes": {
                    channel: list(boxes[channel]) for channel in SHEET_CHANNELS
                },
            },
        )

    for material in spec["source_materials"]:
        if material["source_generator"] != "floor_0101":
            raise ValueError(f"unsupported source material: {material['name']}")
        albedo = source_floor_0101(floor_source, floor_remap, display_palette)
        output_size = albedo.size
        append_material(
            material,
            {
                "base_color": albedo,
                "normal": Image.new("RGB", output_size, (128, 128, 255)),
                "metalness": Image.new("RGB", output_size, (0, 0, 0)),
                "roughness": Image.new("RGB", output_size, (255, 255, 255)),
                "emissive": (
                    emissive_from_albedo(material["name"], albedo)
                    if material.get("emissive_source") == "albedo_mask"
                    else Image.new("RGB", output_size, (0, 0, 0))
                ),
            },
            {
                "source_generator": material["source_generator"],
                "source_assets": {
                    "floor": {
                        "file": floor_source.name,
                        "sha256": sha256_bytes(floor_source.read_bytes()),
                    },
                    "remap": {
                        "file": floor_remap.name,
                        "sha256": sha256_bytes(floor_remap.read_bytes()),
                    },
                    "display_palette": {
                        "file": display_palette.name,
                        "sha256": sha256_bytes(display_palette.read_bytes()),
                    },
                },
            },
        )

    output_materials.sort(key=lambda entry: entry["name"])

    runtime_package = build_runtime_package(output_materials)
    runtime_path = output_dir / "material_runtime.bin"
    runtime_path.write_bytes(runtime_package)

    unexpected = sorted(
        existing.name
        for existing in output_dir.iterdir()
        if existing.is_file() and existing.name not in expected_files
    )
    if unexpected:
        raise ValueError(f"DXR material output contains unexpected files: {unexpected}")
    manifest_materials = []
    for material in output_materials:
        manifest_material = dict(material)
        del manifest_material["runtime_pixels"]
        manifest_materials.append(manifest_material)
    manifest = {
        "schema_version": 2,
        "generator": "tools/build_dxr_materials.py",
        "materials": manifest_materials,
        "runtime_package": {
            "file": runtime_path.name,
            "format": "AB3PBR2",
            "sha256": sha256_bytes(runtime_package),
        },
        "missing_material": {
            "base_color": "decoded_source_albedo",
            "roughness": 1.0,
            "metalness": 0.0,
            "emissive": 0.0,
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
    parser.add_argument("--floor-source", type=Path, required=True)
    parser.add_argument("--floor-remap", type=Path, required=True)
    parser.add_argument("--display-palette", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        manifest = build_materials(
            arguments.source_dir.resolve(),
            arguments.spec.resolve(),
            arguments.output_dir.resolve(),
            arguments.floor_source.resolve(),
            arguments.floor_remap.resolve(),
            arguments.display_palette.resolve(),
        )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"DXR material build failed: {error}", file=sys.stderr)
        return 1
    print(manifest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
