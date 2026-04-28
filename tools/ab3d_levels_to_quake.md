# AB3D to Quake 2 Conversion

Project rule: this port targets Alien Breed 3D 2. Alien Breed 3D 1 is useful only as an example/reference for similar file structures or algorithms; do not use AB3D1 assets as source data for AB3D2 maps.

`tools/ab3d_levels_to_quake.py` converts AB3D level pairs (`twolev.bin` + `twolev.graph.bin`) into Quake 2 `.map` brush geometry.

It auto-handles:
- raw and `=SB=` packed level files
- AB3D2 header layout
- AB3D1-style mixed layout (best-effort)
- multiple edge record sizes (8/16/32 bytes)
- Quake 2 face attributes (`texture x y rot sx sy contents flags value`)
- shell-style Quake solids around empty AB3D room/portal volumes
- AB3D2 wall texture slots from each level's lower and upper graph streams
- AB3D2 floor and roof polygons plus texture offsets from lower and upper graph flat records

## Quick Start (AB3D2)

```powershell
python tools/ab3d_levels_to_quake.py \
  --levels-root media/demolevels \
  --out-dir build/quake2_maps \
  --extract-textures \
  --q2-root build/quake2_assets \
  --verbose
```

Generated maps are written to `build/quake2_maps/`.

Extracted Quake 2 textures are written to:

- `build/quake2_assets/baseq2/textures/ab3d2/*.wal`
- `build/quake2_assets/baseq2/pics/colormap.pcx`
- `build/quake2_assets/ab3d2_textures.wad` as a WAD2 preview/export

Wall texture decoding now uses the AB3D2 `.256wad` files from `media/wallinc` plus the game palette at `media/includes/256pal`. The first 2048 bytes of each wall file are the 32-level palette remap table, and the remaining data is unpacked as vertical strips of three 5-bit texels per 16-bit word.

Floor and roof texture decoding uses the AB3D2 floor atlas at `media/includes/floortile` plus the floor remap table at `media/includes/newtexturemaps.pal`. Graph flat records select atlas offsets which are exported as materials named like `ab3d2/floor_0001` and `ab3d2/floor_0201`. The converter reads both lower and upper graph streams for each zone, then matches flat and wall records to the relevant room span by height, so zones that contain one room above another export both volumes.

## One Level Only

```powershell
python tools/ab3d_levels_to_quake.py \
  --levels-root media/demolevels \
  --out-dir build/quake2_maps \
  --match LEVEL_A \
  --verbose
```

## TrenchBroom Setup

This repo expects the TrenchBroom release next to the project files:

```text
TrenchBroom-Win64-AMD64-v2025.4-Release/TrenchBroom.exe
```

For Quake 2 texture discovery in TrenchBroom, set the Quake 2 game path to the generated asset root:

```text
build/quake2_assets
```

The expected layout below that root is `baseq2/textures/ab3d2/*.wal`.

The converter can also install the generated WAL files into this local TrenchBroom release's `defaults/assets` directory and set TrenchBroom's Quake 2 game path preference to `build/quake2_assets`. That makes the maps open with textures without needing original Quake 2 assets, and it adds a tiny placeholder `models/monsters/insane/tris.md2` so TrenchBroom's `info_player_start` preview can load.

```powershell
python tools/ab3d_levels_to_quake.py \
  --extract-textures \
  --install-trenchbroom-assets \
  --verbose
```

To smoke-test the first generated map in the local TrenchBroom install:

```powershell
python tools/ab3d_levels_to_quake.py \
  --extract-textures \
  --check-trenchbroom \
  --verbose
```

Use `--trenchbroom <path>` if the release directory is renamed or moved again.

## AB3D1 Path Example

```powershell
python tools/ab3d_levels_to_quake.py \
  --levels-root "C:/Users/paula/Documents/Projects/Alien-Breed-3D-I/build/Release/data/levels" \
  --out-dir build/quake2_maps_ab3d1 \
  --verbose
```

## Optional BSP Compilation

If you have a Quake 2 BSP compiler on PATH:

```powershell
python tools/ab3d_levels_to_quake.py \
  --levels-root media/demolevels \
  --out-dir build/quake2_maps \
  --compile-bsp
```

Or pass an explicit compiler path:

```powershell
python tools/ab3d_levels_to_quake.py \
  --levels-root media/demolevels \
  --out-dir build/quake2_maps \
  --compile-bsp \
  --qbsp "C:/tools/qbsp3.exe"
```

## Notes

- Quake editors typically import/edit `.map` directly.
- Quake 2 maps reference material paths such as `ab3d2/hullmetal` and `ab3d2/floor_0001`; they do not use the Quake 1 `worldspawn` `wad` key.
- Use `--solid-mode volumes` only for inspecting the old filled-sector output. The default `--solid-mode shell` exports floors, ceilings, and walls around empty room space.
- In shell mode, horizontal brush faces are flat-textured and vertical brush faces are wall-textured. Floor slabs extend down to the nearest lower joined floor where possible, so raised platform sides come from the platform slab itself instead of separate wall-top strips. Ceiling slabs similarly extend up to the nearest higher joined roof where possible. Remaining wall brush horizontal faces use stable floor/ceiling fallback materials to allow longer wall pieces to merge.
- Shell brushes are merged conservatively after generation. Only adjacent coplanar prisms with identical heights, roles, and materials are combined, and only when the resulting footprint remains convex, because Quake brushes cannot represent concave or holed solids safely. Wall shell thickness is placed using polygon winding rather than a centroid test, which keeps thickness outside concave rooms and doorways.
- Verbose conversion output reports total brushes/faces, raw and merged shell brush counts, and merged floor, ceiling, and wall region counts.
- `.bsp` output requires an external Quake 2 BSP compiler; this script does not implement BSP tree compilation itself.
- A few zones in some levels may be skipped when their polygons cannot be reconstructed cleanly from source edge lists.

## Tests

```powershell
python -m unittest tools.test_ab3d_levels_to_quake
```

The tests cover simple rectangular floors, long corridors, L-shaped floors, adjacent rooms, doorway gaps, straight wall runs, and the winding-based shell direction used for concave AB3D2 sectors.
