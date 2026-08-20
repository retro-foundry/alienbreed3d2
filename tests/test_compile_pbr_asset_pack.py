import json
import struct
import tempfile
import unittest
from collections import Counter
from pathlib import Path

from PIL import Image

from tools.compile_pbr_asset_pack import (
    CHANNELS,
    MATERIAL_DIRECTORIES,
    RUNTIME_CLASSES,
    RUNTIME_CLASS_SHIFT,
    RUNTIME_HEADER,
    RUNTIME_MAGIC,
    RUNTIME_RECORD,
    RUNTIME_VERSION,
    compile_pack,
)


ROOT = Path(__file__).resolve().parents[1]
ASSET_DIR = ROOT / "assets" / "renderer_dxr" / "materials"
SPEC = ASSET_DIR / "materials.json"


class PbrAssetPackCompilerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.spec = json.loads(SPEC.read_text(encoding="utf-8"))

    def test_artist_directory_is_complete_sorted_and_zip_ready(self) -> None:
        materials = self.spec["materials"]
        self.assertEqual(self.spec["schema_version"], 4)
        self.assertEqual(len(materials), 973)
        self.assertEqual(
            Counter(material["class"] for material in materials),
            {
                "wall": 14,
                "floor": 20,
                "weapon": 341,
                "vector_model": 284,
                "enemy_billboard": 233,
                "billboard": 46,
                "effect_billboard": 31,
                "environment": 1,
                "ui": 3,
            },
        )
        expected_pngs = {
            material["channels"][channel]
            for material in materials
            for channel in CHANNELS
        }
        self.assertEqual(len(expected_pngs), 4_865)
        self.assertEqual(
            {
                path.relative_to(ASSET_DIR).as_posix()
                for path in ASSET_DIR.rglob("*.png")
            },
            expected_pngs,
        )
        self.assertEqual(
            {path.name for path in ASSET_DIR.iterdir() if path.is_file()},
            {"materials.json", "README.md"},
        )
        self.assertEqual(
            {path.name for path in ASSET_DIR.iterdir() if path.is_dir()},
            set(MATERIAL_DIRECTORIES.values()),
        )
        for material_class, directory_name in MATERIAL_DIRECTORIES.items():
            directory = ASSET_DIR / directory_name
            expected_count = sum(
                material["class"] == material_class for material in materials
            ) * len(CHANNELS)
            self.assertEqual(len(list(directory.glob("*.png"))), expected_count)
            self.assertTrue(all(path.is_file() for path in directory.iterdir()))
        self.assertTrue((ASSET_DIR / "README.md").is_file())

        for material in materials:
            self.assertEqual(set(material["channels"]), set(CHANNELS))
            self.assertTrue(
                all(
                    filename.startswith(
                        MATERIAL_DIRECTORIES[material["class"]] + "/"
                    )
                    for filename in material["channels"].values()
                )
            )
            for source_file in material["source"]["files"]:
                self.assertFalse(Path(source_file).is_absolute())
                self.assertNotIn("..", Path(source_file).parts)

        sample = next(
            material
            for material in materials
            if material["name"] == "weapon_03_blaster_material_000"
        )
        self.assertEqual(
            set(sample["generated_channels"]),
            {"normal", "metalness", "roughness", "emissive"},
        )
        expected_rgb = {
            "normal": (128, 128, 255),
            "metalness": (0, 0, 0),
            "roughness": (255, 255, 255),
            "emissive": (0, 0, 0),
        }
        for channel, rgb in expected_rgb.items():
            with Image.open(ASSET_DIR / sample["channels"][channel]) as opened:
                self.assertEqual({pixel[:3] for pixel in opened.convert("RGBA").getdata()}, {rgb})

    def test_runtime_catalog_references_pngs_instead_of_embedding_pixels(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "materials"
            manifest_path = compile_pack(ASSET_DIR, SPEC, output)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            runtime = (output / "material_runtime.bin").read_bytes()
            magic, version, count, channels, record_size = RUNTIME_HEADER.unpack_from(runtime)
            self.assertEqual(magic, RUNTIME_MAGIC)
            self.assertEqual(version, RUNTIME_VERSION)
            self.assertEqual(count, 973)
            self.assertEqual(channels, len(CHANNELS))
            self.assertEqual(record_size, RUNTIME_RECORD.size)
            self.assertEqual(len(runtime), RUNTIME_HEADER.size + count * record_size)
            self.assertNotIn(b"\x89PNG\r\n\x1a\n", runtime)
            self.assertFalse(manifest["runtime_package"]["contains_pixels"])
            self.assertEqual(len(list(output.rglob("*.png"))), 4_865)
            self.assertEqual(
                {path.name for path in output.iterdir() if path.is_dir()},
                set(MATERIAL_DIRECTORIES.values()),
            )

            records = [
                RUNTIME_RECORD.unpack_from(runtime, RUNTIME_HEADER.size + index * record_size)
                for index in range(count)
            ]
            names = [record[-1].split(b"\0", 1)[0].decode("ascii") for record in records]
            weapon_index = names.index("weapon_03_blaster_material_000")
            weapon = records[weapon_index]
            self.assertEqual(weapon[0], 3)  # vector binding
            self.assertEqual(weapon[1:4], (3, 0, 0x3F000200))
            self.assertEqual((weapon[4], weapon[5]), (3, 64))
            self.assertEqual(
                (weapon[10] >> RUNTIME_CLASS_SHIFT) & 0xF,
                RUNTIME_CLASSES["weapon"],
            )


if __name__ == "__main__":
    unittest.main()
