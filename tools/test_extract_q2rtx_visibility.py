#!/usr/bin/env python3
"""Unit and committed-golden tests for compact Q2RTX visibility assets."""

from __future__ import annotations

import hashlib
import json
import pathlib
import struct
import sys
import unittest

TOOLS = pathlib.Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import extract_q2rtx_visibility as visibility  # noqa: E402


def build_minimal_bsp() -> bytes:
    lumps: list[bytes] = [b"" for _ in range(visibility.LUMP_COUNT)]
    lumps[visibility.LUMP_PLANES] = struct.pack("<4fi", 0.0, 0.0, 1.0, 0.0, 2)
    lumps[visibility.LUMP_VERTICES] = b"".join(
        struct.pack("<3f", *vertex)
        for vertex in ((0.0, 0.0, 0.0), (2.0, 0.0, 0.0), (0.0, 3.0, 0.0)))
    pvs_header = struct.pack("<I2I", 1, 12, 12)
    lumps[visibility.LUMP_VISIBILITY] = pvs_header + b"\x01"
    lumps[visibility.LUMP_NODES] = struct.pack(
        "<i2i3h3h2H", 0, -1, -2, 0, 0, 0, 2, 3, 1, 0, 1)
    texinfo = bytearray(76)
    struct.pack_into("<i", texinfo, 32, 0)
    lumps[visibility.LUMP_TEXINFO] = bytes(texinfo)
    lumps[visibility.LUMP_FACES] = struct.pack(
        "<HHiHH4Bi", 0, 0, 0, 3, 0, 255, 255, 255, 255, -1)
    leaf_zero = struct.pack(
        "<ihh3h3h4H", 0, 0, 0, 0, 0, 0, 2, 3, 1, 0, 0, 0, 0)
    leaf_solid = struct.pack(
        "<ihh3h3h4H", 1, -1, 0, 0, 0, -1, 2, 3, 0, 0, 0, 0, 0)
    lumps[visibility.LUMP_LEAVES] = leaf_zero + leaf_solid
    lumps[visibility.LUMP_EDGES] = b"".join(
        struct.pack("<2H", *edge) for edge in ((0, 1), (2, 0), (1, 2)))
    lumps[visibility.LUMP_SURFEDGES] = struct.pack("<3i", 0, 1, 2)
    lumps[visibility.LUMP_MODELS] = struct.pack(
        "<9f3i", 0.0, 0.0, -1.0, 2.0, 3.0, 1.0,
        0.0, 0.0, 0.0, 0, 0, 1)

    header_size = 8 + visibility.LUMP_COUNT * 8
    output = bytearray(header_size)
    output[:8] = struct.pack("<4sI", b"IBSP", 38)
    cursor = header_size
    for index, lump in enumerate(lumps):
        struct.pack_into("<II", output, 8 + index * 8, cursor, len(lump))
        output.extend(lump)
        cursor += len(lump)
    return bytes(output)


class VisibilityExtractorTests(unittest.TestCase):
    def test_pvs_decompression_and_validation(self) -> None:
        compressed = struct.pack(
            "<I6I", 3, 28, 28, 29, 29, 30, 30) + b"\x01\x03\x04"
        count, stride, matrix = visibility._decompress_pvs(memoryview(compressed))
        self.assertEqual((count, stride, bytes(matrix)), (3, 1, b"\x01\x03\x04"))
        with self.assertRaises(visibility.VisibilityError):
            visibility._decompress_pvs(memoryview(struct.pack("<I", 2048)))
        with self.assertRaises(visibility.VisibilityError):
            visibility._decompress_pvs(memoryview(compressed[:-1]))

    def test_transparent_link_and_symmetry(self) -> None:
        matrix = bytearray((0b00000001, 0b00000010, 0b00000100))
        visibility._connect_pvs(matrix, 1, 3, 0, 1)
        visibility._make_pvs_symmetric(matrix, 1, 3)
        self.assertEqual(matrix[0] & 0b11, 0b11)
        self.assertEqual(matrix[1] & 0b11, 0b11)

    def test_point_leaf_and_off_center_assignment(self) -> None:
        planes = [visibility.Plane((1.0, 0.0, 0.0), 0.0)]
        nodes = [visibility.Node(0, (-1, -2))]
        leaves = [visibility.Leaf(0, 7), visibility.Leaf(1, -1)]
        self.assertEqual(
            visibility.point_leaf_cluster((1.0, 0.0, 0.0), planes, nodes, leaves), 7)
        self.assertEqual(
            visibility.point_leaf_cluster((-1.0, 0.0, 0.0), planes, nodes, leaves), -1)
        center = visibility.triangle_off_center(
            ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)), 0.01)
        self.assertIsNotNone(center)
        self.assertAlmostEqual(center[2], 0.01)

    def test_bsp_parsing_and_cluster_aabb(self) -> None:
        parsed = visibility.parse_bsp(build_minimal_bsp())
        self.assertEqual(parsed.cluster_count, 1)
        self.assertEqual(parsed.pvs, b"\x01")
        self.assertEqual(parsed.cluster_bounds[0], (0.0, 0.0, 0.0, 2.0, 3.0, 0.0))
        with self.assertRaises(visibility.VisibilityError):
            visibility.parse_bsp(build_minimal_bsp()[:-1])

    def test_committed_sidecar_golden_counts_and_hashes(self) -> None:
        expected = (304, 366, 233, 74, 361, 43, 1, 521,
                    1428, 12, 244, 304, 1815, 305, 1167, 928)
        asset_root = ROOT / "assets" / "rtx_visibility"
        manifest = json.loads((asset_root / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["format"], visibility.VERSION)
        for index, cluster_count in enumerate(expected):
            letter = chr(ord("a") + index)
            path = asset_root / f"level_{letter}.rtxvis"
            data = path.read_bytes()
            entry = manifest["levels"][letter.upper()]
            self.assertEqual(struct.unpack_from("<I", data, 12)[0], index)
            self.assertEqual(struct.unpack_from("<I", data, 60)[0], cluster_count)
            self.assertEqual(entry["clusters"], cluster_count)
            self.assertEqual(hashlib.sha256(data).hexdigest(), entry["sidecar_sha256"])


if __name__ == "__main__":
    unittest.main()
