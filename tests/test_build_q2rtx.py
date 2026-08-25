import importlib.util
import pathlib
import struct
import sys
import tempfile
import unittest

from PIL import Image


ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "build_q2rtx.py"
SPEC = importlib.util.spec_from_file_location("build_q2rtx_under_test", SCRIPT)
assert SPEC is not None
assert SPEC.loader is not None
q2rtx = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = q2rtx
SPEC.loader.exec_module(q2rtx)


class Q2RtxMaterialTests(unittest.TestCase):
    def test_catalog_covers_every_converter_wal(self):
        sources = q2rtx.load_material_sources()
        floor_names = {f"floor_{row:02x}{column:02x}" for row in range(5) for column in range(4)}
        wall_names = {
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
            "brownwithyellowstripes",
        }
        self.assertEqual(set(sources), floor_names | wall_names)

    def test_catalog_has_only_the_two_proven_emitters(self):
        emitters = {
            name: source.emissive_factor
            for name, source in q2rtx.load_material_sources().items()
            if source.emissive_factor > 0.0
        }
        self.assertEqual(emitters, {"floor_0101": 200.0, "technolights": 200.0})

    def test_q2rtx_channels_pack_roughness_and_metalness_into_alpha(self):
        albedo = Image.new("RGBA", (2, 1))
        albedo.putdata([(10, 20, 30, 255), (40, 50, 60, 255)])
        roughness = Image.new("L", (2, 1))
        roughness.putdata([70, 80])
        normal = Image.new("RGBA", (2, 1))
        normal.putdata([(100, 110, 20, 255), (120, 130, 140, 255)])
        metalness = Image.new("L", (2, 1))
        metalness.putdata([150, 160])

        base = q2rtx.pack_base(albedo, roughness)
        packed_normal = q2rtx.pack_normal(normal, metalness)

        self.assertEqual(list(base.getdata()), [(10, 20, 30, 70), (40, 50, 60, 80)])
        self.assertEqual(
            list(packed_normal.getdata()),
            [(100, 110, 96, 150), (120, 130, 140, 160)],
        )

    def test_world_channel_scaling_is_shared_with_native_export(self):
        source = Image.new("RGBA", (5, 3), (1, 2, 3, 255))
        target = (8, 8)
        self.assertEqual(q2rtx.resize_world_channel(source, target).size, target)
        self.assertEqual(q2rtx.crop_to_aspect(source, target).size, (3, 3))

    def test_material_file_keeps_proven_emissive_strengths(self):
        sources = q2rtx.load_material_sources()
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "ab3d2_pbr.mat"
            q2rtx.write_material_file(path, sources.values())
            text = path.read_text(encoding="ascii")

        self.assertIn("textures/ab3d2/technolights:", text)
        self.assertEqual(text.count("emissive_factor 200"), 2)
        self.assertIn("textures/ab3d2/floor_0101:", text)
        self.assertIn("emissive_factor 200", text)
        self.assertNotIn("textures/ab3d2/brownspeakers_light.tga", text)

    def test_legacy_install_cleanup_is_limited_to_known_hashes(self):
        self.assertEqual(
            set(q2rtx.LEGACY_INSTALL_FILES),
            {
                pathlib.Path("materials/ab3d2_neon.mat"),
                pathlib.Path("overrides/ab3d2_technolights_light.tga"),
            },
        )
        self.assertTrue(
            all(len(value) == 64 for value in q2rtx.LEGACY_INSTALL_FILES.values())
        )


def make_test_bsp(path: pathlib.Path, bad_edge: bool = False) -> None:
    lumps = [b"" for _ in range(q2rtx.Q2_BSP_LUMP_COUNT)]
    lumps[q2rtx.Q2_LUMP_ENTITIES] = (
        b'{\n"classname" "worldspawn"\n}\n'
        b'{\n"classname" "info_player_start"\n}\n\0'
    )
    lumps[q2rtx.Q2_LUMP_VERTEXES] = struct.pack("<fff", 0.0, 0.0, 0.0)
    edge_vertex = 1 if bad_edge else 0
    lumps[q2rtx.Q2_LUMP_EDGES] = struct.pack("<HH", 0, edge_vertex)
    lumps[q2rtx.Q2_LUMP_SURFEDGES] = struct.pack("<i", 0)
    lumps[q2rtx.Q2_LUMP_FACES] = struct.pack(
        "<Hhihh4si", 0, 0, 0, 1, 0, b"\0\0\0\0", -1
    )

    header_size = 8 + q2rtx.Q2_BSP_LUMP_COUNT * 8
    data = bytearray(struct.pack("<4si", q2rtx.Q2_BSP_IDENT, q2rtx.Q2_BSP_VERSION))
    data.extend(b"\0" * (q2rtx.Q2_BSP_LUMP_COUNT * 8))
    offset = header_size
    for index, payload in enumerate(lumps):
        struct.pack_into("<ii", data, 8 + index * 8, offset, len(payload))
        data.extend(payload)
        offset += len(payload)
    path.write_bytes(data)


class Q2RtxBspTests(unittest.TestCase):
    def test_validator_accepts_q2_ibsp38_with_in_range_edges(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "valid.bsp"
            make_test_bsp(path)
            stats = q2rtx.validate_bsp(path)
        self.assertEqual(stats, q2rtx.BspStats(vertices=1, edges=1, surfedges=1, faces=1))

    def test_validator_rejects_out_of_range_edge_vertex(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "invalid.bsp"
            make_test_bsp(path, bad_edge=True)
            with self.assertRaisesRegex(ValueError, "references vertex"):
                q2rtx.validate_bsp(path)

    def test_sky_wal_has_its_own_q2_texture_name(self):
        with tempfile.TemporaryDirectory() as directory:
            baseq2 = pathlib.Path(directory) / "baseq2"
            source = baseq2 / "textures" / "ab3d2" / "floor_0201.wal"
            source.parent.mkdir(parents=True)
            data = bytearray(104)
            data[:32] = b"ab3d2/floor_0201" + b"\0" * (32 - len("ab3d2/floor_0201"))
            struct.pack_into("<ii", data, 32, 2, 2)
            struct.pack_into("<i", data, 40, 100)
            source.write_bytes(data)

            sky, skip = q2rtx.write_compiler_metadata_wals(baseq2)
            written = sky.read_bytes()
            size = q2rtx.read_wal_size(sky)
            skip_name = skip.read_bytes()[:32].split(b"\0", 1)[0]

        self.assertEqual(written[:32].split(b"\0", 1)[0], b"sky")
        self.assertEqual(size, (2, 2))
        self.assertEqual(skip_name, b"skip")


if __name__ == "__main__":
    unittest.main()
