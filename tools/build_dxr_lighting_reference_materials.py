#!/usr/bin/env python3
"""Build the isolated constant-PBR package used by the DXR lighting GPU test."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image

from compile_pbr_asset_pack import CHANNELS, compile_pack


MATERIAL_SIZE = (4, 4)


def write_constant_png(path: Path, color: tuple[int, int, int, int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.new("RGBA", MATERIAL_SIZE, color).save(path, format="PNG")


def build_material(
    source_dir: Path,
    name: str,
    source_asset_id: int,
    base_color: tuple[int, int, int, int],
    metalness: int,
    roughness: int,
    emissive_factor: float = 0.0,
) -> dict[str, object]:
    colors = {
        "base_color": base_color,
        "normal": (128, 128, 255, 255),
        "metalness": (metalness, metalness, metalness, 255),
        "roughness": (roughness, roughness, roughness, 255),
        "emissive": (
            (255, 255, 255, 255)
            if emissive_factor > 0.0
            else (0, 0, 0, 255)
        ),
    }
    channels = {
        channel: f"walls/{name}_{channel}.png" for channel in CHANNELS
    }
    for channel, color in colors.items():
        write_constant_png(source_dir / channels[channel], color)
    return {
        "name": name,
        "class": "wall",
        "width": MATERIAL_SIZE[0],
        "height": MATERIAL_SIZE[1],
        "normal_strength": 1.0,
        "specular_factor": 1.0,
        "alpha_mode": "opaque",
        "emissive_factor": [emissive_factor] * 3,
        "two_sided": False,
        "binding": {
            "kind": "shared_wall",
            "source_asset_id": source_asset_id,
            "v_period": 1,
        },
        "channels": channels,
        "source": {"files": ["tests/dxr_lighting_reference"]},
    }


def build_reference_package(source_dir: Path, output_dir: Path) -> Path:
    materials = [
        build_material(
            source_dir,
            "reference_roughness_below_016",
            0xF0000001,
            (128, 128, 128, 255),
            0,
            40,
        ),
        build_material(
            source_dir,
            "reference_roughness_020",
            0xF0000002,
            (128, 128, 128, 255),
            0,
            51,
        ),
        build_material(
            source_dir,
            "reference_roughness_030",
            0xF0000003,
            (128, 128, 128, 255),
            0,
            77,
        ),
        build_material(
            source_dir,
            "reference_roughness_100",
            0xF0000004,
            (128, 128, 128, 255),
            0,
            255,
        ),
        build_material(
            source_dir,
            "reference_metal_roughness_020",
            0xF0000005,
            (180, 96, 48, 255),
            255,
            51,
        ),
        build_material(
            source_dir,
            "reference_emitter",
            0xF0000006,
            (255, 255, 255, 255),
            0,
            255,
            200.0,
        ),
    ]
    manifest = {
        "schema_version": 7,
        "channels": list(CHANNELS),
        "materials": materials,
        "non_color_source_assets": [],
    }
    spec_path = source_dir / "materials.json"
    spec_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return compile_pack(source_dir, spec_path, output_dir)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    arguments = parser.parse_args()
    manifest = build_reference_package(
        arguments.source_dir.resolve(), arguments.output_dir.resolve()
    )
    print(manifest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
