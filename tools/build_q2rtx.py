#!/usr/bin/env python3
"""Build, validate, install, and launch the converted AB3D2 Q2RTX maps.

The original AB3D2 level pairs and extracted textures remain the map authority.
PBR channels come from the committed renderer-native material catalog generated
from those assets and the hand-authored sheets; this tool only repacks their
explicit channels into Q2RTX's base-alpha/normal-alpha convention.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import time
import urllib.request
import zipfile
from typing import Iterable, Sequence

from PIL import Image


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[1]
MEDIA_ROOT = PROJECT_ROOT / "amiga" / "media"
MATERIAL_ROOT = PROJECT_ROOT / "assets" / "renderer_dxr" / "materials"
DEFAULT_PACKAGE_ROOT = PROJECT_ROOT / "build" / "q2rtx"
DEFAULT_Q2RTX_ROOTS = (
    pathlib.Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake II RTX"),
    pathlib.Path(r"C:\Program Files\Steam\steamapps\common\Quake II RTX"),
)
LEVEL_NAMES = tuple(f"level_{letter}" for letter in "abcdefghijklmnop")

ERICW_VERSION = "2.0.0-alpha11"
ERICW_WINDOWS_ARCHIVE = f"ericw-tools-{ERICW_VERSION}-win64.zip"
ERICW_WINDOWS_URL = (
    "https://github.com/ericwa/ericw-tools/releases/download/"
    f"{ERICW_VERSION}/{ERICW_WINDOWS_ARCHIVE}"
)
ERICW_WINDOWS_SHA256 = (
    "4e5ea11be2194a1c4acac6d6da9d5b5b9f65324fda2d67efa0731d1fd8e0745f"
)
LEGACY_INSTALL_FILES = {
    pathlib.Path("materials/ab3d2_neon.mat"): (
        "e3502542b69e4acc3559eb07371e00a286d25b9d9e49a40aa29ea75d51759a4d"
    ),
    pathlib.Path("overrides/ab3d2_technolights_light.tga"): (
        "a80e79c5e86e626f9b48a52bb378d82bb55aeebfc51749a40e026028ed6be87d"
    ),
}

Q2_BSP_IDENT = b"IBSP"
Q2_BSP_VERSION = 38
Q2_BSP_LUMP_COUNT = 19
Q2_LUMP_ENTITIES = 0
Q2_LUMP_VERTEXES = 2
Q2_LUMP_FACES = 6
Q2_LUMP_EDGES = 11
Q2_LUMP_SURFEDGES = 12


@dataclass(frozen=True)
class MaterialSource:
    name: str
    channels: dict[str, pathlib.Path]
    emissive_factor: float
    base_factor: float
    specular_factor: float
    normal_strength: float


@dataclass(frozen=True)
class BspStats:
    vertices: int
    edges: int
    surfedges: int
    faces: int


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def project_path(value: pathlib.Path) -> pathlib.Path:
    return value.resolve() if value.is_absolute() else (PROJECT_ROOT / value).resolve()


def material_name(entry: dict[str, object]) -> str | None:
    name = str(entry["name"])
    binding = entry.get("binding")
    kind = binding.get("kind") if isinstance(binding, dict) else None
    if kind == "shared_floor" and name.startswith("floor_"):
        return name
    if kind == "shared_wall":
        match = re.fullmatch(r"wall_\d{2}_(.+)", name)
        if not match:
            raise ValueError(f"shared wall material has an invalid name: {name}")
        return match.group(1)
    if binding is None and name.startswith("wall_archive_"):
        return name.removeprefix("wall_archive_")
    return None


def scalar_emissive_factor(entry: dict[str, object]) -> float:
    values = entry.get("emissive_factor")
    if not isinstance(values, list) or len(values) != 3:
        raise ValueError(f"material {entry['name']} has an invalid emissive factor")
    factors = [float(value) for value in values]
    if not all(value == factors[0] for value in factors):
        raise ValueError(
            f"Q2RTX material {entry['name']} requires a scalar emissive factor"
        )
    return factors[0]


def q2_factors(name: str) -> tuple[float, float]:
    """Return the established Q2RTX compatibility factors for a material."""
    lowered = name.lower()
    if lowered.startswith("floor_") or lowered in {
        "rocky",
        "stonewall",
        "brownstonestep",
    }:
        return 0.15, 1.4
    if any(word in lowered for word in ("metal", "techno", "door", "pipe")):
        return 0.65, 1.3
    return 0.35, 1.35


def load_material_sources(root: pathlib.Path = MATERIAL_ROOT) -> dict[str, MaterialSource]:
    manifest_path = root / "materials.json"
    document = json.loads(manifest_path.read_text(encoding="utf-8"))
    sources: dict[str, MaterialSource] = {}
    for entry in document["materials"]:
        name = material_name(entry)
        if name is None:
            continue
        if name in sources:
            raise ValueError(f"duplicate Q2RTX material source: {name}")
        channel_values = entry.get("channels")
        if not isinstance(channel_values, dict):
            raise ValueError(f"material {entry['name']} has no channel map")
        channels = {
            channel: root / str(channel_values[channel])
            for channel in ("base_color", "normal", "metalness", "roughness", "emissive")
        }
        missing = [str(path) for path in channels.values() if not path.is_file()]
        if missing:
            raise FileNotFoundError(
                f"material {entry['name']} has missing channel files: {', '.join(missing)}"
            )
        specular, base = q2_factors(name)
        sources[name] = MaterialSource(
            name=name,
            channels=channels,
            emissive_factor=scalar_emissive_factor(entry),
            base_factor=base,
            specular_factor=specular,
            normal_strength=float(entry.get("normal_strength", 1.0)),
        )
    return sources


def read_wal_size(path: pathlib.Path) -> tuple[int, int]:
    data = path.read_bytes()
    if len(data) < 100:
        raise ValueError(f"WAL is too small: {path}")
    width, height = struct.unpack_from("<ii", data, 32)
    first_mip = struct.unpack_from("<i", data, 40)[0]
    if width <= 0 or height <= 0 or first_mip < 100:
        raise ValueError(f"WAL has invalid dimensions or first mip: {path}")
    if first_mip + width * height > len(data):
        raise ValueError(f"WAL first mip is out of bounds: {path}")
    return width, height


def crop_to_aspect(image: Image.Image, aspect: float) -> Image.Image:
    width, height = image.size
    current = width / max(height, 1)
    if abs(current - aspect) < 0.001:
        return image
    if current > aspect:
        new_width = max(1, round(height * aspect))
        left = max(0, (width - new_width) // 2)
        return image.crop((left, 0, left + new_width, height))
    new_height = max(1, round(width / aspect))
    top = max(0, (height - new_height) // 2)
    return image.crop((0, top, width, top + new_height))


def load_scaled_channel(path: pathlib.Path, target_size: tuple[int, int]) -> Image.Image:
    with Image.open(path) as source:
        image = source.convert("RGBA")
    aspect = target_size[0] / max(target_size[1], 1)
    return crop_to_aspect(image, aspect).resize(target_size, Image.Resampling.LANCZOS)


def pack_base(albedo: Image.Image, roughness: Image.Image) -> Image.Image:
    red, green, blue, _alpha = albedo.convert("RGBA").split()
    return Image.merge("RGBA", (red, green, blue, roughness.convert("L")))


def pack_normal(normal: Image.Image, metalness: Image.Image) -> Image.Image:
    red, green, blue, _alpha = normal.convert("RGBA").split()
    blue = blue.point(lambda value: max(96, value))
    return Image.merge("RGBA", (red, green, blue, metalness.convert("L")))


def greyscale_rgba(image: Image.Image) -> Image.Image:
    grey = image.convert("L")
    alpha = Image.new("L", image.size, 255)
    return Image.merge("RGBA", (grey, grey, grey, alpha))


def write_tga(path: pathlib.Path, image: Image.Image) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.convert("RGBA").save(path, format="TGA")


def write_material_file(path: pathlib.Path, sources: Iterable[MaterialSource]) -> None:
    lines = [
        "# Generated by tools/build_q2rtx.py from the committed AB3D2 material catalog.",
        "# Base alpha stores roughness; normal alpha stores metalness.",
        "",
    ]
    for source in sorted(sources, key=lambda item: item.name):
        name = source.name
        lines.extend(
            [
                f"ab3d2/{name},",
                f"textures/ab3d2/{name}:",
                f"    texture_base overrides/ab3d2/{name}.tga",
                f"    texture_normals overrides/ab3d2/{name}_n.tga",
            ]
        )
        if source.emissive_factor > 0.0:
            lines.extend(
                [
                    f"    texture_emissive overrides/ab3d2/{name}_light.tga",
                    "    is_light 1",
                    f"    emissive_factor {source.emissive_factor:g}",
                    f"    bsp_radiance {source.emissive_factor:g}",
                ]
            )
        lines.extend(
            [
                "    metalness_factor 1.0",
                f"    specular_factor {source.specular_factor:.2f}",
                f"    base_factor {source.base_factor:.2f}",
                f"    bump_scale {min(source.normal_strength, 0.6):.2f}",
                "",
            ]
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="ascii")


def build_material_package(
    baseq2: pathlib.Path,
    scale: int,
    source_root: pathlib.Path = MATERIAL_ROOT,
) -> dict[str, MaterialSource]:
    if scale < 1:
        raise ValueError("Q2RTX material scale must be at least one")
    wal_dir = baseq2 / "textures" / "ab3d2"
    wal_paths = {path.stem: path for path in sorted(wal_dir.glob("*.wal"))}
    sources = load_material_sources(source_root)
    if set(wal_paths) != set(sources):
        missing_sources = sorted(set(wal_paths) - set(sources))
        missing_wals = sorted(set(sources) - set(wal_paths))
        raise ValueError(
            "converter/material catalog mismatch: "
            f"no source={missing_sources}; no WAL={missing_wals}"
        )

    override_dir = baseq2 / "overrides" / "ab3d2"
    for name, source in sorted(sources.items()):
        wal_width, wal_height = read_wal_size(wal_paths[name])
        target_size = (wal_width * scale, wal_height * scale)
        channels = {
            channel: load_scaled_channel(path, target_size)
            for channel, path in source.channels.items()
        }
        write_tga(
            override_dir / f"{name}.tga",
            pack_base(channels["base_color"], channels["roughness"]),
        )
        write_tga(
            override_dir / f"{name}_n.tga",
            pack_normal(channels["normal"], channels["metalness"]),
        )
        write_tga(override_dir / f"{name}_r.tga", greyscale_rgba(channels["roughness"]))
        write_tga(override_dir / f"{name}_m.tga", greyscale_rgba(channels["metalness"]))
        if source.emissive_factor > 0.0:
            if channels["emissive"].convert("RGB").getbbox() is None:
                raise ValueError(f"emissive material has an empty map: {name}")
            write_tga(override_dir / f"{name}_light.tga", channels["emissive"])

    material_path = baseq2 / "materials" / "ab3d2_pbr.mat"
    write_material_file(material_path, sources.values())
    material_text = material_path.read_text(encoding="ascii")
    maps_dir = baseq2 / "maps"
    for level_name in LEVEL_NAMES:
        (maps_dir / f"{level_name}.mat").write_text(material_text, encoding="ascii")
    return sources


def write_compiler_metadata_wals(baseq2: pathlib.Path) -> tuple[pathlib.Path, pathlib.Path]:
    """Write compiler metadata for SURF_SKY and ericw internal skip faces.

    Q2RTX renders SURF_SKY procedurally; the pixel mip is not sampled for those
    faces. ericw-tools still requires valid Quake II WAL dimensions for `sky`
    and its internal `skip` surface while compiling Q2RTX output.
    """
    source = baseq2 / "textures" / "ab3d2" / "floor_0201.wal"
    source_data = source.read_bytes()
    targets = []
    for material_name in ("sky", "skip"):
        data = bytearray(source_data)
        name = material_name.encode("ascii")
        data[:32] = name + b"\0" * (32 - len(name))
        target = baseq2 / "textures" / f"{material_name}.wal"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        read_wal_size(target)
        targets.append(target)
    return targets[0], targets[1]


def run_logged(command: Sequence[str], log_path: pathlib.Path) -> str:
    process = subprocess.run(
        list(command),
        cwd=PROJECT_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        errors="replace",
        check=False,
    )
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(process.stdout, encoding="utf-8")
    if process.returncode != 0:
        tail = "\n".join(process.stdout.splitlines()[-30:])
        raise RuntimeError(
            f"command failed with exit code {process.returncode}: {' '.join(command)}\n"
            f"log: {log_path}\n{tail}"
        )
    return process.stdout


def download_ericw_archive(archive: pathlib.Path) -> None:
    archive.parent.mkdir(parents=True, exist_ok=True)
    if archive.is_file() and sha256_file(archive) == ERICW_WINDOWS_SHA256:
        return
    if archive.exists():
        archive.unlink()
    partial = archive.with_suffix(archive.suffix + ".partial")
    if partial.exists():
        partial.unlink()
    print(f"Downloading ericw-tools {ERICW_VERSION}...")
    request = urllib.request.Request(
        ERICW_WINDOWS_URL,
        headers={"User-Agent": "alienbreed3d2-q2rtx-builder"},
    )
    try:
        with urllib.request.urlopen(request) as response, partial.open("wb") as output:
            shutil.copyfileobj(response, output)
    except Exception as error:
        if partial.exists():
            partial.unlink()
        raise RuntimeError(
            f"failed to download {ERICW_WINDOWS_URL}: {error}; "
            "pass --ericw-tools with an existing alpha/dev ericw-tools directory"
        ) from error
    actual = sha256_file(partial)
    if actual != ERICW_WINDOWS_SHA256:
        partial.unlink()
        raise RuntimeError(
            f"ericw-tools archive hash mismatch: expected {ERICW_WINDOWS_SHA256}, got {actual}"
        )
    partial.replace(archive)


def safe_extract_zip(archive: pathlib.Path, destination: pathlib.Path) -> None:
    destination_resolved = destination.resolve()
    with zipfile.ZipFile(archive) as source:
        for member in source.infolist():
            member_path = (destination / member.filename).resolve()
            if os.path.commonpath((str(destination_resolved), str(member_path))) != str(
                destination_resolved
            ):
                raise RuntimeError(f"archive member escapes extraction root: {member.filename}")
        source.extractall(destination)


def compiler_executables(root: pathlib.Path) -> dict[str, pathlib.Path] | None:
    result: dict[str, pathlib.Path] = {}
    for name in ("qbsp", "vis", "light"):
        candidates = sorted(root.rglob(f"{name}.exe"))
        if not candidates:
            return None
        result[name] = candidates[0]
    return result


def ensure_ericw_tools(explicit_root: pathlib.Path | None) -> dict[str, pathlib.Path]:
    if explicit_root is not None:
        root = project_path(explicit_root)
        tools = compiler_executables(root)
        if tools is None:
            raise FileNotFoundError(f"qbsp.exe, vis.exe, and light.exe were not found under {root}")
        return tools

    root = PROJECT_ROOT / "build" / "tools" / f"ericw-tools-{ERICW_VERSION}"
    existing = compiler_executables(root) if root.is_dir() else None
    if existing is not None:
        return existing
    archive = PROJECT_ROOT / "build" / "downloads" / ERICW_WINDOWS_ARCHIVE
    download_ericw_archive(archive)
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    safe_extract_zip(archive, root)
    tools = compiler_executables(root)
    if tools is None:
        raise FileNotFoundError(f"downloaded ericw-tools package has no compiler trio: {root}")
    return tools


def prepare_package_root(package_root: pathlib.Path) -> pathlib.Path:
    package_root = project_path(package_root)
    build_root = (PROJECT_ROOT / "build").resolve()
    if package_root == build_root or build_root not in package_root.parents:
        raise ValueError(f"Q2RTX package root must be a child of {build_root}: {package_root}")
    if package_root.exists():
        shutil.rmtree(package_root)
    package_root.mkdir(parents=True)
    (package_root / ".ab3d2-q2rtx-package").write_text(
        "Generated by tools/build_q2rtx.py\n", encoding="ascii"
    )
    return package_root


def convert_levels(package_root: pathlib.Path, lighting: str) -> pathlib.Path:
    baseq2 = package_root / "baseq2"
    command = [
        sys.executable,
        str(PROJECT_ROOT / "tools" / "ab3d_levels_to_quake.py"),
        "--levels-root",
        str(MEDIA_ROOT / "demolevels"),
        "--out-dir",
        str(baseq2 / "maps"),
        "--extract-textures",
        "--texture-source",
        str(MEDIA_ROOT / "wallinc"),
        "--texture-palette",
        str(MEDIA_ROOT / "includes" / "256pal"),
        "--floor-source",
        str(MEDIA_ROOT / "includes" / "floortile"),
        "--floor-remap",
        str(MEDIA_ROOT / "includes" / "newtexturemaps.pal"),
        "--q2-root",
        str(package_root),
        "--wad-out",
        str(package_root / "ab3d2_textures.wad"),
        "--lighting",
        lighting,
        "--verbose",
    ]
    output = run_logged(command, package_root / "conversion.log")
    print(output, end="" if output.endswith("\n") else "\n")
    maps = sorted((baseq2 / "maps").glob("level_*.map"))
    if tuple(path.stem for path in maps) != LEVEL_NAMES:
        raise RuntimeError(f"conversion did not produce Levels A-P: {[path.name for path in maps]}")
    return baseq2


def validate_bsp(path: pathlib.Path) -> BspStats:
    data = path.read_bytes()
    header_size = 8 + Q2_BSP_LUMP_COUNT * 8
    if len(data) < header_size:
        raise ValueError(f"BSP header is truncated: {path}")
    ident, version = struct.unpack_from("<4si", data, 0)
    if ident != Q2_BSP_IDENT or version != Q2_BSP_VERSION:
        raise ValueError(
            f"Q2RTX requires IBSP version 38; {path} is {ident!r} version {version}"
        )
    lumps: list[tuple[int, int]] = []
    for index in range(Q2_BSP_LUMP_COUNT):
        offset, length = struct.unpack_from("<ii", data, 8 + index * 8)
        if offset < 0 or length < 0 or offset + length > len(data):
            raise ValueError(f"BSP lump {index} is out of bounds: {path}")
        lumps.append((offset, length))

    def count_lump(index: int, stride: int) -> int:
        length = lumps[index][1]
        if length % stride:
            raise ValueError(f"BSP lump {index} is not aligned to {stride} bytes: {path}")
        return length // stride

    vertex_count = count_lump(Q2_LUMP_VERTEXES, 12)
    edge_count = count_lump(Q2_LUMP_EDGES, 4)
    surfedge_count = count_lump(Q2_LUMP_SURFEDGES, 4)
    face_count = count_lump(Q2_LUMP_FACES, 20)

    edge_offset = lumps[Q2_LUMP_EDGES][0]
    for edge_index in range(edge_count):
        first, second = struct.unpack_from("<HH", data, edge_offset + edge_index * 4)
        if first >= vertex_count or second >= vertex_count:
            raise ValueError(
                f"BSP edge {edge_index} references vertex {max(first, second)} "
                f"but only {vertex_count} vertices exist: {path}"
            )

    surfedge_offset = lumps[Q2_LUMP_SURFEDGES][0]
    for surfedge_index in range(surfedge_count):
        edge = struct.unpack_from("<i", data, surfedge_offset + surfedge_index * 4)[0]
        if edge == -0x80000000 or abs(edge) >= edge_count:
            raise ValueError(
                f"BSP surfedge {surfedge_index} references edge {edge} "
                f"but only {edge_count} edges exist: {path}"
            )

    face_offset = lumps[Q2_LUMP_FACES][0]
    for face_index in range(face_count):
        firstedge = struct.unpack_from("<i", data, face_offset + face_index * 20 + 4)[0]
        numedges = struct.unpack_from("<h", data, face_offset + face_index * 20 + 8)[0]
        if firstedge < 0 or numedges < 0 or firstedge + numedges > surfedge_count:
            raise ValueError(
                f"BSP face {face_index} has invalid surfedge range "
                f"{firstedge}:{firstedge + numedges}: {path}"
            )

    entity_offset, entity_length = lumps[Q2_LUMP_ENTITIES]
    entities = data[entity_offset : entity_offset + entity_length]
    if b'"classname" "worldspawn"' not in entities:
        raise ValueError(f"BSP has no worldspawn: {path}")
    if b'"classname" "info_player_start"' not in entities:
        raise ValueError(f"BSP has no info_player_start: {path}")
    return BspStats(vertex_count, edge_count, surfedge_count, face_count)


def compile_levels(
    baseq2: pathlib.Path,
    tools: dict[str, pathlib.Path],
) -> dict[str, BspStats]:
    stats: dict[str, BspStats] = {}
    maps_dir = baseq2 / "maps"
    common = ["-q2rtx", "-nopercent", "-nostat", "-noprogress", "-basedir", str(baseq2)]
    for level_name in LEVEL_NAMES:
        map_path = maps_dir / f"{level_name}.map"
        bsp_path = maps_dir / f"{level_name}.bsp"
        print(f"Compiling {level_name}...")
        run_logged(
            [str(tools["qbsp"]), *common, str(map_path), str(bsp_path)],
            maps_dir / f"{level_name}-qbsp.log",
        )
        run_logged(
            [str(tools["vis"]), *common, str(bsp_path)],
            maps_dir / f"{level_name}-vis.log",
        )
        run_logged(
            [str(tools["light"]), *common, str(bsp_path)],
            maps_dir / f"{level_name}-light.log",
        )
        stats[level_name] = validate_bsp(bsp_path)
    return stats


def detect_q2rtx_root(value: pathlib.Path | None) -> pathlib.Path:
    candidates = (project_path(value),) if value is not None else DEFAULT_Q2RTX_ROOTS
    for candidate in candidates:
        if (candidate / "q2rtx.exe").is_file():
            baseq2 = candidate / "baseq2"
            required = ("pak0.pak", "q2rtx_media.pkz", "blue_noise.pkz")
            missing = [name for name in required if not (baseq2 / name).is_file()]
            if missing:
                raise FileNotFoundError(
                    f"Q2RTX runtime at {candidate} is missing required game data: {missing}"
                )
            return candidate
    searched = ", ".join(str(path) for path in candidates)
    raise FileNotFoundError(f"Q2RTX runtime was not found; searched: {searched}")


def install_relative_paths(baseq2: pathlib.Path) -> list[pathlib.Path]:
    paths: list[pathlib.Path] = []
    for level_name in LEVEL_NAMES:
        paths.extend(
            [
                pathlib.Path("maps") / f"{level_name}.bsp",
                pathlib.Path("maps") / f"{level_name}.mat",
            ]
        )
    paths.append(pathlib.Path("materials") / "ab3d2_pbr.mat")
    paths.append(pathlib.Path("pics") / "colormap.pcx")
    paths.extend(path.relative_to(baseq2) for path in sorted((baseq2 / "textures" / "ab3d2").glob("*.wal")))
    paths.extend(path.relative_to(baseq2) for path in sorted((baseq2 / "overrides" / "ab3d2").glob("*.tga")))
    missing = [str(path) for path in paths if not (baseq2 / path).is_file()]
    if missing:
        raise FileNotFoundError(f"Q2RTX package is missing install files: {missing}")
    return paths


def write_build_manifest(
    package_root: pathlib.Path,
    tools: dict[str, pathlib.Path],
    sources: dict[str, MaterialSource],
    stats: dict[str, BspStats],
) -> pathlib.Path:
    baseq2 = package_root / "baseq2"
    install_paths = install_relative_paths(baseq2)
    document = {
        "format": 1,
        "generator": "tools/build_q2rtx.py",
        "levels": list(LEVEL_NAMES),
        "materials": sorted(sources),
        "emitters": {
            name: source.emissive_factor
            for name, source in sorted(sources.items())
            if source.emissive_factor > 0.0
        },
        "ericw_tools": {
            "release": ERICW_VERSION,
            "executables": {
                name: {"path": str(path), "sha256": sha256_file(path)}
                for name, path in sorted(tools.items())
            },
        },
        "bsp": {name: vars(value) for name, value in sorted(stats.items())},
        "install_files": {
            path.as_posix(): sha256_file(baseq2 / path) for path in install_paths
        },
    }
    path = package_root / "manifest.json"
    path.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return path


def install_package(package_root: pathlib.Path, q2rtx_root: pathlib.Path) -> pathlib.Path:
    source_baseq2 = package_root / "baseq2"
    destination_baseq2 = q2rtx_root / "baseq2"
    for relative, expected_hash in LEGACY_INSTALL_FILES.items():
        legacy_path = destination_baseq2 / relative
        if not legacy_path.exists():
            continue
        actual_hash = sha256_file(legacy_path)
        if actual_hash != expected_hash:
            raise RuntimeError(
                f"refusing to remove modified legacy AB3D2 file {legacy_path}; "
                f"expected SHA-256 {expected_hash}, got {actual_hash}"
            )
        legacy_path.unlink()
        print(f"Removed superseded generated file: {legacy_path}")
    for relative in install_relative_paths(source_baseq2):
        source = source_baseq2 / relative
        destination = destination_baseq2 / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        if sha256_file(source) != sha256_file(destination):
            raise RuntimeError(f"installed file hash mismatch: {destination}")
    manifest = package_root / "manifest.json"
    installed_manifest = destination_baseq2 / "ab3d2_q2rtx_manifest.json"
    shutil.copy2(manifest, installed_manifest)
    return installed_manifest


def q2rtx_command(q2rtx_root: pathlib.Path, level_name: str) -> list[str]:
    return [
        str(q2rtx_root / "q2rtx.exe"),
        "+set",
        "vid_fullscreen",
        "0",
        "+set",
        "logfile",
        "2",
        "+map",
        level_name,
    ]


def smoke_test_q2rtx(q2rtx_root: pathlib.Path, level_name: str, timeout: float) -> None:
    log_path = q2rtx_root / "baseq2" / "logs" / "console.log"
    log_position = log_path.stat().st_size if log_path.exists() else 0
    creationflags = getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
    process = subprocess.Popen(
        q2rtx_command(q2rtx_root, level_name),
        cwd=q2rtx_root,
        creationflags=creationflags,
    )
    deadline = time.monotonic() + timeout
    observed = ""
    expected_material_count = len(load_material_sources()) * 2
    try:
        while time.monotonic() < deadline:
            if log_path.exists():
                current_size = log_path.stat().st_size
                if current_size < log_position:
                    log_position = 0
                    observed = ""
                if current_size > log_position:
                    with log_path.open("rb") as stream:
                        stream.seek(log_position)
                        observed += stream.read().decode("utf-8", errors="replace").lower()
                    log_position = current_size
                loaded_materials = (
                    f"loaded {expected_material_count} materials from "
                    f"maps/{level_name}.mat"
                )
                entered_game = "entered the game" in observed
                if f"loading {level_name}" in observed and loaded_materials in observed and entered_game:
                    rejected = (
                        "unknown material attribute",
                        f"couldn't load maps/{level_name}.bsp",
                        f"failed to load maps/{level_name}.bsp",
                    )
                    problem = next((text for text in rejected if text in observed), None)
                    if problem is not None:
                        raise RuntimeError(
                            f"Q2RTX reported '{problem}' while loading {level_name}; log: {log_path}"
                        )
                    return
            returncode = process.poll()
            if returncode is not None:
                raise RuntimeError(
                    f"Q2RTX exited with code {returncode} before loading {level_name}; log: {log_path}"
                )
            time.sleep(0.25)
        raise TimeoutError(
            f"Q2RTX did not finish loading {level_name} within {timeout:g} seconds; log: {log_path}"
        )
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=10)


def launch_q2rtx(q2rtx_root: pathlib.Path, level_name: str) -> int:
    creationflags = getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
    process = subprocess.Popen(
        q2rtx_command(q2rtx_root, level_name),
        cwd=q2rtx_root,
        creationflags=creationflags,
    )
    return process.pid


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Convert all AB3D2 maps, build a validated Q2RTX package, and optionally install/run it"
    )
    parser.add_argument(
        "--package-root",
        type=pathlib.Path,
        default=DEFAULT_PACKAGE_ROOT,
        help="Generated package directory; must remain below this repository's build directory",
    )
    parser.add_argument(
        "--ericw-tools",
        type=pathlib.Path,
        default=None,
        help=f"Directory containing qbsp/vis/light; otherwise pinned {ERICW_VERSION} is downloaded",
    )
    parser.add_argument(
        "--q2rtx-root",
        type=pathlib.Path,
        default=None,
        help="Q2RTX runtime root; the local Steam install is auto-detected when omitted",
    )
    parser.add_argument(
        "--lighting",
        choices=("none", "zone", "points"),
        default="none",
        help="Optional baked-light entities; Q2RTX's default uses authored emissive surfaces only",
    )
    parser.add_argument("--material-scale", type=int, default=4)
    parser.add_argument("--install", action="store_true")
    parser.add_argument("--smoke-test", action="store_true")
    parser.add_argument("--smoke-timeout", type=float, default=60.0)
    parser.add_argument("--launch", action="store_true")
    parser.add_argument("--level", choices=LEVEL_NAMES, default="level_a")
    args = parser.parse_args(argv)

    package_root = prepare_package_root(args.package_root)
    baseq2 = convert_levels(package_root, args.lighting)
    write_compiler_metadata_wals(baseq2)
    print("Packing the committed AB3D2 PBR material catalog...")
    sources = build_material_package(baseq2, args.material_scale)
    tools = ensure_ericw_tools(args.ericw_tools)
    stats = compile_levels(baseq2, tools)
    manifest = write_build_manifest(package_root, tools, sources, stats)
    print(
        f"Built {len(stats)} valid IBSP:38 maps and {len(sources)} materials -> {package_root}"
    )
    print(f"Manifest: {manifest}")

    needs_runtime = args.install or args.smoke_test or args.launch
    q2rtx_root = detect_q2rtx_root(args.q2rtx_root) if needs_runtime else None
    if args.install:
        assert q2rtx_root is not None
        installed_manifest = install_package(package_root, q2rtx_root)
        print(f"Installed package -> {q2rtx_root / 'baseq2'}")
        print(f"Installed manifest: {installed_manifest}")
    if (args.smoke_test or args.launch) and not args.install:
        raise ValueError("--smoke-test and --launch require --install in the same invocation")
    if args.smoke_test:
        assert q2rtx_root is not None
        smoke_test_q2rtx(q2rtx_root, args.level, args.smoke_timeout)
        print(f"Q2RTX smoke test loaded {args.level} and entered the game")
    if args.launch:
        assert q2rtx_root is not None
        pid = launch_q2rtx(q2rtx_root, args.level)
        print(f"Launched Q2RTX on {args.level} (PID {pid})")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, RuntimeError, TimeoutError, ValueError) as error:
        print(f"Q2RTX build failed: {error}", file=sys.stderr)
        raise SystemExit(1)
