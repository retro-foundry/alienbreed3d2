#!/usr/bin/env python3
"""Build Q2RTX packed PBR textures from hand-authored texture sheets."""

from __future__ import annotations

import argparse
import pathlib
from typing import Dict, List, Sequence, Tuple

from PIL import Image


VERTICAL_SHEETS = {"gieger", "steampunk"}
EMISSIVE_TEXTURES = {"technolights", "floor_0101"}


def crop_content(image: Image.Image) -> Image.Image:
    rgb = image.convert("RGB")
    width, height = rgb.size
    pixels = rgb.load()

    rows: List[int] = []
    for y in range(height):
        active = sum(1 for x in range(width) if max(pixels[x, y]) > 24)
        if active / max(width, 1) > 0.08:
            rows.append(y)
    cols: List[int] = []
    for x in range(width):
        active = sum(1 for y in range(height) if max(pixels[x, y]) > 24)
        if active / max(height, 1) > 0.01:
            cols.append(x)

    if not rows or not cols:
        return image
    return image.crop((min(cols), min(rows), max(cols) + 1, max(rows) + 1))


def split_2x2(sheet: Image.Image) -> Dict[str, Image.Image]:
    width, height = sheet.size
    mid_x = width // 2
    mid_y = height // 2
    return {
        "albedo": crop_content(sheet.crop((0, 0, mid_x, mid_y))),
        "normal": crop_content(sheet.crop((mid_x, 0, width, mid_y))),
        "metalness": crop_content(sheet.crop((0, mid_y, mid_x, height))),
        "roughness": crop_content(sheet.crop((mid_x, mid_y, width, height))),
    }


def active_row_bands(sheet: Image.Image) -> List[Tuple[int, int]]:
    rgb = sheet.convert("RGB")
    width, height = rgb.size
    pixels = rgb.load()
    bands: List[Tuple[int, int]] = []
    start = None
    for y in range(height):
        active = sum(1 for x in range(width) if max(pixels[x, y]) > 24)
        is_active = active / max(width, 1) > 0.08
        if is_active and start is None:
            start = y
        if (not is_active or y == height - 1) and start is not None:
            end = y - 1 if not is_active else y
            if end - start > 10:
                bands.append((start, end + 1))
            start = None
    return bands


def split_vertical(sheet: Image.Image) -> Dict[str, Image.Image]:
    bands = active_row_bands(sheet)
    if len(bands) < 4:
        width, height = sheet.size
        step = height // 4
        bands = [(0, step), (step, step * 2), (step * 2, step * 3), (step * 3, height)]
    bands = bands[:4]
    keys = ("albedo", "normal", "metalness", "roughness")
    return {
        key: crop_content(sheet.crop((0, y0, sheet.width, y1)))
        for key, (y0, y1) in zip(keys, bands)
    }


def split_sheet(path: pathlib.Path) -> Dict[str, Image.Image]:
    sheet = Image.open(path).convert("RGBA")
    if path.stem.lower() in VERTICAL_SHEETS:
        return split_vertical(sheet)
    return split_2x2(sheet)


def resize_like(image: Image.Image, target: Image.Image, mode: int) -> Image.Image:
    if image.size == target.size:
        return image.convert("RGBA")
    return image.convert("RGBA").resize(target.size, mode)


def greyscale_values(image: Image.Image) -> List[int]:
    return list(image.convert("L").getdata())


def pack_base(albedo: Image.Image, roughness: Image.Image) -> Image.Image:
    rgba = albedo.convert("RGBA")
    rough = greyscale_values(resize_like(roughness, rgba, Image.Resampling.LANCZOS))
    out = Image.new("RGBA", rgba.size)
    out.putdata([(r, g, b, a) for (r, g, b, _old_a), a in zip(rgba.getdata(), rough)])
    return out


def pack_normal(normal: Image.Image, metalness: Image.Image, target: Image.Image) -> Image.Image:
    normal_rgba = resize_like(normal, target, Image.Resampling.LANCZOS)
    metal = greyscale_values(resize_like(metalness, target, Image.Resampling.LANCZOS))
    out = Image.new("RGBA", target.size)
    out.putdata([(r, g, max(96, b), a) for (r, g, b, _old_a), a in zip(normal_rgba.getdata(), metal)])
    return out


def greyscale_rgba(image: Image.Image, target: Image.Image) -> Image.Image:
    values = greyscale_values(resize_like(image, target, Image.Resampling.LANCZOS))
    out = Image.new("RGBA", target.size)
    out.putdata([(v, v, v, 255) for v in values])
    return out


def emissive_from_albedo(name: str, albedo: Image.Image) -> Image.Image:
    rgba = albedo.convert("RGBA")
    pixels = []
    for r, g, b, _a in rgba.getdata():
        bright = max(r, g, b)
        if name == "floor_0101":
            keep = b >= max(r, g) * 0.85 and bright > 40
        else:
            keep = bright > 56 and (b > r * 1.05 or r > g * 1.15 or (r > 190 and g > 190 and b > 190))
        if keep:
            pixels.append((min(255, r * 2), min(255, g * 2), min(255, b * 2), 255))
        else:
            pixels.append((0, 0, 0, 255))
    out = Image.new("RGBA", rgba.size)
    out.putdata(pixels)
    return out


def write_tga(path: pathlib.Path, image: Image.Image) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.convert("RGBA").save(path, format="TGA")


def material_factors(name: str) -> Tuple[float, float]:
    lowered = name.lower()
    if lowered.startswith("floor_") or lowered in {"rocky", "stonewall", "brownstonestep"}:
        return 0.15, 1.4
    if "metal" in lowered or "techno" in lowered or "door" in lowered or "pipe" in lowered:
        return 0.65, 1.3
    return 0.35, 1.35


def write_materials(path: pathlib.Path, names: Sequence[str]) -> None:
    lines = [
        "# Generated by tools/build_q2rtx_pbr_from_sheets.py",
        "# Base alpha stores roughness; normal alpha stores metalness.",
        "",
    ]
    for name in sorted(names):
        specular, base_factor = material_factors(name)
        lines.append(f"ab3d2/{name},")
        lines.append(f"textures/ab3d2/{name}:")
        lines.append(f"    texture_base overrides/ab3d2/{name}.tga")
        lines.append(f"    texture_normals overrides/ab3d2/{name}_n.tga")
        if name in EMISSIVE_TEXTURES:
            lines.append(f"    texture_emissive overrides/ab3d2/{name}_light.tga")
            lines.append("    is_light 1")
            lines.append("    emissive_factor 200")
            lines.append("    bsp_radiance 200")
        lines.append("    metalness_factor 1.0")
        lines.append(f"    specular_factor {specular:.2f}")
        lines.append(f"    base_factor {base_factor:.2f}")
        lines.append("    bump_scale 0.6")
        lines.append("    correct_albedo 1")
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="ascii")


def build(source_dir: pathlib.Path, out_root: pathlib.Path) -> int:
    override_dir = out_root / "baseq2" / "overrides" / "ab3d2"
    material_path = out_root / "baseq2" / "materials" / "ab3d2_pbr.mat"
    names: List[str] = []
    for sheet_path in sorted(source_dir.glob("*.png")):
        name = sheet_path.stem.lower()
        maps = split_sheet(sheet_path)
        albedo = maps["albedo"].convert("RGBA")
        write_tga(override_dir / f"{name}.tga", pack_base(albedo, maps["roughness"]))
        write_tga(override_dir / f"{name}_n.tga", pack_normal(maps["normal"], maps["metalness"], albedo))
        write_tga(override_dir / f"{name}_r.tga", greyscale_rgba(maps["roughness"], albedo))
        write_tga(override_dir / f"{name}_m.tga", greyscale_rgba(maps["metalness"], albedo))
        if name in EMISSIVE_TEXTURES:
            write_tga(override_dir / f"{name}_light.tga", emissive_from_albedo(name, albedo))
        names.append(name)
    write_materials(material_path, names)
    return len(names)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build Q2RTX packed PBR maps from texture sheets")
    parser.add_argument("--source-dir", type=pathlib.Path, default=pathlib.Path("textures_pbr"))
    parser.add_argument("--out-root", type=pathlib.Path, default=pathlib.Path("q2rtx_pbr"))
    args = parser.parse_args()
    count = build(args.source_dir, args.out_root)
    print(f"Built {count} packed PBR texture sets -> {args.out_root / 'baseq2' / 'overrides' / 'ab3d2'}")
    print(f"Generated material file -> {args.out_root / 'baseq2' / 'materials' / 'ab3d2_pbr.mat'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
