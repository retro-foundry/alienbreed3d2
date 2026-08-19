# Clean-room DirectX 12/DXR Renderer with NVIDIA Ray Reconstruction

## Handoff status

This document is the implementation authority for a new Windows RTX renderer for Alien Breed 3D II. It is intended to be sufficient for a fresh context to start at the current clean baseline and implement the renderer without consulting any removed renderer code.

The repository baseline for this work is commit `86241dd` (`Replace GPL RTX renderer with clean-room scaffold`). At that commit:

- The original gameplay port, `SceneFrame` producer, OpenGL renderer, Web build, and tests remain intact.
- `renderer=rtx` selects a deliberately non-functional, fail-fast scaffold in `src/renderer_rtx_stub.c`.
- No functional RTX renderer was present at that baseline.
- The implementation had to replace the stub incrementally while leaving OpenGL and Web behavior unchanged.

Those bullets describe the historical `86241dd` baseline, not the current
tree. A Windows build configured with `AB3D2_ENABLE_DXR=ON` now contains the
working experimental opaque-world DXR path tracer described below; an
`AB3D2_ENABLE_DXR=OFF` build still contains the fail-fast stub by design.
Following explicit user direction on 2026-08-17, the sibling
`alienbreed3d2-rtx-renderer` source was inspected only to recover its emissive
material catalog and mask behavior. Generated Q2RTX package output remains
excluded; the result is rebuilt into the renderer-native material format.

The implementation has reached the Streamline DLSS Ray Reconstruction
milestone. The foundation was validated on a GeForce RTX 4080 in Debug and
Release, and the current opaque PBR path was validated in Debug on a GeForce
RTX 3090, with Windows SDK DXC 1.8.2502.11 (SHA-256
`7C6918A0E2D4E437629FA8549F5CE800970494780F363BBBE1E3D3034F435AEE`). The
Phase 2 D3D12/DXR lifecycle remains the foundation. Phase 4 deterministically
builds the 13 project-authored PBR sheets plus the source `floor_0101` panel
into 70 renderer-native channel textures, a hashed manifest, and a strict
runtime package. Base color, tangent normal, metalness, roughness, and explicit
emissive textures are loaded, atlased, uploaded, and sampled through their
declared color spaces. Missing bindings retain the documented decoded-source
fallback.

The implemented Phase 5/6 slice shares the tested native world-coordinate
conversion and concave X/Z ear clipping with OpenGL, compiles opaque
`SceneFrame` world surfaces, decodes exact source albedo fallbacks, uploads
positions/UVs/material indices and an atlas, and builds default-heap BLAS/TLAS
resources. Stable static world meshes and dynamic door/lift/water meshes use
separate BLAS objects. Topology-stable dynamic frames reuse material atlases,
upload only geometry/emitter buffers through a three-frame upload set, refit
changed dynamic BLAS objects, and update the TLAS without a queue flush. It
deliberately excludes sprites, vector objects, water-specific behavior, the
view weapon, HUD, and text.

Phase 7 now writes one fresh un-denoised `R16G16B16A16_FLOAT` sample per pixel
and presents it with a full-screen tone-map pass. The path integrator evaluates
energy-consistent Lambertian diffuse plus Cook-Torrance GGX specular, samples
GGX visible normals with the matching mixture PDF, follows up to three surface
hits, and performs environment and area-emitter next-event sampling with shadow
rays and power-heuristic MIS. Emissive triangles come only from the explicit
`technolights` and source `floor_0101` emissive textures, scaled to scene-linear
radiance by their manifest factors, and use a global
area-times-average-luminance distribution; source Gouraud/ZoneT lighting is not
consumed. CPU tests cover
the material equations, lobe probability, sampler/PDF agreement, normal
transform, deterministic sample sequence, PDF mass, and finite throughput.
The former linearly seeded xorshift stream has been replaced with the pinned
256-spp Heitz et al. Owen-scrambled Sobol sampler. Eight explicit dimensions
per bounce remove branch-dependent sample consumption, and translated optimized
tiles pad later dimension groups. The specular hit-distance guide now records
the actual first secondary ray when the sampled primary event is glossy instead
of tracing an independently sampled guide ray. Each completed 256-sample block applies
a deterministic tile translation so the reference package does not repeat the
same aligned screen-space pattern every 256 presented frames.
The visible Level A capture has also been checked through the hidden readback
path. Phase 8 now allocates and writes the seven required RR guide resources at
the primary hit. Diffuse/specular albedo, world shading normal, perceptual
roughness, linear view depth, dense scene motion, and stochastic GGX specular
hit distance use the formats in the frame contract below. The hit-distance
sample is coupled to the glossy radiance path rather than an unrelated random
direction. A renderer-owned
history retains the previous camera basis, per-frame Halton jitter, dimensions,
history epoch, and a GPU copy of the previous vertices; motion is
`previousPixel - currentPixel` in pixel units. `SceneFrame.history_epoch`
invalidates history across level loads and quickloads, while scene rebuilds and
resizes also reset it. Phase 9 integrates the pinned Streamline 2.12.0
production runtime using manual hooks and a stable custom project GUID. On a
GeForce RTX 3090 it initializes NGX, selects a fixed optimal low-resolution
input for the chosen quality mode, evaluates DLSS-RR, and presents the
full-resolution reconstructed HDR output. The raw-noise mode and every Phase 8
guide view remain available for diagnosis. Dynamic sprite/vector-object
geometry, raw per-guide readback statistics and ID overlays, transparencies,
and overlays remain outstanding.

Phase 11 then attacked the reconstructed image's temporal stability, which had
never settled the way TAA does. Its first result is a measurement: the hidden
smoke freezes the camera, view, and `SceneFrame`, presents
`AB3D2_DXR_STABILITY_FRAMES` frames, and reports the mean absolute per-component
difference between consecutive presented frames. Fixed-phase jitter, non-degenerate
sky guides, and a reprojected specular hit-distance guide moved the Level A plateau
from 1.3596 to 1.1829 and, more importantly, made the image keep settling instead of
flatlining immediately. An alias table replaced the linear emitter
cumulative-distribution walk. Reservoir resampling of direct lighting is implemented
and, measured against that metric, **disproves the phase's own premise**: every
increase in candidate count or temporal history lowered the path-traced input's
variance and raised the reconstructed image's residual difference, because it trades
high-frequency screen-space blue noise for correlated error a denoiser cannot
remove. Resampling therefore ships present but disabled by default, behind
`AB3D2_DXR_CANDIDATES` and `AB3D2_DXR_RESERVOIR_LIMIT`, and spatial reuse is
contraindicated rather than merely deferred. No level yet demonstrates a settled
image; read section 11 before spending further effort on the estimator.

### Current dependency gate

The ID-independent DirectX 12/DXR renderer may continue without Streamline. The
Streamline source checkout has been verified at `v2.12.0` / `e8aaa6e`, and the
official signed v2.12.0 release archive has been verified with SHA-256
`F5C0A3D870707DDDC3570FB4BCD3655CF48A8A68C3A9D342910CFA21B77DCF48`.
Extract that archive outside this repository and expose the resulting directory
through the `AB3D2_STREAMLINE_ROOT` environment variable; never commit its SDK
files or a local path.

Current Streamline explicitly supports projects without an NVIDIA-issued
numeric application ID. This project therefore commits its own stable GUID,
`6b0d5e3a-a774-4e9e-8937-e6ca29a6885e`, and initializes Streamline with engine
`CUSTOM`, engine version `0.1.0`, and numeric application ID zero. The build
validates the exact official 2.12.0 production payload; the runtime verifies
the staged NVIDIA signatures, disables OTA plugins, and loads only from the
executable directory.

## Goal

Build a new renderer with the following frame pipeline:

```text
SceneFrame
  -> renderer-owned material and geometry compilation
  -> DirectX 12 BLAS/TLAS scene
  -> stochastic DXR path-tracing pass
       noisy HDR radiance
       diffuse albedo
       specular albedo
       world-space shading normals
       linear roughness
       linear depth
       dense scene motion vectors
       specular ray hit distance
  -> NVIDIA Streamline DLSS Ray Reconstruction
  -> exposure and tone mapping
  -> transparent presentation effects, weapon, HUD, and text
  -> DXGI swap chain
```

The renderer should produce a physically coherent, deliberately noisy ray-traced image and let NVIDIA Ray Reconstruction perform denoising and reconstruction. It must not add a hand-written temporal accumulator, bilateral filter, reflection denoiser, TAA, spatial specular blur, or other pre-RR workaround.

The target is a stable and convincing PBR presentation of the native AB3D2 scene. Q2RTX is neither an implementation source nor a pixel-matching oracle for this renderer.

## Non-negotiable clean-room boundary

The following rules apply to every implementation commit:

1. Do not inspect, copy, diff, port, link, regenerate from, or use as an algorithmic reference any Q2RTX source, shader, renderer asset, generated BSP, visibility sidecar, material file, light list, sampling strategy, denoiser, or prior removed implementation.
2. Do not recover renderer code from this repository's older commits. The history necessarily records the removal, but those commits are prohibited implementation material. Start from `86241dd` or a descendant and use only files present in that clean tree.
3. Do not add a Q2 BSP, PVS, cluster, ZoneT lighting proposal, light cap, importance workaround, temporal-reprojection hack, or Q2 material convention to the new backend.
4. Keep a provenance record for every imported source file, shader, generated table, binary, and third-party dependency. Copying a substantial MIT-licensed implementation requires retaining its copyright and licence notice.
5. Run a source and packaged-artifact audit before each renderer milestone. The new DXR directories must contain no Q2RTX-derived text, binary, or data.
6. AB3D2 source simulation and content remain the behavioral authority. The renderer may interpolate presentation data already exposed through `SceneFrame`; it must not invent or modify gameplay state.

The raw, committed images in `textures_pbr/` are project-authored inputs and may be used. Do not consume the generated `q2rtx_pbr/` package, Q2 `.mat` files, WAL files, Q2 roughness/metalness packing, or Q2 texture path conventions. Create a renderer-native material build from the source sheets.

## Approved references and exact revisions

### `fisica-rt`: PBR path-tracing concepts

Local path: `C:\Users\paula\Documents\Projects\fisica-rt`

- Commit: `1784cba270676b8c49f85a9022041dc98528ab54`
- Upstream: `binaryfoundry/fisica-rt`
- Licence: MIT, Copyright 2019 Paul Alexander Welch
- Relevant evidence: `files/gl/raytracing.glsl`, `files/gl/environment.glsl`, `src/pipelines/Raytracing.*`, `src/Geometry.*`, `src/Noise.*`, and `src/Camera.*`

Use this project for its material vocabulary, stochastic primary-ray jitter, multi-bounce rendering flow, environment separation, blue-noise/Sobol sampling concept, and separation of HDR ray output from exposure/tone mapping.

Do not transplant its bilateral filter: Ray Reconstruction replaces that stage. Do not treat its brute-force sphere scene, binary Fresnel choice, or simplified material sampling as a production BRDF. Any sample table copied from it needs a separate provenance/licence audit first; generating a project-owned deterministic sampler is preferable.

### `dxr-demo`: DirectX 12 and DXR setup concepts

Local path: `C:\Users\paula\Documents\Projects\dxr-demo`

- Commit: `d08175a58e2737eb87b3cb81eb55416e3fa89ee9`
- Upstream: `binaryfoundry/dxr-demo`
- Licence: MIT, Copyright 2019 Paul Alexander Welch
- Relevant evidence: `src/d3d12/Renderer.*`, `Context.hpp`, `Frame.hpp`, `Scene.*`, `raytracing/Allocation.*`, `BottomStructure.*`, `TopStructure.*`, and `shaders/shader.hlsl`

Use this project for SDL-to-`HWND` integration, D3D12 device/queue/swap-chain structure, per-frame allocators and fences, BLAS/TLAS build ordering, UAV barriers, a DXR state object, aligned shader tables, and `DispatchRays` flow.

Do not copy its hard-coded scene, upload-heap geometry policy, default-adapter selection, old shader-header build, or minimal error handling. The production backend must select and validate an adapter explicitly, use default-heap geometry, diagnose device removal, and compile current HLSL with DXC.

### Heitz et al. screen-space blue-noise sampler

Following explicit user direction on 2026-08-18, use the sampler described by
Eric Heitz, Laurent Belcour, Victor Ostromoukhov, David Coeurjolly, and
Jean-Claude Iehl in *A Low-Discrepancy Sampler that Distributes Monte Carlo
Errors as a Blue Noise in Screen Space* (SIGGRAPH Talks 2019). The sampler
combines per-pixel Owen-scrambled Sobol sequences with optimized screen-space
ranking and scrambling keys. Project page:
`https://belcour.github.io/blog/research/publication/2019/06/17/sampling-bluenoise.html`.

The committed 256-spp tables are the byte-exact MIT-licensed transcription in
`Jasper-Bekkers/blue-noise-sampler`, commit
`b1720f637a3580c8711580873dcf84cce82f6504`. The pinned `src/spp256.rs` input
has SHA-256
`1713E8F1A593B9505860E8CAB9E47976434EBD17915B2B5FCEAD635E00E71AC2`;
the renderer-native contiguous byte package has SHA-256
`3381BA037DB1940BD3A9A82C4C09A1EE145060268C8E08D8A3D02AD5895D69AB`.
Retain `docs/third_party/blue-noise-sampler-MIT.txt` in source and packaged
distributions. Use the authors' documented two-XOR addressing; do not replace
it with a visually similar hash or a hidden accumulation/filter pass.

### Reservoir resampling for direct lighting

Following explicit user direction on 2026-08-19, the direct-lighting estimator is
rebuilt from Benedikt Bitterli, Chris Wyman, Matt Pharr, Peter Shirley, Aaron
Lefohn, and Wojciech Jarosz, *Spatiotemporal reservoir resampling for real-time
ray tracing with dynamic direct lighting* (ACM Transactions on Graphics 39(4),
SIGGRAPH 2020). The paper supplies the reservoir update rule, the unbiased
contribution weight, and the temporal combination weights.

The candidate loop needs far more dimensions than the eight the Heitz tables
optimize, so its stream comes from Mark Jarzynski and Marc Olano, *Hash Functions
for GPU Rendering* (Journal of Computer Graphics Techniques 9(3), 2020).

**Papers only.** No NVIDIA RTXDI header, shader, sample, or other third-party
resampling implementation may be read, adapted, linked, or staged. Both
implementations are written from the published mathematics and carry the citation
in the shader and in the CPU test that pins them, exactly as the Heitz sampler
does. Resampling is a sampling technique, not a denoiser: the path-traced input
remains a single-sample stochastic estimate and disabling Ray Reconstruction must
still reveal visible noise.

### NVIDIA Streamline and Ray Reconstruction

Pin the integration to NVIDIA Streamline SDK **2.12.0**, release tag `v2.12.0` (release commit shown as `e8aaa6e`). Do not silently float to `main`. The fresh context must recheck the pinned headers before using any field name because the online guide contains examples from multiple revisions.

Primary official references:

- [Streamline 2.12.0 release](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.12.0)
- [DLSS Ray Reconstruction programming guide, v2.12.0](https://github.com/NVIDIA-RTX/Streamline/blob/v2.12.0/docs/ProgrammingGuideDLSS_RR.md)
- [DLSS programming guide, v2.12.0](https://github.com/NVIDIA-RTX/Streamline/blob/v2.12.0/docs/ProgrammingGuideDLSS.md)
- [General Streamline programming guide, v2.12.0](https://github.com/NVIDIA-RTX/Streamline/blob/v2.12.0/docs/ProgrammingGuide.md)
- [Streamline manual-hooking guide, v2.12.0](https://github.com/NVIDIA-RTX/Streamline/blob/v2.12.0/docs/ProgrammingGuideManualHooking.md)
- [NVIDIA Streamline sample](https://github.com/NVIDIA-RTX/Streamline_Sample)
- [NVIDIA DLSS 4 integration article](https://developer.nvidia.com/blog/how-to-integrate-nvidia-dlss-4-into-your-game-with-nvidia-streamline/)
- [Streamline SDK licence, v2.12.0](https://github.com/NVIDIA-RTX/Streamline/blob/v2.12.0/license.txt)

Before committing SDK headers, libraries, or redistributable DLLs, review the pinned SDK licence and third-party notices and record which files may be distributed. Do not commit development binaries as production runtime binaries.

## Existing host contracts to preserve

The backend boundary is already suitable for a clean implementation:

- `src/renderer.h` and `src/renderer.c` own API-neutral backend selection and lifecycle.
- `src/renderer_rtx.h` defines the C-facing RTX backend interface.
- `src/renderer_rtx_stub.c` is the fail-fast replacement point.
- `src/scene_frame.h` is the authoritative render snapshot. Do not make gameplay code call D3D12 directly.
- `src/renderer_resources.h` owns immutable source vector-resource data.
- `src/render_view.h` supplies host-rate yaw and pitch presentation state.
- `src/source_world_material.*` decodes source wall/flat material pixels to API-neutral RGBA8 sRGB.
- `src/source_bitmap_sprite.*` decodes bitmap, lighted-bitmap, additive, and glare sprite frames.
- `src/source_vector_model_scene.*` exposes vector-model geometry, but some existing entry points produce view-projected data. Add a renderer-neutral object-space compiler where DXR requires true world geometry rather than reusing projected OpenGL output.
- `src/level_static_scene.*` and `src/object_scene.*` construct stable world/object scene records.

`SceneFrame` already supplies:

- A host-rate interpolated camera.
- Triangle lists and polygon boundaries for walls, floors, ceilings, and water.
- Stable source material, geometry, mesh, instance, and sprite identities.
- Static versus dynamic acceleration-structure classification.
- Interpolated dynamic object and sprite state.
- Environment, HUD, text, bitmap sprite, vector model, and glare commands.

Preserve source `ZoneT`, vertex light levels, ambient values, `SceneLighting`, and PVST in the shared frame because OpenGL and source-faithful rendering still use them. The new DXR radiance path must deliberately ignore those values. In particular, AB3D Gouraud lighting is not multiplied into albedo, emissive radiance, direct lighting, RR guide buffers, or final DXR color.

The existing world-coordinate conversion is mixed-domain source data: X/Z are signed low-word units, while Y is source 8.8 down-positive data converted to an up-positive renderer space. Extract a single tested, renderer-neutral conversion from the current OpenGL interpretation rather than creating a second approximate scale. Derive both the OpenGL and DXR matrices from that shared convention only after regression tests establish identical camera/frustum geometry.

## Target source layout

Keep the public C API small and hide DirectX/Streamline types from C gameplay code:

```text
src/
  renderer_rtx.h                    C ABI, no D3D headers
  renderer_rtx_stub.c               built when DXR is unavailable/disabled
  renderer_rtx.cpp                  C ABI wrapper around the C++ backend
  renderer_dxr/
    dxr_renderer.{h,cpp}            lifecycle and frame orchestration
    dxr_device.{h,cpp}              adapter/device/queue/swap chain/fences
    dxr_descriptors.{h,cpp}         persistent and per-frame descriptor allocation
    dxr_upload.{h,cpp}              upload ring and default-heap copies
    dxr_scene.{h,cpp}               SceneFrame compilation and history
    dxr_acceleration.{h,cpp}        BLAS/TLAS ownership and build/update policy
    dxr_materials.{h,cpp}           source identity -> PBR material table
    dxr_pipeline.{h,cpp}            root signatures, state object, shader tables
    dxr_ray_reconstruction.{h,cpp}  Streamline lifecycle, tags, constants, evaluate
    dxr_post.{h,cpp}                exposure, tone map, overlays, HUD, present
    dxr_debug.{h,cpp}               named buffers, readback statistics, captures
    shaders/
      shared.hlsli
      raygen.hlsl
      closest_hit.hlsl
      any_hit.hlsl
      miss.hlsl
      pbr.hlsli
      sampling.hlsli
      guides.hlsli
      post.hlsl
tools/
  build_dxr_materials.py
data/renderer_dxr/
  material_manifest.json
  generated material textures only
```

The exact subdivision can change when implementation reveals a better boundary, but do not collapse device setup, scene compilation, path tracing, Streamline, and presentation into one source file.

## Build and dependency strategy

1. Add `AB3D2_ENABLE_DXR`, default `OFF`, valid only on native Windows.  It enables the DirectX 12 diagnostic foundation.  The Emscripten configuration must never discover or stage DirectX/Streamline files.
2. Keep the project C11 generally. Enable C++20 only for the Windows DXR source set. Export `extern "C"` functions matching `renderer_rtx.h`.
3. When DXR is disabled or unsupported at build time, compile the existing stub.  When DXR is enabled, compile the D3D12 diagnostic backend and omit the stub from that target.  It must not claim to render `SceneFrame` content until the later scene phases implement it.
4. Add `AB3D2_ENABLE_STREAMLINE`, default `OFF`, valid only when `AB3D2_ENABLE_DXR=ON`. When enabled, resolve the extracted pinned release root from `AB3D2_STREAMLINE_ROOT`, with an identically named CMake cache path as an explicit per-build override. Use the committed stable `AB3D2_STREAMLINE_PROJECT_ID` GUID with engine `CUSTOM` and the project version; an NVIDIA-issued numeric application ID is not required by current NGX. Fail configuration clearly when the GUID, exact production payload, or a required signed file is invalid; do not download or unpack an SDK during configure.
5. Follow the pinned manual-hooking guide only in the Streamline-enabled build.  Prefer static `sl.interposer.lib` integration for DirectX, and make the link graph explicit so that build has no incompatible direct DXGI/D3D entry path.  The ID-independent foundation may link the ordinary DirectX import libraries.
6. The Streamline-enabled build must pass the project GUID, engine `CUSTOM`, and project version to `slInit`, with the optional numeric application ID set to zero. Disable OTA/downloaded-plugin flags, use only the extracted production plugin directory, verify NVIDIA signatures, and log exact loaded plugin paths/versions. Do not use an invented or sample NVIDIA-issued numeric ID.
7. When DXR is enabled, resolve `dxc.exe` with `find_program` from `PATH`; retain `AB3D2_DXC_EXECUTABLE` only as an explicit per-build cache override.  Record the resolved compiler version and SHA-256 in build evidence, then compile HLSL with that pinned tool at build time, starting with Shader Model 6.6 unless the pinned Streamline/DXR requirements dictate otherwise. Warnings are errors; generated DXIL belongs in the build tree, not hand-maintained C headers.
8. Stage only the material runtime files and, when Streamline is enabled, the needed production redistributables and licence/notice files beside the executable.  Runtime signature checks and loading use those full staged paths, never `PATH`.  The diagnostic-only build stages no Streamline DLLs.
9. `renderer=rtx` must fail with a specific reason if the OS, adapter, driver, or DXR tier is unavailable. In a Streamline-enabled build, it must additionally fail for an invalid project GUID, SDK payload, plugin signature, or RR feature. Do not silently substitute OpenGL after an explicit RTX request.

## DirectX 12 foundation

Implement and test the platform layer before ray tracing:

1. Create the SDL native window without `SDL_WINDOW_OPENGL`, then retrieve its `HWND` through `SDL_SysWMinfo`.
2. In debug builds, enable the D3D12 debug layer before device creation; make GPU-based validation an opt-in developer mode because of its cost.
3. Create a DXGI 1.6 factory and enumerate high-performance hardware adapters. Reject software adapters unless a separate test-only mode explicitly requests WARP.  Require an appropriate `ID3D12Device5+` interface and a nonzero `D3D12_OPTIONS5.RaytracingTier`.
4. Create a direct queue, flip-discard swap chain, three frame contexts, RTVs, command allocators, command lists, fence values, and one fence event. Never reset an allocator still referenced by the GPU.
5. Add named resources, complete HRESULT context, DRED/device-removed reporting, and orderly resize/flush/shutdown behavior. Use default heaps for resident geometry and textures, a bounded upload ring for transfers, and explicit state transitions/UAV barriers; avoid per-frame committed-resource churn.
6. Establish an SDR presentation baseline first: clear, draw a diagnostic triangle or compute pattern, copy to the swap chain, resize, minimize/restore, and run for several thousand frames with the debug layer clean.  This diagnostic consumes no game geometry or `SceneFrame` command and reports zero scene/UI coverage by design.

### Streamline activation gate

This gate is implemented when `AB3D2_ENABLE_STREAMLINE=ON`:

1. Validate the extracted v2.12.0 release layout, its recorded payload hashes,
   and the full paths and signatures of `sl.interposer.dll`, `sl.common.dll`,
   `sl.dlss_d.dll`, and `nvngx_dlssd.dll` before loading any module.
2. Configure manual hooking with the production `sl.interposer.lib`; request
   the DLSS/RR features that the pinned headers require, install the log
   callback, and call `slInit` with the project GUID and custom-engine identity
   before any DXGI/D3D API call. Do not enable OTA or downloaded plugins.
3. Use each high-performance adapter's LUID for
   `slIsFeatureSupported(sl::kFeatureDLSS_RR, ...)`, create the D3D12 device
   only for that same adapter, then call `slSetD3DDevice`.  Fail explicitly if
   the selected adapter, driver, plugin, or RR feature is unsupported.
4. Upgrade the DirectX presentation interface as the manual-hooking guide
   requires so `presentCommon()` executes exactly once per frame.  Call
   `slShutdown` before destroying the D3D/DXGI device stack.

## Renderer-owned scene and material compilation

### Geometry

- Triangulate `SceneGeometry` polygon boundaries deterministically. Preserve the authored winding and use a shared robust triangulator; do not create renderer-specific tessellation as a lighting control.
- Deduplicate immutable world vertices where safe, but retain stable primitive-to-source identity for materials and debugging.
- Build one or a small number of static world BLAS objects per level. Use `SceneAccelerationClass` and stable mesh IDs as the initial ownership signal.
- Build or refit dynamic BLAS objects for doors, lifts, water, vector models, and other moving geometry. Rebuild when topology changes; update only when the DXR update rules and measured cost justify it.
- Build the TLAS every presented frame using current transforms. Retain previous transforms for motion output.
- Compile opaque or alpha-tested world bitmap objects to camera-facing quads only when their source semantics require it. Keep additive/glare effects out of the opaque TLAS for the first RR milestone.
- Add a true object-space vector-model compiler for DXR. The current view-projected helper must remain available to OpenGL and the source-faithful weapon path.

### PBR materials

Create a renderer-native manifest keyed by stable source material identity. It must describe, independently for each material:

- Linear base color/albedo texture.
- Tangent-space normal texture and normal strength.
- Linear roughness texture or scalar.
- Metalness texture or scalar.
- Optional emissive texture, tint, and physically documented radiance scale.
- Alpha mode and cutoff.
- UV transform.

`tools/build_dxr_materials.py` should extract the albedo, normal, metalness, and roughness panels from the committed `textures_pbr/*.png` sheets and produce conventional renderer-native textures plus a manifest. It must not invoke the old Q2RTX package builder or inherit its channel packing and naming. Add deterministic hashes and golden tests for every output.

Decode base-color textures from sRGB to linear before BRDF use. Treat normal, roughness, metalness, and emissive scalar data according to their declared color spaces. A missing PBR entry falls back visibly and deterministically to decoded source albedo, roughness `1`, metalness `0`, emissive `0`; log the material identity once. Do not infer metalness or emission from pixel brightness at runtime.

For a metallic-roughness model:

- `diffuseReflectance = baseColor * (1 - metalness)`.
- `F0 = lerp(0.04, baseColor, metalness)` unless the manifest provides a justified dielectric IOR/specular value.
- Feed linear roughness to Ray Reconstruction, while the microfacet BRDF may use its squared alpha convention internally.
- Compute the RR specular-albedo guide from material specular color, roughness, and view angle using the pinned NVIDIA RR guidance; do not simply write raw metalness or `F0` without validation.

### Emitters and environment

Build renderer-owned emissive triangle records from explicitly authored emissive material entries. Weight a global sampling distribution by triangle area and emitted luminance, and trace visibility rays against the TLAS. This replaces every old spatial-light-list concept; there is no BSP, PVS, cluster, ZoneT, arbitrary cap, or global-fallback workaround.

Use the `SceneEnvironment` backdrop/sky through a documented lat-long or equivalent environment mapping and build an importance distribution when the environment is emissive. Source additive/glare sprites are visual effects, not light emitters, until the material manifest explicitly assigns radiometric behavior.

## Noisy path tracer

The first complete ray-tracing pass should be simple enough to validate yet physically coherent:

1. Dispatch one camera ray per input-resolution pixel per frame with a deterministic frame-varying subpixel jitter supplied by the same jitter generator used in Streamline constants.
2. Use the pinned, licensed blue-noise/Owen-scrambled Sobol sampler. Key it by pixel, explicit sample dimension, and monotonically increasing history sample index. Reset the sequence only on an explicit history reset.
3. Trace primary visibility and write the RR guide values at the first opaque surface. Miss pixels get documented background sentinels and environment radiance.
4. Evaluate an energy-consistent Lambertian diffuse plus GGX/Cook-Torrance metallic-roughness BRDF. Use cosine-weighted diffuse sampling and a well-defined GGX visible-normal sampling method with matching PDFs.
5. Perform next-event sampling of emissive triangles and the environment with shadow rays. Combine light and BSDF sampling with multiple-importance sampling using the PDFs actually used.
6. Continue diffuse/specular paths for an initially fixed small bounce count (for example, three total surface hits), then use Russian roulette only after throughput and PDF tests are in place. Bounce count is a performance setting, not a hidden quality repair.
7. Accumulate radiance only inside the current path. Write a fresh noisy HDR sample every frame. There is no renderer-owned cross-frame radiance accumulation before RR.
8. Use firefly-resistant numerics rather than image-space hacks: finite checks, origin offsets scaled to scene units, bounded texture inputs, and robust PDF handling. Do not clamp legitimate HDR radiance merely to make reconstruction look smoother.

Write CPU unit tests for the material equations, lobe selection probabilities, PDF agreement, normal transforms, and finite throughput. Add small GPU reference scenes containing diffuse, dielectric, metal, rough-to-smooth, emissive, shadowed, and environment-lit surfaces before testing game levels.

## Ray Reconstruction frame contract

The user's conceptual call

```text
DLSS_RR(noisyHDR, depth, motionVectors, normals, roughness,
        diffuseAlbedo, specularAlbedo, specularMotion, cameraData, ...)
```

maps to Streamline resource tags, constants, options, and `slEvaluateFeature`; it is not one direct function with that signature.

Start with separate, inspectable guide textures rather than packing normal/roughness. Pack only after a byte-for-byte/debug-view equivalence test.

| Semantic | Initial DXGI format | Resolution | Streamline tag | Required content |
| --- | --- | --- | --- | --- |
| Noisy HDR radiance | `R16G16B16A16_FLOAT` | RR input | `kBufferTypeScalingInputColor` | Fresh, linear HDR path-traced radiance; no UI, tone map, temporal accumulation, or denoising |
| RR output | `R16G16B16A16_FLOAT` | Display output | `kBufferTypeScalingOutputColor` | Streamline output, consumed by exposure/tone mapping |
| Diffuse albedo | `R16G16B16A16_FLOAT` | RR input | `kBufferTypeAlbedo` | Linear diffuse reflectance, with metal diffuse removed |
| Specular albedo | `R16G16B16A16_FLOAT` | RR input | `kBufferTypeSpecularAlbedo` | NVIDIA-guided view/roughness-aware specular reflectance |
| Shading normal | `R16G16B16A16_FLOAT` | RR input | `kBufferTypeNormals` | Normalized world-space shading normal in RGB |
| Linear roughness | `R16_FLOAT` | RR input | `kBufferTypeRoughness` | Perceptual/linear material roughness in R, not squared BRDF alpha |
| Linear depth | `R32_FLOAT` | RR input | `kBufferTypeLinearDepth` | Positive camera/view-space ray depth using one documented clear sentinel |
| Scene motion | `R16G16_FLOAT` initially | RR input | `kBufferTypeMotionVectors` | Dense camera and dynamic-object motion in pixel units |
| Specular hit distance | `R32_FLOAT` | RR input | `kBufferTypeSpecularHitDistance` | World-space distance from the primary surface ray origin to its specular-ray hit; documented miss value |
| Exposure | `R32_FLOAT`, 1x1 | 1x1 | `kBufferTypeExposure` if required by the pinned integration | Explicit exposure shared by RR and tone mapping |

Use specular hit distance for the first complete integration, not specular motion vectors. The path tracer already knows the first specular ray origin and hit position, so this avoids a second virtual-reflection motion pipeline. Supply the exact additional world/view camera matrices named by the pinned `sl_dlss_d.h`. Once baseline quality is validated, specular motion vectors may be evaluated as a measured alternative, never as two simultaneously ambiguous inputs.

The initial formats intentionally favor observability and correctness over bandwidth. Optimize formats only after debug captures prove ranges, signs, spaces, and quantization are unchanged.

### Camera and motion history

Maintain a renderer-owned `DxrFrameHistory` containing:

- Current and previous unjittered world/view/projection matrices and their inverses.
- Current and previous jitter in pixel units.
- Camera position, forward, right, up, FOV, aspect, near/far conventions.
- RR input/output dimensions and a monotonically increasing presented-frame number.
- Previous object transforms keyed by `(level_generation, stable_source_identity)`.
- An explicit validity/reset bit.

Generate dense scene motion from the same primary hit, geometry transform, depth, and matrices supplied to RR. For the chosen initial pixel-space representation, begin with `previousPixel - currentPixel`, exclude jitter from the vector, set `motionVectorsJittered=false`, and set `mvecScale={1/renderWidth, 1/renderHeight}`. Verify direction, Y convention, and scale with synthetic translations and NVIDIA's debug overlay before accepting this convention. Never tune the sign by visual guesswork.

Static world surfaces contain camera motion. Dynamic geometry contains both camera and object motion. Newly appearing geometry, destroyed instances, invalid previous transforms, and disocclusions use a documented invalid/history-reset path rather than zero vectors that falsely claim stationarity.

Add an explicit renderer history epoch or reset signal for:

- Level load/unload.
- Renderer creation/recreation.
- Input/output resolution or display-mode change.
- Teleport or non-contiguous camera state.
- Return from non-game text/menu presentation.
- Shader/material layout changes during development.

Prefer host-issued discontinuity events over distance heuristics. If `SceneFrame` lacks the necessary epoch, extend the API-neutral frame with a level/presentation generation field rather than exposing a DXR concept to gameplay.

All matrices passed to Streamline are row-major and unjittered. Jitter is supplied separately in pixel space. Set every `sl::Constants` field deliberately, including depth convention, `cameraMotionIncluded`, `motionVectors3D`, `motionVectorsDilated`, `motionVectorsJittered`, `reset`, and `renderingGameFrames`.

### Streamline frame order

For every rendered game frame:

1. Obtain one Streamline frame token and one stable viewport handle.
2. Query or retain the fixed RR input size selected through `slDLSSDGetOptimalSettings` for the chosen performance/quality mode.
3. Update the unjittered camera matrices, jitter, previous-frame transforms, and reset state.
4. Build/update BLAS and TLAS, dispatch the path tracer, and transition all RR inputs to the states required by the pinned SDK.
5. Call `slSetConstants` once for this frame token and viewport.
6. Set compatible DLSS SR and DLSS-RR options with HDR enabled. Use the exact v2.12.0 structure/enum names from the headers.
7. Tag every resource for this frame with `slSetTagForFrame`, correct extents, lifetimes, and resource states.
8. Evaluate `sl::kFeatureDLSS_RR` on the D3D12 command list using that same token/viewport.
9. Restore every command-list state that Streamline documents as host-owned after evaluation.
10. Tone map and composite post-RR presentation, submit, present, and only then promote current history to previous.

Ray Reconstruction does not support changing its input resolution in-place without reconstruction reinitialization. Use a fixed input size for the selected mode; on resize or mode change, flush, release RR resources, recreate the size-dependent targets, and set reset for the first valid frame.

Call `slIsFeatureSupported(sl::kFeatureDLSS_RR, adapterInfo)` for the selected adapter LUID. Report driver/OS/hardware/plugin failures verbatim enough to diagnose them. Do not claim RR is running unless evaluation succeeds and the expected plugin/version is logged.

## Transparency, sprites, water, weapon, and HUD

Establish the opaque RR path before adding ambiguous presentation layers:

- Opaque and alpha-tested world geometry participates in primary rays, depth, normals, materials, motion, and TLAS visibility.
- Additive/glare sprites and other transparent effects are initially rendered after RR in output resolution. This prevents their missing geometry depth/motion from corrupting reconstruction.
- If post-RR transparent quality is insufficient, add NVIDIA's premultiplied transparency overlay and color-before-transparency guides exactly as the pinned RR guide describes. Do not invent partial guide semantics.
- The camera-space weapon and HUD/text are presentation overlays after RR and tone mapping. A future true 3D weapon path must have valid geometry, depth, materials, and motion before moving it into RR input.
- Water first needs an explicitly authored PBR material and valid moving geometry history. Begin with a rough dielectric reflection model. Defer transmission/refraction until its ray type, absorption, nested-medium behavior, guides, and motion tests are specified.
- Text screens and non-game presentation set `renderingGameFrames=false` and reset history when returning to the world.

## Diagnostics required before visual tuning

Add a DXR debug-view selection that can display each resource without tone-map ambiguity:

- Noisy HDR with controllable exposure.
- Diffuse albedo.
- Specular albedo.
- World normal remapped to display range.
- Linear roughness.
- Linear depth with configurable visualization range.
- Scene motion as direction/hue and magnitude.
- Specular hit distance.
- Instance ID, primitive ID, material ID, and history-validity overlays.
- RR output before and after tone mapping.

Add asynchronous readback statistics for each floating-point guide: minimum, maximum, mean, NaN count, infinity count, invalid-sentinel count, and nonzero coverage. Name all D3D resources so PIX/Nsight captures identify them.

No lighting or history change may be called an improvement based only on one screenshot. Capture a fixed scripted sequence containing a stationary convergence interval, small yaw, small translation, dynamic-object motion, disocclusion, and return to rest. Save raw guide buffers and output frames with the commit hash, SDK version, driver, GPU, input/output sizes, material manifest hash, and scene-frame number.

## Implementation phases and commit sequence

Every commit should build and test independently. Do not batch the whole renderer into one unreviewable change.

### 1. `Document clean-room DXR dependencies and provenance`

- Commit this plan, approved reference revisions, licence inventory, and forbidden-source boundary.
- Add no functional renderer code yet.

### 2. `Add the ID-independent Windows DirectX 12/DXR diagnostic foundation`

- Add the opt-in CMake/C++ backend selection, `AB3D2_DXC_EXECUTABLE`, and an HLSL-built diagnostic pattern.
- Implement SDL/`HWND` creation, high-performance hardware-adapter selection, DXR-tier validation, D3D12 device, queue, swap chain, frame contexts, fences, resize, DRED, and shutdown without including or linking Streamline.
- Present the diagnostic pattern for the requested window and hidden smoke window; retain the fail-fast stub for disabled/non-Windows builds.  It deliberately does not render gameplay, HUD, or text.
- Add a dedicated hidden-window `ab3d2_renderer_rtx_foundation_test` that repeatedly calls the C ABI without scene-content assertions.  Keep the existing all-level `--gpu-smoke` OpenGL-only until a later phase implements the scene/UI metrics it verifies.
- Unit-test configuration and failure strings; run the D3D debug layer clean.

### 3. `Activate the Streamline-gated DXR foundation`

- Current status: implemented with the committed project GUID and custom-engine identity; the exact extracted signed Streamline v2.12.0 production payload is validated at configure time.
- Integrate the manual-hooking, signature-validation, `slInit`-before-DXGI, LUID support-check, `slSetD3DDevice`, and shutdown ordering described above.
- Verify the production interposer dependency graph and `presentCommon()` path before any RR resource/tag work begins.
- DXR-only builds remain ID-independent and do not include, link, load, discover, or stage Streamline.

### 4. `Add renderer-native AB3D2 PBR materials`

Current status: all five runtime channels are packaged and sampled. The
old-renderer-compatible colored `technolights` mask and source-authored
`floor_0101` panel are explicit emissive textures with factor 200;
`brownspeakers` and `technotritile` are non-emissive. Source `floor_0201` is
also bound to its authored PBR sheet. Material debug spheres/planes remain
later work.

- Add deterministic sheet extraction, manifest schema, hashes, and golden tests.
- Upload base color, normals, roughness, metalness, and explicit emissive textures.
- Add material debug spheres/planes and fallback reporting.
- Include required MIT/project/asset notices; include no Q2 package output.

### 5. `Compile SceneFrame geometry for DXR`

Current status: opaque static and door/lift/water world geometry, shared
coordinate conversion and triangulation, stable material indices, and bounded
three-frame dynamic GPU upload are implemented. Dynamic sprite/vector-object
categories and previous/current transforms remain later work.

- Add shared tested world-coordinate conversion and triangulation.
- Add renderer-neutral object-space vector geometry where needed.
- Upload static and dynamic meshes, material indices, stable identities, and previous/current transforms.
- Render raster/compute debug views before acceleration structures.

### 6. `Build and validate DXR acceleration structures`

Current status: opaque, two-sided static and dynamic world BLAS objects plus one
TLAS are built and primary visibility is traced for all Levels A--P. Moving
door/lift/water vertex ranges retain PBR/emissive atlases, refit only changed
dynamic BLAS objects, and update the TLAS in place without the old per-frame
queue flush. The D3D debug-layer foundation test exercises this update path.
Dynamic sprite/vector-object acceleration structures, auxiliary guide outputs,
and PIX validation remain later work.

- Add default-heap BLAS/TLAS resources, scratch allocation, barriers, build/update policy, and shader tables.
- Trace primary visibility into IDs, normals, depth, and albedo.
- Validate all game levels, moving doors/lifts/water, sprites, and vector objects in PIX.

### 7. `Add the clean-room noisy PBR path tracer`

Current status: the fresh noisy HDR target, stochastic primary rays, authored
PBR sampling, Lambertian/GGX mixture, visible-normal specular sampling,
three-hit indirect paths, global emissive/environment next-event sampling,
visibility rays, MIS, and CPU finite/PDF checks are implemented for opaque
world geometry. Dynamic sprite/vector-object geometry and later presentation
classes remain incomplete.

- The pinned 256-spp blue-noise/Owen-scrambled Sobol sampler is implemented
  with explicit dimensions for every environment, emitter, BSDF, and specular
  guide decision. Deterministic tile translation between 256-sample blocks
  prevents an aligned long-run screen-space repeat. Add the small GPU material
  reference scenes.
- Produce fresh un-denoised `R16G16B16A16_FLOAT` radiance each frame.
- Keep source Gouraud/ZoneT/PVST data unused in this pass.

### 8. `Generate complete Ray Reconstruction guides`

- Current status: all seven mandatory guide textures are separate, named, and
  written by the opaque-world path tracer. Primary misses write zero albedo,
  normal, roughness, depth, and hit distance; a zero hit distance also denotes
  a specular-ray miss. History-invalid motion is `(65504, 65504)`, reserving the
  largest finite FP16 value outside the clamped valid range, while valid sky
  pixels retain rotational camera motion. Surface albedo/normal alpha is one and
  miss alpha is zero for inspection. Environment-selected debug views cover
  every guide; raw asynchronous guide statistics and the ID/history overlay
  remain to finish the diagnostics bullet.
- Add separate diffuse/specular albedo, normals, roughness, linear depth, dense motion, and specular hit-distance resources.
- Add previous-frame identity/transform history and explicit reset epochs.
- Add every debug view/readback statistic and synthetic camera/object motion test.

### 9. `Integrate Streamline DLSS Ray Reconstruction 2.12`

- Current status: implemented for the opaque-world pipeline. Quality,
  balanced, performance, and ultra-performance modes use Streamline's fixed
  optimal input dimensions and a full-resolution output. The renderer submits
  all required Phase 8 guides, matrices/constants, options, frame token, and
  viewport, restores presentation command-list state after evaluation, and frees
  resources across resize/shutdown. `AB3D2_DXR_RR_MODE=off` is the raw-noise
  diagnostic path.
- Add pinned SDK detection, licence staging, secure plugin load, feature checks, optimal fixed resolution, options, tags, constants, and evaluation.
- Restore command-list state after evaluation and validate with NVIDIA/PIX tooling.
- Add an RR-off raw-noise mode for diagnosis only, not a shipping denoiser fallback.

### 10. `Complete DXR presentation and regression coverage`

- Add exposure/tone mapping, post-RR transparencies, weapon, HUD, text, and optional NVIDIA transparency guides if captures prove they are needed.
- Add scripted camera/dynamic-scene captures, all-level native smoke tests, resize/device-loss tests, packaging, documentation, and licence audit.
- Run the complete OpenGL, converter/material, Web, and source-runtime suites.

### 11. `Converge the DXR estimator with staged ReSTIR direct lighting`

The reconstructed image never settled the way TAA does. Blue noise shapes error
spatially per frame and makes no temporal claim, and Ray Reconstruction is a
short-history learned filter rather than an accumulator, so nothing in the
pipeline was integrating. Two estimator defects dominated the residual variance:
emitter sampling drew one sample per bounce from a global area-times-luminance
distribution through a linear CDF walk, and environment sampling was a
one-sample ambient-occlusion estimate against a bright analytic sky evaluated at
every bounce.

`--gpu-smoke` now measures this directly. With the camera, view, and `SceneFrame`
frozen it presents `AB3D2_DXR_STABILITY_FRAMES` frames (default 24) and reports
the mean absolute per-component difference between consecutive presented frames
on the 0-255 display scale, plus a count of pixels saturating tone mapping. The
metric is reported rather than bounded until each stage has a recorded baseline.

#### 11a. Guide and jitter prerequisites — complete

- `reconstruction::frame_jitter` wraps its index by `jitter_phase_count` (32).
  An unbounded Halton index never repeats, so the upscaler had no fixed point to
  settle onto.
- Background pixels report `SceneFarPlane` linear depth, a camera-facing unit
  normal, and roughness 1. A zero linear depth with `depthInverted = eFalse` is
  the nearest representable distance and inverted every sky silhouette.
  `reconstruction::scene_near_plane` and `scene_far_plane` are now the single
  source of truth, mirrored in the shader as `SceneFarPlane`.
- The specular hit-distance guide is a reprojected running estimate blended
  towards each stochastic measurement by `SpecularHitDistanceBlend`, published
  into `specular_hit_distance_history` by a copy after `DispatchRays`. Writing
  zero on the frames whose primary lobe choice went diffuse made Ray
  Reconstruction resize its specular filter footprint per pixel per frame. A
  specular ray that escapes now reports the far plane rather than zero. This
  filters a guide, not radiance.
- No radiance clamp was added. A firefly clamp is biased and would be a visual
  workaround for the estimator defects 11b and 11c remove; the saturated-pixel
  count exists to measure whether outliers actually survive.

Measured on a frozen Level A camera with DLSS-RR active, mean absolute
per-component frame-to-frame difference over the final four frames:

| Sweep length | Before 11a | After 11a |
| --- | --- | --- |
| 24 frames | 1.3699 | 1.3705 |
| 192 frames | 1.3596 | 1.1829 |

Before 11a the delta was flat from 24 to 192 frames (a 0.8% drop), which is the
absence of a fixed point. After 11a it keeps settling (a 13.7% drop) and reaches
a floor of 1.18. That floor is estimator variance and is what 11b and 11c
address. The raw path without Ray Reconstruction measures 29.65 with a ratio of
0.9955, confirming that no accumulation happens anywhere in the path tracer.

#### 11b. O(1) light selection — complete

- Header-only Walker alias table in `src/renderer_dxr/dxr_alias_table.h`, built
  with Vose's stable partition pass beside the existing emitter weights in
  `dxr_scene.cpp` and carried through the established emitter upload path.
  `selection_probability` stays as the source pdf; the linear `selection_cdf`
  walk is retired, along with the quantisation of light selection to the 256
  distinct values the blue-noise tables return.
- `tests/dxr_alias_table_test.cpp` checks the realised distribution analytically
  rather than by sampling, over a sweep of emitter counts, plus the degenerate
  and rejected cases.
- Measured neutral for stability, which is the correct expectation: it changes
  the cost and the resolution of selection, not the variance.

#### 11c. Single-pass temporal ReSTIR direct lighting — complete, and the premise
is disproven

Implemented as designed, except that the environment was left out of the
reservoir. Mixing the emitter and cosine-hemisphere strategies in one reservoir
requires the mixture source pdf to stay unbiased, and the cosine strategy's pdf
for an emitter-sampled direction is not computable without an extra ray. Emitters
alone are the dominant, clearly diagnosed noise source, so they were measured
first.

- One 32-byte reservoir per render-resolution pixel, double buffered on the
  sample index's parity, holding the surviving emitter index, the two canonical
  randoms that place the point on it as 16-bit fixed point, the unbiased
  contribution weight, the sample count, and the owning surface's world position
  and octahedral shading normal. The target pdf is deliberately not stored:
  recomputing it at the reusing pixel's own surface is what makes reuse exact.
- Carrying the owning surface inside the reservoir avoids a second G-buffer
  history. Validation compares against the current surface's *previous* world
  position, which the motion vector already needs, so geometry that translates
  between frames keeps its history instead of being treated as a disocclusion.
- The power heuristic is folded into the target function, so the reservoir
  estimates the light strategy's MIS-weighted share and the BSDF strategy
  independently estimates its own.
- Both buffers bind as unordered-access root descriptors, so neither needs a
  resource-state transition, and a UAV barrier after `DispatchRays` orders this
  frame's writes before the next frame's reads.

**The measurements do not support the premise.** With a frozen Level A camera and
DLSS-RR active, mean absolute per-component frame-to-frame difference over the
final four frames of a 192-frame sweep:

| Candidates | No temporal reuse | Reuse, limit 640 |
| --- | --- | --- |
| 1 | 1.1896 | 1.4366 |
| 4 | 1.2219 | 1.4080 |
| 8 | 1.2601 | 1.4205 |
| 32 | 1.4085 | 1.4675 |

One candidate with no reuse measures 1.1896 against the 11a baseline of 1.1829,
which is the quantisation of the stored position sample and confirms the
implementation reduces to the estimator it replaces exactly as intended. From
there, **every increase in either count makes the reconstructed image less stable,
monotonically**, while the raw path-traced input improves from 29.72 to 13.77 and
begins to converge for the first time (ratio 0.9955 to 0.8352).

The history cap is irrelevant to this: sweeping it from 32 to 640 moved the
plateau by under one percent, so sample switching is not the mechanism. Giving the
first candidate back the blue-noise dimensions recovered only 0.03 of the 0.25
regression, so the noise source alone is not the mechanism either.

The mechanism is the error's spectral character. Resampling trades
high-frequency, spatially decorrelated error for lower-magnitude error that is
correlated across neighbouring pixels and across frames, because neighbours
increasingly agree on which emitter they picked and a reservoir holds its choice
for many frames. A denoiser removes high-frequency error and cannot remove
correlated error, and Ray Reconstruction's history makes correlated error persist
and drift, which is what reads as boiling. Reducing estimator variance was
therefore the wrong lever for this renderer's output stability.

Both counts consequently default to the measured-best configuration, one
candidate and no temporal reuse, with `AB3D2_DXR_CANDIDATES` and
`AB3D2_DXR_RESERVOIR_LIMIT` re-enabling resampling for measurement and for the
raw diagnostic path it genuinely improves.

**Spatial reservoir reuse is contraindicated by this result** and must not be
implemented on the assumption that it will help: it increases exactly the
neighbour correlation the measurements identify as harmful. If it is tried, it has
to be justified by its own measurement first.

#### 11e. Where the instability actually lives

The all-level sweep at the measured defaults passes for every level. Its stability
numbers must be read alongside how much each level actually renders, because the
DXR path still covers opaque world geometry only and several levels are nearly
black at their smoke camera:

| Level | Late delta | Mean presented value | Pixels at 250 or above |
| --- | --- | --- | --- |
| A | 1.3457 | 89.9 | 5.8% |
| F | 1.2014 | 88.9 | 5.5% |
| O | 0.7251 | 71.5 | 0.0% |
| G | 0.6720 | not captured | 2.0% |
| B | 0.0018 | 0.10 | 0.0% |
| C | 0.0003 | 0.01 | 0.0% |

The low deltas are not evidence of convergence: Levels B and C have means of 0.10
and 0.01, so almost nothing is being rendered and there is nothing to be unstable.
Every level that renders substantial lit content still plateaus above 0.7. No
level demonstrates a settled image.

Saturation correlates with the worst cases but does not explain them. The two most
saturated levels are the two worst, yet Level O plateaus at 0.7251 with no
saturated pixels at all, so blown-out highlights are an aggravating factor rather
than the mechanism.

The untested lever is sample count against error character: more *independent*
blue-noise samples per pixel per frame lower the error's magnitude without
correlating neighbours, which is the one combination none of these measurements
cover. Outlier magnitude and exposure are the other untouched candidate, which is
where 11a deliberately declined to intervene on the grounds that resampling would
remove the root cause; it did not. The environment term also remains a one-sample
binary-visibility estimate against a bright analytic sky at every bounce.

The all-level smoke's stability numbers should not be turned into a regression
threshold until the near-black levels are understood, because a level that renders
nothing passes any stability bound trivially.

#### 11f. Deferred

- ReSTIR GI for the indirect channel. Deferred indefinitely: it is the same trade
  the 11c measurements reject, applied to a second channel.
- `kBufferTypeDiffuseHitDistance`, and confirming whether tagging
  `kBufferTypeLinearDepth` with no `kBufferTypeDepth` is supported.
- Dropping the forced `ePresetD` on every quality level.

## Test matrix

### CPU/unit tests

- World-coordinate and camera basis conversion against existing source/OpenGL expectations.
- Polygon triangulation, tangent generation, UVs, winding, and stable identity mapping.
- PBR sheet extraction, color-space declarations, manifest parsing, hashes, missing/corrupt assets, and deterministic rebuilds.
- BRDF energy sanity, finite output, PDFs, material guide values, and random-sequence reproducibility.
- Current/previous transform lookup, level-generation isolation, camera resets, object births/deaths, and analytical motion vectors.
- CMake configuration coverage for DXR-disabled, ID-independent DXR discovered through `PATH`, and Streamline-enabled builds discovered through environment variables, including missing-root, invalid-project-GUID, and altered-payload failures.
- Streamline option/tag construction without invoking the proprietary runtime.
- Jitter phase period, distinctness, and containment within the pixel footprint.
- Alias-table construction checked analytically against the requested
  distribution over a sweep of emitter counts, plus zero-weight, single-emitter,
  and rejected inputs.
- Hash-stream determinism against a pinned transcription, uniformity by
  chi-square over the candidate axis, and decorrelation between adjacent pixels
  and consecutive frames.

### Native GPU tests

- ID-independent debug-layer-clean diagnostic create/render/resize/minimize/restore/shutdown loops, including a multi-thousand-frame run.
- The dedicated foundation test does not require game content. The RTX
  all-level smoke separately renders each opaque world frame twice and requires
  nonzero, different readback checksums; it does not claim UI, weapon,
  projectile, or complete scene-category coverage. The foundation's synthetic
  scene also retains one camera/geometry state for 32 presented frames and
  rejects a repeated adjacent temporal sample before exercising motion.
- Adapter DXR-tier checks and clear unsupported-device diagnostics.
- After phase 3, adapter-LUID consistency, signed-plugin loading, `presentCommon()` execution, and clear unsupported driver/plugin/RR diagnostics.
- BLAS/TLAS correctness for static, updated, rebuilt, appearing, and disappearing instances.
- Guide buffer format, extent, state, range, finite-value, clear-value, and coverage assertions.
- Fixed-scene captures at RR input scales corresponding to quality and performance modes.
- Small camera translation/yaw and dynamic object motion with no unexplained guide collapse.
- Level A through P smoke runs, including water, doors/lifts, bitmap sprites, vector models, glare, weapon, text, and HUD.
- Streamline evaluation success, resource release/recreation, frame-token consistency, and history reset after mode changes.

### Existing project regressions

- Native Release build and CTest with DXR disabled.
- Native OpenGL renderer smoke and gameplay/source-runtime tests.
- Material/converter unit tests.
- Emscripten/Web build and smoke.
- A package/source scan proving no prohibited Q2RTX/GPL-derived renderer code, data, shader, sidecar, or generated asset was introduced.

## Acceptance criteria

The renderer is ready for normal use only when all of these are true:

- It is implemented entirely from the clean baseline, the two recorded MIT references, AB3D2 project code/assets, public graphics specifications, and the pinned NVIDIA SDK/documentation.
- The current explicit `renderer=rtx` creates the documented experimental raw
  opaque-world D3D12/DXR backend on supported Windows/DXR hardware and fails
  clearly elsewhere. It is not yet the complete renderer. Normal-use
  completion additionally requires the open scene/PBR/guide/presentation work.
  The opaque-world path does provide working Streamline DLSS-RR support on
  compatible hardware.
- The raw path-traced input is visibly noisy and physically coherent; disabling RR reveals no hidden temporal/spatial denoiser.
- Every mandatory RR input is present at the correct resolution, format, range, space, and frame, with dense camera/dynamic motion and correct reset behavior.
- Small camera movement does not erase lighting or reflection information from the reconstructed result.
- Near and far surfaces receive lighting according to traced visibility and material response, without BSP/PVS/zone lists, arbitrary light caps, or distance fallbacks.
- AB3D Gouraud/ZoneT lighting does not affect DXR radiance, while OpenGL behavior remains unchanged.
- Static and dynamic geometry, water, opaque sprites, vector objects, transparent effects, weapon, HUD, and text each follow their documented pipeline stage.
- Resize, minimize/restore, level transition, device loss, and shutdown are deterministic and debug-layer clean.
- OpenGL, Web, gameplay, source-runtime, converter, and material tests remain green.
- The distributable contains all required notices and only approved production binaries/assets.

## First actions for a fresh context

1. Confirm `git status` is clean and `git rev-parse HEAD` is this plan's commit or a descendant of `86241dd`.
2. Read `README.md`, `PORT_PLAN.md`, `src/scene_frame.h`, `src/renderer.{h,c}`, `src/renderer_rtx.h`, and `src/renderer_rtx_stub.c` before editing.
3. Read the two approved local references at the exact commits above and record any file actually adapted.
4. Phase 2 requires only the pinned `dxc.exe` discovered through `PATH`; record its version and hash.  Do not copy, unpack, discover, link, or stage Streamline while `AB3D2_ENABLE_STREAMLINE=OFF`.
5. Preserve the validated Phase 2 lifecycle, noisy path tracer, guide contract,
   and Streamline evaluation while completing the outstanding geometry and
   presentation work.
6. Keep the stable project GUID and custom-engine identity intact unless the
   project is deliberately assigned a different identity.
7. For a Streamline build, extract the verified v2.12.0 release archive outside
   the repository, set `AB3D2_STREAMLINE_ROOT` (or pass the deliberate CMake
   override), and retain all configure-time payload and package audits.
8. Commit each accepted phase separately with build/test evidence before advancing.

## Deliberately deferred decisions

- Exact shipping RR performance/quality mode and supported output resolutions: choose after the foundation can query the selected GPU and `slDLSSDGetOptimalSettings`.
- FP16 guide optimizations and normal/roughness packing: defer until separate FP32/FP16 debug comparisons show no integration error.
- Specular motion vectors: defer while specular hit distance is the validated baseline.
- Transmission/refraction, nested dielectrics, volumetrics, depth of field, motion blur, and frame generation: out of the initial renderer scope.
- Responsivity, disocclusion, transparency, or other optional Streamline masks/guides: add only when the baseline required inputs are proven and a reproducible capture demonstrates the need.
- Static BLAS compaction, bindless layout, sampler choice, and bounce-count/performance presets: measure after correctness; none may become a visual workaround.
- Spatial reservoir reuse: not deferred but contraindicated. It increases the
  neighbour correlation section 11c measures as harmful, so it may only be
  revisited behind its own measurement, never on the assumption that more
  resampling helps.
- ReSTIR GI: deferred indefinitely for the same reason.
- Independent samples per pixel per frame, and radiance outlier magnitude: the
  two levers section 11e identifies as untested. Both lower error magnitude
  without correlating neighbours, which is the combination no Phase 11
  measurement covers.

This plan intentionally leaves no compatibility path to the removed renderer. If a required behavior is missing, extend the clean renderer and its API-neutral `SceneFrame` evidence rather than reviving old code or data.
