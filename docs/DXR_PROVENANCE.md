# DXR renderer provenance

This record applies to the clean-room Windows D3D12/DXR renderer introduced by
Phase 2 of `DXR_RAY_RECONSTRUCTION_PLAN.md` and its renderer-native material
and raw scene/image increments from Phases 4--7.
On 2026-08-17 the user explicitly authorized a narrow comparison with the
sibling `alienbreed3d2-rtx-renderer` to recover its emissive material behavior;
that exception is recorded below.

## Project-authored implementation

The files under `src/renderer_dxr/`, `src/renderer_rtx.cpp`, and
`tests/renderer_rtx_foundation_test.c` were written for this repository from
the clean baseline. All diagnostic, ray-tracing, and presentation HLSL is
project-authored. No source, shader, generated table, binary, scene data, or
asset was imported from Q2RTX, a removed renderer, or repository history
before clean baseline `86241dd`. The later emissive compatibility comparison
did not import a generated Q2RTX package, renderer binary, shader, or scene.

The enabled build produces DXIL in the build tree and stages only its
project-built diagnostic, ray-tracing, and presentation shader objects beside
enabled executables. It does not include, link, discover, load, or stage NVIDIA
Streamline or NGX files.

The Phase 4 material build reads the committed project-authored
`textures_pbr/*.png` sheets, `data/renderer_dxr/material_sources.json`, and the
authoritative `amiga/media/includes/{floortile,newtexturemaps.pal,256pal}`
source assets. `tools/build_dxr_materials.py` extracts conventional, separate
base-color, normal, metalness, roughness, and emissive RGB textures. It records
source, pixel, and output hashes in a renderer-native manifest; it does not
invoke or consume the old Q2 package builder, its output, packed channels, or
material files.

The authoritative `shared_wall` IDs come from the wall texture load order in
`amiga/ab3d2_source/modules/res.s:Res_LoadWallTextures`, as published by
`game_bootstrap_make_wall_surface` in `SceneMaterial.source_asset_id`.
Authored sheets without a demonstrated runtime binding remain unbound. Source
wall IDs 0 and 12 deliberately use the plan's visible fallback until matching
authored PBR entries exist. The user-authorized comparison demonstrated the
`floor_0201` binding and the two-emitter catalog. Metalness and emissive
intensity are not inferred from brightness at runtime.

The compatibility evidence was
`tools/build_native_rtx_materials.py`,
`tools/build_q2rtx_pbr_from_sheets.py`,
`tools/test_rtx_material_builders.py`, and
`src/renderer_vulkan_rtx_materials.{c,h}` in the sibling renderer. These show
that only PBR `technolights` and source floor tile `0x0101` emit, both with
factor 200. They also define the colored albedo-mask conversion and decode the
floor panel through bright palette row 32. The native DXR builder reproduces
that conversion at build time into explicit hashed emissive textures. The
runtime samples those textures as sRGB data and never derives emission from
base color. `brownspeakers` and `technotritile`, which the earlier scaffold had
guessed were emitters, are explicitly black in the emissive channel.

`src/scene_geometry_compile.c` is a project-authored extraction of the native
port's current coordinate interpretation and polygon triangulation. Its
coordinate evidence remains `modules/transform.s:RotateLevelPts`,
`hires.s:Draw_Flats`, and the `SceneWorldPoint`/`SceneGeometry` producer
contract. OpenGL now calls this renderer-neutral implementation; no geometry
rule came from a removed renderer or generated Q2 scene.

`src/renderer_dxr/dxr_scene.cpp` consumes that shared geometry
and the public `SceneFrame` contract. It decodes source wall data through the
existing project `source_world_material_decode` path, creates a renderer-local
five-channel material atlas, and constructs project-authored vertex/material
buffers and BLAS/TLAS resources. The current ray shader uses a conventional
per-pixel xorshift generator, jittered primary ray, Lambertian/GGX sampling,
explicit emissive/environment next-event sampling, MIS, and an analytic sky
gradient. The renderer-native PBR material package is consumed directly by
this runtime slice.

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
  ray-tracing pipeline files were inspected for conventional concepts only.
  No code, shader, table, constant set, algorithm implementation, or data was
  copied or adapted from this reference.

Because no substantial reference implementation was copied, no third-party
source file or licence text is embedded in the DXR source set.

## Toolchain evidence

The validated Windows build used the Windows SDK DXC `1.8.2502.11`; the
compiler executable SHA-256 was
`7C6918A0E2D4E437629FA8549F5CE800970494780F363BBBE1E3D3034F435AEE`.
CMake records the resolved DXC path, version, and hash for every enabled build;
the local path is configuration evidence and is never written to source.
