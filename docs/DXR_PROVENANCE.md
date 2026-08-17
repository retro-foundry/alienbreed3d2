# DXR renderer provenance

This record applies to the clean-room Windows D3D12/DXR renderer introduced by
Phase 2 of `DXR_RAY_RECONSTRUCTION_PLAN.md` and its renderer-native material
build begun in Phase 4.

## Project-authored implementation

The files under `src/renderer_dxr/`, `src/renderer_rtx.cpp`, and
`tests/renderer_rtx_foundation_test.c` were written for this repository from
the clean baseline. The diagnostic HLSL is project-authored. No source,
shader, generated table, binary, scene data, or asset was imported from
Q2RTX, a removed renderer, or repository history before clean baseline
`86241dd`.

The Phase 2 build produces DXIL in the build tree and stages only the two
project-built diagnostic shader objects beside enabled executables. It does
not include, link, discover, load, or stage NVIDIA Streamline or NGX files.

The Phase 4 material build reads only the committed project-authored
`textures_pbr/*.png` sheets and `data/renderer_dxr/material_sources.json`.
`tools/build_dxr_materials.py` independently extracts conventional, separate
base-color, normal, metalness, and roughness RGB textures. It records source,
pixel, and output hashes in a renderer-native manifest; it neither invokes nor
consumes the prohibited Q2 package builder or its output, channel packing,
material files, names, or conventions.

The authoritative `shared_wall` IDs come from the wall texture load order in
`amiga/ab3d2_source/modules/res.s:Res_LoadWallTextures`, as published by
`game_bootstrap_make_wall_surface` in `SceneMaterial.source_asset_id`.
Authored sheets without a demonstrated runtime binding remain unbound. Source
wall IDs 0 and 12 deliberately use the plan's visible fallback until matching
authored PBR entries exist. No emissive intensity or metalness is inferred
from image brightness.

`src/scene_geometry_compile.c` is a project-authored extraction of the native
port's current coordinate interpretation and polygon triangulation. Its
coordinate evidence remains `modules/transform.s:RotateLevelPts`,
`hires.s:Draw_Flats`, and the `SceneWorldPoint`/`SceneGeometry` producer
contract. OpenGL now calls this renderer-neutral implementation; no geometry
rule came from a removed renderer or generated Q2 scene.

## Approved conceptual references inspected

- `binaryfoundry/dxr-demo`, commit
  `d08175a58e2737eb87b3cb81eb55416e3fa89ee9`, MIT, Copyright 2019 Paul
  Alexander Welch. Files inspected: `src/sdl/Main.cpp`,
  `src/d3d12/Renderer.{hpp,cpp}`, `Context.{hpp,cpp}`, and
  `Frame.{hpp,cpp}`. Only the documented SDL-to-`HWND`, command queue,
  flip-discard swap-chain, per-frame allocator/fence, resource-transition,
  and frame-submission concepts were used. No source was copied or adapted;
  the AB3D2 implementation independently adds explicit adapter selection,
  DXR-tier validation, HRESULT context, DRED, named objects, bounded frame
  reuse, debug-message failure, and deterministic resize/shutdown behavior.
- `binaryfoundry/fisica-rt`, commit
  `1784cba270676b8c49f85a9022041dc98528ab54`, MIT, Copyright 2019 Paul
  Alexander Welch. The plan-listed geometry, camera, noise, environment, and
  ray-tracing pipeline files were inspected. No Phase 2 code, shader, table,
  or algorithm was copied or adapted from this reference.

Because no substantial reference implementation was copied, no third-party
source file or licence text is embedded in the Phase 2 source set.

## Toolchain evidence

The validated Windows build used the Windows SDK DXC `1.8.2502.11`; the
compiler executable SHA-256 was
`7C6918A0E2D4E437629FA8549F5CE800970494780F363BBBE1E3D3034F435AEE`.
CMake records the resolved DXC path, version, and hash for every enabled build;
the local path is configuration evidence and is never written to source.
