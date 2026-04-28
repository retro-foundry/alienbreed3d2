#!/usr/bin/env python3
"""
Convert Alien Breed 3D level binaries (twolev.bin + twolev.graph.bin) into
Quake .map files and optionally compile to .bsp via an external qbsp tool.

This tool is intentionally format-tolerant:
- Handles raw and =SB= packed files.
- Auto-detects AB3D1-style and AB3D2-style level headers.
- Tries multiple edge entry sizes found across source variants.

Notes:
- Geometry output is brush-based and intended for inspection/editing in Quake editors.
- BSP compilation requires an external qbsp binary (ericw-tools, txqbsp, etc.).
"""

from __future__ import annotations

import argparse
from collections import Counter
import dataclasses
import json
import math
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import time
from typing import Dict, Iterable, List, Mapping, Optional, Sequence, Set, Tuple


# ---------------------------------------------------------------------------
# =SB= decompression (ported from Alien-Breed-3D-I src/sb_decompress.c)
# ---------------------------------------------------------------------------

SB_DICBIT = 15
DICSIZ = 1 << SB_DICBIT
MAXMATCH = 256
THRESHOLD = 3
NC = 255 + MAXMATCH + 2 - THRESHOLD
USHRT_BIT = 16
NT = USHRT_BIT + 3
NP_SB = SB_DICBIT + 1
TBIT = 5
CBIT = 9
PBIT_SB = 5
NPT = 0x80


def be_u32(data: bytes, off: int) -> int:
    return struct.unpack_from(">I", data, off)[0]


def be_s32(data: bytes, off: int) -> int:
    return struct.unpack_from(">i", data, off)[0]


def be_s16(data: bytes, off: int) -> int:
    return struct.unpack_from(">h", data, off)[0]


def be_u16(data: bytes, off: int) -> int:
    return struct.unpack_from(">H", data, off)[0]


def le_u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def le_s32(data: bytes, off: int) -> int:
    return struct.unpack_from("<i", data, off)[0]


def le_u16(data: bytes, off: int) -> int:
    return struct.unpack_from("<H", data, off)[0]


def le_s16(data: bytes, off: int) -> int:
    return struct.unpack_from("<h", data, off)[0]


@dataclasses.dataclass
class LH5State:
    inbuf: bytes
    insize: int
    inpos: int = 0

    bitbuf: int = 0
    subbitbuf: int = 0
    bitcount: int = 0

    left: List[int] = dataclasses.field(default_factory=lambda: [0] * (2 * NC - 1))
    right: List[int] = dataclasses.field(default_factory=lambda: [0] * (2 * NC - 1))
    c_table: List[int] = dataclasses.field(default_factory=lambda: [0] * 4096)
    pt_table: List[int] = dataclasses.field(default_factory=lambda: [0] * 256)
    c_len: List[int] = dataclasses.field(default_factory=lambda: [0] * NC)
    pt_len: List[int] = dataclasses.field(default_factory=lambda: [0] * NPT)

    blocksize: int = 0
    np: int = NP_SB
    pbit: int = PBIT_SB

    dtext: bytearray = dataclasses.field(default_factory=lambda: bytearray([0x20] * DICSIZ))
    loc: int = 0


def sb_is_compressed(buf: bytes) -> bool:
    return len(buf) >= 12 and buf[0:4] == b"=SB="


def fillbuf(st: LH5State, n: int) -> None:
    while n > st.bitcount:
        n -= st.bitcount
        st.bitbuf = ((st.bitbuf << st.bitcount) + (st.subbitbuf >> (8 - st.bitcount))) & 0xFFFF
        if st.inpos < st.insize:
            st.subbitbuf = st.inbuf[st.inpos]
            st.inpos += 1
        else:
            st.subbitbuf = 0
        st.bitcount = 8

    st.bitcount -= n
    st.bitbuf = ((st.bitbuf << n) + (st.subbitbuf >> (8 - n))) & 0xFFFF
    st.subbitbuf = (st.subbitbuf << n) & 0xFF


def getbits(st: LH5State, n: int) -> int:
    x = (st.bitbuf >> (16 - n)) & ((1 << n) - 1)
    fillbuf(st, n)
    return x


def peekbits(st: LH5State, n: int) -> int:
    return st.bitbuf >> (16 - n)


def init_getbits(st: LH5State) -> None:
    st.bitbuf = 0
    st.subbitbuf = 0
    st.bitcount = 0
    fillbuf(st, 16)


def make_table(st: LH5State, nchar: int, bitlen: Sequence[int], tablebits: int, table: List[int]) -> bool:
    count = [0] * 17
    weight = [0] * 17
    start = [0] * 17
    avail = nchar

    for i in range(1, 17):
        count[i] = 0
        weight[i] = 1 << (16 - i)

    for i in range(nchar):
        bl = bitlen[i]
        if bl > 16:
            return False
        count[bl] += 1

    total = 0
    for i in range(1, 17):
        start[i] = total
        total += weight[i] * count[i]

    if (total & 0xFFFF) != 0:
        return False

    m = 16 - tablebits
    for i in range(1, tablebits + 1):
        start[i] >>= m
        weight[i] >>= m

    j = start[tablebits + 1] >> m
    k = min(1 << tablebits, 4096)
    if j != 0:
        for i in range(j, k):
            table[i] = 0

    for j in range(nchar):
        bl = bitlen[j]
        if bl == 0:
            continue

        l = start[bl] + weight[bl]
        if bl <= tablebits:
            l = min(l, 4096)
            for i in range(start[bl], l):
                table[i] = j
        else:
            i = start[bl]
            if (i >> m) > 4096:
                return False

            # Pointer target: (kind, index)
            # kind=0 -> table[index], kind=1 -> right[index], kind=2 -> left[index]
            kind = 0
            index = i >> m
            i <<= tablebits
            n = bl - tablebits

            def ptr_get(k: int, idx: int) -> int:
                if k == 0:
                    return table[idx]
                if k == 1:
                    return st.right[idx]
                return st.left[idx]

            def ptr_set(k: int, idx: int, value: int) -> None:
                if k == 0:
                    table[idx] = value
                elif k == 1:
                    st.right[idx] = value
                else:
                    st.left[idx] = value

            while True:
                n -= 1
                if n < 0:
                    break

                value = ptr_get(kind, index)
                if value == 0:
                    if avail >= len(st.left):
                        return False
                    st.right[avail] = 0
                    st.left[avail] = 0
                    value = avail
                    ptr_set(kind, index, value)
                    avail += 1

                if i & 0x8000:
                    kind = 1
                    index = value
                else:
                    kind = 2
                    index = value

                i = (i << 1) & 0xFFFFFFFF

            ptr_set(kind, index, j)

        start[bl] = l

    return True


def read_pt_len(st: LH5State, nn: int, nbit: int, i_special: int) -> bool:
    n = getbits(st, nbit)
    if n == 0:
        c = getbits(st, nbit)
        for i in range(nn):
            st.pt_len[i] = 0
        for i in range(256):
            st.pt_table[i] = c
        return True

    i = 0
    while i < min(n, NPT):
        c = peekbits(st, 3)
        if c != 7:
            fillbuf(st, 3)
        else:
            mask = 1 << (16 - 4)
            while st.bitbuf & mask:
                mask >>= 1
                c += 1
            fillbuf(st, c - 3)

        st.pt_len[i] = c
        i += 1
        if i == i_special:
            c = getbits(st, 2)
            while c > 0 and i < NPT:
                c -= 1
                st.pt_len[i] = 0
                i += 1

    while i < nn:
        st.pt_len[i] = 0
        i += 1

    return make_table(st, nn, st.pt_len, 8, st.pt_table)


def read_c_len(st: LH5State) -> bool:
    n = getbits(st, CBIT)
    if n == 0:
        c = getbits(st, CBIT)
        for i in range(NC):
            st.c_len[i] = 0
        for i in range(4096):
            st.c_table[i] = c
        return True

    i = 0
    while i < min(n, NC):
        c = st.pt_table[peekbits(st, 8)]
        if c >= NT:
            mask = 1 << (16 - 9)
            while c >= NT and (mask or c != st.left[c]):
                if st.bitbuf & mask:
                    c = st.right[c]
                else:
                    c = st.left[c]
                mask >>= 1
        fillbuf(st, st.pt_len[c])

        if c <= 2:
            if c == 0:
                c = 1
            elif c == 1:
                c = getbits(st, 4) + 3
            else:
                c = getbits(st, CBIT) + 20
            while c > 0 and i < NC:
                c -= 1
                st.c_len[i] = 0
                i += 1
        else:
            st.c_len[i] = c - 2
            i += 1

    while i < NC:
        st.c_len[i] = 0
        i += 1

    return make_table(st, NC, st.c_len, 12, st.c_table)


def decode_c(st: LH5State) -> int:
    if st.blocksize == 0:
        st.blocksize = getbits(st, 16)
        if not read_pt_len(st, NT, TBIT, 3):
            return -1
        if not read_c_len(st):
            return -1
        if not read_pt_len(st, st.np, st.pbit, -1):
            return -1

    st.blocksize -= 1

    j = st.c_table[peekbits(st, 12)]
    if j < NC:
        fillbuf(st, st.c_len[j])
        return j

    fillbuf(st, 12)
    mask = 1 << (16 - 1)
    while j >= NC and (mask or j != st.left[j]):
        if st.bitbuf & mask:
            j = st.right[j]
        else:
            j = st.left[j]
        mask >>= 1
    fillbuf(st, st.c_len[j] - 12)
    return j


def decode_p(st: LH5State) -> int:
    j = st.pt_table[peekbits(st, 8)]
    if j < st.np:
        fillbuf(st, st.pt_len[j])
    else:
        fillbuf(st, 8)
        mask = 1 << (16 - 1)
        while j >= st.np and (mask or j != st.left[j]):
            if st.bitbuf & mask:
                j = st.right[j]
            else:
                j = st.left[j]
            mask >>= 1
        fillbuf(st, st.pt_len[j] - 8)

    if j != 0:
        j = (1 << (j - 1)) + getbits(st, j - 1)
    return j


def sb_decompress(src: bytes) -> bytes:
    if not sb_is_compressed(src):
        return src

    if len(src) < 12:
        raise ValueError("SB buffer too small")

    unpacked = be_u32(src, 4)
    payload = src[12:]

    st = LH5State(payload, len(payload))
    init_getbits(st)

    out = bytearray()
    while len(out) < unpacked:
        c = decode_c(st)
        if c < 0:
            raise ValueError("SB decode_c failed")

        if c < 256:
            b = c & 0xFF
            out.append(b)
            st.dtext[st.loc] = b
            st.loc = (st.loc + 1) & (DICSIZ - 1)
        else:
            match_len = c - 256 + THRESHOLD
            match_pos = decode_p(st)
            if match_pos < 0:
                raise ValueError("SB decode_p failed")

            match_off = (st.loc - match_pos - 1) & (DICSIZ - 1)
            for i in range(match_len):
                b = st.dtext[(match_off + i) & (DICSIZ - 1)]
                out.append(b)
                st.dtext[st.loc] = b
                st.loc = (st.loc + 1) & (DICSIZ - 1)
                if len(out) >= unpacked:
                    break

    if len(out) != unpacked:
        raise ValueError(f"SB unpack size mismatch: got {len(out)} expected {unpacked}")

    return bytes(out)


# ---------------------------------------------------------------------------
# AB3D level format parsing
# ---------------------------------------------------------------------------


@dataclasses.dataclass
class LevelHeader:
    variant: str
    offset_endian: str
    header_offset: int
    plr1_x: int
    plr1_z: int
    plr1_zone: int
    num_control_points: int
    num_points: int
    num_zones: int
    num_objects: int
    points_offset: int
    floorline_offset: int
    object_data_offset: int
    shot_data_offset: int
    alien_shot_data_offset: int
    object_points_offset: int
    plr1_object_offset: int
    plr2_object_offset: int
    score: int


@dataclasses.dataclass
class Edge:
    x: int
    z: int
    dx: int
    dz: int
    join_zone: int
    flags: int


@dataclasses.dataclass
class Zone:
    zone_id: int
    floor: int
    roof: int
    upper_floor: int
    upper_roof: int
    edge_ids: List[int]


@dataclasses.dataclass
class TextureImage:
    name: str
    width: int
    height: int
    pixels: List[Tuple[int, int, int]]


SegmentKey = Tuple[Tuple[int, int], Tuple[int, int]]


@dataclasses.dataclass
class WallTextureSpan:
    texture_id: int
    material: str
    low: float
    high: float


@dataclasses.dataclass
class FlatTextureSpan:
    command_type: int
    material: str
    z: float
    poly: List[Tuple[float, float]] = dataclasses.field(default_factory=list)


# ---------------------------------------------------------------------------
# AB3D texture extraction and Quake 2 texture writing
# ---------------------------------------------------------------------------


def sanitize_texture_name(name: str) -> str:
    out = []
    for ch in name.lower():
        if ch.isalnum():
            out.append(ch)
        else:
            out.append("_")
    clean = "".join(out).strip("_")
    return clean or "texture"


def rgb444_to_rgb(word: int) -> Tuple[int, int, int]:
    return (((word >> 8) & 0xF) * 17, ((word >> 4) & 0xF) * 17, (word & 0xF) * 17)


AB3D_WALL_EXPORT_BRIGHTNESS = 16
AB3D_WALL_TEXTURE_OVERRIDES: Dict[str, Tuple[int, int]] = {
    "bigdoor": (126, 128),
    "dirt": (258, 128),
    "shinymetal": (258, 128),
    "switches": (66, 32),
}

AB3D2_TEXTURE_PREFIX = "ab3d2"
AB3D2_DEFAULT_TEXTURE = f"{AB3D2_TEXTURE_PREFIX}/hullmetal"
AB3D2_DEFAULT_FLOOR_TEXTURE = f"{AB3D2_TEXTURE_PREFIX}/floor_0001"
AB3D2_DEFAULT_CEILING_TEXTURE = f"{AB3D2_TEXTURE_PREFIX}/floor_0201"
AB3D2_FLOOR_EXPORT_BRIGHTNESS = 8
AB3D2_WALL_SLOT_NAMES: List[Optional[str]] = [
    "stonewall",
    "brownpipes",
    "hullmetal",
    "technotritile",
    "brownspeakers",
    "chevrondoor",
    "technolights",
    "redhullmetal",
    "alienredwall",
    "gieger",
    "rocky",
    "steampunk",
    "brownstonestep",
    None,
    None,
    None,
]
AB3D2_WALL_TEXTURE_DIMS: Dict[str, Tuple[int, int]] = {
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
AB3D2_FLOOR_TILE_OFFSETS = [row * 256 + col for row in range(5) for col in range(4)]


def align_up(value: int, multiple: int) -> int:
    return ((value + multiple - 1) // multiple) * multiple


def wall_texture_dims_from_size(name: str, pixel_size: int) -> Tuple[int, int]:
    override = AB3D_WALL_TEXTURE_OVERRIDES.get(sanitize_texture_name(name))
    if override is not None:
        return override

    valshift = -1
    for vs in range(6, 2, -1):
        bytes_per_strip = (1 << vs) * 2
        if pixel_size % bytes_per_strip == 0:
            valshift = vs
            break

    if valshift < 0:
        best_strips = 0
        for vs in range(6, 2, -1):
            bytes_per_strip = (1 << vs) * 2
            strips = pixel_size // bytes_per_strip
            if strips > best_strips:
                best_strips = strips
                valshift = vs

    if valshift < 0:
        valshift = 6

    rows = 1 << valshift
    strips = max(1, pixel_size // (rows * 2))
    cols = strips * 3
    return cols, rows


def decode_ab3d_wall_texture(name: str, data: bytes) -> Optional[TextureImage]:
    if len(data) <= 2048:
        return None

    # Plain AB3D wall WADs start with a 2048-byte brightness LUT:
    # 32 shade blocks * 32 12-bit Amiga colours. The wall texels that follow
    # are 5-bit indices into one shade block.
    lut = data[:2048]
    chunk = data[2048:]
    if not chunk:
        return None

    width, height = wall_texture_dims_from_size(name, len(chunk))
    bytes_per_strip = height * 2
    shade_off = AB3D_WALL_EXPORT_BRIGHTNESS * 64

    pixels = [(0, 0, 0)] * (width * height)
    for x in range(width):
        strip = x // 3
        shift = (x % 3) * 5
        strip_off = strip * bytes_per_strip
        if strip_off + bytes_per_strip > len(chunk):
            continue
        for y in range(height):
            off = strip_off + y * 2
            word = (chunk[off] << 8) | chunk[off + 1]
            texel = (word >> shift) & 31
            colour_off = shade_off + texel * 2
            pixels[y * width + x] = rgb444_to_rgb((lut[colour_off] << 8) | lut[colour_off + 1])

    return TextureImage(name=sanitize_texture_name(name), width=width, height=height, pixels=pixels)


def load_ab3d_wall_textures(walls_dir: pathlib.Path) -> List[TextureImage]:
    if not walls_dir.exists():
        return []

    candidates = list(walls_dir.glob("*.wad"))
    chosen: Dict[str, pathlib.Path] = {}
    for path in sorted(candidates):
        key = sanitize_texture_name(path.stem)
        if key not in chosen:
            chosen[key] = path

    textures: List[TextureImage] = []
    for key, path in sorted(chosen.items()):
        try:
            data = sb_decompress(path.read_bytes())
        except Exception:
            continue
        image = decode_ab3d_wall_texture(key, data)
        if image is not None:
            textures.append(image)

    return textures


def ab3d2_wall_slot_materials() -> Dict[int, str]:
    materials: Dict[int, str] = {}
    for i, name in enumerate(AB3D2_WALL_SLOT_NAMES):
        if name:
            materials[i] = f"{AB3D2_TEXTURE_PREFIX}/{sanitize_texture_name(name)}"
    return materials


def ab3d2_floor_material(tile_offset: int) -> str:
    return f"{AB3D2_TEXTURE_PREFIX}/floor_{tile_offset & 0xFFFF:04x}"


def read_ab3d2_palette(path: pathlib.Path) -> Optional[List[Tuple[int, int, int]]]:
    if not path.exists():
        return None

    data = path.read_bytes()
    if len(data) < 256 * 6:
        return None

    palette: List[Tuple[int, int, int]] = []
    for i in range(256):
        off = i * 6
        r = be_u16(data, off)
        g = be_u16(data, off + 2)
        b = be_u16(data, off + 4)
        palette.append((min(r, 255), min(g, 255), min(b, 255)))
    return palette


def infer_ab3d2_wall_dims(name: str, chunk_size: int) -> Tuple[int, int]:
    key = sanitize_texture_name(name)
    dims = AB3D2_WALL_TEXTURE_DIMS.get(key)
    if dims is not None:
        return dims

    # AB3D2 wall records use height masks/shifts and the shipped global wall
    # set is overwhelmingly 128 rows. Most files include a harmless 2-byte pad.
    for rows in (128, 64, 32, 256):
        stride = rows * 2
        if chunk_size >= stride and chunk_size % stride in (0, 2):
            usable = chunk_size - (chunk_size % stride)
            strips = max(1, usable // stride)
            return strips * 3, rows
    rows = 128
    usable = chunk_size - (chunk_size % (rows * 2))
    strips = max(1, usable // (rows * 2))
    return strips * 3, rows


def decode_ab3d2_wall_texture(
    name: str,
    data: bytes,
    game_palette: Sequence[Tuple[int, int, int]],
) -> Optional[TextureImage]:
    if len(data) <= 2048 or len(game_palette) < 256:
        return None

    # AB3D2 .256wad wall files start with a 32x32 byte palette remap table
    # stored in UWORD slots, followed by packed 5-bit texels.
    lut = data[:2048]
    chunk = data[2048:]
    if not chunk:
        return None

    width, rows = infer_ab3d2_wall_dims(name, len(chunk))
    strips = (width + 2) // 3
    bytes_per_strip = rows * 2
    usable = strips * bytes_per_strip
    if usable > len(chunk):
        usable = len(chunk) - (len(chunk) % bytes_per_strip)
    if usable <= 0:
        return None

    strips = usable // bytes_per_strip
    width = min(width, strips * 3)
    if width <= 0:
        return None

    shade_off = AB3D_WALL_EXPORT_BRIGHTNESS * 64
    pixels = [(0, 0, 0)] * (width * rows)
    for x in range(width):
        strip = x // 3
        shift = (x % 3) * 5
        strip_off = strip * bytes_per_strip
        if strip_off + bytes_per_strip > usable:
            continue
        for y in range(rows):
            off = strip_off + y * 2
            word = (chunk[off] << 8) | chunk[off + 1]
            texel = (word >> shift) & 31
            palette_index = lut[shade_off + texel * 2]
            pixels[y * width + x] = game_palette[palette_index]

    return TextureImage(name=sanitize_texture_name(name), width=width, height=rows, pixels=pixels)


def load_ab3d2_wall_textures(
    walls_dir: pathlib.Path,
    palette_path: pathlib.Path,
) -> Tuple[List[TextureImage], Optional[List[Tuple[int, int, int]]]]:
    if not walls_dir.exists():
        return [], None

    palette = read_ab3d2_palette(palette_path)
    if palette is None:
        return [], None

    textures: List[TextureImage] = []
    for path in sorted(walls_dir.glob("*.256wad")):
        key = sanitize_texture_name(path.stem)
        try:
            data = sb_decompress(path.read_bytes())
        except Exception:
            continue
        image = decode_ab3d2_wall_texture(key, data, palette)
        if image is not None:
            textures.append(image)

    return textures, palette


def decode_ab3d2_floor_texture(
    tile_offset: int,
    floor_data: bytes,
    remap: bytes,
    game_palette: Sequence[Tuple[int, int, int]],
) -> Optional[TextureImage]:
    if len(game_palette) < 256:
        return None

    width = 64
    height = 64
    pixels: List[Tuple[int, int, int]] = []

    remap_off = 8192 + AB3D2_FLOOR_EXPORT_BRIGHTNESS * 512
    if remap_off + 256 > len(remap):
        remap_off = 8192 if len(remap) >= 8192 + 256 else -1

    for y in range(height):
        for x in range(width):
            src = tile_offset + (((y << 8) | x) * 4)
            texel = floor_data[src] if 0 <= src < len(floor_data) else 0
            palette_index = remap[remap_off + texel] if remap_off >= 0 else texel
            pixels.append(game_palette[palette_index])

    return TextureImage(
        name=f"floor_{tile_offset & 0xFFFF:04x}",
        width=width,
        height=height,
        pixels=pixels,
    )


def load_ab3d2_floor_textures(
    floor_path: pathlib.Path,
    remap_path: pathlib.Path,
    game_palette: Sequence[Tuple[int, int, int]],
) -> List[TextureImage]:
    if not floor_path.exists():
        return []

    try:
        floor_data = sb_decompress(floor_path.read_bytes())
    except Exception:
        return []

    try:
        remap = sb_decompress(remap_path.read_bytes()) if remap_path.exists() else b""
    except Exception:
        remap = b""

    textures: List[TextureImage] = []
    for tile_offset in AB3D2_FLOOR_TILE_OFFSETS:
        image = decode_ab3d2_floor_texture(tile_offset, floor_data, remap, game_palette)
        if image is not None:
            textures.append(image)
    return textures


def load_wall_textures(
    walls_dir: pathlib.Path,
    palette_path: pathlib.Path,
) -> Tuple[List[TextureImage], List[Tuple[int, int, int]], str]:
    if walls_dir.exists() and any(walls_dir.glob("*.256wad")):
        textures, palette = load_ab3d2_wall_textures(walls_dir, palette_path)
        if textures and palette is not None:
            return textures, palette, AB3D2_TEXTURE_PREFIX
        return [], [], AB3D2_TEXTURE_PREFIX

    textures = load_ab3d_wall_textures(walls_dir)
    if not textures:
        return [], [], "ab3d"
    return textures, build_global_palette(textures), "ab3d"


def build_global_palette(images: Sequence[TextureImage]) -> List[Tuple[int, int, int]]:
    counts: Counter[Tuple[int, int, int]] = Counter()
    for image in images:
        counts.update(image.pixels)

    palette = [(0, 0, 0)]
    seen = {palette[0]}
    for colour, _count in counts.most_common():
        if colour in seen:
            continue
        palette.append(colour)
        seen.add(colour)
        if len(palette) == 256:
            break

    while len(palette) < 256:
        palette.append((0, 0, 0))
    return palette


def nearest_palette_index(
    colour: Tuple[int, int, int],
    palette: Sequence[Tuple[int, int, int]],
    cache: Dict[Tuple[int, int, int], int],
) -> int:
    cached = cache.get(colour)
    if cached is not None:
        return cached

    cr, cg, cb = colour
    best_i = 0
    best_d = 1 << 62
    for i, (pr, pg, pb) in enumerate(palette):
        dr = cr - pr
        dg = cg - pg
        db = cb - pb
        d = dr * dr + dg * dg + db * db
        if d < best_d:
            best_i = i
            best_d = d
            if d == 0:
                break

    cache[colour] = best_i
    return best_i


def index_texture(image: TextureImage, palette: Sequence[Tuple[int, int, int]]) -> List[int]:
    cache: Dict[Tuple[int, int, int], int] = {}
    return [nearest_palette_index(pixel, palette, cache) for pixel in image.pixels]


def downsample_mip(
    pixels: Sequence[int],
    width: int,
    height: int,
    palette: Sequence[Tuple[int, int, int]],
) -> Tuple[List[int], int, int]:
    new_w = max(1, width // 2)
    new_h = max(1, height // 2)
    cache: Dict[Tuple[int, int, int], int] = {}
    out: List[int] = []

    for y in range(new_h):
        for x in range(new_w):
            samples = []
            for yy in (y * 2, min(y * 2 + 1, height - 1)):
                for xx in (x * 2, min(x * 2 + 1, width - 1)):
                    samples.append(palette[pixels[yy * width + xx]])
            r = sum(c[0] for c in samples) // len(samples)
            g = sum(c[1] for c in samples) // len(samples)
            b = sum(c[2] for c in samples) // len(samples)
            out.append(nearest_palette_index((r, g, b), palette, cache))

    return out, new_w, new_h


def texture_mips(image: TextureImage, palette: Sequence[Tuple[int, int, int]]) -> Tuple[List[bytes], int, int]:
    width = image.width
    height = image.height
    current = index_texture(image, palette)

    mips = [bytes(current)]
    cur_w = width
    cur_h = height
    for _ in range(3):
        current, cur_w, cur_h = downsample_mip(current, cur_w, cur_h, palette)
        mips.append(bytes(current))

    return mips, width, height


def write_wal(path: pathlib.Path, material_name: str, image: TextureImage, palette: Sequence[Tuple[int, int, int]]) -> None:
    mips, width, height = texture_mips(image, palette)
    offsets = []
    off = 100
    for mip in mips:
        offsets.append(off)
        off += len(mip)

    name_bytes = material_name.encode("ascii", errors="ignore")[:31]
    header = struct.pack(
        "<32sII4I32sIII",
        name_bytes + b"\0" * (32 - len(name_bytes)),
        width,
        height,
        offsets[0],
        offsets[1],
        offsets[2],
        offsets[3],
        b"\0" * 32,
        0,
        0,
        0,
    )

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + b"".join(mips))


def pcx_rle(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(data):
        value = data[i]
        run = 1
        while i + run < len(data) and data[i + run] == value and run < 63:
            run += 1
        if run > 1 or value >= 0xC0:
            out.append(0xC0 | run)
            out.append(value)
        else:
            out.append(value)
        i += run
    return bytes(out)


def write_indexed_pcx(
    path: pathlib.Path,
    width: int,
    height: int,
    image: bytes,
    palette: Sequence[Tuple[int, int, int]],
) -> None:
    if len(image) != width * height:
        raise ValueError(f"PCX image data is {len(image)} bytes, expected {width * height}")

    header = bytearray(128)
    header[0] = 0x0A
    header[1] = 5
    header[2] = 1
    header[3] = 8
    struct.pack_into("<HHHH", header, 4, 0, 0, width - 1, height - 1)
    struct.pack_into("<HH", header, 12, width, height)
    header[65] = 1
    struct.pack_into("<H", header, 66, width)
    struct.pack_into("<H", header, 68, 1)
    struct.pack_into("<HH", header, 70, width, height)

    pal_bytes = bytearray()
    pal = list(palette[:256])
    while len(pal) < 256:
        pal.append((0, 0, 0))
    for r, g, b in pal:
        pal_bytes.extend((r, g, b))

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(header) + pcx_rle(image) + b"\x0c" + bytes(pal_bytes))


def write_palette_pcx(path: pathlib.Path, palette: Sequence[Tuple[int, int, int]]) -> None:
    width = 16
    height = 16
    write_indexed_pcx(path, width, height, bytes(range(256)), palette)


def write_wad2(path: pathlib.Path, images: Sequence[TextureImage], palette: Sequence[Tuple[int, int, int]]) -> None:
    lumps: List[Tuple[str, bytes]] = []
    for image in images:
        mips, width, height = texture_mips(image, palette)
        offsets = []
        off = 40
        for mip in mips:
            offsets.append(off)
            off += len(mip)

        name_bytes = image.name.encode("ascii", errors="ignore")[:15]
        header = struct.pack(
            "<16sII4I",
            name_bytes + b"\0" * (16 - len(name_bytes)),
            width,
            height,
            offsets[0],
            offsets[1],
            offsets[2],
            offsets[3],
        )
        lumps.append((image.name[:15], header + b"".join(mips)))

    data = bytearray()
    directory = bytearray()
    for name, payload in lumps:
        filepos = 12 + len(data)
        data.extend(payload)
        name_bytes = name.encode("ascii", errors="ignore")[:15]
        directory.extend(
            struct.pack(
                "<IIIBBBB16s",
                filepos,
                len(payload),
                len(payload),
                0x44,
                0,
                0,
                0,
                name_bytes + b"\0" * (16 - len(name_bytes)),
            )
        )

    header = struct.pack("<4sII", b"WAD2", len(lumps), 12 + len(data))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + bytes(data) + bytes(directory))


def write_texture_assets(
    walls_dir: pathlib.Path,
    q2_root: pathlib.Path,
    wad_path: pathlib.Path,
    palette_path: pathlib.Path,
    floor_path: pathlib.Path,
    floor_remap_path: pathlib.Path,
) -> Tuple[int, List[str], str]:
    textures, palette, prefix = load_wall_textures(walls_dir, palette_path)
    if prefix == AB3D2_TEXTURE_PREFIX and palette:
        textures.extend(load_ab3d2_floor_textures(floor_path, floor_remap_path, palette))
    if not textures:
        return 0, [], prefix

    write_palette_pcx(q2_root / "baseq2" / "pics" / "colormap.pcx", palette)

    texture_base = q2_root / "baseq2" / "textures"
    for stale_prefix in ("ab3d", AB3D2_TEXTURE_PREFIX):
        stale_dir = texture_base / stale_prefix
        if stale_dir.exists():
            for stale in stale_dir.glob("*.wal"):
                stale.unlink()

    material_names = []
    texture_dir = texture_base / prefix
    for image in textures:
        material = f"{prefix}/{image.name}"
        material_names.append(material)
        write_wal(texture_dir / f"{image.name}.wal", material, image, palette)

    write_wad2(wad_path, textures, palette)
    return len(textures), material_names, prefix


def trenchbroom_executable(trenchbroom: pathlib.Path) -> pathlib.Path:
    exe = trenchbroom
    if exe.is_dir():
        exe = exe / "TrenchBroom.exe"
    return exe


def placeholder_palette() -> List[Tuple[int, int, int]]:
    palette: List[Tuple[int, int, int]] = []
    for i in range(256):
        if i < 64:
            palette.append((i * 3, i * 3, i * 3))
        elif i < 128:
            palette.append((64 + (i - 64) * 2, 28 + (i - 64), 20))
        elif i < 192:
            palette.append((30, 55 + (i - 128) * 2, 88 + (i - 128) * 2))
        else:
            palette.append((120 + (i - 192) * 2, 115 + (i - 192) * 2, 86))
    return [(min(r, 255), min(g, 255), min(b, 255)) for r, g, b in palette]


def write_placeholder_md2(path: pathlib.Path) -> None:
    """Write a tiny MD2 model for TrenchBroom's Quake 2 info_player_start preview."""

    skin_names = [
        b"models/monsters/insane/skin0.pcx",
        b"models/monsters/insane/skin1.pcx",
    ]
    st = [(0, 0), (63, 0), (0, 63), (63, 63)]
    tris = [
        ((0, 1, 2), (0, 1, 2)),
        ((0, 3, 1), (0, 3, 1)),
        ((1, 3, 2), (1, 3, 2)),
        ((2, 3, 0), (2, 3, 0)),
    ]
    verts = [
        (0, 0, 0, 0),
        (2, 0, 0, 0),
        (0, 2, 0, 0),
        (1, 1, 2, 0),
    ]

    num_skins = len(skin_names)
    num_vertices = len(verts)
    num_st = len(st)
    num_tris = len(tris)
    num_glcmds = 0
    num_frames = 210
    skinwidth = 64
    skinheight = 64
    framesize = 40 + num_vertices * 4
    ofs_skins = 17 * 4
    ofs_st = ofs_skins + num_skins * 64
    ofs_tris = ofs_st + num_st * 4
    ofs_frames = ofs_tris + num_tris * 12
    ofs_glcmds = ofs_frames + num_frames * framesize
    ofs_end = ofs_glcmds + num_glcmds * 4

    payload = bytearray()
    payload.extend(
        struct.pack(
            "<17i",
            844121161,  # IDP2
            8,
            skinwidth,
            skinheight,
            framesize,
            num_skins,
            num_vertices,
            num_st,
            num_tris,
            num_glcmds,
            num_frames,
            ofs_skins,
            ofs_st,
            ofs_tris,
            ofs_frames,
            ofs_glcmds,
            ofs_end,
        )
    )
    for skin_name in skin_names:
        payload.extend(skin_name[:63] + b"\0" * (64 - min(len(skin_name), 63)))
    for s, t in st:
        payload.extend(struct.pack("<hh", s, t))
    for vertex_indices, st_indices in tris:
        payload.extend(struct.pack("<3H3H", *vertex_indices, *st_indices))

    frame_vertices = b"".join(struct.pack("<4B", *v) for v in verts)
    for frame in range(num_frames):
        name = f"stand{frame:03d}".encode("ascii")
        payload.extend(struct.pack("<3f3f16s", 16.0, 16.0, 28.0, -16.0, -16.0, -24.0, name[:15]))
        payload.extend(frame_vertices)

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(payload))


def write_placeholder_md2_skins(directory: pathlib.Path) -> int:
    palette = placeholder_palette()
    skin_count = 0
    for skin_index in range(2):
        image = bytearray()
        for y in range(64):
            for x in range(64):
                checker = ((x // 8) + (y // 8) + skin_index) % 2
                image.append(104 if checker else 34)
        write_indexed_pcx(directory / f"skin{skin_index}.pcx", 64, 64, bytes(image), palette)
        skin_count += 1
    return skin_count


def configure_trenchbroom_quake2_path(q2_root: pathlib.Path) -> Tuple[bool, str]:
    appdata = os.environ.get("APPDATA")
    if not appdata:
        return False, "APPDATA is not set"

    prefs_path = pathlib.Path(appdata) / "TrenchBroom" / "Preferences.json"
    prefs_path.parent.mkdir(parents=True, exist_ok=True)
    if prefs_path.exists():
        try:
            prefs = json.loads(prefs_path.read_text(encoding="utf-8"))
            if not isinstance(prefs, dict):
                prefs = {}
        except (OSError, json.JSONDecodeError):
            prefs = {}
    else:
        prefs = {}

    game_path = str(q2_root.resolve())
    prefs["Games/Quake 2/Path"] = game_path
    try:
        prefs_path.write_text(json.dumps(prefs, indent=4, sort_keys=True) + "\n", encoding="utf-8")
    except OSError as exc:
        return False, f"failed to write {prefs_path}: {exc}"
    return True, f"configured Quake 2 game path {game_path}"


def install_trenchbroom_assets(q2_root: pathlib.Path, trenchbroom: pathlib.Path) -> Tuple[bool, str]:
    exe = trenchbroom_executable(trenchbroom)
    if not exe.exists():
        return False, f"TrenchBroom executable not found: {exe}"

    src_base = q2_root / "baseq2"
    src_textures = src_base / "textures"
    if not src_textures.exists():
        return False, f"Quake 2 texture directory not found: {src_textures}"

    dst_assets = exe.parent / "defaults" / "assets"
    copied = 0
    for prefix in ("ab3d", AB3D2_TEXTURE_PREFIX):
        for generated_dir in (dst_assets / "textures" / prefix, dst_assets / "baseq2" / "textures" / prefix):
            if generated_dir.exists():
                for stale in generated_dir.glob("*.wal"):
                    stale.unlink()

    for src in src_textures.rglob("*"):
        if not src.is_file():
            continue
        rel = src.relative_to(src_textures)
        for dst_root in (dst_assets / "textures", dst_assets / "baseq2" / "textures"):
            dst = dst_root / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
            copied += 1

    src_palette = src_base / "pics" / "colormap.pcx"
    if src_palette.exists():
        for dst_root in (dst_assets / "pics", dst_assets / "baseq2" / "pics"):
            dst_palette = dst_root / "colormap.pcx"
            dst_palette.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src_palette, dst_palette)
            copied += 1

    model_dir = dst_assets / "models" / "monsters" / "insane"
    write_placeholder_md2(model_dir / "tris.md2")
    copied += 1
    copied += write_placeholder_md2_skins(model_dir)

    pref_ok, pref_msg = configure_trenchbroom_quake2_path(q2_root)
    if pref_ok:
        return True, f"installed {copied} texture/model assets into {dst_assets}; {pref_msg}"
    return True, f"installed {copied} texture/model assets into {dst_assets}; game path not configured: {pref_msg}"


def _candidate_score(
    data_len: int,
    num_points: int,
    num_zones: int,
    num_objects: int,
    points_off: int,
    floor_off: int,
    obj_off: int,
    shot_off: int,
    alien_shot_off: int,
    obj_pts_off: int,
    p1_obj_off: int,
    p2_obj_off: int,
) -> int:
    score = 0

    if 1 <= num_points <= 8192:
        score += 3
    if 1 <= num_zones <= 2048:
        score += 4
    if 0 <= num_objects <= 4096:
        score += 1

    offs = [
        points_off,
        floor_off,
        obj_off,
        shot_off,
        alien_shot_off,
        obj_pts_off,
        p1_obj_off,
        p2_obj_off,
    ]
    for off in offs:
        if 0 <= off < data_len:
            score += 1

    # Newer AB3D2 variants often have: points < floorlines < objects
    if 0 <= points_off < floor_off < obj_off < data_len:
        score += 6

    # Older variants can keep object data before points/floorlines.
    if 0 <= obj_off < points_off < floor_off < data_len:
        score += 6

    if points_off % 2 == 0 and floor_off % 2 == 0 and obj_off % 2 == 0:
        score += 1

    return score


def parse_header_ab3d2(data: bytes) -> Optional[LevelHeader]:
    # AB3D2: 1600-byte message block, then TLBT (54 bytes)
    base = 1600
    if len(data) < base + 54:
        return None

    p1x = be_s16(data, base + 0)
    p1z = be_s16(data, base + 2)
    p1zone = be_u16(data, base + 4)

    # Player 2 start exists in this variant; ignored for export.
    _p2x = be_s16(data, base + 6)
    _p2z = be_s16(data, base + 8)
    _p2zone = be_u16(data, base + 10)

    num_cp = be_u16(data, base + 12)
    num_points = be_u16(data, base + 14)
    num_zones_m1 = be_u16(data, base + 16)
    _unknown = be_u16(data, base + 18)
    num_objs = be_u16(data, base + 20)

    points_off = be_u32(data, base + 22)
    floor_off = be_u32(data, base + 26)
    obj_off = be_u32(data, base + 30)
    shot_off = be_u32(data, base + 34)
    alien_shot_off = be_u32(data, base + 38)
    obj_pts_off = be_u32(data, base + 42)
    p1_obj_off = be_u32(data, base + 46)
    p2_obj_off = be_u32(data, base + 50)

    num_zones = int(num_zones_m1) + 1
    score = _candidate_score(
        len(data),
        num_points,
        num_zones,
        num_objs,
        points_off,
        floor_off,
        obj_off,
        shot_off,
        alien_shot_off,
        obj_pts_off,
        p1_obj_off,
        p2_obj_off,
    )

    return LevelHeader(
        variant="ab3d2",
        offset_endian="be",
        header_offset=base,
        plr1_x=p1x,
        plr1_z=p1z,
        plr1_zone=p1zone,
        num_control_points=num_cp,
        num_points=num_points,
        num_zones=num_zones,
        num_objects=num_objs,
        points_offset=points_off,
        floorline_offset=floor_off,
        object_data_offset=obj_off,
        shot_data_offset=shot_off,
        alien_shot_data_offset=alien_shot_off,
        object_points_offset=obj_pts_off,
        plr1_object_offset=p1_obj_off,
        plr2_object_offset=p2_obj_off,
        score=score,
    )


def parse_header_ab3d1(data: bytes, long_endian: str) -> Optional[LevelHeader]:
    # AB3D1-style layout starts at offset 0, 48 bytes total
    if len(data) < 48:
        return None

    p1x = be_s16(data, 0)
    p1z = be_s16(data, 2)
    p1zone = be_u16(data, 4)
    num_cp = be_u16(data, 6)
    num_points = be_u16(data, 8)
    num_zones = be_u16(data, 10)
    _unknown = be_u16(data, 12)
    num_objs = be_u16(data, 14)

    if long_endian == "be":
        u32 = be_u32
        variant = "ab3d1"
    else:
        u32 = le_u32
        variant = "ab3d1_mixed"

    points_off = u32(data, 16)
    floor_off = u32(data, 20)
    obj_off = u32(data, 24)
    shot_off = u32(data, 28)
    alien_shot_off = u32(data, 32)
    obj_pts_off = u32(data, 36)
    p1_obj_off = u32(data, 40)
    p2_obj_off = u32(data, 44)

    score = _candidate_score(
        len(data),
        num_points,
        num_zones,
        num_objs,
        points_off,
        floor_off,
        obj_off,
        shot_off,
        alien_shot_off,
        obj_pts_off,
        p1_obj_off,
        p2_obj_off,
    )

    return LevelHeader(
        variant=variant,
        offset_endian=long_endian,
        header_offset=0,
        plr1_x=p1x,
        plr1_z=p1z,
        plr1_zone=p1zone,
        num_control_points=num_cp,
        num_points=num_points,
        num_zones=num_zones,
        num_objects=num_objs,
        points_offset=points_off,
        floorline_offset=floor_off,
        object_data_offset=obj_off,
        shot_data_offset=shot_off,
        alien_shot_data_offset=alien_shot_off,
        object_points_offset=obj_pts_off,
        plr1_object_offset=p1_obj_off,
        plr2_object_offset=p2_obj_off,
        score=score,
    )


def detect_header(data: bytes) -> LevelHeader:
    candidates = [
        c
        for c in (
            parse_header_ab3d2(data),
            parse_header_ab3d1(data, "be"),
            parse_header_ab3d1(data, "le"),
        )
        if c is not None
    ]
    if not candidates:
        raise ValueError("Could not parse any known header variant")

    best = max(candidates, key=lambda c: c.score)
    if best.score < 8:
        raise ValueError(f"Header detection failed (best score={best.score})")
    return best


def detect_header_with_graph(data: bytes, graph: bytes, graph_endian: str) -> LevelHeader:
    candidates = [
        c
        for c in (
            parse_header_ab3d2(data),
            parse_header_ab3d1(data, "be"),
            parse_header_ab3d1(data, "le"),
        )
        if c is not None
    ]
    if not candidates:
        raise ValueError("Could not parse any known header variant")

    zone_graph_off = graph_u32(graph, 12, graph_endian)
    max_slots = 0
    if 16 < zone_graph_off <= len(graph):
        max_slots = max(0, (zone_graph_off - 16) // 4)

    def score(c: LevelHeader) -> float:
        s = float(c.score)
        if max_slots > 0:
            ratio = min(c.num_zones, max_slots) / float(max(c.num_zones, max_slots))
            s += 10.0 * ratio
            if abs(c.num_zones - max_slots) <= 2:
                s += 3.0
            if c.num_zones < max_slots * 0.25:
                s -= 6.0
        return s

    best = max(candidates, key=score)
    if best.score < 8:
        raise ValueError(f"Header detection failed (best score={best.score})")
    return best


def choose_graph_endianness(graph: bytes) -> str:
    def ok(vals: Tuple[int, int, int, int]) -> bool:
        door, lift, switch, zone_graph = vals
        n = len(graph)
        if not (0 <= zone_graph <= n):
            return False
        # door/lift/switch can be absent, but if present should be in range
        for off in (door, lift, switch):
            if off < 0 or off > n:
                return False
        return True

    be = (
        be_u32(graph, 0),
        be_u32(graph, 4),
        be_u32(graph, 8),
        be_u32(graph, 12),
    )
    if ok(be):
        return "be"

    le = (
        le_u32(graph, 0),
        le_u32(graph, 4),
        le_u32(graph, 8),
        le_u32(graph, 12),
    )
    if ok(le):
        return "le"

    return "be"


def graph_u32(graph: bytes, off: int, endian: str) -> int:
    return be_u32(graph, off) if endian == "be" else le_u32(graph, off)


def graph_s32(graph: bytes, off: int, endian: str) -> int:
    return be_s32(graph, off) if endian == "be" else le_s32(graph, off)


def graph_u16(graph: bytes, off: int, endian: str) -> int:
    return be_u16(graph, off) if endian == "be" else le_u16(graph, off)


def graph_s16(graph: bytes, off: int, endian: str) -> int:
    return be_s16(graph, off) if endian == "be" else le_s16(graph, off)


def parse_points(data: bytes, header: LevelHeader) -> List[Tuple[int, int]]:
    points = []
    off = header.points_offset
    count = header.num_points
    if count <= 0:
        return points

    if off < 0 or off + count * 4 > len(data):
        raise ValueError("Points table out of range")

    for i in range(count):
        x, z = struct.unpack_from(">hh", data, off + i * 4)
        points.append((x, z))
    return points


def parse_edges_with_size(data: bytes, edge_start: int, edge_end: int, edge_size: int) -> List[Edge]:
    start = edge_start
    end = edge_end
    if start < 0 or end <= start or end > len(data):
        return []

    region = end - start
    count = region // edge_size
    edges: List[Edge] = []

    for i in range(count):
        off = start + i * edge_size
        if edge_size == 16:
            x, z, dx, dz, join_zone, _word5, _b12, _b13, flags = struct.unpack_from(">hhhhhhbbH", data, off
            )
        elif edge_size == 8:
            x, z, dx, dz = struct.unpack_from(">hhhh", data, off)
            join_zone = -1
            flags = 0
        else:
            # Older variants may have larger floorline entries; first fields are compatible.
            x, z, dx, dz = struct.unpack_from(">hhhh", data, off)
            join_zone = be_s16(data, off + 8)
            flags = 0

        edges.append(Edge(x=x, z=z, dx=dx, dz=dz, join_zone=join_zone, flags=flags))

    return edges


def parse_zone_edge_ids(data: bytes, zone_off: int, edge_list_offset: int) -> List[int]:
    if edge_list_offset == 0:
        return []

    # Stored as a signed relative offset from zone base.
    start = zone_off + edge_list_offset
    if start < 0 or start >= len(data):
        return []

    edge_ids: List[int] = []
    off = start
    for _ in range(2048):
        if off + 2 > len(data):
            break
        idx = be_s16(data, off)
        off += 2
        if idx < 0:
            break
        edge_ids.append(idx)

    return edge_ids


def parse_zones(data: bytes, zone_offsets: Sequence[int]) -> List[Zone]:
    zones: List[Zone] = []
    for zoff in zone_offsets:
        if zoff < 0 or zoff + 48 > len(data):
            continue

        zone_id = be_s16(data, zoff + 0)
        floor = be_s32(data, zoff + 2)
        roof = be_s32(data, zoff + 6)
        upper_floor = be_s32(data, zoff + 10)
        upper_roof = be_s32(data, zoff + 14)
        edge_list_offset = be_s16(data, zoff + 32)

        edge_ids = parse_zone_edge_ids(data, zoff, edge_list_offset)
        zones.append(
            Zone(
                zone_id=zone_id,
                floor=floor,
                roof=roof,
                upper_floor=upper_floor,
                upper_roof=upper_roof,
                edge_ids=edge_ids,
            )
        )

    return zones


def evaluate_edge_fit(zones: Sequence[Zone], edge_count: int) -> float:
    total = 0
    valid = 0
    for z in zones:
        for idx in z.edge_ids:
            total += 1
            if 0 <= idx < edge_count:
                valid += 1
    if total == 0:
        return 0.0
    return valid / float(total)


def unique_polygon(points: Sequence[Tuple[float, float]]) -> List[Tuple[float, float]]:
    out: List[Tuple[float, float]] = []
    for p in points:
        if not out or (abs(out[-1][0] - p[0]) > 1e-6 or abs(out[-1][1] - p[1]) > 1e-6):
            out.append(p)

    if len(out) > 1 and abs(out[0][0] - out[-1][0]) < 1e-6 and abs(out[0][1] - out[-1][1]) < 1e-6:
        out.pop()

    # Remove colinear points
    changed = True
    while changed and len(out) >= 3:
        changed = False
        n = len(out)
        for i in range(n):
            a = out[(i - 1) % n]
            b = out[i]
            c = out[(i + 1) % n]
            area2 = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
            if abs(area2) < 1e-6:
                del out[i]
                changed = True
                break

    return out


def polygon_area(poly: Sequence[Tuple[float, float]]) -> float:
    a = 0.0
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % n]
        a += x1 * y2 - x2 * y1
    return 0.5 * a


def pick_non_colinear(poly: Sequence[Tuple[float, float]]) -> Tuple[int, int, int]:
    n = len(poly)
    if n < 3:
        raise ValueError("Polygon has fewer than 3 points")

    for i in range(n - 2):
        p0 = poly[i]
        for j in range(i + 1, n - 1):
            p1 = poly[j]
            for k in range(j + 1, n):
                p2 = poly[k]
                area2 = (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p1[1] - p0[1]) * (p2[0] - p0[0])
                if abs(area2) > 1e-6:
                    return i, j, k
    raise ValueError("Polygon points are colinear")


def fmt_vec(p: Tuple[float, float, float]) -> str:
    return f"( {p[0]:.3f} {p[1]:.3f} {p[2]:.3f} )"


def make_face(
    p1: Tuple[float, float, float],
    p2: Tuple[float, float, float],
    p3: Tuple[float, float, float],
    texture: str,
    map_format: str,
) -> str:
    # Quake map plane winding is opposite of the initial AB3D-facing winding.
    # Swap p2/p3 so normals point the way TrenchBroom/qbsp expects.
    points = f"{fmt_vec(p1)} {fmt_vec(p3)} {fmt_vec(p2)}"
    if map_format == "quake2":
        return f"{points} {texture} 0 0 0 1 1 0 0 0"
    return f"{points} {texture} 0 0 0 1 1"


def brush_from_prism(
    poly: Sequence[Tuple[float, float]],
    z0: float,
    z1: float,
    texture: str,
    map_format: str,
    top_texture: Optional[str] = None,
    bottom_texture: Optional[str] = None,
    side_texture: Optional[str] = None,
) -> List[str]:
    if len(poly) < 3:
        return []

    top_texture = top_texture or texture
    bottom_texture = bottom_texture or texture
    side_texture = side_texture or texture

    low = min(z0, z1)
    high = max(z0, z1)

    area = polygon_area(poly)
    clockwise = area < 0.0

    i0, i1, i2 = pick_non_colinear(poly)

    faces: List[str] = []

    # Bottom and top
    if clockwise:
        b = (
            (poly[i0][0], poly[i0][1], low),
            (poly[i1][0], poly[i1][1], low),
            (poly[i2][0], poly[i2][1], low),
        )
        t = (
            (poly[i2][0], poly[i2][1], high),
            (poly[i1][0], poly[i1][1], high),
            (poly[i0][0], poly[i0][1], high),
        )
    else:
        b = (
            (poly[i2][0], poly[i2][1], low),
            (poly[i1][0], poly[i1][1], low),
            (poly[i0][0], poly[i0][1], low),
        )
        t = (
            (poly[i0][0], poly[i0][1], high),
            (poly[i1][0], poly[i1][1], high),
            (poly[i2][0], poly[i2][1], high),
        )

    faces.append(make_face(*b, texture=bottom_texture, map_format=map_format))
    faces.append(make_face(*t, texture=top_texture, map_format=map_format))

    # Side faces
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % n]

        if clockwise:
            p1 = (x1, y1, low)
            p2 = (x1, y1, high)
            p3 = (x2, y2, high)
        else:
            p1 = (x1, y1, low)
            p2 = (x2, y2, high)
            p3 = (x1, y1, high)

        faces.append(make_face(p1, p2, p3, texture=side_texture, map_format=map_format))

    return faces


def to_quake_coords(x: float, z: float, scale_xy: float) -> Tuple[float, float]:
    # AB3D X/Z plan -> Quake X/Y
    return x * scale_xy, z * scale_xy


def to_quake_height(y_raw: int, scale_z: float) -> float:
    # AB3D height is 26.6 fixed in practice and positive toward ground.
    y = y_raw / 64.0
    return -y * scale_z


def zone_polygon_from_edges(zone: Zone, edges: Sequence[Edge], scale_xy: float) -> List[Tuple[float, float]]:
    verts: List[Tuple[float, float]] = []
    for idx in zone.edge_ids:
        if 0 <= idx < len(edges):
            e = edges[idx]
            qx, qy = to_quake_coords(e.x, e.z, scale_xy)
            verts.append((qx, qy))

    return unique_polygon(verts)


def zone_room_spans(zone: Zone, scale_z: float) -> List[Tuple[float, float]]:
    spans: List[Tuple[float, float]] = []

    def add_span(floor_raw: int, roof_raw: int) -> None:
        if floor_raw == roof_raw:
            return
        z_floor = to_quake_height(floor_raw, scale_z)
        z_roof = to_quake_height(roof_raw, scale_z)
        low = min(z_floor, z_roof)
        high = max(z_floor, z_roof)
        if high - low >= 0.01:
            spans.append((low, high))

    add_span(zone.floor, zone.roof)
    if not (zone.upper_floor == 0 and zone.upper_roof == 0):
        if not (zone.upper_floor == zone.floor and zone.upper_roof == zone.roof):
            add_span(zone.upper_floor, zone.upper_roof)

    spans.sort()
    return spans


def subtract_spans(
    span: Tuple[float, float],
    blockers: Sequence[Tuple[float, float]],
    eps: float = 0.01,
) -> List[Tuple[float, float]]:
    remaining = [span]
    for block_lo, block_hi in blockers:
        next_remaining: List[Tuple[float, float]] = []
        for lo, hi in remaining:
            cut_lo = max(lo, block_lo)
            cut_hi = min(hi, block_hi)
            if cut_hi <= cut_lo + eps:
                next_remaining.append((lo, hi))
                continue
            if cut_lo > lo + eps:
                next_remaining.append((lo, cut_lo))
            if hi > cut_hi + eps:
                next_remaining.append((cut_hi, hi))
        remaining = next_remaining
        if not remaining:
            break
    return remaining


def centroid(poly: Sequence[Tuple[float, float]]) -> Tuple[float, float]:
    if not poly:
        return (0.0, 0.0)
    return (sum(p[0] for p in poly) / len(poly), sum(p[1] for p in poly) / len(poly))


def wall_poly_for_segment(
    p1: Tuple[float, float],
    p2: Tuple[float, float],
    zone_centroid: Tuple[float, float],
    thickness: float,
) -> List[Tuple[float, float]]:
    dx = p2[0] - p1[0]
    dy = p2[1] - p1[1]
    length = math.hypot(dx, dy)
    if length < 0.01:
        return []

    nx = dy / length
    ny = -dx / length
    mid = ((p1[0] + p2[0]) * 0.5, (p1[1] + p2[1]) * 0.5)
    to_centroid = (zone_centroid[0] - mid[0], zone_centroid[1] - mid[1])
    if nx * to_centroid[0] + ny * to_centroid[1] > 0.0:
        nx = -nx
        ny = -ny

    return [
        p1,
        p2,
        (p2[0] + nx * thickness, p2[1] + ny * thickness),
        (p1[0] + nx * thickness, p1[1] + ny * thickness),
    ]


def zone_edge_segments(
    zone: Zone,
    edges: Sequence[Edge],
    scale_xy: float,
) -> List[Tuple[Edge, Tuple[float, float], Tuple[float, float]]]:
    segments = []
    for idx in zone.edge_ids:
        if not (0 <= idx < len(edges)):
            continue
        edge = edges[idx]
        p1 = to_quake_coords(edge.x, edge.z, scale_xy)
        p2 = to_quake_coords(edge.x + edge.dx, edge.z + edge.dz, scale_xy)
        if math.hypot(p2[0] - p1[0], p2[1] - p1[1]) >= 0.01:
            segments.append((edge, p1, p2))
    return segments


def raw_segment_key(p1: Tuple[int, int], p2: Tuple[int, int]) -> SegmentKey:
    return (p1, p2) if p1 <= p2 else (p2, p1)


def edge_segment_key(edge: Edge) -> SegmentKey:
    return raw_segment_key((edge.x, edge.z), (edge.x + edge.dx, edge.z + edge.dz))


def parse_graph_wall_textures(
    graph: bytes,
    header: LevelHeader,
    endian: str,
    points: Sequence[Tuple[int, int]],
    slot_materials: Mapping[int, str],
    fallback_material: str,
    scale_z: float,
) -> Dict[int, Dict[SegmentKey, List[WallTextureSpan]]]:
    zone_graph_adds_off = graph_u32(graph, 12, endian)
    table_end = zone_graph_adds_off + max(0, header.num_zones) * 8
    if not (0 <= zone_graph_adds_off <= table_end <= len(graph)):
        return {}

    wall_textures: Dict[int, Dict[SegmentKey, List[WallTextureSpan]]] = {}

    def parse_stream(start: int) -> None:
        if not (0 < start + 2 <= len(graph)):
            return

        off = start
        zone_id = graph_s16(graph, off, endian)
        off += 2
        if zone_id < 0:
            return

        zone_map = wall_textures.setdefault(zone_id, {})
        for _ in range(10000):
            if off + 2 > len(graph):
                break

            command_word = graph_u16(graph, off, endian)
            command_type = command_word & 0xFF
            if command_type >= 0x80:
                break

            if command_type in (0, 13):
                if off + 30 > len(graph):
                    break
                left = graph_s16(graph, off + 2, endian)
                right = graph_s16(graph, off + 4, endian)
                texture_id = graph_s16(graph, off + 14, endian)
                top = graph_s32(graph, off + 20, endian)
                bottom = graph_s32(graph, off + 24, endian)
                if 0 <= left < len(points) and 0 <= right < len(points):
                    z0 = to_quake_height(top, scale_z)
                    z1 = to_quake_height(bottom, scale_z)
                    low = min(z0, z1)
                    high = max(z0, z1)
                    material = slot_materials.get(texture_id, fallback_material)
                    key = raw_segment_key(points[left], points[right])
                    zone_map.setdefault(key, []).append(
                        WallTextureSpan(
                            texture_id=texture_id,
                            material=material,
                            low=low,
                            high=high,
                        )
                    )
                off += 30
                continue

            if command_type in (1, 2, 7, 8, 9, 10, 11):
                if off + 6 > len(graph):
                    break
                sides_minus_one = graph_s16(graph, off + 4, endian)
                if sides_minus_one < 0 or sides_minus_one > 4096:
                    break
                off += 16 + sides_minus_one * 2
                continue

            if command_type == 4:
                off += 4
                continue

            # Clip/backdrop/unused short records do not carry texture data.
            off += 2

    for zone_index in range(header.num_zones):
        entry = zone_graph_adds_off + zone_index * 8
        lower = graph_u32(graph, entry, endian)
        upper = graph_u32(graph, entry + 4, endian)
        parse_stream(lower)
        parse_stream(upper)

    return wall_textures


def parse_graph_flat_textures(
    graph: bytes,
    header: LevelHeader,
    endian: str,
    points: Sequence[Tuple[int, int]],
    scale_xy: float,
    scale_z: float,
) -> Dict[int, List[FlatTextureSpan]]:
    zone_graph_adds_off = graph_u32(graph, 12, endian)
    table_end = zone_graph_adds_off + max(0, header.num_zones) * 8
    if not (0 <= zone_graph_adds_off <= table_end <= len(graph)):
        return {}

    flat_textures: Dict[int, List[FlatTextureSpan]] = {}

    def parse_stream(start: int) -> None:
        if not (0 < start + 2 <= len(graph)):
            return

        off = start
        zone_id = graph_s16(graph, off, endian)
        off += 2
        if zone_id < 0:
            return

        zone_flats = flat_textures.setdefault(zone_id, [])
        for _ in range(10000):
            if off + 2 > len(graph):
                break

            command_word = graph_u16(graph, off, endian)
            command_type = command_word & 0xFF
            if command_type >= 0x80:
                break

            if command_type in (0, 13):
                off += 30
                continue

            if command_type in (1, 2, 7, 8, 9, 10, 11):
                if off + 6 > len(graph):
                    break
                y_word = graph_s16(graph, off + 2, endian)
                sides_minus_one = graph_s16(graph, off + 4, endian)
                if sides_minus_one < 0 or sides_minus_one > 4096:
                    break
                sides = sides_minus_one + 1
                poly: List[Tuple[float, float]] = []
                for i in range(sides):
                    point_off = off + 6 + i * 2
                    if point_off + 2 > len(graph):
                        break
                    point_index = graph_u16(graph, point_off, endian) & 0x0FFF
                    if 0 <= point_index < len(points):
                        poly.append(to_quake_coords(points[point_index][0], points[point_index][1], scale_xy))
                poly = unique_polygon(poly)
                tile_off = off + 2 * (sides_minus_one + 6)
                if tile_off + 2 <= len(graph):
                    tile_offset = graph_u16(graph, tile_off, endian)
                    zone_flats.append(
                        FlatTextureSpan(
                            command_type=command_type,
                            material=ab3d2_floor_material(tile_offset),
                            z=to_quake_height(y_word * 64, scale_z),
                            poly=poly if len(poly) >= 3 else [],
                        )
                    )
                off += 16 + sides_minus_one * 2
                continue

            if command_type == 4:
                off += 4
                continue

            off += 2

    for zone_index in range(header.num_zones):
        entry = zone_graph_adds_off + zone_index * 8
        lower = graph_u32(graph, entry, endian)
        upper = graph_u32(graph, entry + 4, endian)
        parse_stream(lower)
        parse_stream(upper)

    return flat_textures


def pick_wall_texture(
    zone_textures: Mapping[SegmentKey, Sequence[WallTextureSpan]],
    edge: Edge,
    low: float,
    high: float,
    fallback: str,
) -> str:
    spans = zone_textures.get(edge_segment_key(edge))
    if not spans:
        return fallback

    best_texture = fallback
    best_overlap = 0.0
    for span in spans:
        overlap = min(high, span.high) - max(low, span.low)
        if overlap > best_overlap:
            best_overlap = overlap
            best_texture = span.material

    if best_overlap > 0.01:
        return best_texture
    return spans[0].material if spans else fallback


def pick_flat_texture(
    flat_textures: Sequence[FlatTextureSpan],
    command_type: int,
    z: float,
    fallback: str,
) -> str:
    best = None
    best_delta = 1 << 30
    for span in flat_textures:
        if span.command_type != command_type:
            continue
        delta = abs(span.z - z)
        if delta < best_delta:
            best = span
            best_delta = delta

    if best is not None and best_delta <= 0.05:
        return best.material
    return fallback


def pick_flat_texture_from_sets(
    flat_texture_sets: Sequence[Sequence[FlatTextureSpan]],
    command_types: Sequence[int],
    z: float,
    fallback: str,
) -> str:
    best = None
    best_delta = 1 << 30
    for command_type in command_types:
        for flat_textures in flat_texture_sets:
            for span in flat_textures:
                if span.command_type != command_type:
                    continue
                delta = abs(span.z - z)
                if delta < best_delta:
                    best = span
                    best_delta = delta
        if best is not None and best_delta <= 0.05:
            return best.material

    return fallback


def flat_cap_polygons(
    flat_textures: Sequence[FlatTextureSpan],
    command_type: int,
    z: float,
    fallback_poly: Sequence[Tuple[float, float]],
    fallback_material: str,
) -> List[Tuple[List[Tuple[float, float]], str]]:
    matches = [
        span
        for span in flat_textures
        if span.command_type == command_type and abs(span.z - z) <= 0.05
    ]
    caps = [(span.poly, span.material) for span in matches if len(span.poly) >= 3]
    if caps:
        return [(list(poly), material) for poly, material in caps]

    material = pick_flat_texture(flat_textures, command_type, z, fallback_material)
    return [(list(fallback_poly), material)]


def zone_volume_brushes(
    zone: Zone,
    poly: Sequence[Tuple[float, float]],
    scale_z: float,
    texture: str,
    map_format: str,
) -> List[List[str]]:
    brushes: List[List[str]] = []

    if len(poly) < 3:
        return brushes

    for z0, z1 in zone_room_spans(zone, scale_z):
        faces = brush_from_prism(poly, z0, z1, texture, map_format)
        if faces:
            brushes.append(faces)

    return brushes


def zone_shell_brushes(
    zone: Zone,
    zones_by_id: Mapping[int, Zone],
    poly: Sequence[Tuple[float, float]],
    edges: Sequence[Edge],
    scale_xy: float,
    scale_z: float,
    wall_texture: str,
    floor_texture: str,
    ceiling_texture: str,
    wall_textures_by_zone: Mapping[int, Mapping[SegmentKey, Sequence[WallTextureSpan]]],
    flat_textures_by_zone: Mapping[int, Sequence[FlatTextureSpan]],
    map_format: str,
    thickness: float,
    cap_thickness: float,
) -> List[List[str]]:
    brushes: List[List[str]] = []
    if len(poly) < 3:
        return brushes

    spans = zone_room_spans(zone, scale_z)
    if not spans:
        return brushes

    zone_textures = wall_textures_by_zone.get(zone.zone_id, {})
    zone_flat_textures = flat_textures_by_zone.get(zone.zone_id, [])

    for low, high in spans:
        for cap_poly, floor_material in flat_cap_polygons(zone_flat_textures, 1, low, poly, floor_texture):
            floor_faces = brush_from_prism(
                cap_poly,
                low - cap_thickness,
                low,
                floor_material,
                map_format,
                top_texture=floor_material,
                bottom_texture=floor_material,
                side_texture=wall_texture,
            )
            if floor_faces:
                brushes.append(floor_faces)
        for cap_poly, ceiling_material in flat_cap_polygons(zone_flat_textures, 2, high, poly, ceiling_texture):
            ceiling_faces = brush_from_prism(
                cap_poly,
                high,
                high + cap_thickness,
                ceiling_material,
                map_format,
                top_texture=ceiling_material,
                bottom_texture=ceiling_material,
                side_texture=wall_texture,
            )
            if ceiling_faces:
                brushes.append(ceiling_faces)

    zone_ctr = centroid(poly)
    for edge, p1, p2 in zone_edge_segments(zone, edges, scale_xy):
        neighbour_spans: Sequence[Tuple[float, float]] = []
        neighbour_flat_textures: Sequence[FlatTextureSpan] = []
        neighbour = zones_by_id.get(edge.join_zone)
        if neighbour is not None and neighbour.zone_id != zone.zone_id:
            neighbour_spans = zone_room_spans(neighbour, scale_z)
            neighbour_flat_textures = flat_textures_by_zone.get(neighbour.zone_id, [])

        for span in spans:
            wall_spans = subtract_spans(span, neighbour_spans) if neighbour_spans else [span]
            for low, high in wall_spans:
                if high - low < 0.01:
                    continue
                wall_poly = wall_poly_for_segment(p1, p2, zone_ctr, thickness)
                if not wall_poly:
                    continue
                material = pick_wall_texture(zone_textures, edge, low, high, wall_texture)
                flat_texture_sets = (zone_flat_textures, neighbour_flat_textures)
                top_material = pick_flat_texture_from_sets(flat_texture_sets, (1, 2), high, material)
                bottom_material = pick_flat_texture_from_sets(flat_texture_sets, (2, 1), low, material)
                wall_faces = brush_from_prism(
                    wall_poly,
                    low,
                    high,
                    material,
                    map_format,
                    top_texture=top_material,
                    bottom_texture=bottom_material,
                    side_texture=material,
                )
                if wall_faces:
                    brushes.append(wall_faces)

    return brushes


def map_entity(kv: Sequence[Tuple[str, str]], brushes: Sequence[Sequence[str]]) -> str:
    lines = ["{"]
    for k, v in kv:
        lines.append(f'"{k}" "{v}"')

    for b in brushes:
        lines.append("{")
        lines.extend(b)
        lines.append("}")

    lines.append("}")
    return "\n".join(lines)


def write_quake_map(
    out_path: pathlib.Path,
    level_name: str,
    header: LevelHeader,
    zones: Sequence[Zone],
    edges: Sequence[Edge],
    wad: str,
    texture: str,
    floor_texture: str,
    ceiling_texture: str,
    scale_xy: float,
    scale_z: float,
    spawn_height: float,
    map_format: str,
    solid_mode: str,
    solid_thickness: float,
    cap_thickness: float,
    wall_textures_by_zone: Mapping[int, Mapping[SegmentKey, Sequence[WallTextureSpan]]],
    flat_textures_by_zone: Mapping[int, Sequence[FlatTextureSpan]],
) -> Tuple[int, int]:
    brushes: List[List[str]] = []
    skipped = 0
    zones_by_id = {zone.zone_id: zone for zone in zones}

    for zone in zones:
        poly = zone_polygon_from_edges(zone, edges, scale_xy)
        if solid_mode == "volumes":
            z_brushes = zone_volume_brushes(zone, poly, scale_z, texture, map_format)
        else:
            z_brushes = zone_shell_brushes(
                zone,
                zones_by_id,
                poly,
                edges,
                scale_xy,
                scale_z,
                wall_texture=texture,
                floor_texture=floor_texture,
                ceiling_texture=ceiling_texture,
                wall_textures_by_zone=wall_textures_by_zone,
                flat_textures_by_zone=flat_textures_by_zone,
                map_format=map_format,
                thickness=solid_thickness,
                cap_thickness=cap_thickness,
            )
        if not z_brushes:
            skipped += 1
            continue
        brushes.extend(z_brushes)

    worldspawn_kv = [
        ("classname", "worldspawn"),
        ("message", f"Converted from {level_name}"),
    ]
    if map_format != "quake2" and wad:
        worldspawn_kv.append(("wad", wad))

    worldspawn = map_entity(
        worldspawn_kv,
        brushes,
    )

    spawn_x, spawn_y = to_quake_coords(header.plr1_x, header.plr1_z, scale_xy)
    # If we can find the spawn zone, place above that zone floor.
    spawn_z = spawn_height
    for zone in zones:
        if zone.zone_id == header.plr1_zone:
            spawn_z = to_quake_height(zone.floor, scale_z) + spawn_height
            break

    player = map_entity(
        [
            ("classname", "info_player_start"),
            ("origin", f"{spawn_x:.3f} {spawn_y:.3f} {spawn_z:.3f}"),
            ("angle", "0"),
        ],
        [],
    )

    header_lines = []
    if map_format == "quake2":
        header_lines = ["// Game: Quake 2", "// Format: Quake2", ""]

    text = "\n".join(header_lines) + worldspawn + "\n\n" + player + "\n"
    out_path.write_text(text, encoding="ascii", errors="strict")
    return len(brushes), skipped


def maybe_compile_bsp(map_path: pathlib.Path, qbsp: Optional[str]) -> Tuple[bool, str]:
    candidates = [qbsp] if qbsp else ["qbsp3", "q2qbsp", "qbsp", "ericw-qbsp", "txqbsp"]

    exe = None
    for c in candidates:
        if not c:
            continue
        found = shutil.which(c)
        if found:
            exe = found
            break
        if pathlib.Path(c).exists():
            exe = c
            break

    if not exe:
        return False, "No qbsp executable found"

    try:
        proc = subprocess.run([exe, str(map_path)], capture_output=True, text=True, check=False)
    except OSError as exc:
        return False, f"Failed to run qbsp: {exc}"

    if proc.returncode != 0:
        msg = (proc.stdout + "\n" + proc.stderr).strip()
        return False, f"qbsp failed (code {proc.returncode}): {msg}"

    return True, exe


def smoke_test_trenchbroom(map_path: pathlib.Path, trenchbroom: pathlib.Path, wait_seconds: float) -> Tuple[bool, str]:
    exe = trenchbroom_executable(trenchbroom)

    if not exe.exists():
        return False, f"TrenchBroom executable not found: {exe}"
    if not map_path.exists():
        return False, f"Map file not found: {map_path}"

    try:
        proc = subprocess.Popen(
            [str(exe), str(map_path.resolve())],
            cwd=str(exe.parent),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except OSError as exc:
        return False, f"Failed to launch TrenchBroom: {exc}"

    deadline = time.monotonic() + wait_seconds
    while time.monotonic() < deadline:
        code = proc.poll()
        if code is not None:
            return False, f"TrenchBroom exited early with code {code}"
        time.sleep(0.1)

    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)

    return True, f"TrenchBroom opened {map_path} for {wait_seconds:g}s"


def load_level_pair(bin_path: pathlib.Path, graph_path: pathlib.Path) -> Tuple[bytes, bytes]:
    raw_bin = bin_path.read_bytes()
    raw_graph = graph_path.read_bytes()

    data = sb_decompress(raw_bin)
    graph = sb_decompress(raw_graph)
    return data, graph


def find_level_dirs(levels_root: pathlib.Path) -> List[pathlib.Path]:
    found = []
    for p in levels_root.rglob("twolev.bin"):
        d = p.parent
        if (d / "twolev.graph.bin").exists():
            found.append(d)

    found.sort()
    return found


def parse_zone_offsets(graph: bytes, header: LevelHeader, endian: str) -> List[int]:
    zone_graph_off = graph_u32(graph, 12, endian)

    table_start = 16
    if not (table_start <= zone_graph_off <= len(graph)):
        # fallback: assume exactly num_zones pointers
        zone_graph_off = min(len(graph), table_start + max(0, header.num_zones) * 4)

    max_slots = max(0, (zone_graph_off - table_start) // 4)
    if max_slots == 0:
        return []

    count = min(header.num_zones, max_slots) if header.num_zones > 0 else max_slots
    offs = [graph_u32(graph, table_start + i * 4, endian) for i in range(count)]
    return offs


def convert_one_level(
    level_dir: pathlib.Path,
    out_dir: pathlib.Path,
    wad: str,
    texture: str,
    floor_texture: str,
    ceiling_texture: str,
    scale_xy: float,
    scale_z: float,
    spawn_height: float,
    map_format: str,
    solid_mode: str,
    solid_thickness: float,
    cap_thickness: float,
    compile_bsp: bool,
    qbsp: Optional[str],
    verbose: bool,
) -> Tuple[bool, str]:
    bin_path = level_dir / "twolev.bin"
    graph_path = level_dir / "twolev.graph.bin"

    try:
        data, graph = load_level_pair(bin_path, graph_path)
    except Exception as exc:
        return False, f"{level_dir.name}: failed to load/decompress ({exc})"

    endian = choose_graph_endianness(graph)

    try:
        header = detect_header_with_graph(data, graph, endian)
    except Exception as exc:
        return False, f"{level_dir.name}: header detection failed ({exc})"

    zone_offsets = parse_zone_offsets(graph, header, endian)
    if not zone_offsets:
        return False, f"{level_dir.name}: could not read zone offset table"

    zones = parse_zones(data, zone_offsets)
    if not zones:
        return False, f"{level_dir.name}: no zones parsed"

    edge_start = header.floorline_offset
    edge_end_candidates = []
    for off in (
        header.object_data_offset,
        header.points_offset,
        header.shot_data_offset,
        header.alien_shot_data_offset,
        min(zone_offsets) if zone_offsets else 0,
    ):
        if edge_start < off <= len(data):
            edge_end_candidates.append(off)

    if edge_end_candidates:
        edge_end = min(edge_end_candidates)
    else:
        edge_end = len(data)

    # Pick edge size by how many zone edge references are in range.
    edge_candidates = []
    for size in (16, 8, 32):
        parsed = parse_edges_with_size(data, edge_start, edge_end, size)
        fit = evaluate_edge_fit(zones, len(parsed)) if parsed else 0.0
        edge_candidates.append((fit, size, parsed))

    edge_candidates.sort(key=lambda x: x[0], reverse=True)
    best_fit, edge_size, edges = edge_candidates[0]
    fit16 = next((f for f, s, _ in edge_candidates if s == 16), 0.0)
    fit8 = next((f for f, s, _ in edge_candidates if s == 8), 0.0)
    fit32 = next((f for f, s, _ in edge_candidates if s == 32), 0.0)

    if not edges:
        return False, f"{level_dir.name}: no edges parsed"

    try:
        points = parse_points(data, header)
    except Exception:
        points = []

    wall_textures_by_zone = parse_graph_wall_textures(
        graph,
        header,
        endian,
        points,
        ab3d2_wall_slot_materials(),
        texture,
        scale_z,
    )
    graph_wall_count = sum(len(spans) for zone_map in wall_textures_by_zone.values() for spans in zone_map.values())
    flat_textures_by_zone = parse_graph_flat_textures(graph, header, endian, points, scale_xy, scale_z)
    graph_flat_count = sum(len(spans) for spans in flat_textures_by_zone.values())

    out_dir.mkdir(parents=True, exist_ok=True)
    map_path = out_dir / f"{level_dir.name.lower()}.map"

    try:
        brush_count, skipped = write_quake_map(
            map_path,
            level_dir.name,
            header,
            zones,
            edges,
            wad,
            texture,
            floor_texture,
            ceiling_texture,
            scale_xy,
            scale_z,
            spawn_height,
            map_format,
            solid_mode,
            solid_thickness,
            cap_thickness,
            wall_textures_by_zone,
            flat_textures_by_zone,
        )
    except Exception as exc:
        return False, f"{level_dir.name}: map write failed ({exc})"

    bsp_note = ""
    if compile_bsp:
        ok, msg = maybe_compile_bsp(map_path, qbsp)
        if not ok:
            bsp_note = f"; bsp: {msg}"
        else:
            bsp_note = f"; bsp: compiled via {msg}"

    if verbose:
        return (
            True,
            f"{level_dir.name}: ok variant={header.variant} zones={len(zones)} edges={len(edges)}"
            f" edge_size={edge_size} fit16={fit16:.2f} fit8={fit8:.2f} fit32={fit32:.2f}"
            f" edge_range=[{edge_start},{edge_end})"
            f" graph_walls={graph_wall_count} graph_flats={graph_flat_count}"
            f" brushes={brush_count} skipped_zones={skipped} -> {map_path}{bsp_note}",
        )

    return True, f"{level_dir.name}: ok -> {map_path}{bsp_note}"


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Convert AB3D levels to Quake map/BSP")
    parser.add_argument(
        "--levels-root",
        type=pathlib.Path,
        default=pathlib.Path("media/demolevels"),
        help="Root directory to search for level folders containing twolev.bin and twolev.graph.bin",
    )
    parser.add_argument(
        "--out-dir",
        type=pathlib.Path,
        default=pathlib.Path("build/quake2_maps"),
        help="Output directory for generated .map files",
    )
    parser.add_argument(
        "--texture",
        default=AB3D2_DEFAULT_TEXTURE,
        help="Texture/material name used for wall brushes",
    )
    parser.add_argument(
        "--floor-texture",
        default=AB3D2_DEFAULT_FLOOR_TEXTURE,
        help="Fallback texture/material name used for floor slab brushes",
    )
    parser.add_argument(
        "--ceiling-texture",
        default=AB3D2_DEFAULT_CEILING_TEXTURE,
        help="Fallback texture/material name used for ceiling slab brushes",
    )
    parser.add_argument(
        "--map-format",
        choices=("quake2", "quake1"),
        default="quake2",
        help="Map face format to write",
    )
    parser.add_argument(
        "--solid-mode",
        choices=("shell", "volumes"),
        default="shell",
        help="shell writes Quake solids around empty AB3D zones; volumes preserves the old solid-sector export",
    )
    parser.add_argument(
        "--solid-thickness",
        type=float,
        default=16.0,
        help="Thickness in map units for generated shell wall brushes",
    )
    parser.add_argument(
        "--cap-thickness",
        type=float,
        default=1.0,
        help="Thickness in map units for generated shell floor and ceiling brushes",
    )
    parser.add_argument(
        "--wad",
        default="",
        help="Quake 1 WAD path string written into worldspawn when --map-format quake1 is used",
    )
    parser.add_argument(
        "--extract-textures",
        action="store_true",
        help="Extract AB3D wall WADs into Quake 2 WAL textures and a preview WAD2",
    )
    parser.add_argument(
        "--texture-source",
        type=pathlib.Path,
        default=pathlib.Path("media/wallinc"),
        help="Directory containing AB3D wall texture .wad/.256wad files",
    )
    parser.add_argument(
        "--texture-palette",
        type=pathlib.Path,
        default=pathlib.Path("media/includes/256pal"),
        help="AB3D2 256-colour palette used when decoding .256wad textures",
    )
    parser.add_argument(
        "--floor-source",
        type=pathlib.Path,
        default=pathlib.Path("media/includes/floortile"),
        help="AB3D2 global floor texture atlas",
    )
    parser.add_argument(
        "--floor-remap",
        type=pathlib.Path,
        default=pathlib.Path("media/includes/newtexturemaps.pal"),
        help="AB3D2 floor palette/remap table",
    )
    parser.add_argument(
        "--q2-root",
        type=pathlib.Path,
        default=pathlib.Path("build/quake2_assets"),
        help="Output root that will receive baseq2/textures/<prefix>/*.wal",
    )
    parser.add_argument(
        "--wad-out",
        type=pathlib.Path,
        default=pathlib.Path("build/quake2_assets/ab3d2_textures.wad"),
        help="Output WAD2 preview file generated from AB3D wall textures",
    )
    parser.add_argument("--scale-xy", type=float, default=1.0, help="Scale factor for AB3D X/Z")
    parser.add_argument("--scale-z", type=float, default=1.0, help="Scale factor for AB3D vertical axis")
    parser.add_argument(
        "--spawn-height",
        type=float,
        default=32.0,
        help="Extra height added above spawn zone floor for info_player_start",
    )
    parser.add_argument(
        "--compile-bsp",
        action="store_true",
        help="Run qbsp on each generated .map if a qbsp executable is available",
    )
    parser.add_argument("--qbsp", default=None, help="Optional explicit qbsp executable path")
    parser.add_argument(
        "--check-trenchbroom",
        action="store_true",
        help="Launch TrenchBroom against the first generated map as a startup smoke test",
    )
    parser.add_argument(
        "--install-trenchbroom-assets",
        action="store_true",
        help="Copy generated WAL textures and editor placeholder models into the local TrenchBroom assets directory",
    )
    parser.add_argument(
        "--trenchbroom",
        type=pathlib.Path,
        default=pathlib.Path("TrenchBroom-Win64-AMD64-v2025.4-Release/TrenchBroom.exe"),
        help="TrenchBroom executable or release directory used by --check-trenchbroom",
    )
    parser.add_argument(
        "--trenchbroom-wait",
        type=float,
        default=10.0,
        help="Seconds to wait before closing TrenchBroom during --check-trenchbroom",
    )
    parser.add_argument(
        "--match",
        default=None,
        help="Only process levels whose directory name contains this substring (case-insensitive)",
    )
    parser.add_argument("--verbose", action="store_true", help="Print detailed diagnostics")

    args = parser.parse_args(argv)

    if args.extract_textures:
        count, material_names, texture_prefix = write_texture_assets(
            args.texture_source,
            args.q2_root,
            args.wad_out,
            args.texture_palette,
            args.floor_source,
            args.floor_remap,
        )
        if count:
            print(
                f"[OK]  textures: extracted {count} textures -> "
                f"{args.q2_root / 'baseq2' / 'textures' / texture_prefix}; wad2={args.wad_out}"
            )
            if args.texture == AB3D2_DEFAULT_TEXTURE and args.texture not in material_names:
                args.texture = material_names[0]
        else:
            print(f"[ERR] textures: no usable AB3D wall textures found in {args.texture_source}")

    if not args.levels_root.exists():
        print(f"Levels root does not exist: {args.levels_root}", file=sys.stderr)
        return 2

    level_dirs = find_level_dirs(args.levels_root)
    if args.match:
        m = args.match.lower()
        level_dirs = [d for d in level_dirs if m in d.name.lower()]

    if not level_dirs:
        print("No levels found", file=sys.stderr)
        return 2

    ok_count = 0
    fail_count = 0
    generated_maps: List[pathlib.Path] = []
    for d in level_dirs:
        ok, msg = convert_one_level(
            d,
            args.out_dir,
            wad=args.wad,
            texture=args.texture,
            floor_texture=args.floor_texture,
            ceiling_texture=args.ceiling_texture,
            scale_xy=args.scale_xy,
            scale_z=args.scale_z,
            spawn_height=args.spawn_height,
            map_format=args.map_format,
            solid_mode=args.solid_mode,
            solid_thickness=args.solid_thickness,
            cap_thickness=args.cap_thickness,
            compile_bsp=args.compile_bsp,
            qbsp=args.qbsp,
            verbose=args.verbose,
        )
        if ok:
            ok_count += 1
            generated_maps.append(args.out_dir / f"{d.name.lower()}.map")
            print("[OK]  " + msg)
        else:
            fail_count += 1
            print("[ERR] " + msg)

    if args.install_trenchbroom_assets or args.check_trenchbroom:
        ok, msg = install_trenchbroom_assets(args.q2_root, args.trenchbroom)
        if ok:
            print("[OK]  trenchbroom assets: " + msg)
        else:
            print("[ERR] trenchbroom assets: " + msg)
            fail_count += 1

    if args.check_trenchbroom:
        map_candidates = [p for p in generated_maps if p.exists()]
        if not map_candidates:
            print("[ERR] trenchbroom: no generated map available to check")
            fail_count += 1
        else:
            ok, msg = smoke_test_trenchbroom(map_candidates[0], args.trenchbroom, args.trenchbroom_wait)
            if ok:
                print("[OK]  trenchbroom: " + msg)
            else:
                print("[ERR] trenchbroom: " + msg)
                fail_count += 1

    print(f"Done: {ok_count} succeeded, {fail_count} failed")
    return 1 if fail_count else 0


if __name__ == "__main__":
    raise SystemExit(main())
