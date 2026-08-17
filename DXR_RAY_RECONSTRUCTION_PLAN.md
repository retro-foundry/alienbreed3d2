# Clean-room DirectX 12/DXR Renderer with NVIDIA Ray Reconstruction

## Handoff status

This document is the implementation authority for a new Windows RTX renderer for Alien Breed 3D II. It is intended to be sufficient for a fresh context to start at the current clean baseline and implement the renderer without consulting any removed renderer code.

The repository baseline for this work is commit `86241dd` (`Replace GPL RTX renderer with clean-room scaffold`). At that commit:

- The original gameplay port, `SceneFrame` producer, OpenGL renderer, Web build, and tests remain intact.
- `renderer=rtx` selects a deliberately non-functional, fail-fast scaffold in `src/renderer_rtx_stub.c`.
- No functional RTX renderer is present in the current tree.
- The next implementation must replace the stub incrementally while leaving OpenGL and Web behavior unchanged.

This is a plan, not an assertion that any DXR or Streamline code already exists.

### Current dependency gate

The ID-independent DirectX 12 diagnostic foundation may proceed now.  The
Streamline source checkout has been verified at `v2.12.0` / `e8aaa6e`, and the
official signed v2.12.0 release archive has been verified with SHA-256
`F5C0A3D870707DDDC3570FB4BCD3655CF48A8A68C3A9D342910CFA21B77DCF48`.
Extract that archive outside this repository and expose the resulting directory
through the `AB3D2_STREAMLINE_ROOT` environment variable; never commit its SDK
files or a local path.

No NVIDIA-issued Streamline application ID is currently available.  The pinned
DLSS-RR guide requires one for its NGX component, so no code may call `slInit`
or load Streamline until the user supplies it as an uncommitted configuration
value.  This blocks only the Streamline activation milestone.  The
ID-independent DirectX foundation, material/geometry compilation, acceleration
structures, and raw noisy path tracer may proceed without Streamline; leave
Phase 3 deferred and do not claim Ray Reconstruction support until its gate is
satisfied.

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
4. Add `AB3D2_ENABLE_STREAMLINE`, default `OFF`, valid only when `AB3D2_ENABLE_DXR=ON`.  When enabled, resolve the extracted pinned release root from `AB3D2_STREAMLINE_ROOT`, with an identically named CMake cache path as an explicit per-build override.  Resolve the NVIDIA application ID from `AB3D2_STREAMLINE_APPLICATION_ID`, with an identically named CMake cache string as an explicit per-build override; never write its value to source or logs.  Fail configuration clearly when either value or a required signed production file is absent; do not download or unpack an SDK during configure.
5. Follow the pinned manual-hooking guide only in the Streamline-enabled build.  Prefer static `sl.interposer.lib` integration for DirectX, and make the link graph explicit so that build has no incompatible direct DXGI/D3D entry path.  The ID-independent foundation may link the ordinary DirectX import libraries.
6. The Streamline-enabled build must pass the supplied application ID to `slInit`, disable OTA/downloaded-plugin flags, use only the extracted production plugin directory, verify NVIDIA signatures, and log exact loaded plugin paths/versions.  Do not add an invented, sample, zero, or source-controlled application ID.
7. When DXR is enabled, resolve `dxc.exe` with `find_program` from `PATH`; retain `AB3D2_DXC_EXECUTABLE` only as an explicit per-build cache override.  Record the resolved compiler version and SHA-256 in build evidence, then compile HLSL with that pinned tool at build time, starting with Shader Model 6.6 unless the pinned Streamline/DXR requirements dictate otherwise. Warnings are errors; generated DXIL belongs in the build tree, not hand-maintained C headers.
8. Stage only the material runtime files and, when Streamline is enabled, the needed production redistributables and licence/notice files beside the executable.  Runtime signature checks and loading use those full staged paths, never `PATH`.  The diagnostic-only build stages no Streamline DLLs.
9. `renderer=rtx` must fail with a specific reason if the OS, adapter, driver, or DXR tier is unavailable.  In a Streamline-enabled build, it must additionally fail for a missing/invalid application ID, plugin, or RR feature.  Do not silently substitute OpenGL after an explicit RTX request.

## DirectX 12 foundation

Implement and test the platform layer before ray tracing:

1. Create the SDL native window without `SDL_WINDOW_OPENGL`, then retrieve its `HWND` through `SDL_SysWMinfo`.
2. In debug builds, enable the D3D12 debug layer before device creation; make GPU-based validation an opt-in developer mode because of its cost.
3. Create a DXGI 1.6 factory and enumerate high-performance hardware adapters. Reject software adapters unless a separate test-only mode explicitly requests WARP.  Require an appropriate `ID3D12Device5+` interface and a nonzero `D3D12_OPTIONS5.RaytracingTier`.
4. Create a direct queue, flip-discard swap chain, three frame contexts, RTVs, command allocators, command lists, fence values, and one fence event. Never reset an allocator still referenced by the GPU.
5. Add named resources, complete HRESULT context, DRED/device-removed reporting, and orderly resize/flush/shutdown behavior. Use default heaps for resident geometry and textures, a bounded upload ring for transfers, and explicit state transitions/UAV barriers; avoid per-frame committed-resource churn.
6. Establish an SDR presentation baseline first: clear, draw a diagnostic triangle or compute pattern, copy to the swap chain, resize, minimize/restore, and run for several thousand frames with the debug layer clean.  This diagnostic consumes no game geometry or `SceneFrame` command and reports zero scene/UI coverage by design.

### Streamline activation gate

Perform this only after the user provides the NVIDIA-issued application ID and
enables `AB3D2_ENABLE_STREAMLINE`:

1. Validate the extracted v2.12.0 release layout, its recorded archive hash,
   and the full paths and signatures of `sl.interposer.dll`, `sl.common.dll`,
   `sl.dlss.dll`, and `nvngx_dlss.dll` before loading any module.
2. Configure manual hooking with the production `sl.interposer.lib`; request
   the DLSS/RR features that the pinned headers require, install the log
   callback, and call `slInit` with the supplied application ID before any
   DXGI/D3D API call.  Do not enable OTA or downloaded plugins.
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
2. Use a project-owned blue-noise/Sobol or similarly well-distributed sampler. Key it by pixel, sample dimension, and monotonically increasing rendered-frame index. Reset the sequence only on an explicit history reset.
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

- Require the NVIDIA-issued application ID from the uncommitted environment or explicit CMake override and validate the extracted signed Streamline v2.12.0 production layout.
- Integrate the manual-hooking, signature-validation, `slInit`-before-DXGI, LUID support-check, `slSetD3DDevice`, and shutdown ordering described above.
- Verify the production interposer dependency graph and `presentCommon()` path before any RR resource/tag work begins.
- If the application ID is unavailable, leave this phase deferred.  Phases 4--7 remain ID-independent and may proceed to the raw noisy-image milestone without loading or linking Streamline; Phases 8--10 must not claim a complete RR integration while this gate is open.

### 4. `Add renderer-native AB3D2 PBR materials`

- Add deterministic sheet extraction, manifest schema, hashes, and golden tests.
- Upload base color, normals, roughness, metalness, and explicit emissive textures.
- Add material debug spheres/planes and fallback reporting.
- Include required MIT/project/asset notices; include no Q2 package output.

### 5. `Compile SceneFrame geometry for DXR`

- Add shared tested world-coordinate conversion and triangulation.
- Add renderer-neutral object-space vector geometry where needed.
- Upload static and dynamic meshes, material indices, stable identities, and previous/current transforms.
- Render raster/compute debug views before acceleration structures.

### 6. `Build and validate DXR acceleration structures`

- Add default-heap BLAS/TLAS resources, scratch allocation, barriers, build/update policy, and shader tables.
- Trace primary visibility into IDs, normals, depth, and albedo.
- Validate all game levels, moving doors/lifts/water, sprites, and vector objects in PIX.

### 7. `Add the clean-room noisy PBR path tracer`

- Add deterministic stochastic sampling, metallic-roughness BRDF, emissive/environment next-event sampling, shadow rays, multiple bounces, and finite/PDF tests.
- Produce fresh un-denoised `R16G16B16A16_FLOAT` radiance each frame.
- Keep source Gouraud/ZoneT/PVST data unused in this pass.

### 8. `Generate complete Ray Reconstruction guides`

- Add separate diffuse/specular albedo, normals, roughness, linear depth, dense motion, and specular hit-distance resources.
- Add previous-frame identity/transform history and explicit reset epochs.
- Add every debug view/readback statistic and synthetic camera/object motion test.

### 9. `Integrate Streamline DLSS Ray Reconstruction 2.12`

- Add pinned SDK detection, licence staging, secure plugin load, feature checks, optimal fixed resolution, options, tags, constants, and evaluation.
- Restore command-list state after evaluation and validate with NVIDIA/PIX tooling.
- Add an RR-off raw-noise mode for diagnosis only, not a shipping denoiser fallback.

### 10. `Complete DXR presentation and regression coverage`

- Add exposure/tone mapping, post-RR transparencies, weapon, HUD, text, and optional NVIDIA transparency guides if captures prove they are needed.
- Add scripted camera/dynamic-scene captures, all-level native smoke tests, resize/device-loss tests, packaging, documentation, and licence audit.
- Run the complete OpenGL, converter/material, Web, and source-runtime suites.

## Test matrix

### CPU/unit tests

- World-coordinate and camera basis conversion against existing source/OpenGL expectations.
- Polygon triangulation, tangent generation, UVs, winding, and stable identity mapping.
- PBR sheet extraction, color-space declarations, manifest parsing, hashes, missing/corrupt assets, and deterministic rebuilds.
- BRDF energy sanity, finite output, PDFs, material guide values, and random-sequence reproducibility.
- Current/previous transform lookup, level-generation isolation, camera resets, object births/deaths, and analytical motion vectors.
- CMake configuration coverage for DXR-disabled, ID-independent DXR discovered through `PATH`, and Streamline-enabled builds discovered through environment variables, including missing-root or application-ID failures.
- Streamline option/tag construction without invoking the proprietary runtime.

### Native GPU tests

- ID-independent debug-layer-clean diagnostic create/render/resize/minimize/restore/shutdown loops, including a multi-thousand-frame run.
- The dedicated foundation test must not reuse the OpenGL all-level smoke's UI, weapon, projectile, or frame-checksum assertions before those DXR outputs exist.
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
- Phase 2's explicit `renderer=rtx` creates only the documented D3D12 diagnostic backend on supported Windows/DXR hardware and fails clearly elsewhere; it is not a gameplay renderer.  The completed renderer additionally requires the Streamline gate and then creates the full DirectX 12/DXR/RR backend on supported NVIDIA hardware.
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
5. Implement phase 2 only. Do not begin path-tracing or RR shader work until the D3D12 diagnostic frame, resize, fence, shutdown, and unsupported-hardware paths are validated.
6. After Phase 2 is committed, either implement Phase 3 when the user supplies
   the application ID or record it as deferred and continue only the
   ID-independent Phases 4--7 needed for a raw noisy image.
7. Before implementing Phase 3, extract the already verified v2.12.0 release
   archive outside the repository, set `AB3D2_STREAMLINE_ROOT` and
   `AB3D2_STREAMLINE_APPLICATION_ID` in the environment (or pass deliberate
   CMake overrides), and re-read the pinned licence, notices, headers, and
   guides.
8. Commit each accepted phase separately with build/test evidence before advancing.

## Deliberately deferred decisions

- Exact shipping RR performance/quality mode and supported output resolutions: choose after the foundation can query the selected GPU and `slDLSSDGetOptimalSettings`.
- FP16 guide optimizations and normal/roughness packing: defer until separate FP32/FP16 debug comparisons show no integration error.
- Specular motion vectors: defer while specular hit distance is the validated baseline.
- Transmission/refraction, nested dielectrics, volumetrics, depth of field, motion blur, and frame generation: out of the initial renderer scope.
- Responsivity, disocclusion, transparency, or other optional Streamline masks/guides: add only when the baseline required inputs are proven and a reproducible capture demonstrates the need.
- Static BLAS compaction, bindless layout, sampler choice, and bounce-count/performance presets: measure after correctness; none may become a visual workaround.

This plan intentionally leaves no compatibility path to the removed renderer. If a required behavior is missing, extend the clean renderer and its API-neutral `SceneFrame` evidence rather than reviving old code or data.
