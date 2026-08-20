#!/usr/bin/env python3
"""Validate and stage the category-sorted artist PBR PNG pack for DXR."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import shutil
import struct
import sys
from pathlib import Path, PurePosixPath

try:
    from PIL import Image
except ImportError as error:  # pragma: no cover - build-host diagnostic
    raise SystemExit(
        "compile_pbr_asset_pack requires Pillow; install the Python 'Pillow' package"
    ) from error


CHANNELS = ("base_color", "normal", "metalness", "roughness", "emissive")
RUNTIME_MAGIC = b"AB3PBR4\0"
RUNTIME_VERSION = 4
RUNTIME_SOURCE_NONE = 0
RUNTIME_SOURCE_SHARED_WALL = 1
RUNTIME_SOURCE_SHARED_FLOOR = 2
RUNTIME_SOURCE_VECTOR = 3
RUNTIME_SOURCE_BITMAP = 4
RUNTIME_ALPHA = {"opaque": 0, "mask": 1, "additive": 2}
RUNTIME_FLAG_EMISSIVE_TEXTURE = 1 << 8
RUNTIME_FLAG_TWO_SIDED = 1 << 9
RUNTIME_FLAG_VECTOR_GLARE = 1 << 10
RUNTIME_CLASS_SHIFT = 12
RUNTIME_HEADER = struct.Struct("<8sIIII")
RUNTIME_RECORD = struct.Struct("<IIIIIIffffI96s")
BITMAP_MODES = {
    "bitmap": 0,
    "lighted_2": 2,
    "lighted_3": 3,
    "lighted_4": 4,
    "lighted_5": 5,
    "additive": 6,
    "glare": 7,
}
MATERIAL_DIRECTORIES = {
    "wall": "walls",
    "floor": "floors",
    "weapon": "weapons",
    "vector_model": "vector_models",
    "enemy_billboard": "enemies",
    "billboard": "billboards",
    "effect_billboard": "effects",
    "environment": "environment",
    "ui": "ui",
}
RUNTIME_CLASSES = {
    "wall": 1,
    "floor": 2,
    "weapon": 3,
    "vector_model": 4,
    "enemy_billboard": 5,
    "billboard": 6,
    "effect_billboard": 7,
    "environment": 8,
    "ui": 9,
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require_relative_file(value: object, description: str) -> str:
    if not isinstance(value, str):
        raise ValueError(f"{description} is not a string")
    path = PurePosixPath(value)
    if (
        "\\" in value
        or path.is_absolute()
        or len(path.parts) != 2
        or any(part in ("", ".", "..") for part in path.parts)
    ):
        raise ValueError(f"{description} is not a category-relative filename: {value}")
    return value


def validate_binding(binding: object, name: str) -> tuple[int, int, int, int]:
    if binding is None:
        return RUNTIME_SOURCE_NONE, 0xFFFFFFFF, 0, 0
    if not isinstance(binding, dict):
        raise ValueError(f"PBR material {name} has a malformed binding")
    kind = binding.get("kind")
    asset_id = binding.get("source_asset_id")
    if not isinstance(asset_id, int) or not 0 <= asset_id <= 0xFFFFFFFF:
        raise ValueError(f"PBR material {name} has an invalid source asset ID")
    if kind == "shared_wall":
        if set(binding) != {"kind", "source_asset_id"}:
            raise ValueError(f"PBR wall binding {name} has unexpected fields")
        return RUNTIME_SOURCE_SHARED_WALL, asset_id, 0, 0
    if kind == "shared_floor":
        if set(binding) != {"kind", "source_asset_id"}:
            raise ValueError(f"PBR floor binding {name} has unexpected fields")
        return RUNTIME_SOURCE_SHARED_FLOOR, asset_id, 0, 0
    if kind == "vector":
        fields = (
            "map_offset",
            "minimum_u",
            "maximum_u",
            "minimum_v",
            "maximum_v",
            "glare",
        )
        if set(binding) != {"kind", "source_asset_id", *fields}:
            raise ValueError(f"PBR vector binding {name} has unexpected fields")
        values = [binding.get(field) for field in fields]
        if (
            not all(isinstance(value, int) for value in values)
            or not 0 <= values[0] <= 0xFFFFFFFF
            or not all(0 <= value <= 255 for value in values[1:5])
            or values[1] > values[2]
            or values[3] > values[4]
            or values[5] not in (0, 1)
        ):
            raise ValueError(f"PBR vector binding {name} is invalid")
        packed_uv = values[1] | values[2] << 8 | values[3] << 16 | values[4] << 24
        return RUNTIME_SOURCE_VECTOR, asset_id, values[0], packed_uv
    if kind == "bitmap":
        if set(binding) != {"kind", "source_asset_id", "frame_index", "mode"}:
            raise ValueError(f"PBR bitmap binding {name} has unexpected fields")
        frame = binding.get("frame_index")
        mode = binding.get("mode")
        if not isinstance(frame, int) or not 0 <= frame <= 0xFFFFFFFF or mode not in BITMAP_MODES:
            raise ValueError(f"PBR bitmap binding {name} is invalid")
        return RUNTIME_SOURCE_BITMAP, asset_id, frame, BITMAP_MODES[mode]
    raise ValueError(f"PBR material {name} has unsupported binding kind: {kind}")


def validate_source_metadata(material: dict, name: str) -> None:
    source = material.get("source")
    if not isinstance(source, dict) or not isinstance(source.get("files"), list):
        raise ValueError(f"PBR material {name} has incomplete source provenance")
    for value in source["files"]:
        if not isinstance(value, str):
            raise ValueError(f"PBR material {name} has a non-string source path")
        path = Path(value)
        if path.is_absolute() or ".." in path.parts or re.match(r"^[A-Za-z]:", value):
            raise ValueError(f"PBR material {name} contains a private/absolute source path")


def compile_pack(source_dir: Path, spec_path: Path, output_dir: Path) -> Path:
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    if not isinstance(spec, dict) or spec.get("schema_version") != 4:
        raise ValueError("PBR artist manifest must use schema_version 4")
    materials = spec.get("materials")
    if not isinstance(materials, list) or not materials:
        raise ValueError("PBR artist manifest contains no materials")
    if spec.get("channels") != list(CHANNELS):
        raise ValueError("PBR artist manifest channel contract is invalid")

    names: set[str] = set()
    bindings: set[tuple[int, int, int, int]] = set()
    expected_pngs: set[str] = set()
    runtime_records: list[bytes] = []
    output_materials: list[dict[str, object]] = []
    output_dir.mkdir(parents=True, exist_ok=True)

    for material in materials:
        if not isinstance(material, dict):
            raise ValueError("PBR artist manifest material is not an object")
        name = material.get("name")
        if (
            not isinstance(name, str)
            or not re.fullmatch(r"[a-z0-9_]+", name)
            or len(name.encode("ascii")) > 95
            or name in names
        ):
            raise ValueError(f"PBR artist manifest contains invalid/duplicate name: {name}")
        names.add(name)
        material_class = material.get("class")
        if material_class not in MATERIAL_DIRECTORIES:
            raise ValueError(f"PBR material {name} has an invalid class")
        width = material.get("width")
        height = material.get("height")
        normal_strength = material.get("normal_strength")
        if (
            not isinstance(width, int)
            or not isinstance(height, int)
            or not 0 < width <= 8192
            or not 0 < height <= 8192
            or not isinstance(normal_strength, (int, float))
            or not math.isfinite(normal_strength)
            or normal_strength <= 0.0
        ):
            raise ValueError(f"PBR material {name} has invalid dimensions/normal strength")
        alpha_mode = material.get("alpha_mode")
        if alpha_mode not in RUNTIME_ALPHA:
            raise ValueError(f"PBR material {name} has an invalid alpha mode")
        emissive_factor = material.get("emissive_factor")
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
            raise ValueError(f"PBR material {name} has invalid emissive radiance")
        binding = validate_binding(material.get("binding"), name)
        allowed_classes = {
            RUNTIME_SOURCE_NONE: {"wall", "environment", "ui"},
            RUNTIME_SOURCE_SHARED_WALL: {"wall"},
            RUNTIME_SOURCE_SHARED_FLOOR: {"floor"},
            RUNTIME_SOURCE_VECTOR: {"weapon", "vector_model"},
            RUNTIME_SOURCE_BITMAP: {
                "enemy_billboard", "billboard", "effect_billboard"
            },
        }[binding[0]]
        if material_class not in allowed_classes:
            raise ValueError(
                f"PBR material {name} class disagrees with its source binding"
            )
        if binding[0] != RUNTIME_SOURCE_NONE and binding in bindings:
            raise ValueError(f"PBR artist manifest contains duplicate binding: {binding}")
        bindings.add(binding)
        validate_source_metadata(material, name)

        channel_spec = material.get("channels")
        if not isinstance(channel_spec, dict) or set(channel_spec) != set(CHANNELS):
            raise ValueError(f"PBR material {name} does not list all five channels")
        output_channels: dict[str, dict[str, object]] = {}
        for channel in CHANNELS:
            filename = require_relative_file(
                channel_spec[channel], f"PBR material {name} {channel} channel"
            )
            expected_filename = (
                f"{MATERIAL_DIRECTORIES[material_class]}/{name}_{channel}.png"
            )
            if filename != expected_filename or filename in expected_pngs:
                raise ValueError(f"PBR material {name} has an invalid/duplicate channel filename")
            source_path = source_dir.joinpath(*PurePosixPath(filename).parts)
            if not source_path.is_file():
                raise ValueError(f"PBR material channel is missing: {source_path}")
            data = source_path.read_bytes()
            with Image.open(source_path) as opened:
                opened.load()
                if opened.format != "PNG" or opened.size != (width, height):
                    raise ValueError(
                        f"PBR channel {filename} is not a {width}x{height} PNG"
                    )
                rgba = opened.convert("RGBA")
                output_channels[channel] = {
                    "file": filename,
                    "sha256": sha256(data),
                    "pixel_sha256": sha256(rgba.tobytes()),
                    "source_mode": opened.mode,
                }
            expected_pngs.add(filename)
            output_path = output_dir.joinpath(*PurePosixPath(filename).parts)
            output_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source_path, output_path)

        flags = RUNTIME_ALPHA[alpha_mode]
        if any(value > 0.0 for value in emissive_factor):
            flags |= RUNTIME_FLAG_EMISSIVE_TEXTURE
        if material.get("two_sided") is True:
            flags |= RUNTIME_FLAG_TWO_SIDED
        if binding[0] == RUNTIME_SOURCE_VECTOR and material["binding"]["glare"]:
            flags |= RUNTIME_FLAG_VECTOR_GLARE
        flags |= RUNTIME_CLASSES[material_class] << RUNTIME_CLASS_SHIFT
        encoded_name = name.encode("ascii")
        runtime_records.append(
            RUNTIME_RECORD.pack(
                binding[0],
                binding[1],
                binding[2],
                binding[3],
                width,
                height,
                float(normal_strength),
                float(emissive_factor[0]),
                float(emissive_factor[1]),
                float(emissive_factor[2]),
                flags,
                encoded_name + bytes(96 - len(encoded_name)),
            )
        )
        output_material = dict(material)
        output_material["channels"] = output_channels
        output_materials.append(output_material)

    discovered_pngs = {
        path.relative_to(source_dir).as_posix()
        for path in source_dir.rglob("*.png")
    }
    if discovered_pngs != expected_pngs:
        raise ValueError(
            "PBR artist directory/manifest mismatch; "
            f"unlisted={sorted(discovered_pngs - expected_pngs)}, "
            f"missing={sorted(expected_pngs - discovered_pngs)}"
        )
    runtime = bytearray(
        RUNTIME_HEADER.pack(
            RUNTIME_MAGIC,
            RUNTIME_VERSION,
            len(runtime_records),
            len(CHANNELS),
            RUNTIME_RECORD.size,
        )
    )
    for record in runtime_records:
        runtime.extend(record)
    runtime_path = output_dir / "material_runtime.bin"
    runtime_path.write_bytes(runtime)
    expected_outputs = expected_pngs | {"material_runtime.bin", "material_manifest.json"}
    unexpected = sorted(
        path.relative_to(output_dir).as_posix()
        for path in output_dir.rglob("*")
        if path.is_file() and path.relative_to(output_dir).as_posix() not in expected_outputs
    )
    if unexpected:
        raise ValueError(f"PBR runtime output contains stale/unexpected files: {unexpected}")
    manifest = {
        "schema_version": 4,
        "generator": "tools/compile_pbr_asset_pack.py",
        "source_manifest_sha256": sha256(spec_path.read_bytes()),
        "materials": output_materials,
        "runtime_package": {
            "file": runtime_path.name,
            "format": "AB3PBR4",
            "sha256": sha256(runtime),
            "contains_pixels": False,
        },
        "non_color_source_assets": spec.get("non_color_source_assets", []),
    }
    manifest_path = output_dir / "material_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return manifest_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--spec", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        manifest = compile_pack(
            arguments.source_dir.resolve(),
            arguments.spec.resolve(),
            arguments.output_dir.resolve(),
        )
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"PBR runtime package build failed: {error}", file=sys.stderr)
        return 1
    print(manifest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
