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
amiga/media/wallinc/*.256wad
amiga/media/includes/256pal
```

and floor/roof textures from:

```text
amiga/media/includes/floortile
amiga/media/includes/newtexturemaps.pal
amiga/media/includes/256pal
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

Use the complete Q2RTX pipeline from the repository root:

```powershell
python tools\build_q2rtx.py --install --smoke-test --launch --level level_a
```

This command:

- converts the original AB3D2 Level A--P pairs into editable Quake 2 `.map` files;
- extracts all 34 source wall/floor textures as Quake 2 WAL files;
- repacks the committed renderer-native material catalog into Q2RTX base, normal, roughness, metalness, and explicit emissive TGAs;
- downloads the pinned Windows ericw-tools `2.0.0-alpha11` archive and rejects it unless its SHA-256 is `4e5ea11be2194a1c4acac6d6da9d5b5b9f65324fda2d67efa0731d1fd8e0745f`;
- runs `qbsp`, `vis`, and `light` with `-q2rtx` on all 16 maps;
- rejects BSPs that are not Quake II `IBSP` version 38 or contain invalid lump, vertex, edge, surfedge, face, worldspawn, or player-start data;
- installs the runtime files into the detected local Steam Q2RTX `baseq2` tree;
- loads the selected map, waits until the player enters it, closes the smoke-test process, and launches the playable window.

The clean generated package and its file-hash manifest are written under:

```text
build/q2rtx
```

Use `--q2rtx-root <path>` for a non-Steam runtime or `--ericw-tools <path>` for an already installed alpha/dev compiler trio. `--lighting none` is the default because Q2RTX consumes the two source-proven emissive materials directly. `--lighting zone` and `--lighting points` deliberately add the converter's optional classic light entities.

The Q2RTX install target used for local testing is:

```text
C:\Program Files (x86)\Steam\steamapps\common\Quake II RTX\baseq2
```

The pipeline installs compiled BSPs and map materials under `baseq2\maps\`, generated WAL textures under `baseq2\textures\ab3d2\`, packed PBR textures under `baseq2\overrides\ab3d2\`, and the generated palette and global material file. It does not copy either proprietary Quake II game data or Q2RTX media into this repository.

## Q2RTX PBR Textures

The source sheets in `textures_pbr` are compiled into the committed renderer-native catalog in:

```text
assets/renderer_dxr/materials
```

`tools/build_q2rtx.py` consumes the exact catalog bindings and channels. This covers all 20 floor slots and all 14 extracted wall textures, including source-decoded color with explicit neutral channels where no authored PBR sheet exists. It does not synthesize normals or infer materials during Q2RTX packaging.

Q2RTX receives roughness in base-texture alpha and metalness in normal-texture alpha. The only emitters are the two already proven by the project material catalog:

- `ab3d2/technolights`: factor `900`, matching the converter's `SURF_LIGHT` value;
- `ab3d2/floor_0101`: factor `200`, decoded from the source bright palette row.

Restart Q2RTX after replacing `.mat` or `.tga` files so material definitions and overrides are reloaded.

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
python -m unittest tools.test_ab3d_levels_to_quake tests.test_build_q2rtx
```

The tests cover geometry merging, lower/upper room spans, sky-open ceilings, mitered wall overlaps, texture preservation, lighting table export, the complete 34-material binding, the two exact emitters, Q2RTX alpha-channel packing, compiler metadata WALs, and invalid BSP edge rejection.

## Generated Files

Generated maps, WAL textures, probe images, and local TrenchBroom installs are ignored by git. The source files intended for commit are the converter script and its markdown notes.

More implementation detail is in:

```text
tools/ab3d_levels_to_quake.md
```
