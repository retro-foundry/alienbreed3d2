import importlib.util
import dataclasses
import pathlib
import struct
import sys
import unittest


SCRIPT = pathlib.Path(__file__).with_name("ab3d_levels_to_quake.py")
SPEC = importlib.util.spec_from_file_location("ab3d_levels_to_quake_under_test", SCRIPT)
assert SPEC is not None
assert SPEC.loader is not None
ab3d = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ab3d
SPEC.loader.exec_module(ab3d)


def rect(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def bounds(poly):
    xs = [p[0] for p in poly]
    ys = [p[1] for p in poly]
    return min(xs), min(ys), max(xs), max(ys)


def area_sum(polys):
    return sum(abs(ab3d.polygon_area(poly)) for poly in polys)


def prism(poly, role="floor", z0=0.0, z1=8.0, texture="ab3d2/floor_0001"):
    side = "ab3d2/hullmetal"
    return ab3d.PrismBrush(
        poly=poly,
        z0=z0,
        z1=z1,
        texture=texture,
        top_texture=texture,
        bottom_texture=texture,
        side_texture=side,
        role=role,
    )


def header(num_zones=1):
    return ab3d.LevelHeader(
        variant="ab3d2",
        offset_endian=">",
        header_offset=0,
        plr1_x=0,
        plr1_z=0,
        plr1_zone=0,
        num_control_points=0,
        num_points=0,
        num_zones=num_zones,
        num_objects=0,
        points_offset=0,
        floorline_offset=0,
        object_data_offset=0,
        shot_data_offset=0,
        alien_shot_data_offset=0,
        object_points_offset=0,
        plr1_object_offset=0,
        plr2_object_offset=0,
        score=0,
    )


def graph_with_streams(lower_stream, upper_stream):
    table_off = 20
    lower_off = table_off + 8
    upper_off = lower_off + len(lower_stream)
    graph = bytearray(upper_off + len(upper_stream))
    struct.pack_into(">I", graph, 12, table_off)
    struct.pack_into(">II", graph, table_off, lower_off, upper_off)
    graph[lower_off : lower_off + len(lower_stream)] = lower_stream
    graph[upper_off : upper_off + len(upper_stream)] = upper_stream
    return bytes(graph)


def flat_stream(zone_id, command_type, y_word, tile_offset, point_ids):
    sides_minus_one = len(point_ids) - 1
    record_len = 16 + sides_minus_one * 2
    record = bytearray(record_len)
    struct.pack_into(">Hhh", record, 0, command_type, y_word, sides_minus_one)
    for i, point_id in enumerate(point_ids):
        struct.pack_into(">H", record, 6 + i * 2, point_id)
    tile_off = 2 * (sides_minus_one + 6)
    struct.pack_into(">H", record, tile_off, tile_offset)
    return struct.pack(">h", zone_id) + bytes(record) + struct.pack(">H", 0x80)


def wall_stream(zone_id, left, right, texture_id, top, bottom):
    record = bytearray(30)
    struct.pack_into(">Hhh", record, 0, 0, left, right)
    struct.pack_into(">h", record, 14, texture_id)
    struct.pack_into(">ii", record, 20, top, bottom)
    return struct.pack(">h", zone_id) + bytes(record) + struct.pack(">H", 0x80)


class MapGeometryMergeTests(unittest.TestCase):
    def test_parse_zones_reads_draw_backdrop_flag(self):
        data = bytearray(50)
        struct.pack_into(">hiiii", data, 0, 7, 0, -512, 0, 0)
        struct.pack_into(">hh", data, 22, -7, 5)
        struct.pack_into(">h", data, 32, 48)
        data[36] = 255
        struct.pack_into(">hh", data, 44, 3, 9)
        struct.pack_into(">h", data, 48, -1)

        zones = ab3d.parse_zones(bytes(data), [0])

        self.assertEqual(zones[0].draw_backdrop, 255)
        self.assertEqual(zones[0].brightness, -7)
        self.assertEqual(zones[0].upper_brightness, 5)
        self.assertEqual(zones[0].floor_noise, 3)
        self.assertEqual(zones[0].upper_floor_noise, 9)

    def test_parse_level_point_brightness_tables(self):
        h = dataclasses.replace(header(num_zones=2), points_offset=100, num_points=3)
        start = ab3d.point_brightness_table_offset(h)
        border_start = start + h.num_zones * 40 * 2
        data = bytearray(border_start + h.num_zones * 10 * 2)
        struct.pack_into(">h", data, start + 0, -15)
        struct.pack_into(">h", data, start + 80 + 4, 12)
        struct.pack_into(">hhhh", data, border_start, 0, 2, -1, 99)
        struct.pack_into(">hhhh", data, border_start + 20, 1, -1, 99, 99)

        brightnesses = ab3d.parse_point_brightnesses(bytes(data), h)
        border_points = ab3d.parse_zone_border_points(bytes(data), h)

        self.assertEqual(brightnesses[0][0], -15)
        self.assertEqual(brightnesses[1][2], 12)
        self.assertEqual(border_points[0], [0, 2])
        self.assertEqual(border_points[1], [1])

    def test_simple_rectangular_room_floor_merges_to_one_brush(self):
        prisms = [
            prism(rect(0, 0, 64, 64)),
            prism(rect(64, 0, 128, 64)),
            prism(rect(0, 64, 64, 128)),
            prism(rect(64, 64, 128, 128)),
        ]
        stats = ab3d.MergeStats()
        merged = ab3d.merge_prism_brushes(prisms, stats)

        self.assertEqual(len(merged), 1)
        self.assertEqual(bounds(merged[0].poly), (0, 0, 128, 128))
        self.assertEqual(stats.raw_floor_regions, 4)
        self.assertEqual(stats.merged_floor_regions, 1)

    def test_long_corridor_floor_merges_to_one_long_brush(self):
        prisms = [prism(rect(x, 0, x + 64, 64)) for x in range(0, 512, 64)]
        merged = ab3d.merge_prism_brushes(prisms)

        self.assertEqual(len(merged), 1)
        self.assertEqual(bounds(merged[0].poly), (0, 0, 512, 64))

    def test_l_shaped_room_stays_split_into_convex_brushes(self):
        prisms = [
            prism(rect(0, 0, 64, 128)),
            prism(rect(64, 0, 128, 64)),
        ]
        merged = ab3d.merge_prism_brushes(prisms)

        self.assertEqual(len(merged), 2)
        self.assertEqual(area_sum([p.poly for p in merged]), 64 * 128 + 64 * 64)

    def test_multiple_adjacent_rooms_merge_when_coplanar_and_same_material(self):
        prisms = [
            prism(rect(0, 0, 128, 128)),
            prism(rect(128, 0, 256, 128)),
        ]
        merged = ab3d.merge_prism_brushes(prisms)

        self.assertEqual(len(merged), 1)
        self.assertEqual(bounds(merged[0].poly), (0, 0, 256, 128))

    def test_doorway_gap_does_not_get_bridged(self):
        prisms = [
            prism(rect(0, -8, 64, 0), role="wall"),
            prism(rect(128, -8, 192, 0), role="wall"),
        ]
        merged = ab3d.merge_prism_brushes(prisms)

        self.assertEqual(len(merged), 2)
        self.assertEqual(area_sum([p.poly for p in merged]), 2 * 64 * 8)

    def test_straight_wall_run_merges_to_one_brush(self):
        prisms = [prism(rect(x, -8, x + 64, 0), role="wall") for x in range(0, 256, 64)]
        stats = ab3d.MergeStats()
        merged = ab3d.merge_prism_brushes(prisms, stats)

        self.assertEqual(len(merged), 1)
        self.assertEqual(bounds(merged[0].poly), (0, -8, 256, 0))
        self.assertEqual(stats.raw_wall_runs, 4)
        self.assertEqual(stats.merged_wall_runs, 1)

    def test_wall_shell_uses_winding_not_centroid_for_outside_direction(self):
        ccw_wall = ab3d.wall_poly_for_segment((0, 0), (64, 0), zone_clockwise=False, thickness=8)
        cw_wall = ab3d.wall_poly_for_segment((64, 0), (0, 0), zone_clockwise=True, thickness=8)

        self.assertEqual(bounds(ccw_wall), (0, -8, 64, 0))
        self.assertEqual(bounds(cw_wall), (0, -8, 64, 0))

    def test_single_material_ceiling_flat_uses_zone_footprint(self):
        spans = [
            ab3d.FlatTextureSpan(2, "ab3d2/floor_0201", 128.0, [(0, 0), (128, 0), (96, 64)]),
            ab3d.FlatTextureSpan(2, "ab3d2/floor_0201", 128.0, [(0, 0), (96, 64), (0, 64)]),
        ]
        caps = ab3d.flat_cap_polygons(spans, 2, 128.0, rect(0, 0, 128, 64), "fallback")

        self.assertEqual(len(caps), 1)
        self.assertEqual(caps[0][1], "ab3d2/floor_0201")
        self.assertEqual(bounds(caps[0][0]), (0, 0, 128, 64))

    def test_preferred_zone_footprint_ignores_mixed_ceiling_draw_materials(self):
        spans = [
            ab3d.FlatTextureSpan(2, "ab3d2/floor_0201", 128.0, [(0, 0), (64, 0), (64, 64), (0, 64)]),
            ab3d.FlatTextureSpan(2, "ab3d2/floor_0202", 128.0, [(64, 0), (128, 0), (128, 64), (64, 64)]),
        ]
        caps = ab3d.flat_cap_polygons(
            spans,
            2,
            128.0,
            rect(0, 0, 128, 64),
            "ab3d2/floor_0201",
            prefer_zone_footprint=True,
        )

        self.assertEqual(len(caps), 1)
        self.assertEqual(caps[0][1], "ab3d2/floor_0201")
        self.assertEqual(bounds(caps[0][0]), (0, 0, 128, 64))

    def test_ceiling_cap_extends_to_higher_neighbour_like_floor_caps(self):
        zone = ab3d.Zone(zone_id=0, floor=0, roof=-512, upper_floor=0, upper_roof=0, edge_ids=[0])
        neighbour = ab3d.Zone(zone_id=1, floor=0, roof=-1024, upper_floor=0, upper_roof=0, edge_ids=[])
        edge = ab3d.Edge(x=0, z=0, dx=64, dz=0, join_zone=1, flags=0)
        prisms = ab3d.zone_shell_prisms(
            zone,
            {0: zone, 1: neighbour},
            rect(0, 0, 64, 64),
            [edge],
            scale_xy=1.0,
            scale_z=1.0,
            wall_texture="ab3d2/hullmetal",
            floor_texture="ab3d2/floor_0001",
            ceiling_texture="ab3d2/floor_0201",
            wall_textures_by_zone={},
            flat_textures_by_zone={},
            map_format="quake2",
            thickness=8.0,
            cap_thickness=1.0,
        )
        ceilings = [prism for prism in prisms if prism.role == "ceiling"]

        self.assertEqual(len(ceilings), 1)
        self.assertEqual((ceilings[0].z0, ceilings[0].z1), (8.0, 16.0))

    def test_neighbour_wall_strip_is_removed_where_ceiling_cap_extends(self):
        low_zone = ab3d.Zone(zone_id=0, floor=0, roof=-512, upper_floor=0, upper_roof=0, edge_ids=[0])
        high_zone = ab3d.Zone(zone_id=1, floor=0, roof=-1024, upper_floor=0, upper_roof=0, edge_ids=[1])
        edges = [
            ab3d.Edge(x=0, z=0, dx=64, dz=0, join_zone=1, flags=0),
            ab3d.Edge(x=64, z=0, dx=-64, dz=0, join_zone=0, flags=0),
        ]
        prisms = ab3d.zone_shell_prisms(
            high_zone,
            {0: low_zone, 1: high_zone},
            rect(0, 0, 64, 64),
            edges,
            scale_xy=1.0,
            scale_z=1.0,
            wall_texture="ab3d2/hullmetal",
            floor_texture="ab3d2/floor_0001",
            ceiling_texture="ab3d2/floor_0201",
            wall_textures_by_zone={},
            flat_textures_by_zone={},
            map_format="quake2",
            thickness=8.0,
            cap_thickness=1.0,
        )
        wall_spans = [(prism.z0, prism.z1) for prism in prisms if prism.role == "wall"]

        self.assertNotIn((8.0, 16.0), wall_spans)

    def test_touching_corner_shells_are_not_mitered(self):
        blocker = prism(rect(48, -32, 128, -16), role="wall")
        short_wall = prism(rect(0, -16, 64, 0), role="wall")

        mitered = ab3d.miter_overlapping_shell_prisms([blocker, short_wall], amount=16.0)

        self.assertEqual(bounds(mitered[0].poly), (48, -32, 128, -16))
        self.assertEqual(mitered[1].poly, short_wall.poly)

    def test_overlapping_corner_walls_share_one_grid_miter(self):
        horizontal = prism(rect(0, -16, 64, 0), role="wall")
        vertical = prism(rect(48, -16, 64, 64), role="wall")
        horizontal.inward_normal = (0.0, 1.0)
        vertical.inward_normal = (-1.0, 0.0)

        mitered = ab3d.miter_overlapping_shell_prisms([horizontal, vertical], amount=16.0)

        self.assertEqual(mitered[0].poly, [(0, -16), (64, -16), (48.0, 0.0), (0, 0)])
        self.assertEqual(mitered[1].poly, [(48.0, 0.0), (64.0, -16.0), (64, 64), (48, 64)])

    def test_merged_perpendicular_corner_walls_keep_normals_for_miter(self):
        horizontal = prism(rect(0, -16, 64, 0), role="wall")
        vertical = prism(rect(48, -16, 64, 64), role="wall")
        horizontal.inward_normal = (0.0, 1.0)
        vertical.inward_normal = (-1.0, 0.0)

        merged = ab3d.merge_prism_brushes([horizontal, vertical])
        mitered = ab3d.miter_overlapping_shell_prisms(merged, amount=16.0)

        self.assertEqual([p.inward_normal for p in merged], [(0.0, 1.0), (-1.0, 0.0)])
        self.assertEqual(mitered[0].poly, [(0, -16), (64, -16), (48.0, 0.0), (0, 0)])
        self.assertEqual(mitered[1].poly, [(48.0, 0.0), (64.0, -16.0), (64, 64), (48, 64)])

    def test_wall_can_be_mitered_at_both_ends(self):
        horizontal = prism(rect(0, -16, 128, 0), role="wall")
        left = prism(rect(0, -64, 16, 0), role="wall")
        right = prism(rect(112, -16, 128, 64), role="wall")
        horizontal.inward_normal = (0.0, 1.0)
        left.inward_normal = (1.0, 0.0)
        right.inward_normal = (-1.0, 0.0)

        mitered = ab3d.miter_overlapping_shell_prisms([horizontal, left, right], amount=16.0)

        self.assertEqual(mitered[0].poly, [(112.0, 0.0), (0.0, 0.0), (16.0, -16.0), (128, -16)])
        self.assertEqual(mitered[1].poly, [(0, -64), (16, -64), (16.0, -16.0), (0.0, 0.0)])
        self.assertEqual(mitered[2].poly, [(112.0, 0.0), (128.0, -16.0), (128, 64), (112, 64)])

    def test_shorter_vertical_wall_strip_is_mitered_inside(self):
        tall_wall = prism(rect(0, -16, 128, 0), role="wall", z0=0, z1=256)
        short_wall = prism(rect(0, -16, 128, 0), role="wall", z0=0, z1=64)
        tall_wall.wall_length = 128
        short_wall.wall_length = 128
        tall_wall.inward_normal = (0.0, 1.0)
        short_wall.inward_normal = (0.0, 1.0)

        mitered = ab3d.miter_overlapping_shell_prisms([tall_wall, short_wall], amount=1.0)

        self.assertEqual(bounds(mitered[0].poly), (0, -16, 128, 0))
        self.assertNotEqual(mitered[1].poly, short_wall.poly)

    def test_extended_ceiling_cap_side_uses_replaced_neighbour_wall_texture(self):
        low_zone = ab3d.Zone(zone_id=0, floor=0, roof=-512, upper_floor=0, upper_roof=0, edge_ids=[0])
        high_zone = ab3d.Zone(zone_id=1, floor=0, roof=-1024, upper_floor=0, upper_roof=0, edge_ids=[1])
        edges = [
            ab3d.Edge(x=0, z=0, dx=64, dz=0, join_zone=1, flags=0),
            ab3d.Edge(x=64, z=0, dx=-64, dz=0, join_zone=0, flags=0),
        ]
        wall_key = ab3d.raw_segment_key((0, 0), (64, 0))
        prisms = ab3d.zone_shell_prisms(
            low_zone,
            {0: low_zone, 1: high_zone},
            rect(0, 0, 64, 64),
            edges,
            scale_xy=1.0,
            scale_z=1.0,
            wall_texture="ab3d2/hullmetal",
            floor_texture="ab3d2/floor_0001",
            ceiling_texture="ab3d2/floor_0201",
            wall_textures_by_zone={
                1: {
                    wall_key: [
                        ab3d.WallTextureSpan(
                            texture_id=9,
                            material="ab3d2/replaced_neighbour_wall",
                            low=8.0,
                            high=16.0,
                        )
                    ]
                }
            },
            flat_textures_by_zone={},
            map_format="quake2",
            thickness=8.0,
            cap_thickness=1.0,
        )
        ceiling = next(prism for prism in prisms if prism.role == "ceiling")

        self.assertIn("ab3d2/replaced_neighbour_wall", ceiling.side_textures)

    def test_cap_side_texture_uses_partial_overlapping_wall_segment(self):
        zone = ab3d.Zone(zone_id=0, floor=0, roof=-512, upper_floor=0, upper_roof=0, edge_ids=[0])
        edge = ab3d.Edge(x=0, z=0, dx=128, dz=0, join_zone=-1, flags=0)
        side_textures = ab3d.cap_side_textures(
            [(32, 0), (96, 0), (96, 32), (32, 32)],
            [(edge, (0, 0), (128, 0))],
            {
                ab3d.raw_segment_key((0, 0), (128, 0)): [
                    ab3d.WallTextureSpan(
                        texture_id=7,
                        material="ab3d2/partial_wall",
                        low=8.0,
                        high=16.0,
                    )
                ]
            },
            {0: zone},
            {},
            8.0,
            16.0,
            "ab3d2/hullmetal",
        )

        self.assertEqual(side_textures[0], "ab3d2/partial_wall")

    def test_side_textured_prisms_do_not_merge_and_lose_face_materials(self):
        prisms = [
            prism(rect(0, 0, 64, 64), role="ceiling"),
            prism(rect(64, 0, 128, 64), role="ceiling"),
        ]
        prisms[0].side_textures = ("a", "b", "c", "d")
        prisms[1].side_textures = ("a", "b", "c", "d")
        merged = ab3d.merge_prism_brushes(prisms)

        self.assertEqual(len(merged), 2)
        self.assertEqual(merged[0].side_textures, ("a", "b", "c", "d"))
        self.assertEqual(merged[1].side_textures, ("a", "b", "c", "d"))

    def test_zone_room_spans_include_lower_and_upper_rooms(self):
        zone = ab3d.Zone(zone_id=0, floor=0, roof=-512, upper_floor=-1024, upper_roof=-1536, edge_ids=[0])
        spans = ab3d.zone_room_spans(zone, scale_z=1.0)

        self.assertEqual(spans, [(0.0, 8.0), (16.0, 24.0)])

    def test_shell_prisms_are_created_for_lower_and_upper_room_spans(self):
        zone = ab3d.Zone(zone_id=0, floor=0, roof=-512, upper_floor=-1024, upper_roof=-1536, edge_ids=[])
        prisms = ab3d.zone_shell_prisms(
            zone,
            {0: zone},
            rect(0, 0, 64, 64),
            [],
            scale_xy=1.0,
            scale_z=1.0,
            wall_texture="ab3d2/hullmetal",
            floor_texture="ab3d2/floor_0001",
            ceiling_texture="ab3d2/floor_0201",
            wall_textures_by_zone={},
            flat_textures_by_zone={},
            map_format="quake2",
            thickness=8.0,
            cap_thickness=1.0,
        )

        self.assertEqual([(p.role, p.z0, p.z1) for p in prisms], [
            ("floor", -1.0, 0.0),
            ("ceiling", 8.0, 9.0),
            ("floor", 15.0, 16.0),
            ("ceiling", 24.0, 25.0),
        ])

    def test_lighting_entities_use_zone_and_point_brightness(self):
        zone = ab3d.Zone(
            zone_id=0,
            floor=0,
            roof=-512,
            upper_floor=-1024,
            upper_roof=-1536,
            edge_ids=[0, 1, 2, 3],
            brightness=-5,
            upper_brightness=5,
        )
        edges = [
            ab3d.Edge(x=0, z=0, dx=64, dz=0, join_zone=-1, flags=0),
            ab3d.Edge(x=64, z=0, dx=0, dz=64, join_zone=-1, flags=0),
            ab3d.Edge(x=64, z=64, dx=-64, dz=0, join_zone=-1, flags=0),
            ab3d.Edge(x=0, z=64, dx=0, dz=-64, join_zone=-1, flags=0),
        ]
        entries = [0] * 40
        entries[0] = 10
        entries[1] = -10
        entries[2] = 20
        lights = ab3d.build_light_entities(
            [zone],
            edges,
            [(0, 0)],
            {0: entries},
            {0: [0]},
            scale_xy=1.0,
            scale_z=1.0,
            mode="points",
            zone_light_base=180.0,
            zone_light_scale=8.0,
            point_light_scale=8.0,
        )
        by_origin = {
            tuple(round(coord, 3) for coord in light.origin): light.intensity
            for light in lights
        }

        self.assertEqual(by_origin[(32.0, 32.0, 4.0)], 140)
        self.assertEqual(by_origin[(32.0, 32.0, 20.0)], 220)
        self.assertEqual(by_origin[(0.0, 0.0, 2.0)], 80)
        self.assertEqual(by_origin[(0.0, 0.0, 6.0)], 80)
        self.assertEqual(by_origin[(0.0, 0.0, 18.0)], 160)

    def test_backdrop_zone_skips_only_topmost_ceiling_cap(self):
        zone = ab3d.Zone(
            zone_id=0,
            floor=0,
            roof=-512,
            upper_floor=-1024,
            upper_roof=-1536,
            edge_ids=[],
            draw_backdrop=255,
        )
        prisms = ab3d.zone_shell_prisms(
            zone,
            {0: zone},
            rect(0, 0, 64, 64),
            [],
            scale_xy=1.0,
            scale_z=1.0,
            wall_texture="ab3d2/hullmetal",
            floor_texture="ab3d2/floor_0001",
            ceiling_texture="ab3d2/floor_0201",
            wall_textures_by_zone={},
            flat_textures_by_zone={},
            map_format="quake2",
            thickness=8.0,
            cap_thickness=1.0,
        )

        self.assertEqual([(p.role, p.z0, p.z1) for p in prisms], [
            ("floor", -1.0, 0.0),
            ("ceiling", 8.0, 9.0),
            ("floor", 15.0, 16.0),
        ])

    def test_backdrop_ceiling_does_not_extend_into_neighbour_blockers(self):
        zone = ab3d.Zone(zone_id=0, floor=0, roof=-512, upper_floor=0, upper_roof=0, edge_ids=[0], draw_backdrop=255)
        neighbour = ab3d.Zone(zone_id=1, floor=0, roof=-1024, upper_floor=0, upper_roof=0, edge_ids=[])
        edge = ab3d.Edge(x=0, z=0, dx=64, dz=0, join_zone=1, flags=0)

        extents = ab3d.zone_cap_extents(
            zone,
            {0: zone, 1: neighbour},
            [(edge, (0, 0), (64, 0))],
            scale_z=1.0,
            cap_thickness=1.0,
        )

        self.assertEqual(extents, [(0.0, 8.0, -1.0, 8.0)])

    def test_graph_flat_parser_reads_lower_and_upper_streams(self):
        points = [(0, 0), (64, 0), (64, 64), (0, 64)]
        graph = graph_with_streams(
            flat_stream(0, 1, 0, 0x0001, [0, 1, 2, 3]),
            flat_stream(0, 2, -24, 0x0201, [0, 1, 2, 3]),
        )
        spans = ab3d.parse_graph_flat_textures(graph, header(), "be", points, scale_xy=1.0, scale_z=1.0)[0]

        self.assertEqual([(span.stream, span.command_type, span.z, span.material) for span in spans], [
            ("lower", 1, 0.0, "ab3d2/floor_0001"),
            ("upper", 2, 24.0, "ab3d2/floor_0201"),
        ])

    def test_graph_wall_parser_reads_lower_and_upper_streams(self):
        points = [(0, 0), (64, 0)]
        graph = graph_with_streams(
            wall_stream(0, 0, 1, 3, 0, -512),
            wall_stream(0, 0, 1, 4, -1024, -1536),
        )
        spans = ab3d.parse_graph_wall_textures(
            graph,
            header(),
            "be",
            points,
            {3: "lower_wall", 4: "upper_wall"},
            "fallback",
            scale_z=1.0,
        )[0][((0, 0), (64, 0))]

        self.assertEqual([(span.stream, span.material, span.low, span.high) for span in spans], [
            ("lower", "lower_wall", 0.0, 8.0),
            ("upper", "upper_wall", 16.0, 24.0),
        ])


if __name__ == "__main__":
    unittest.main()
