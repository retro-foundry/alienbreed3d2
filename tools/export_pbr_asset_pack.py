#!/usr/bin/env python3
"""Export every renderer-visible AB3D2 image as an editable PBR PNG set.

The original media and GLFT tables remain the authority.  This tool is an
offline authoring export: it resolves the same wall, floor, bitmap, vector,
backdrop, and font sources as the native renderer and writes one flat
directory that can be zipped and handed to artists.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

try:
    from PIL import Image
except ImportError as error:  # pragma: no cover - build-host diagnostic
    raise SystemExit(
        "export_pbr_asset_pack requires Pillow; install the Python 'Pillow' package"
    ) from error

import build_dxr_materials as sheet_tools


CHANNELS = ("base_color", "normal", "metalness", "roughness", "emissive")
FLOOR_OFFSETS = tuple(row * 256 + column for row in range(5) for column in range(4))
WALL_DIMENSIONS = {
    "alienredwall": (258, 128),
    "brownpipes": (258, 128),
    "brownspeakers": (129, 128),
    "brownstonestep": (129, 32),
    "brownwithyellowstripes": (258, 128),
    "chevrondoor": (129, 128),
    "gieger": (642, 128),
    "hullmetal": (258, 128),
    "redhullmetal": (129, 128),
    "rocky": (513, 128),
    "steampunk": (513, 128),
    "stonewall": (96, 128),
    "technolights": (258, 128),
    "technotritile": (258, 128),
}
GLFT_SIZE = 86268
OBJECT_COUNT = 30
OBJECT_FRAME_COUNT = 32
OBJECT_ANIMATION_FRAME_COUNT = 20
ALIEN_COUNT = 20
ALIEN_OPTION_COUNT = 11
ALIEN_FRAME_COUNT = 20
BULLET_COUNT = 20
VECTOR_COUNT = 30
WALL_COUNT = 16
GUN_COUNT = 10


def be16(data: bytes, offset: int) -> int:
    return (data[offset] << 8) | data[offset + 1]


def be16s(data: bytes, offset: int) -> int:
    value = be16(data, offset)
    return value if value < 0x8000 else value - 0x10000


def be32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "big")


def signed32(value: int) -> int:
    return value if value < 0x80000000 else value - 0x100000000


def signed8(value: int) -> int:
    return value if value < 0x80 else value - 0x100


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def slug(text: str) -> str:
    result = re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")
    return result or "unnamed"


def field(data: bytes, offset: int, size: int) -> str:
    return data[offset : offset + size].split(b"\0", 1)[0].decode("ascii").rstrip()


@dataclass(frozen=True)
class GlftLayout:
    object_graphics: int
    floor_filename: int
    texture_filename: int
    bullet_definitions: int
    gun_names: int
    alien_names: int
    alien_definitions: int
    frame_data: int
    object_names: int
    object_definitions: int
    object_default_animations: int
    object_action_animations: int
    alien_animations: int
    vector_names: int
    wall_names: int
    gun_objects: int


def glft_layout() -> GlftLayout:
    offset = 64
    offset += 16 * 40
    object_graphics = offset
    offset += OBJECT_COUNT * 64
    offset += 64 * 60
    floor_filename = offset
    offset += 64
    texture_filename = offset
    offset += 192
    offset += 64  # gun graphics filename
    offset += 64  # story filename
    bullet_definitions = offset
    offset += BULLET_COUNT * 300
    offset += BULLET_COUNT * 20
    gun_names = offset
    offset += GUN_COUNT * 20
    offset += GUN_COUNT * 8
    alien_names = offset
    offset += ALIEN_COUNT * 20
    alien_definitions = offset
    offset += ALIEN_COUNT * 42
    frame_data = offset
    offset += OBJECT_COUNT * OBJECT_FRAME_COUNT * 8
    object_names = offset
    offset += OBJECT_COUNT * 20
    object_definitions = offset
    offset += OBJECT_COUNT * 40
    object_default_animations = offset
    offset += OBJECT_COUNT * OBJECT_ANIMATION_FRAME_COUNT * 6
    object_action_animations = offset
    offset += OBJECT_COUNT * OBJECT_ANIMATION_FRAME_COUNT * 6
    offset += OBJECT_COUNT * 44
    offset += OBJECT_COUNT * 24
    alien_animations = offset
    offset += ALIEN_COUNT * ALIEN_OPTION_COUNT * ALIEN_FRAME_COUNT * 11
    vector_names = offset
    offset += VECTOR_COUNT * 64
    wall_names = offset
    offset += WALL_COUNT * 64
    offset += WALL_COUNT * 2
    offset += ALIEN_COUNT * 2
    gun_objects = offset
    return GlftLayout(
        object_graphics,
        floor_filename,
        texture_filename,
        bullet_definitions,
        gun_names,
        alien_names,
        alien_definitions,
        frame_data,
        object_names,
        object_definitions,
        object_default_animations,
        object_action_animations,
        alien_animations,
        vector_names,
        wall_names,
        gun_objects,
    )


class MediaIndex:
    def __init__(self, root: Path):
        self.root = root.resolve()
        self.paths: dict[str, Path] = {}
        for path in self.root.rglob("*"):
            if not path.is_file():
                continue
            key = path.relative_to(self.root).as_posix().lower()
            if key in self.paths:
                raise ValueError(f"case-folding media collision: {key}")
            self.paths[key] = path

    def resolve_volume(self, source: str, suffix: str = "") -> Path:
        if ":" not in source:
            raise ValueError(f"GLFT media path has no volume: {source}")
        volume, relative = source.split(":", 1)
        if volume.lower() not in ("ab3", "tkg1", "tkg2"):
            raise ValueError(f"unsupported GLFT media volume: {volume}")
        key = (relative.replace("\\", "/") + suffix).lower().lstrip("/")
        path = self.paths.get(key)
        if path is None:
            raise ValueError(f"GLFT media asset is missing: {source}{suffix}")
        return path

    def require(self, relative: str) -> Path:
        path = self.paths.get(relative.replace("\\", "/").lower())
        if path is None:
            raise ValueError(f"required media asset is missing: {relative}")
        return path


def display_palette(path: Path) -> list[tuple[int, int, int, int]]:
    data = path.read_bytes()
    if len(data) != 256 * 6:
        raise ValueError(f"source display palette has an invalid size: {path}")
    return [
        (
            min(be16(data, index * 6), 255),
            min(be16(data, index * 6 + 2), 255),
            min(be16(data, index * 6 + 4), 255),
            255,
        )
        for index in range(256)
    ]


def wall_image(
    path: Path, palette: list[tuple[int, int, int, int]]
) -> Image.Image:
    data = path.read_bytes()
    key = slug(path.stem)
    if key not in WALL_DIMENSIONS or len(data) < 2050:
        raise ValueError(f"source wall texture has no validated extent: {path}")
    width, height = WALL_DIMENSIONS[key]
    required = 2048 + ((width + 2) // 3) * height * 2
    # Some retained WADs carry an unused partial/final packed strip after the
    # extent selected by GLFT_WallHeights.  The source renderer never indexes
    # it; accept at most one such strip while decoding the validated extent.
    if len(data) < required or len(data) > required + height * 2 + 2:
        raise ValueError(
            f"source wall texture size disagrees with {width}x{height}: {path}"
        )
    pixels: list[tuple[int, int, int, int]] = []
    for y in range(height):
        for x in range(width):
            packed_offset = 2048 + (x // 3) * height * 2 + y * 2
            word = be16(data, packed_offset)
            third = x % 3
            source_index = (
                word & 31 if third == 0 else (word >> 5) & 31 if third == 1 else (word >> 10) & 31
            )
            pixels.append(palette[data[source_index * 2]])
    image = Image.new("RGBA", (width, height))
    image.putdata(pixels)
    return image


def floor_image(
    floor_data: bytes,
    remap: bytes,
    palette: list[tuple[int, int, int, int]],
    tile_offset: int,
) -> Image.Image:
    if len(floor_data) != 65536 or len(remap) < 33 * 256:
        raise ValueError("source floor atlas or palette has an invalid size")
    pixels = []
    for y in range(64):
        for x in range(64):
            source_offset = (tile_offset + y * 1024 + x * 4) % len(floor_data)
            pixels.append(palette[remap[32 * 256 + floor_data[source_offset]]])
    image = Image.new("RGBA", (64, 64))
    image.putdata(pixels)
    return image


def default_channels(base_color: Image.Image, emissive: bool = False) -> dict[str, Image.Image]:
    base = base_color.convert("RGBA")
    alpha = base.getchannel("A")

    def solid(color: tuple[int, int, int]) -> Image.Image:
        image = Image.new("RGBA", base.size, (*color, 255))
        image.putalpha(alpha)
        return image

    emission = base.copy() if emissive else solid((0, 0, 0))
    return {
        "base_color": base,
        "normal": solid((128, 128, 255)),
        "metalness": solid((0, 0, 0)),
        "roughness": solid((255, 255, 255)),
        "emissive": emission,
    }


def decode_packed_texel(word: int, third: int) -> int:
    return word & 31 if third == 0 else (word >> 5) & 31 if third == 1 else (word >> 10) & 31


@dataclass(frozen=True, order=True)
class BitmapReference:
    asset_id: int
    frame: int
    mode: str


def bitmap_references(data: bytes, layout: GlftLayout) -> tuple[set[BitmapReference], set[int]]:
    references: set[BitmapReference] = set()
    enemy_assets: set[int] = set()

    def add(asset: int, frame: int, mode: str) -> None:
        if asset < OBJECT_COUNT and frame < OBJECT_FRAME_COUNT:
            references.add(BitmapReference(asset, frame, mode))

    for object_index in range(OBJECT_COUNT):
        definition = layout.object_definitions + object_index * 40
        graphics_type = be16(data, definition + 2)
        for animation, initial_offset in (
            (layout.object_default_animations, 12),
            (layout.object_action_animations, 22),
        ):
            frame_index = be16(data, definition + initial_offset)
            visited: set[int] = set()
            while frame_index not in visited:
                if frame_index >= OBJECT_ANIMATION_FRAME_COUNT:
                    raise ValueError(
                        f"object {object_index} animation selects frame {frame_index}"
                    )
                visited.add(frame_index)
                record = data[
                    animation + object_index * 120 + frame_index * 6 :
                    animation + object_index * 120 + frame_index * 6 + 6
                ]
                if graphics_type == 0:
                    add(record[0], record[1], "bitmap")
                elif graphics_type > 1:
                    add(record[0], record[1], "glare")
                # newaliencontrol.s:DEFANIMOBJ/ACTANIMOBJ writes byte five
                # straight back to EntT_Timer1_w; follow only reachable rows.
                frame_index = record[5]

    for alien_index in range(ALIEN_COUNT):
        graphics_type = be16(data, layout.alien_definitions + alien_index * 42)
        for option in range(ALIEN_OPTION_COUNT):
            for frame_index in range(ALIEN_FRAME_COUNT):
                offset = layout.alien_animations + (
                    (alien_index * ALIEN_OPTION_COUNT + option) * ALIEN_FRAME_COUNT + frame_index
                ) * 11
                record = data[offset : offset + 11]
                if signed8(record[0]) < 0:
                    break
                display_frame = abs(signed8(record[1])) - 1
                if display_frame < 0:
                    continue
                if graphics_type != 1:
                    mode = (
                        f"lighted_{graphics_type}"
                        if 2 <= graphics_type < 6
                        else "additive"
                        if graphics_type >= 6
                        else "bitmap"
                    )
                    add(record[0], display_frame, mode)
                    enemy_assets.add(record[0])

    for bullet_index in range(BULLET_COUNT):
        definition = layout.bullet_definitions + bullet_index * 300
        for animation_offset, count_offset, graphics_offset in (
            (60, 36, 52),
            (180, 40, 56),
        ):
            frame_count = min(be32(data, definition + count_offset), ALIEN_FRAME_COUNT - 1)
            graphics_type = signed32(be32(data, definition + graphics_offset))
            mode = "bitmap" if graphics_type < 1 else "glare" if graphics_type == 1 else "additive"
            for frame_index in range(frame_count + 1):
                record = data[
                    definition + animation_offset + frame_index * 6 :
                    definition + animation_offset + frame_index * 6 + 6
                ]
                add(record[0], record[1], mode)
    return references, enemy_assets


def decode_bitmap(
    wad: bytes,
    pointers: bytes,
    object_palette: bytes,
    texture_palette: bytes,
    palette: list[tuple[int, int, int, int]],
    metrics: tuple[int, int, int, int],
    mode: str,
) -> Image.Image:
    pointer_index, down_strip, half_width, half_height = metrics
    width = half_width * 2
    height = half_height * 2
    if width <= 0 or height <= 0:
        raise ValueError("bitmap frame has zero extent")
    output = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    pixels = output.load()
    lighted = mode.startswith("lighted_")
    light_selector = int(mode.removeprefix("lighted_")) if lighted else 0
    table_offset = pointer_index * 4
    if table_offset + width * 4 > len(pointers):
        raise ValueError("bitmap PTR frame is outside its asset")
    for x in range(width):
        pointer = be32(pointers, table_offset + x * 4)
        if pointer == 0:
            continue
        packed_third = pointer >> 24
        column_offset = pointer if lighted else pointer & 0x00FFFFFF
        for y in range(height):
            if lighted:
                source_offset = column_offset + down_strip + y
                if source_offset >= len(wad):
                    raise ValueError("lighted bitmap WAD column is outside its asset")
                texel = wad[source_offset]
                palette_offset = (light_selector - 2) * 256 + (texel & 7)
                if not (2 <= light_selector < 6) or palette_offset >= len(object_palette):
                    raise ValueError("lighted bitmap palette is outside its asset")
                color_index = object_palette[palette_offset]
                rgba = palette[color_index]
                if texel == 0:
                    rgba = (*rgba[:3], 0)
            else:
                if packed_third > 2:
                    raise ValueError("packed bitmap PTR selector is invalid")
                source_offset = column_offset + (down_strip + y) * 2
                if source_offset + 2 > len(wad):
                    raise ValueError("packed bitmap WAD column is outside its asset")
                texel = decode_packed_texel(be16(wad, source_offset), packed_third)
                if mode == "bitmap":
                    palette_offset = texel * 2
                    if palette_offset >= len(object_palette):
                        raise ValueError("bitmap palette is outside its asset")
                    color_index = object_palette[palette_offset]
                elif mode == "glare":
                    if texel == 0:
                        continue
                    palette_offset = (texel - 1) * 512
                    if palette_offset >= len(texture_palette):
                        raise ValueError("glare palette is outside its asset")
                    color_index = texture_palette[palette_offset]
                elif mode == "additive":
                    if texel == 0:
                        continue
                    palette_offset = texel * 256
                    if palette_offset >= len(object_palette):
                        raise ValueError("additive bitmap palette is outside its asset")
                    color_index = object_palette[palette_offset]
                else:
                    raise ValueError(f"unsupported bitmap mode: {mode}")
                rgba = palette[color_index]
                if texel == 0:
                    rgba = (*rgba[:3], 0)
            pixels[x, y] = rgba
    return output


@dataclass(frozen=True, order=True)
class VectorMaterialKey:
    map_offset: int
    minimum_u: int
    maximum_u: int
    minimum_v: int
    maximum_v: int
    glare: int


def vector_material_keys(model: bytes) -> list[VectorMaterialKey]:
    if len(model) < 10:
        raise ValueError("vector model is smaller than its header")
    point_count = be16(model, 2)
    frame_count = be16(model, 4)
    lines_offset = 6 + frame_count * 4
    if point_count == 0 or frame_count == 0 or lines_offset > len(model):
        raise ValueError("vector model header is malformed")
    active_parts = 0
    for frame in range(frame_count):
        frame_offset = 2 + be16(model, 6 + frame * 4)
        if frame_offset + 4 > len(model):
            raise ValueError("vector model frame is outside its asset")
        active_parts |= be32(model, frame_offset)
    part_offsets: list[int] = []
    for part in range(32):
        entry = lines_offset + part * 4
        if entry + 4 > len(model):
            raise ValueError("vector model has no part-list terminator")
        relative = be16s(model, entry)
        if relative < 0:
            break
        if active_parts & (1 << part):
            part_offsets.append(2 + (relative & 0xFFFF))
    keys: set[VectorMaterialKey] = set()
    for part_offset in part_offsets:
        offset = part_offset
        while True:
            if offset + 2 > len(model):
                raise ValueError("vector model polygon is outside its asset")
            line_count_minus_one = be16s(model, offset)
            if line_count_minus_one < 0:
                break
            polygon_points = line_count_minus_one + 1
            if polygon_points < 3:
                raise ValueError("vector model polygon has fewer than three points")
            polygon_size = 18 + line_count_minus_one * 4
            if offset + polygon_size > len(model):
                raise ValueError("vector model polygon is truncated")
            point_entries = offset + 4
            uv = [
                (model[point_entries + corner * 4 + 2], model[point_entries + corner * 4 + 3])
                for corner in range(polygon_points)
            ]
            face = point_entries + (polygon_points + 1) * 4
            source_word = be16s(model, face)
            map_offset = 65536 + (source_word & 0x7FFF) if source_word < 0 else source_word
            gouraud = model[face + 5] != 0
            glare = int(not gouraud and model[face + 4] != 0)
            keys.add(
                VectorMaterialKey(
                    map_offset,
                    min(value[0] for value in uv),
                    max(value[0] for value in uv),
                    min(value[1] for value in uv),
                    max(value[1] for value in uv),
                    glare,
                )
            )
            offset += polygon_size
    return sorted(keys)


def vector_image(
    key: VectorMaterialKey,
    texture_maps: bytes,
    texture_palette: bytes,
    palette: list[tuple[int, int, int, int]],
) -> Image.Image:
    width = key.maximum_u - key.minimum_u + 1
    height = key.maximum_v - key.minimum_v + 1
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    pixels = image.load()
    for y in range(height):
        for x in range(width):
            source_u = key.minimum_u + x
            source_v = key.minimum_v + y
            coordinate = (source_v << 8) | source_u
            if coordinate >= 0x8000:
                coordinate -= 0x10000
            source_offset = key.map_offset + coordinate * 4
            if not 0 <= source_offset < len(texture_maps):
                raise ValueError(
                    f"vector texture coordinate {coordinate} is outside map {key.map_offset}"
                )
            texel = texture_maps[source_offset]
            if key.glare and texel == 0:
                continue
            if key.glare and texel <= 32:
                palette_offset = (texel - 1) * 512
            else:
                palette_offset = 32 * 256 + texel
            if not 0 <= palette_offset < len(texture_palette):
                raise ValueError("vector texture palette lookup is outside its asset")
            pixels[x, y] = palette[texture_palette[palette_offset]]
    return image


def save_channels(
    output_dir: Path,
    name: str,
    images: dict[str, Image.Image],
) -> dict[str, str]:
    if set(images) != set(CHANNELS):
        raise ValueError(f"material {name} does not supply all PBR channels")
    size = images["base_color"].size
    result = {}
    for channel in CHANNELS:
        image = images[channel].convert("RGBA")
        if image.size != size:
            raise ValueError(f"material {name} channel dimensions disagree")
        filename = f"{name}_{channel}.png"
        image.save(output_dir / filename, format="PNG", optimize=False, compress_level=9)
        result[channel] = filename
    return result


class PackWriter:
    def __init__(self, output_dir: Path):
        self.output_dir = output_dir
        self.materials: list[dict[str, object]] = []
        self.names: set[str] = set()
        self.bindings: set[str] = set()

    def add(
        self,
        name: str,
        material_class: str,
        images: dict[str, Image.Image],
        *,
        alpha_mode: str,
        emissive_factor: list[float] | None = None,
        binding: dict[str, object] | None = None,
        source: dict[str, object],
        generated_channels: list[str] | None = None,
        tags: list[str] | None = None,
    ) -> None:
        if name in self.names:
            raise ValueError(f"duplicate PBR material name: {name}")
        if binding is not None:
            binding_key = json.dumps(binding, sort_keys=True, separators=(",", ":"))
            if binding_key in self.bindings:
                raise ValueError(f"duplicate PBR material binding: {binding}")
            self.bindings.add(binding_key)
        self.names.add(name)
        channels = save_channels(self.output_dir, name, images)
        width, height = images["base_color"].size
        self.materials.append(
            {
                "name": name,
                "class": material_class,
                "width": width,
                "height": height,
                "channels": channels,
                "base_color_space": "srgb",
                "normal_space": "linear_tangent",
                "metalness_space": "linear",
                "roughness_space": "linear",
                "emissive_space": "srgb",
                "normal_strength": 1.0,
                "alpha_mode": alpha_mode,
                "alpha_cutoff": 0.5,
                "two_sided": material_class in ("billboard", "enemy_billboard", "effect_billboard"),
                "emissive_factor": emissive_factor or [0.0, 0.0, 0.0],
                "binding": binding,
                "source": source,
                "generated_channels": generated_channels or [],
                "tags": sorted(tags or []),
            }
        )

    def finish(self, non_color_assets: list[dict[str, object]]) -> None:
        manifest = {
            "schema_version": 3,
            "generator": "tools/export_pbr_asset_pack.py",
            "description": "Flat, zip-ready AB3D2 artist PBR texture package",
            "channels": list(CHANNELS),
            "un_authored_channel_defaults": {
                "normal": [128, 128, 255],
                "metalness": [0, 0, 0],
                "roughness": [255, 255, 255],
                "emissive": [0, 0, 0],
            },
            "materials": sorted(self.materials, key=lambda item: str(item["name"])),
            "non_color_source_assets": non_color_assets,
        }
        (self.output_dir / "materials.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
            newline="\n",
        )


def authored_sheet_channels(
    source_dir: Path, entry: dict[str, object]
) -> dict[str, Image.Image]:
    path = source_dir / str(entry["sheet"])
    with Image.open(path) as opened:
        source = opened.convert("RGB")
    boxes = (
        sheet_tools.grid_panel_boxes(source)
        if entry["layout"] == "grid"
        else sheet_tools.vertical_panel_boxes(source)
    )
    base_box = boxes["base_color"]
    output_size = (base_box[2] - base_box[0], base_box[3] - base_box[1])
    channels: dict[str, Image.Image] = {}
    for channel in ("base_color", "normal", "metalness", "roughness"):
        panel = source.crop(boxes[channel])
        if panel.size != output_size:
            panel = panel.resize(output_size, Image.Resampling.LANCZOS)
        channels[channel] = panel.convert("RGBA")
    channels["emissive"] = Image.new("RGBA", output_size, (0, 0, 0, 255))
    if entry.get("emissive_source") == "albedo_mask":
        channels["emissive"] = sheet_tools.emissive_from_albedo(
            str(entry["name"]), channels["base_color"].convert("RGB")
        ).convert("RGBA")
    return channels


def build_pack(
    media_root: Path,
    game_link_path: Path,
    authored_dir: Path,
    authored_spec_path: Path,
    fonts_dir: Path,
    output_dir: Path,
) -> None:
    link = game_link_path.read_bytes()
    if len(link) != GLFT_SIZE:
        raise ValueError(f"game link does not match the GLFT layout: {game_link_path}")
    layout = glft_layout()
    media = MediaIndex(media_root)
    palette_path = media.require("includes/256pal")
    palette = display_palette(palette_path)
    texture_maps_path = media.resolve_volume(field(link, layout.texture_filename, 192))
    texture_palette_path = media.resolve_volume(
        field(link, layout.texture_filename, 192), ".pal"
    )
    texture_maps = texture_maps_path.read_bytes()
    texture_palette = texture_palette_path.read_bytes()
    if len(texture_maps) != 131072 or len(texture_palette) != 16384:
        raise ValueError("source vector texture atlas or palette has an invalid size")

    output_dir.mkdir(parents=True, exist_ok=True)
    for path in output_dir.iterdir():
        if path.is_file():
            path.unlink()
        else:
            raise ValueError(f"PBR output directory must be flat: {path}")
    writer = PackWriter(output_dir)

    authored_spec = json.loads(authored_spec_path.read_text(encoding="utf-8"))
    authored_entries = {entry["name"]: entry for entry in authored_spec["materials"]}
    wall_slots: dict[str, int] = {}
    for index in range(WALL_COUNT):
        source = field(link, layout.wall_names + index * 64, 64)
        if not source:
            break
        wall_slots[slug(Path(source.split(":", 1)[1]).stem)] = index
    for wall_path in sorted(media_root.joinpath("wallinc").glob("*.256wad")):
        wall_name = slug(wall_path.stem)
        authored = authored_entries.get(wall_name)
        base = wall_image(wall_path, palette)
        channels = authored_sheet_channels(authored_dir, authored) if authored else default_channels(base)
        emissive_factor = (
            list(authored.get("emissive_factor", [0.0, 0.0, 0.0])) if authored else [0.0, 0.0, 0.0]
        )
        binding = (
            {"kind": "shared_wall", "source_asset_id": wall_slots[wall_name]}
            if wall_name in wall_slots
            else None
        )
        writer.add(
            f"wall_{wall_slots[wall_name]:02d}_{wall_name}" if binding else f"wall_archive_{wall_name}",
            "wall",
            channels,
            alpha_mode="opaque",
            emissive_factor=emissive_factor,
            binding=binding,
            source={
                "files": [wall_path.relative_to(media_root).as_posix()],
                "sha256": [sha256(wall_path.read_bytes())],
                "authored_sheet": str(authored["sheet"]) if authored else None,
            },
            generated_channels=[] if authored else ["normal", "metalness", "roughness", "emissive"],
            tags=["world"],
        )

    floor_path = media.resolve_volume(field(link, layout.floor_filename, 64))
    floor_data = floor_path.read_bytes()
    floor_remap_path = texture_palette_path
    floor_remap = texture_palette
    floor_authored = {
        int(entry["binding"]["source_asset_id"]): entry
        for entry in authored_spec["materials"]
        if entry.get("binding", {}).get("source") == "shared_floor"
    }
    source_floor_entry = authored_spec["source_materials"][0]
    for tile_offset in FLOOR_OFFSETS:
        base = floor_image(floor_data, floor_remap, palette, tile_offset)
        authored = floor_authored.get(tile_offset)
        if authored:
            channels = authored_sheet_channels(authored_dir, authored)
        else:
            channels = default_channels(base)
        emissive_factor = [0.0, 0.0, 0.0]
        generated = [] if authored else ["normal", "metalness", "roughness", "emissive"]
        if tile_offset == int(source_floor_entry["binding"]["source_asset_id"]):
            channels = default_channels(base)
            channels["emissive"] = sheet_tools.emissive_from_albedo(
                "floor_0101", base.convert("RGB")
            ).convert("RGBA")
            emissive_factor = list(source_floor_entry["emissive_factor"])
            generated = ["normal", "metalness", "roughness"]
        writer.add(
            f"floor_{tile_offset:04x}",
            "floor",
            channels,
            alpha_mode="opaque",
            emissive_factor=emissive_factor,
            binding={"kind": "shared_floor", "source_asset_id": tile_offset},
            source={
                "files": [
                    floor_path.relative_to(media_root).as_posix(),
                    floor_remap_path.relative_to(media_root).as_posix(),
                ],
                "sha256": [sha256(floor_data), sha256(floor_remap)],
                "tile_offset": tile_offset,
                "authored_sheet": str(authored["sheet"]) if authored else None,
            },
            generated_channels=generated,
            tags=["world", "floor_ceiling_water"],
        )

    references, enemy_assets = bitmap_references(link, layout)
    modes_by_asset: dict[int, set[str]] = {}
    for reference in references:
        modes_by_asset.setdefault(reference.asset_id, set()).add(reference.mode)
    for asset_id in range(OBJECT_COUNT):
        source = field(link, layout.object_graphics + asset_id * 64, 64)
        if not source:
            break
        wad_path = media.resolve_volume(source, ".WAD")
        ptr_path = media.resolve_volume(source, ".PTR")
        object_palette_path = media.resolve_volume(source, ".256PAL")
        wad = wad_path.read_bytes()
        pointers = ptr_path.read_bytes()
        object_palette = object_palette_path.read_bytes()
        resource_name = slug(Path(source.split(":", 1)[1]).name)
        candidate_modes = modes_by_asset.get(asset_id, {"bitmap"})
        for frame_index in range(OBJECT_FRAME_COUNT):
            metric_offset = layout.frame_data + (asset_id * OBJECT_FRAME_COUNT + frame_index) * 8
            metrics = tuple(be16(link, metric_offset + component) for component in (0, 2, 4, 6))
            if metrics[2] == 0 or metrics[3] == 0:
                continue
            for mode in sorted(candidate_modes):
                reference = BitmapReference(asset_id, frame_index, mode)
                if reference not in references:
                    continue
                try:
                    base = decode_bitmap(
                        wad,
                        pointers,
                        object_palette,
                        texture_palette,
                        palette,
                        metrics,
                        mode,
                    )
                except ValueError as error:
                    raise ValueError(
                        f"bitmap asset {asset_id} ({resource_name}) frame "
                        f"{frame_index} mode {mode}: {error}"
                    ) from error
                emitted = mode in ("glare", "additive")
                material_class = (
                    "effect_billboard"
                    if emitted
                    else "enemy_billboard"
                    if asset_id in enemy_assets
                    else "billboard"
                )
                writer.add(
                    f"billboard_{asset_id:02d}_{resource_name}_frame_{frame_index:02d}_{mode}",
                    material_class,
                    default_channels(base, emitted),
                    alpha_mode="additive" if emitted else "mask",
                    emissive_factor=[1.0, 1.0, 1.0] if emitted else None,
                    binding={
                        "kind": "bitmap",
                        "source_asset_id": asset_id,
                        "frame_index": frame_index,
                        "mode": mode,
                    },
                    source={
                        "files": [
                            wad_path.relative_to(media_root).as_posix(),
                            ptr_path.relative_to(media_root).as_posix(),
                            object_palette_path.relative_to(media_root).as_posix(),
                        ],
                        "sha256": [sha256(wad), sha256(pointers), sha256(object_palette)],
                        "frame_metrics": list(metrics),
                    },
                    generated_channels=["normal", "metalness", "roughness"] + ([] if emitted else ["emissive"]),
                    tags=["enemy"] if asset_id in enemy_assets else [],
                )

    weapon_vector_ids: set[int] = set()
    for gun_index in range(GUN_COUNT):
        object_index = be16(link, layout.gun_objects + gun_index * 2)
        if object_index >= OBJECT_COUNT:
            continue
        definition = layout.object_definitions + object_index * 40
        if be16(link, definition + 2) != 1:
            continue
        for animation, initial_offset in (
            (layout.object_default_animations, 12),
            (layout.object_action_animations, 22),
        ):
            frame_index = be16(link, definition + initial_offset)
            visited: set[int] = set()
            while frame_index not in visited:
                if frame_index >= OBJECT_ANIMATION_FRAME_COUNT:
                    raise ValueError(
                        f"gun object {object_index} selects animation frame {frame_index}"
                    )
                visited.add(frame_index)
                record = link[
                    animation + object_index * 120 + frame_index * 6 :
                    animation + object_index * 120 + frame_index * 6 + 6
                ]
                weapon_vector_ids.add(record[0])
                frame_index = record[5]
    for asset_id in range(VECTOR_COUNT):
        source = field(link, layout.vector_names + asset_id * 64, 64)
        if not source:
            break
        model_path = media.resolve_volume(source)
        model = model_path.read_bytes()
        resource_name = slug(Path(source.split(":", 1)[1]).name)
        material_class = "weapon" if asset_id in weapon_vector_ids else "vector_model"
        for material_index, key in enumerate(vector_material_keys(model)):
            base = vector_image(key, texture_maps, texture_palette, palette)
            emitted = key.glare != 0
            writer.add(
                f"{material_class}_{asset_id:02d}_{resource_name}_material_{material_index:03d}",
                material_class,
                default_channels(base, emitted),
                alpha_mode="additive" if emitted else "opaque",
                emissive_factor=[1.0, 1.0, 1.0] if emitted else None,
                binding={
                    "kind": "vector",
                    "source_asset_id": asset_id,
                    "map_offset": key.map_offset,
                    "minimum_u": key.minimum_u,
                    "maximum_u": key.maximum_u,
                    "minimum_v": key.minimum_v,
                    "maximum_v": key.maximum_v,
                    "glare": key.glare,
                },
                source={
                    "files": [
                        model_path.relative_to(media_root).as_posix(),
                        texture_maps_path.relative_to(media_root).as_posix(),
                        texture_palette_path.relative_to(media_root).as_posix(),
                    ],
                    "sha256": [sha256(model), sha256(texture_maps), sha256(texture_palette)],
                },
                generated_channels=["normal", "metalness", "roughness"] + ([] if emitted else ["emissive"]),
                tags=["weapon"] if material_class == "weapon" else ["world_object_or_enemy"],
            )

    backdrop_path = media.require("includes/rawback")
    backdrop = backdrop_path.read_bytes()
    if len(backdrop) != 648 * 240:
        raise ValueError("unpacked source backdrop has an invalid size")
    backdrop_image = Image.new("RGBA", (648, 240))
    backdrop_image.putdata(
        [palette[backdrop[x * 240 + y]] for y in range(240) for x in range(648)]
    )
    writer.add(
        "environment_backdrop",
        "environment",
        default_channels(backdrop_image),
        alpha_mode="opaque",
        source={
            "files": [backdrop_path.relative_to(media_root).as_posix()],
            "sha256": [sha256(backdrop)],
        },
        generated_channels=["normal", "metalness", "roughness", "emissive"],
        tags=["sky"],
    )

    for font_path in sorted(fonts_dir.rglob("*.png")):
        with Image.open(font_path) as opened:
            base = opened.convert("RGBA")
        font_name = "ui_" + slug(font_path.relative_to(fonts_dir).with_suffix("").as_posix())
        writer.add(
            font_name,
            "ui",
            default_channels(base),
            alpha_mode="mask",
            source={
                "files": [
                    "fonts/" + font_path.relative_to(fonts_dir).as_posix()
                ],
                "sha256": [sha256(font_path.read_bytes())],
            },
            generated_channels=["normal", "metalness", "roughness", "emissive"],
            tags=["hud_or_text"],
        )

    water_path = media.require("includes/waterfile")
    water = water_path.read_bytes()
    if len(water) != 65536:
        raise ValueError("source water coordinate animation has an invalid size")
    writer.finish(
        [
            {
                "file": water_path.relative_to(media_root).as_posix(),
                "sha256": sha256(water),
                "role": "texture-coordinate animation; water surfaces use the bound floor PBR material",
            }
        ]
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parents[1]
    parser.add_argument("--media-root", type=Path, default=root / "amiga" / "media")
    parser.add_argument("--game-link", type=Path, default=root / "amiga" / "media" / "includes" / "test.lnk")
    parser.add_argument("--authored-dir", type=Path, default=root / "textures_pbr")
    parser.add_argument("--authored-spec", type=Path, default=root / "data" / "renderer_dxr" / "material_sources.json")
    parser.add_argument("--fonts-dir", type=Path, default=root / "fonts")
    parser.add_argument("--output-dir", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        build_pack(
            arguments.media_root.resolve(),
            arguments.game_link.resolve(),
            arguments.authored_dir.resolve(),
            arguments.authored_spec.resolve(),
            arguments.fonts_dir.resolve(),
            arguments.output_dir.resolve(),
        )
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"PBR asset export failed: {error}", file=sys.stderr)
        return 1
    print(arguments.output_dir.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
