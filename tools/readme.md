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

AB3D2 backdrop/open-sky ceilings are still emitted as solid caps, but use the `--sky-texture` material, defaulting to `sky`. Shell-mode maps also get a default outer sky-textured hull so ericw-tools/qbsp sees a sealed world instead of leaks through the original sky/backdrop void. Use `--no-seal-skybox` only when inspecting the raw converted shell geometry.

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

### Q2RTX / Quake 2 BSP Notes

Use ericw-tools 2.0 alpha/dev builds for Q2RTX installs. The older ericw-tools 0.18.x stable release writes Quake 1 BSP files by default; Q2RTX rejects those with `unknown file format`.

For Q2RTX, compile with ericw `qbsp -q2rtx`, then run ericw `vis` and `light` with the same target:

```powershell
build\bin\qbsp.exe -q2rtx -basedir build\q2rtx_ericw build\q2rtx_ericw\maps\level_a.map build\q2rtx_ericw\maps\level_a.bsp
build\bin\vis.exe -q2rtx -basedir build\q2rtx_ericw build\q2rtx_ericw\maps\level_a.bsp
build\bin\light.exe -q2rtx -basedir build\q2rtx_ericw build\q2rtx_ericw\maps\level_a.bsp
```

The `-basedir` directory must look like a Quake 2 `baseq2` tree, including `maps\`, `textures\ab3d2\`, `textures\sky.wal`, and `pics\colormap.pcx`, so the tools can read WAL metadata and surface/content flags. `qbsp -q2rtx` writes normal Quake 2 `IBSP` version 38 files and avoids the invalid edge data seen from `q2tools-220` on these converted maps. If `-q2rtx` gives trouble, try `-q2bsp`; do not use `q2tools-220` for final Q2RTX builds unless its output has been checked for out-of-range edge vertex indices.

The Q2RTX install target used for local testing is:

```text
C:\Program Files (x86)\Steam\steamapps\common\Quake II RTX\baseq2
```

Install compiled BSPs under `baseq2\maps\` and generated WAL textures under `baseq2\textures\ab3d2\`. The compiler/runtime also needs the generated `pics\colormap.pcx`.

### Q1RTX / Quake 1 BSP Notes

Do not load Quake 2 `IBSP` files in `q1rtx.exe`. A Quake 2 BSP may fail in Q1-family engines with errors such as:

```text
couldnt load maps/level_a.bsp: BSP_LoadEdges: bad vertnum
```

For Q1RTX or other Quake 1-family engines, generate Quake 1 map syntax and compile with the ericw-tools `qbsp.exe` from `build\bin`:

```powershell
python tools\ab3d_levels_to_quake.py `
  --map-format quake1 `
  --wad build\quake2_assets\ab3d2_textures.wad `
  --out-dir build\quake1_maps `
  --verbose

build\bin\qbsp.exe build\quake1_maps\level_a.map build\quake1_bsp\level_a.bsp
```

Those Quake 1 BSPs are for Q1RTX-style engines only. Do not copy them into Q2RTX, because Q2RTX expects Quake 2 `IBSP` version 38 BSPs.

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
