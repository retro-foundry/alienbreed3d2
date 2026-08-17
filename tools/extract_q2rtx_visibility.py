#!/usr/bin/env python3
"""Extract the Q2RTX BSP spatial-lighting authority into compact sidecars.

The runtime renders the native AB3D2 scene.  These files retain only the
Quake II BSP data used by Q2RTX's BSP_PointLeaf, compute_cluster_aabbs, and
collect_cluster_lights paths; no Quake geometry is rendered at runtime.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
import struct
from dataclasses import dataclass
from typing import Iterable, Sequence

from ab3d_levels_to_quake import sb_decompress


MAGIC = b"RTXV"
VERSION = 1
HEADER_SIZE = 144
MAX_CLUSTERS = 2047
FNV64_OFFSET = 14695981039346656037
FNV64_PRIME = 1099511628211

LUMP_PLANES = 1
LUMP_VERTICES = 2
LUMP_VISIBILITY = 3
LUMP_NODES = 4
LUMP_TEXINFO = 5
LUMP_FACES = 6
LUMP_LEAVES = 8
LUMP_EDGES = 11
LUMP_SURFEDGES = 12
LUMP_MODELS = 13
LUMP_COUNT = 19

SURF_SKY = 0x4
SURF_WARP = 0x8
SURF_TRANS33 = 0x10
SURF_TRANS66 = 0x20
SURF_TRANSPARENT_MASK = SURF_WARP | SURF_TRANS33 | SURF_TRANS66

PLANE_RECORD = struct.Struct("<4f")
NODE_RECORD = struct.Struct("<Iii")
LEAF_RECORD = struct.Struct("<ii")
BOUNDS_RECORD = struct.Struct("<6f")


class VisibilityError(ValueError):
    pass


@dataclass(frozen=True)
class Plane:
    normal: tuple[float, float, float]
    distance: float


@dataclass(frozen=True)
class Node:
    plane: int
    children: tuple[int, int]


@dataclass(frozen=True)
class Leaf:
    contents: int
    cluster: int


@dataclass(frozen=True)
class Face:
    first_edge: int
    edge_count: int
    texinfo: int


@dataclass
class BspVisibility:
    planes: list[Plane]
    nodes: list[Node]
    leaves: list[Leaf]
    cluster_bounds: list[tuple[float, float, float, float, float, float]]
    pvs: bytes
    pvs_stride: int
    cluster_count: int


def _checked_lump(data: bytes, lumps: Sequence[tuple[int, int]], index: int,
                  record_size: int = 0) -> memoryview:
    offset, length = lumps[index]
    if offset > len(data) or length > len(data) - offset:
        raise VisibilityError(f"BSP lump {index} is outside the file")
    if record_size and length % record_size:
        raise VisibilityError(
            f"BSP lump {index} length {length} is not a multiple of {record_size}")
    return memoryview(data)[offset:offset + length]


def _vector_sub(a: Sequence[float], b: Sequence[float]) -> tuple[float, float, float]:
    return a[0] - b[0], a[1] - b[1], a[2] - b[2]


def _cross(a: Sequence[float], b: Sequence[float]) -> tuple[float, float, float]:
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def triangle_off_center(vertices: Sequence[Sequence[float]], offset: float,
                        anti: bool = False) -> tuple[float, float, float] | None:
    if len(vertices) != 3:
        raise VisibilityError("triangle assignment requires exactly three vertices")
    center = tuple(sum(vertex[axis] for vertex in vertices) / 3.0 for axis in range(3))
    normal = _cross(_vector_sub(vertices[1], vertices[0]),
                    _vector_sub(vertices[2], vertices[0]))
    length = math.sqrt(sum(component * component for component in normal))
    if length == 0.0:
        return None
    sign = -1.0 if anti else 1.0
    return tuple(center[axis] + sign * normal[axis] / length * offset
                 for axis in range(3))


def point_leaf_cluster(point: Sequence[float], planes: Sequence[Plane],
                       nodes: Sequence[Node], leaves: Sequence[Leaf]) -> int:
    node_index = 0
    visits = 0
    while node_index >= 0:
        if node_index >= len(nodes):
            raise VisibilityError(f"BSP node index {node_index} is out of range")
        node = nodes[node_index]
        plane = planes[node.plane]
        side = sum(point[axis] * plane.normal[axis] for axis in range(3)) - plane.distance
        node_index = node.children[1 if side < 0.0 else 0]
        visits += 1
        if visits > len(nodes):
            raise VisibilityError("BSP node tree contains a cycle")
    leaf_index = -1 - node_index
    if leaf_index >= len(leaves):
        raise VisibilityError(f"BSP leaf index {leaf_index} is out of range")
    return leaves[leaf_index].cluster


def triangle_cluster(vertices: Sequence[Sequence[float]], planes: Sequence[Plane],
                     nodes: Sequence[Node], leaves: Sequence[Leaf]) -> int:
    for offset in (0.01, 1.0):
        point = triangle_off_center(vertices, offset)
        if point is None:
            return -1
        cluster = point_leaf_cluster(point, planes, nodes, leaves)
        if cluster >= 0:
            return cluster
    return -1


def _decompress_pvs(vis: memoryview) -> tuple[int, int, bytearray]:
    if len(vis) < 4:
        raise VisibilityError("BSP visibility lump is truncated")
    cluster_count = struct.unpack_from("<I", vis, 0)[0]
    if cluster_count == 0 or cluster_count > MAX_CLUSTERS:
        raise VisibilityError(
            f"BSP declares {cluster_count} clusters; supported range is 1..{MAX_CLUSTERS}")
    header_size = 4 + cluster_count * 8
    if header_size > len(vis):
        raise VisibilityError("BSP visibility offsets are truncated")
    stride = (cluster_count + 7) // 8
    matrix = bytearray(cluster_count * stride)
    for cluster in range(cluster_count):
        source = struct.unpack_from("<I", vis, 4 + cluster * 8)[0]
        if source < header_size or source >= len(vis):
            raise VisibilityError(f"cluster {cluster} has an invalid PVS offset")
        output = cluster * stride
        output_end = output + stride
        while output < output_end:
            if source >= len(vis):
                raise VisibilityError(f"cluster {cluster} PVS stream is truncated")
            value = vis[source]
            source += 1
            if value:
                matrix[output] = value
                output += 1
                continue
            if source >= len(vis):
                raise VisibilityError(f"cluster {cluster} PVS zero run is truncated")
            run = vis[source]
            source += 1
            if run == 0 or run > output_end - output:
                raise VisibilityError(f"cluster {cluster} has an invalid PVS zero run")
            output += run
    return cluster_count, stride, matrix


def _visible_clusters(matrix: bytearray, stride: int, cluster: int,
                      cluster_count: int) -> Iterable[int]:
    row = cluster * stride
    for other in range(cluster_count):
        if matrix[row + (other >> 3)] & (1 << (other & 7)):
            yield other


def _merge_row(matrix: bytearray, stride: int, source: int, destination: int) -> None:
    src = source * stride
    dst = destination * stride
    for index in range(stride):
        matrix[dst + index] |= matrix[src + index]


def _connect_pvs(matrix: bytearray, stride: int, cluster_count: int,
                 cluster_a: int, cluster_b: int) -> None:
    visible_a = list(_visible_clusters(matrix, stride, cluster_a, cluster_count))
    visible_b = list(_visible_clusters(matrix, stride, cluster_b, cluster_count))
    for visible in visible_a:
        if visible not in (cluster_a, cluster_b):
            _merge_row(matrix, stride, cluster_b, visible)
    for visible in visible_b:
        if visible not in (cluster_a, cluster_b):
            _merge_row(matrix, stride, cluster_a, visible)
    _merge_row(matrix, stride, cluster_a, cluster_b)
    _merge_row(matrix, stride, cluster_b, cluster_a)


def _make_pvs_symmetric(matrix: bytearray, stride: int, cluster_count: int) -> None:
    for cluster in range(cluster_count):
        for visible in list(_visible_clusters(matrix, stride, cluster, cluster_count)):
            matrix[visible * stride + (cluster >> 3)] |= 1 << (cluster & 7)


def _face_vertices(face: Face, vertices: Sequence[tuple[float, float, float]],
                   edges: Sequence[tuple[int, int]], surfedges: Sequence[int]) -> list[tuple[float, float, float]]:
    result = []
    if face.first_edge < 0 or face.edge_count < 3 or face.first_edge + face.edge_count > len(surfedges):
        raise VisibilityError("BSP face references invalid surfedges")
    for index in range(face.first_edge, face.first_edge + face.edge_count):
        surfedge = surfedges[index]
        edge_index = abs(surfedge)
        if edge_index >= len(edges):
            raise VisibilityError("BSP surfedge references an invalid edge")
        vertex_index = edges[edge_index][0 if surfedge >= 0 else 1]
        if vertex_index >= len(vertices):
            raise VisibilityError("BSP edge references an invalid vertex")
        result.append(vertices[vertex_index])
    return result


def parse_bsp(data: bytes) -> BspVisibility:
    if len(data) < 8 + LUMP_COUNT * 8 or data[:4] != b"IBSP" or struct.unpack_from("<I", data, 4)[0] != 38:
        raise VisibilityError("visibility source is not a Quake II IBSP version 38 file")
    lumps = [struct.unpack_from("<II", data, 8 + index * 8) for index in range(LUMP_COUNT)]
    planes_lump = _checked_lump(data, lumps, LUMP_PLANES, 20)
    vertices_lump = _checked_lump(data, lumps, LUMP_VERTICES, 12)
    nodes_lump = _checked_lump(data, lumps, LUMP_NODES, 28)
    texinfo_lump = _checked_lump(data, lumps, LUMP_TEXINFO, 76)
    faces_lump = _checked_lump(data, lumps, LUMP_FACES, 20)
    leaves_lump = _checked_lump(data, lumps, LUMP_LEAVES, 28)
    edges_lump = _checked_lump(data, lumps, LUMP_EDGES, 4)
    surfedges_lump = _checked_lump(data, lumps, LUMP_SURFEDGES, 4)
    models_lump = _checked_lump(data, lumps, LUMP_MODELS, 48)
    vis_lump = _checked_lump(data, lumps, LUMP_VISIBILITY)

    planes = [Plane(struct.unpack_from("<3f", planes_lump, offset),
                    struct.unpack_from("<f", planes_lump, offset + 12)[0])
              for offset in range(0, len(planes_lump), 20)]
    nodes = [Node(struct.unpack_from("<i", nodes_lump, offset)[0],
                  struct.unpack_from("<2i", nodes_lump, offset + 4))
             for offset in range(0, len(nodes_lump), 28)]
    leaves = [Leaf(*struct.unpack_from("<ih", leaves_lump, offset))
              for offset in range(0, len(leaves_lump), 28)]
    for index, node in enumerate(nodes):
        if node.plane >= len(planes):
            raise VisibilityError(f"BSP node {index} has an invalid plane")
        for child in node.children:
            if child >= len(nodes) or (child < 0 and -1 - child >= len(leaves)):
                raise VisibilityError(f"BSP node {index} has an invalid child")

    vertices = [struct.unpack_from("<3f", vertices_lump, offset)
                for offset in range(0, len(vertices_lump), 12)]
    edges = [struct.unpack_from("<2H", edges_lump, offset)
             for offset in range(0, len(edges_lump), 4)]
    surfedges = [struct.unpack_from("<i", surfedges_lump, offset)[0]
                 for offset in range(0, len(surfedges_lump), 4)]
    texinfo_flags = [struct.unpack_from("<i", texinfo_lump, offset + 32)[0]
                     for offset in range(0, len(texinfo_lump), 76)]
    faces = [Face(struct.unpack_from("<i", faces_lump, offset + 4)[0],
                  struct.unpack_from("<H", faces_lump, offset + 8)[0],
                  struct.unpack_from("<H", faces_lump, offset + 10)[0])
             for offset in range(0, len(faces_lump), 20)]
    if not models_lump:
        raise VisibilityError("BSP has no world model")
    world_first_face, world_face_count = struct.unpack_from("<2i", models_lump, 40)
    if world_first_face < 0 or world_face_count < 0 or world_first_face + world_face_count > len(faces):
        raise VisibilityError("BSP world model has an invalid face range")

    cluster_count, stride, pvs = _decompress_pvs(vis_lump)
    for index, leaf in enumerate(leaves):
        if leaf.cluster < -1 or leaf.cluster >= cluster_count:
            raise VisibilityError(f"BSP leaf {index} has an invalid cluster")

    bounds = [[math.inf, math.inf, math.inf, -math.inf, -math.inf, -math.inf]
              for _ in range(cluster_count)]
    transparent_pairs: list[tuple[int, int]] = []
    for face_index in range(world_first_face, world_first_face + world_face_count):
        face = faces[face_index]
        if face.texinfo >= len(texinfo_flags):
            raise VisibilityError(f"BSP face {face_index} has invalid texinfo")
        polygon = _face_vertices(face, vertices, edges, surfedges)
        flags = texinfo_flags[face.texinfo]
        if flags & SURF_TRANSPARENT_MASK:
            for triangle_index in range(len(polygon) - 2):
                triangle = (polygon[0], polygon[triangle_index + 2],
                            polygon[triangle_index + 1])
                center = triangle_off_center(triangle, 0.01)
                anti_center = triangle_off_center(triangle, 0.01, anti=True)
                if center is None or anti_center is None:
                    continue
                cluster = point_leaf_cluster(center, planes, nodes, leaves)
                anti_cluster = point_leaf_cluster(anti_center, planes, nodes, leaves)
                if cluster >= 0 and anti_cluster >= 0 and cluster != anti_cluster:
                    transparent_pairs.append((cluster, anti_cluster))
        if flags & (SURF_SKY | SURF_TRANSPARENT_MASK):
            continue
        for triangle_index in range(len(polygon) - 2):
            triangle = (polygon[0], polygon[triangle_index + 2],
                        polygon[triangle_index + 1])
            cluster = triangle_cluster(triangle, planes, nodes, leaves)
            if cluster < 0:
                continue
            cluster_bounds = bounds[cluster]
            for vertex in triangle:
                for axis in range(3):
                    cluster_bounds[axis] = min(cluster_bounds[axis], vertex[axis])
                    cluster_bounds[axis + 3] = max(cluster_bounds[axis + 3], vertex[axis])

    patched = False
    for cluster, anti_cluster in transparent_pairs:
        row = cluster * stride
        anti_visible = pvs[row + (anti_cluster >> 3)] & (1 << (anti_cluster & 7))
        anti_row = anti_cluster * stride
        cluster_visible = pvs[anti_row + (cluster >> 3)] & (1 << (cluster & 7))
        if not anti_visible or not cluster_visible:
            _connect_pvs(pvs, stride, cluster_count, cluster, anti_cluster)
            patched = True
    if patched:
        _make_pvs_symmetric(pvs, stride, cluster_count)

    packed_bounds = []
    for item in bounds:
        if item[0] > item[3]:
            packed_bounds.append((math.inf, math.inf, math.inf,
                                  -math.inf, -math.inf, -math.inf))
        else:
            packed_bounds.append(tuple(item))
    return BspVisibility(planes, nodes, leaves, packed_bounds, bytes(pvs),
                         stride, cluster_count)


def fnv1a64(data: bytes) -> int:
    value = FNV64_OFFSET
    for byte in data:
        value ^= byte
        value = (value * FNV64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return value


def _find_level_directory(root: pathlib.Path, level: str) -> pathlib.Path:
    expected = f"level_{level}".lower()
    for candidate in root.iterdir():
        if candidate.is_dir() and candidate.name.lower() == expected:
            return candidate
    raise VisibilityError(f"could not find {expected} under {root}")


def build_sidecar(level_index: int, bsp_bytes: bytes, level_data: bytes,
                  level_graphics: bytes) -> tuple[bytes, BspVisibility]:
    visibility = parse_bsp(bsp_bytes)
    plane_offset = HEADER_SIZE
    node_offset = plane_offset + len(visibility.planes) * PLANE_RECORD.size
    leaf_offset = node_offset + len(visibility.nodes) * NODE_RECORD.size
    bounds_offset = leaf_offset + len(visibility.leaves) * LEAF_RECORD.size
    pvs_offset = bounds_offset + visibility.cluster_count * BOUNDS_RECORD.size
    file_size = pvs_offset + len(visibility.pvs)
    if file_size > 0xFFFFFFFF:
        raise VisibilityError("visibility sidecar exceeds 32-bit offsets")
    output = bytearray(file_size)
    output[0:4] = MAGIC
    struct.pack_into("<III", output, 4, VERSION, HEADER_SIZE, level_index)
    struct.pack_into("<QQQQ", output, 16, len(level_data), len(level_graphics),
                     fnv1a64(level_data), fnv1a64(level_graphics))
    struct.pack_into("<11I", output, 48, len(visibility.planes),
                     len(visibility.nodes), len(visibility.leaves),
                     visibility.cluster_count, visibility.pvs_stride,
                     plane_offset, node_offset, leaf_offset, bounds_offset,
                     pvs_offset, file_size)
    struct.pack_into("<I", output, 92, 0)
    # Native renderer (x, y-height, z) -> converted Q2 (x, y, z).
    struct.pack_into("<12f", output, 96,
                     7.0 / 24.0, 0.0, 0.0, 0.0,
                     0.0, 0.0, 7.0 / 24.0, 0.0,
                     0.0, 7.0 / 12.0, 0.0, 0.0)
    cursor = plane_offset
    for plane in visibility.planes:
        PLANE_RECORD.pack_into(output, cursor, *plane.normal, plane.distance)
        cursor += PLANE_RECORD.size
    cursor = node_offset
    for node in visibility.nodes:
        NODE_RECORD.pack_into(output, cursor, node.plane, *node.children)
        cursor += NODE_RECORD.size
    cursor = leaf_offset
    for leaf in visibility.leaves:
        LEAF_RECORD.pack_into(output, cursor, leaf.contents, leaf.cluster)
        cursor += LEAF_RECORD.size
    cursor = bounds_offset
    for cluster_bounds in visibility.cluster_bounds:
        BOUNDS_RECORD.pack_into(output, cursor, *cluster_bounds)
        cursor += BOUNDS_RECORD.size
    output[pvs_offset:] = visibility.pvs
    return bytes(output), visibility


def extract_all(bsp_root: pathlib.Path, levels_root: pathlib.Path,
                output_root: pathlib.Path, map_root: pathlib.Path | None) -> dict:
    output_root.mkdir(parents=True, exist_ok=True)
    manifest = {
        "format": VERSION,
        "generator": "tools/extract_q2rtx_visibility.py",
        "generation_options": {
            "cluster_aabbs": "Q2RTX opaque world triangles",
            "pvs": "decompressed with transparent-cluster links and symmetry",
            "triangle_assignment_offsets": [0.01, 1.0],
            "native_to_q2_transform": [
                7.0 / 24.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 7.0 / 24.0, 0.0,
                0.0, 7.0 / 12.0, 0.0, 0.0,
            ],
        },
        "q2rtx_authority": [
            "src/refresh/vkpt/bsp_mesh.c:get_triangle_off_center",
            "src/refresh/vkpt/bsp_mesh.c:compute_cluster_aabbs",
            "src/refresh/vkpt/bsp_mesh.c:collect_cluster_lights",
        ],
        "levels": {},
    }
    for level_index in range(16):
        letter = chr(ord("a") + level_index)
        bsp_path = bsp_root / f"level_{letter}.bsp"
        if not bsp_path.is_file():
            raise VisibilityError(f"required visibility source is missing: {bsp_path}")
        level_directory = _find_level_directory(levels_root, letter)
        data_path = level_directory / "twolev.bin"
        graphics_path = level_directory / "twolev.graph.bin"
        level_data = sb_decompress(data_path.read_bytes())
        level_graphics = sb_decompress(graphics_path.read_bytes())
        bsp_bytes = bsp_path.read_bytes()
        sidecar, visibility = build_sidecar(
            level_index, bsp_bytes, level_data, level_graphics)
        sidecar_path = output_root / f"level_{letter}.rtxvis"
        sidecar_path.write_bytes(sidecar)
        map_path = map_root / f"level_{letter}.map" if map_root else None
        manifest["levels"][letter.upper()] = {
            "sidecar": sidecar_path.name,
            "sidecar_sha256": hashlib.sha256(sidecar).hexdigest(),
            "bsp_sha256": hashlib.sha256(bsp_bytes).hexdigest(),
            "map_sha256": hashlib.sha256(map_path.read_bytes()).hexdigest()
            if map_path and map_path.is_file() else None,
            "level_data_fnv1a64": f"{fnv1a64(level_data):016x}",
            "level_graphics_fnv1a64": f"{fnv1a64(level_graphics):016x}",
            "clusters": visibility.cluster_count,
            "pvs_stride": visibility.pvs_stride,
            "planes": len(visibility.planes),
            "nodes": len(visibility.nodes),
            "leaves": len(visibility.leaves),
        }
    (output_root / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Extract compact Q2RTX BSP cluster/PVS sidecars")
    parser.add_argument("--bsp-root", type=pathlib.Path, required=True)
    parser.add_argument("--levels-root", type=pathlib.Path,
                        default=pathlib.Path("amiga/media/demolevels"))
    parser.add_argument("--output-root", type=pathlib.Path,
                        default=pathlib.Path("assets/rtx_visibility"))
    parser.add_argument("--map-root", type=pathlib.Path)
    args = parser.parse_args(argv)
    try:
        manifest = extract_all(args.bsp_root, args.levels_root,
                               args.output_root, args.map_root)
    except (OSError, VisibilityError) as exc:
        parser.error(str(exc))
    for name, entry in manifest["levels"].items():
        print(f"Level {name}: {entry['clusters']} clusters, "
              f"stride {entry['pvs_stride']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
