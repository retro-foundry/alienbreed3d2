#!/usr/bin/env python3
"""Generate Q2RTX PBR overrides from existing WAL albedo plus API-derived maps."""

from __future__ import annotations

import argparse
import asyncio
import base64
from io import BytesIO
import pathlib
import sys
import time
from typing import List, Sequence, Tuple

from openai import AsyncOpenAI
from PIL import Image

import generate_q2rtx_pbr as walpbr


EMISSIVE_TEXTURES = {"technolights", "floor_0101"}


def write_tga(path: pathlib.Path, image: Image.Image) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.convert("RGBA").save(path, format="TGA")


def write_png(path: pathlib.Path, image: Image.Image) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, format="PNG")


def image_to_png_bytes(image: Image.Image) -> bytes:
    out = BytesIO()
    image.save(out, format="PNG")
    return out.getvalue()


def decode_api_png(encoded: str) -> Image.Image:
    return Image.open(BytesIO(base64.b64decode(encoded))).convert("RGBA")


def rgba_from_wal(image: walpbr.WalImage, palette: Sequence[Tuple[int, int, int]]) -> Image.Image:
    out = Image.new("RGBA", (image.width, image.height))
    out.putdata([(*palette[index], 255) for index in image.pixels])
    return out


def greyscale_to_rgba(values: Sequence[int], size: Tuple[int, int]) -> Image.Image:
    out = Image.new("RGBA", size)
    out.putdata([(value, value, value, 255) for value in values])
    return out


def pack_alpha(image: Image.Image, alpha: Sequence[int]) -> Image.Image:
    rgba = image.convert("RGBA")
    pixels = [(r, g, b, a) for (r, g, b, _old_a), a in zip(rgba.getdata(), alpha)]
    out = Image.new("RGBA", rgba.size)
    out.putdata(pixels)
    return out


def resize_map(image: Image.Image, size: Tuple[int, int]) -> Image.Image:
    return image.convert("RGBA").resize(size, Image.Resampling.LANCZOS)


def extract_roughness_metalness(image: Image.Image, size: Tuple[int, int]) -> Tuple[List[int], List[int]]:
    resized = resize_map(image, size)
    roughness: List[int] = []
    metalness: List[int] = []
    for r, g, b, _a in resized.getdata():
        rough = max(r, round((r + b) * 0.5))
        metal = g
        roughness.append(max(8, min(255, rough)))
        metalness.append(max(0, min(255, metal)))
    return roughness, metalness


def extract_normal_rgb(image: Image.Image, size: Tuple[int, int]) -> List[Tuple[int, int, int]]:
    resized = resize_map(image, size)
    normals: List[Tuple[int, int, int]] = []
    for r, g, b, _a in resized.getdata():
        if b < 96:
            b = max(128, b)
        normals.append((r, g, b))
    return normals


def normal_with_metalness(normal_rgb: Sequence[Tuple[int, int, int]], metalness: Sequence[int], size: Tuple[int, int]) -> Image.Image:
    out = Image.new("RGBA", size)
    out.putdata([(r, g, b, a) for (r, g, b), a in zip(normal_rgb, metalness)])
    return out


def deterministic_normal_rgb(image: Image.Image, strength: float = 3.0) -> List[Tuple[int, int, int]]:
    grey_image = image.convert("L")
    width, height = grey_image.size
    grey = [value / 255.0 for value in grey_image.getdata()]
    normals: List[Tuple[int, int, int]] = []
    for y in range(height):
        for x in range(width):
            left = grey[y * width + max(0, x - 1)]
            right = grey[y * width + min(width - 1, x + 1)]
            up = grey[max(0, y - 1) * width + x]
            down = grey[min(height - 1, y + 1) * width + x]
            dx = (left - right) * strength
            dy = (up - down) * strength
            nz = 1.0
            length = max((dx * dx + dy * dy + nz * nz) ** 0.5, 1e-6)
            nx = dx / length
            ny = dy / length
            nz /= length
            normals.append((
                int((nx * 0.5 + 0.5) * 255),
                int((ny * 0.5 + 0.5) * 255),
                int((nz * 0.5 + 0.5) * 255),
            ))
    return normals


def deterministic_pbr_channels(image: Image.Image, name: str) -> Tuple[List[int], List[int]]:
    lowered = name.lower()
    if lowered.startswith("floor_") or lowered in {"rocky", "stonewall", "brownstonestep"}:
        roughness_base, metalness_base = 0.95, 0.0
    elif "metal" in lowered or "techno" in lowered or "door" in lowered or "pipe" in lowered:
        roughness_base, metalness_base = 0.55, 0.15
    else:
        roughness_base, metalness_base = 0.75, 0.0

    metallic_surface = metalness_base > 0.0
    roughness: List[int] = []
    metalness: List[int] = []
    for r, g, b, _a in image.convert("RGBA").getdata():
        luma = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0
        saturation = (max(r, g, b) - min(r, g, b)) / 255.0
        local_roughness = roughness_base + (0.5 - luma) * 0.12 + saturation * 0.05
        if name in EMISSIVE_TEXTURES and max(r, g, b) > 64:
            local_roughness -= 0.2
        local_metalness = metalness_base
        if metallic_surface:
            local_metalness += (luma - 0.35) * 0.25
            if saturation > 0.45:
                local_metalness *= 0.65
        roughness.append(round(max(0.05, min(1.0, local_roughness)) * 255))
        metalness.append(round(max(0.0, min(1.0, local_metalness)) * 255))
    return roughness, metalness


def material_factors(name: str) -> Tuple[float, float]:
    lowered = name.lower()
    if lowered.startswith("floor_") or lowered in {"rocky", "stonewall", "brownstonestep"}:
        return 0.15, 1.4
    if "metal" in lowered or "techno" in lowered or "door" in lowered or "pipe" in lowered:
        return 0.65, 1.3
    return 0.35, 1.35


def write_materials(path: pathlib.Path, names: Sequence[str], emissive_names: Sequence[str]) -> None:
    emissive_set = set(emissive_names)
    lines: List[str] = [
        "# Generated by tools/generate_q2rtx_pbr_api.py",
        "# Albedo is the original converted WAL texture; roughness is packed in base alpha.",
        "# Normal RGB and metalness alpha are API-derived from the original albedo.",
        "",
    ]
    for name in sorted(names):
        specular, base_factor = material_factors(name)
        lines.append(f"ab3d2/{name},")
        lines.append(f"textures/ab3d2/{name}:")
        lines.append(f"    texture_base overrides/ab3d2/{name}.tga")
        lines.append(f"    texture_normals overrides/ab3d2/{name}_n.tga")
        if name in emissive_set:
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


def normal_prompt(name: str) -> str:
    return (
        f"Create a seamless tangent-space normal map for the provided texture '{name}'. "
        "Preserve the exact panel layout, seams, vents, stripes, bolts, bevels, and tileable edge continuity from the input. "
        "Output only a standard RGB normal map: mostly blue-purple, no albedo color, no lighting, no shadows, no labels, no text."
    )


def pbr_prompt(name: str) -> str:
    return (
        f"Create a seamless packed PBR mask map for the provided texture '{name}'. "
        "Preserve the exact layout and tileable edge continuity. "
        "Encode roughness in the red channel as grayscale brightness: black glossy, white rough. "
        "Encode metalness in the green channel: black dielectric stone/paint, white bare metal. "
        "Use blue as roughness backup grayscale. Output only the technical channel map, no albedo color, no labels, no text."
    )


async def edit_image(
    client: AsyncOpenAI,
    source_png: bytes,
    prompt: str,
    output_path: pathlib.Path,
    *,
    model: str,
    quality: str,
    size: str,
    force: bool,
    semaphore: asyncio.Semaphore,
    max_attempts: int,
) -> None:
    if output_path.exists() and not force:
        print(f"[skip] {output_path.name}", file=sys.stderr)
        return
    async with semaphore:
        result = None
        for attempt in range(1, max_attempts + 1):
            print(f"[start] {output_path.name} attempt {attempt}/{max_attempts}", file=sys.stderr)
            started = time.time()
            try:
                result = await client.images.edit(
                    image=("source.png", source_png, "image/png"),
                    prompt=prompt,
                    model=model,
                    quality=quality,
                    size=size,
                    output_format="png",
                )
                print(f"[done] {output_path.name} in {time.time() - started:.1f}s", file=sys.stderr)
                break
            except Exception as exc:
                if attempt >= max_attempts:
                    raise
                message = str(exc).lower()
                wait = 20.0 if "rate limit" in message or "429" in message else min(60.0, 2.0**attempt)
                print(f"[retry] {output_path.name}: {exc.__class__.__name__}; waiting {wait:.0f}s", file=sys.stderr)
                await asyncio.sleep(wait)
        if result is None:
            raise RuntimeError(f"image generation failed for {output_path.name}")
    write_png(output_path, decode_api_png(result.data[0].b64_json))


async def generate_api_maps(
    wal_images: Sequence[Tuple[str, Image.Image]],
    api_dir: pathlib.Path,
    model: str,
    quality: str,
    size: str,
    concurrency: int,
    force: bool,
    max_attempts: int,
) -> None:
    client = AsyncOpenAI()
    semaphore = asyncio.Semaphore(concurrency)
    tasks = []
    for name, albedo in wal_images:
        api_source = image_to_png_bytes(albedo)
        tasks.append(edit_image(
            client,
            api_source,
            normal_prompt(name),
            api_dir / f"{name}_normal.png",
            model=model,
            quality=quality,
            size=size,
            force=force,
            semaphore=semaphore,
            max_attempts=max_attempts,
        ))
        tasks.append(edit_image(
            client,
            api_source,
            pbr_prompt(name),
            api_dir / f"{name}_pbr.png",
            model=model,
            quality=quality,
            size=size,
            force=force,
            semaphore=semaphore,
            max_attempts=max_attempts,
        ))
    await asyncio.gather(*tasks)


def build_final_outputs(
    wal_images: Sequence[Tuple[str, Image.Image]],
    api_dir: pathlib.Path,
    out_root: pathlib.Path,
    allow_fallback: bool,
) -> int:
    override_dir = out_root / "baseq2" / "overrides" / "ab3d2"
    material_path = out_root / "baseq2" / "materials" / "ab3d2_pbr.mat"
    names: List[str] = []
    emissive: List[str] = []
    for name, albedo in wal_images:
        normal_path = api_dir / f"{name}_normal.png"
        pbr_path = api_dir / f"{name}_pbr.png"
        missing = [path.name for path in (normal_path, pbr_path) if not path.exists()]
        if missing and not allow_fallback:
            raise FileNotFoundError(
                f"Missing API-derived maps for {name}: {', '.join(missing)}. "
                "Rerun without --skip-api to continue generation."
            )
        if normal_path.exists():
            normal_api = Image.open(normal_path).convert("RGBA")
            normal_rgb = extract_normal_rgb(normal_api, albedo.size)
        else:
            print(f"[fallback] {normal_path.name}", file=sys.stderr)
            normal_rgb = deterministic_normal_rgb(albedo)
        if pbr_path.exists():
            pbr_api = Image.open(pbr_path).convert("RGBA")
            roughness, metalness = extract_roughness_metalness(pbr_api, albedo.size)
        else:
            print(f"[fallback] {pbr_path.name}", file=sys.stderr)
            roughness, metalness = deterministic_pbr_channels(albedo, name)
        names.append(name)
        write_tga(override_dir / f"{name}.tga", pack_alpha(albedo, roughness))
        write_tga(override_dir / f"{name}_n.tga", normal_with_metalness(normal_rgb, metalness, albedo.size))
        write_tga(override_dir / f"{name}_r.tga", greyscale_to_rgba(roughness, albedo.size))
        write_tga(override_dir / f"{name}_m.tga", greyscale_to_rgba(metalness, albedo.size))
        if name in EMISSIVE_TEXTURES:
            emissive.append(name)
            # Reuse the albedo image for the emissive mask; it is deterministic and keeps
            # light sources aligned to the original texture rather than generated art.
            light_pixels = []
            for r, g, b, _a in albedo.getdata():
                bright = max(r, g, b)
                if bright >= 72 and (b > r * 1.1 or r > g * 1.2):
                    light_pixels.append((min(255, r * 2), min(255, g * 2), min(255, b * 2), 255))
                else:
                    light_pixels.append((0, 0, 0, 255))
            light = Image.new("RGBA", albedo.size)
            light.putdata(light_pixels)
            write_tga(override_dir / f"{name}_light.tga", light)
    write_materials(material_path, names, emissive)
    return len(names)


def load_wal_albedos(wal_dir: pathlib.Path, palette_path: pathlib.Path, source_dir: pathlib.Path) -> List[Tuple[str, Image.Image]]:
    palette = walpbr.read_pcx_palette(palette_path)
    wal_images: List[Tuple[str, Image.Image]] = []
    for wal_path in sorted(wal_dir.glob("*.wal")):
        wal = walpbr.read_wal(wal_path)
        name = wal_path.stem
        albedo = rgba_from_wal(wal, palette)
        write_png(source_dir / f"{name}.png", albedo)
        wal_images.append((name, albedo))
    return wal_images


async def async_main(args: argparse.Namespace) -> int:
    source_dir = args.work_root / "source_albedo"
    api_dir = args.work_root / "api_maps"
    wal_images = load_wal_albedos(args.wal_dir, args.palette, source_dir)
    if not args.skip_api:
        await generate_api_maps(
            wal_images,
            api_dir,
            args.model,
            args.quality,
            args.size,
            args.concurrency,
            args.force,
            args.max_attempts,
        )
    count = build_final_outputs(wal_images, api_dir, args.out_root, args.allow_fallback)
    print(f"Generated API-derived packed Q2RTX PBR overrides for {count} textures")
    print(f"Source albedo PNGs -> {source_dir}")
    print(f"API normal/PBR maps -> {api_dir}")
    print(f"Q2RTX overrides -> {args.out_root / 'baseq2' / 'overrides' / 'ab3d2'}")
    print(f"Q2RTX materials -> {args.out_root / 'baseq2' / 'materials' / 'ab3d2_pbr.mat'}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate Q2RTX PBR maps from WAL albedo using the OpenAI Images API")
    parser.add_argument("--wal-dir", type=pathlib.Path, default=pathlib.Path("build/quake2_assets/baseq2/textures/ab3d2"))
    parser.add_argument("--palette", type=pathlib.Path, default=pathlib.Path("build/quake2_assets/baseq2/pics/colormap.pcx"))
    parser.add_argument("--work-root", type=pathlib.Path, default=pathlib.Path("build/q2rtx_pbr_api"))
    parser.add_argument("--out-root", type=pathlib.Path, default=pathlib.Path("build/q2rtx_pbr_api"))
    parser.add_argument("--model", default="gpt-image-2")
    parser.add_argument("--quality", default="medium", choices=("low", "medium", "high", "auto"))
    parser.add_argument("--size", default="auto")
    parser.add_argument("--concurrency", type=int, default=1)
    parser.add_argument("--max-attempts", type=int, default=5)
    parser.add_argument("--skip-api", action="store_true", help="Build final packed TGAs from existing API maps and deterministic fallbacks")
    parser.add_argument("--allow-fallback", action="store_true", help="Allow deterministic fallback maps when API maps are missing")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    return asyncio.run(async_main(args))


if __name__ == "__main__":
    raise SystemExit(main())
