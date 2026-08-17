import hashlib
import json
import subprocess
import struct
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "build_dxr_materials.py"
SOURCE = ROOT / "textures_pbr"
SPEC = ROOT / "data" / "renderer_dxr" / "material_sources.json"
EXPECTED_CONTENT_DIGEST = "42c5402f1d47f0b252d656445ced46c4fe1ea59428a4c51f39dc91fa0c42f5f6"
RUNTIME_HEADER = struct.Struct("<8sIIII")
RUNTIME_RECORD = struct.Struct("<IIIIffffII")


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
            runtime = (first / "material_runtime.bin").read_bytes()
            magic, version, material_count, channel_count, record_size = (
                RUNTIME_HEADER.unpack_from(runtime)
            )
            self.assertEqual(magic, b"AB3PBR1\0")
            self.assertEqual(version, 1)
            self.assertEqual(material_count, len(manifest["materials"]))
            self.assertEqual(channel_count, 4)
            self.assertEqual(record_size, RUNTIME_RECORD.size)
            self.assertEqual(manifest["runtime_package"]["format"], "AB3PBR1")
            self.assertEqual(
                manifest["runtime_package"]["sha256"], hashlib.sha256(runtime).hexdigest()
            )
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
                authored_emission = {
                    "brownspeakers": [4.0, 4.0, 4.0],
                    "technolights": [8.0, 8.0, 8.0],
                    "technotritile": [4.0, 4.0, 4.0],
                }
                if material["name"] in authored_emission:
                    self.assertEqual(material["emissive_source"], "base_color")
                    self.assertEqual(
                        material["emissive_factor"],
                        authored_emission[material["name"]],
                    )
                else:
                    self.assertEqual(material["emissive_source"], "none")
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
            pixel_offset = RUNTIME_HEADER.size + material_count * RUNTIME_RECORD.size
            records = []
            for index, material in enumerate(manifest["materials"]):
                record = RUNTIME_RECORD.unpack_from(
                    runtime, RUNTIME_HEADER.size + index * RUNTIME_RECORD.size
                )
                records.append(record)
                self.assertEqual(record[2:4], (material["width"], material["height"]))
                pixel_offset += material["width"] * material["height"] * 4 * channel_count
            self.assertEqual(pixel_offset, len(runtime))
            technolights = records[
                next(
                    index
                    for index, material in enumerate(manifest["materials"])
                    if material["name"] == "technolights"
                )
            ]
            self.assertEqual(technolights[0:2], (1, 6))
            self.assertEqual(technolights[5:8], (8.0, 8.0, 8.0))
            self.assertEqual(technolights[8], 1)


if __name__ == "__main__":
    unittest.main()
