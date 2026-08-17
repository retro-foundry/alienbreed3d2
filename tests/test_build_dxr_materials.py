import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "build_dxr_materials.py"
SOURCE = ROOT / "textures_pbr"
SPEC = ROOT / "data" / "renderer_dxr" / "material_sources.json"
EXPECTED_CONTENT_DIGEST = "b3c20b7bb08cbdfc3f29399360b179ffca2bd2a5d8870f8467e96e7275907be0"


def directory_digest(directory: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(item for item in directory.iterdir() if item.is_file()):
        digest.update(path.name.encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
    return digest.hexdigest()


class DxrMaterialBuilderTest(unittest.TestCase):
    def run_builder(self, output: Path) -> None:
        subprocess.run(
            [
                sys.executable,
                str(TOOL),
                "--source-dir",
                str(SOURCE),
                "--spec",
                str(SPEC),
                "--output-dir",
                str(output),
            ],
            check=True,
            capture_output=True,
            text=True,
        )

    def test_outputs_are_complete_deterministic_and_renderer_native(self) -> None:
        with tempfile.TemporaryDirectory() as first_name, tempfile.TemporaryDirectory() as second_name:
            first = Path(first_name)
            second = Path(second_name)
            self.run_builder(first)
            self.run_builder(second)
            self.assertEqual(directory_digest(first), directory_digest(second))

            manifest = json.loads((first / "material_manifest.json").read_text(encoding="utf-8"))
            content_manifest = json.loads(json.dumps(manifest))
            for material in content_manifest["materials"]:
                for channel in material["channels"].values():
                    channel.pop("sha256")
            content_digest = hashlib.sha256(
                json.dumps(
                    content_manifest, sort_keys=True, separators=(",", ":")
                ).encode("utf-8")
            ).hexdigest()
            self.assertEqual(content_digest, EXPECTED_CONTENT_DIGEST)
            self.assertEqual(manifest["schema_version"], 1)
            self.assertEqual(len(manifest["materials"]), 13)
            self.assertEqual(
                manifest["missing_material"],
                {
                    "base_color": "decoded_source_albedo",
                    "roughness": 1.0,
                    "metalness": 0.0,
                    "emissive_factor": [0.0, 0.0, 0.0],
                },
            )
            bindings = {}
            for material in manifest["materials"]:
                self.assertGreaterEqual(material["width"], 16)
                self.assertGreaterEqual(material["height"], 16)
                self.assertEqual(material["base_color_space"], "srgb")
                self.assertEqual(material["normal_space"], "linear_tangent")
                self.assertEqual(material["roughness_space"], "linear")
                self.assertEqual(material["metalness_space"], "linear")
                self.assertEqual(material["emissive_factor"], [0.0, 0.0, 0.0])
                for channel in ("base_color", "normal", "metalness", "roughness"):
                    channel_info = material["channels"][channel]
                    self.assertEqual(channel_info["mode"], "RGB")
                    self.assertTrue((first / channel_info["file"]).is_file())
                if "binding" in material:
                    binding = material["binding"]
                    key = (binding["source"], binding["source_asset_id"])
                    self.assertNotIn(key, bindings)
                    bindings[key] = material["name"]
            self.assertEqual(
                bindings,
                {
                    ("shared_wall", 1): "brownpipes",
                    ("shared_wall", 2): "hullmetal",
                    ("shared_wall", 3): "technotritile",
                    ("shared_wall", 4): "brownspeakers",
                    ("shared_wall", 5): "chevrondoor",
                    ("shared_wall", 6): "technolights",
                    ("shared_wall", 7): "redhullmetal",
                    ("shared_wall", 8): "alienredwall",
                    ("shared_wall", 9): "gieger",
                    ("shared_wall", 10): "rocky",
                    ("shared_wall", 11): "steampunk",
                },
            )


if __name__ == "__main__":
    unittest.main()
