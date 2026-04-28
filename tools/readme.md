# AB3D2 Maps to Quake 2

This directory contains the AB3D2 to Quake 2 map conversion tool:

```text
tools/ab3d_levels_to_quake.py
```

The target is Alien Breed 3D 2. AB3D1 source or ports are reference material only for similar formats and algorithms; do not use AB3D1 assets for AB3D2 map exports.

## What It Does

The converter reads AB3D2 level pairs:

```text
twolev.bin
twolev.graph.bin
```

It writes Quake 2 `.map` files containing brush geometry, entities, and Quake 2 face attributes. The default output uses shell brushes around AB3D2 room space so the result opens as editable Quake-style solids in TrenchBroom.

Floor and roof slabs are built from the AB3D2 graph flat polygons when available. Floor slabs extend down to the nearest lower joined floor where possible, so raised platforms become slab surfaces plus risers instead of caps surrounded by wall strips. Ceiling slabs similarly extend up to the nearest higher joined roof where possible, so overhead steps become continuous solid brushes instead of thin split caps. Remaining wall horizontal faces use stable floor/ceiling fallback materials so longer wall pieces can merge.

Generated shell brushes are merged when adjacent pieces share the same role, height, and materials and the combined footprint stays convex. Concave shapes are kept split into safe Quake brushes instead of being forced into invalid diagonal hulls. Wall shell thickness is placed from the source polygon winding, which keeps it outside concave AB3D2 sectors and doorways.

The default output also emits Quake `light` entities from AB3D2 zone brightness and point/corner brightness tables. This gives BSP compilers data to bake a lightmap approximation of the original Gouraud shading while keeping the `.map` editable. Use `--lighting zone` for room-center lights only, or `--lighting none` for unlit geometry.

It also extracts AB3D2 wall textures from:

```text
media/wallinc/*.256wad
media/includes/256pal
```

and floor/roof textures from:

```text
media/includes/floortile
media/includes/newtexturemaps.pal
media/includes/256pal
```

and writes Quake 2 WAL textures under:

```text
build/quake2_assets/baseq2/textures/ab3d2
```

Texture names in the generated maps use paths such as:

```text
ab3d2/hullmetal
ab3d2/technotritile
ab3d2/floor_0001
ab3d2/floor_0201
```

## Convert All Demo Levels

From the repository root:

```powershell
python tools\ab3d_levels_to_quake.py --extract-textures --verbose
```

Outputs:

```text
build/quake2_maps/*.map
build/quake2_assets/baseq2/textures/ab3d2/*.wal
build/quake2_assets/baseq2/pics/colormap.pcx
build/quake2_assets/ab3d2_textures.wad
```

The floor material suffix is the AB3D2 floor atlas byte offset from the graph flat record, written as four hex digits.

## Convert One Level

```powershell
python tools\ab3d_levels_to_quake.py --match LEVEL_A --extract-textures --verbose
```

The match is case-insensitive and compares against level directory names.

## TrenchBroom

The local TrenchBroom release is expected at:

```text
TrenchBroom-Win64-AMD64-v2025.4-Release/TrenchBroom.exe
```

To copy generated textures and placeholder editor models into that local TrenchBroom install, and configure TrenchBroom's Quake 2 game path:

```powershell
python tools\ab3d_levels_to_quake.py --extract-textures --install-trenchbroom-assets --verbose
```

To launch a smoke test:

```powershell
python tools\ab3d_levels_to_quake.py --match LEVEL_A --extract-textures --install-trenchbroom-assets --check-trenchbroom --trenchbroom-wait 3 --verbose
```

The configured Quake 2 game path should be:

```text
build/quake2_assets
```

## BSP Compilation

The converter writes `.map` files. It can call an external Quake 2 BSP compiler if one is available:

```powershell
python tools\ab3d_levels_to_quake.py --extract-textures --compile-bsp --qbsp C:\tools\qbsp3.exe
```

If no compiler is provided or found on `PATH`, keep using the generated `.map` files directly in TrenchBroom.

## Tests

```powershell
python -m unittest tools.test_ab3d_levels_to_quake
```

The tests cover geometry merging, lower/upper room spans, sky-open ceilings, mitered wall overlaps, texture preservation, and lighting table export.

## Generated Files

Generated maps, WAL textures, probe images, and local TrenchBroom installs are ignored by git. The source files intended for commit are the converter script and its markdown notes.

More implementation detail is in:

```text
tools/ab3d_levels_to_quake.md
```
