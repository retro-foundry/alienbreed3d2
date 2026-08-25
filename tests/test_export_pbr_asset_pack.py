import json
import sys
import unittest
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import export_pbr_asset_pack as exporter


class PbrAssetExporterTest(unittest.TestCase):
    def test_authoritative_catalog_inventory_is_complete(self) -> None:
        game_link = (ROOT / "amiga" / "media" / "includes" / "test.lnk").read_bytes()
        self.assertEqual(len(game_link), exporter.GLFT_SIZE)
        layout = exporter.glft_layout()
        references, enemy_assets = exporter.bitmap_references(game_link, layout)
        self.assertEqual(len(references), 315)
        self.assertEqual(enemy_assets, {0, 3, 6, 11, 12, 13})
        self.assertEqual(
            Counter(reference.mode for reference in references),
            {
                "bitmap": 60,
                "lighted_2": 85,
                "lighted_3": 60,
                "lighted_4": 54,
                "lighted_5": 20,
                "glare": 21,
                "additive": 15,
            },
        )
        # Object 21's default animation runs rows zero to five, stepping asset
        # eight through frames 14 to 19 before row five self-loops on the last
        # one. Every row has to be bound: the runtime enters at row zero,
        # because DEFANIMOBJ indexes the table by EntT_Timer1_w and every seed
        # of that word is a literal zero. Entering at ODefT_DefaultAnimLen_w
        # instead bound frame 19 alone and the renderer failed on the first
        # glare that reached frame 15.
        self.assertEqual(
            sorted(
                reference.frame
                for reference in references
                if reference.asset_id == 8 and reference.mode == "glare"
            ),
            list(range(20)),
        )

        media = exporter.MediaIndex(ROOT / "amiga" / "media")
        vector_counts = []
        for asset_id in range(exporter.VECTOR_COUNT):
            source = exporter.field(
                game_link, layout.vector_names + asset_id * 64, 64
            )
            if not source:
                break
            vector_counts.append(
                len(
                    exporter.vector_material_keys(
                        media.resolve_volume(source).read_bytes()
                    )
                )
            )
        self.assertEqual(len(vector_counts), 22)
        self.assertEqual(sum(vector_counts), 625)

    def test_representative_source_images_and_default_channels(self) -> None:
        media = exporter.MediaIndex(ROOT / "amiga" / "media")
        palette = exporter.display_palette(media.require("includes/256pal"))
        wall = exporter.wall_image(
            media.require("wallinc/stonewall.256wad"), palette
        )
        self.assertEqual(wall.size, (96, 128))
        floor = exporter.floor_image(
            media.require("includes/floortile").read_bytes(),
            media.require("includes/newtexturemaps.pal").read_bytes(),
            palette,
            0x0101,
        )
        self.assertEqual(floor.size, (64, 64))
        channels = exporter.default_channels(floor)
        self.assertEqual(set(channels), set(exporter.CHANNELS))
        self.assertEqual(channels["normal"].getpixel((0, 0))[:3], (128, 128, 255))
        self.assertEqual(channels["metalness"].getpixel((0, 0))[:3], (0, 0, 0))
        self.assertEqual(channels["roughness"].getpixel((0, 0))[:3], (255, 255, 255))
        self.assertEqual(channels["emissive"].getpixel((0, 0))[:3], (0, 0, 0))

        vector_channels = exporter.default_channels(
            floor, roughness_unorm=exporter.VECTOR_ROUGHNESS_UNORM
        )
        self.assertEqual(
            vector_channels["roughness"].getpixel((0, 0))[:3],
            (184, 184, 184),
        )

        authored_spec = json.loads(
            (ROOT / "data" / "renderer_dxr" / "material_sources.json").read_text(
                encoding="utf-8"
            )
        )
        hullmetal = next(
            entry
            for entry in authored_spec["materials"]
            if entry["name"] == "hullmetal"
        )
        authored_channels = exporter.authored_sheet_channels(
            ROOT / "textures_pbr", hullmetal
        )
        self.assertEqual(
            {image.size for image in authored_channels.values()}, {(754, 374)}
        )
        world_channels = exporter.resize_world_channels(
            authored_channels, tuple(hullmetal["source_texture_size"])
        )
        self.assertEqual(
            {image.size for image in world_channels.values()}, {(1032, 512)}
        )
        self.assertGreaterEqual(
            world_channels["normal"].getchannel("B").getextrema()[0], 96
        )

if __name__ == "__main__":
    unittest.main()
