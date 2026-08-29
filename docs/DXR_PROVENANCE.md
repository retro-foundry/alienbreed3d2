# DXR renderer provenance

This record applies to the clean-room Windows D3D12/DXR renderer introduced by
Phase 2 of `DXR_RAY_RECONSTRUCTION_PLAN.md` and its renderer-native material
and raw scene/image increments from Phases 4--9.
On 2026-08-17 the user explicitly authorized a narrow comparison with the
sibling `alienbreed3d2-rtx-renderer` to recover its emissive material behavior;
that exception is recorded below.
On 2026-08-20 the user separately authorized the plan-listed companion-weapon
files in that sibling as behavioral evidence, then directed a comparison of its
weapon PBR materials. No sibling texture or generated material package was
imported.
On 2026-08-26 the user requested a detailed breakdown of Q2RTX's renderer and
then directed continued investigation of its lower temporal noise. The exact
read-only Q2RTX audit is recorded below. No Q2RTX source text, shader, binary,
asset, generated map, or material package was imported.
On 2026-08-29 the user extended that request to Q2RTX performance parity. The
same clean checkout was read only to establish observable profiling categories,
default bounce-ray workload, dispatch scheduling, and optional dynamic-
resolution behavior. No implementation text or data was imported.

## Project-authored implementation

The files under `src/renderer_dxr/`, `src/renderer_rtx.cpp`, and
`tests/renderer_rtx_foundation_test.c` were written for this repository from
the clean baseline. All diagnostic, ray-tracing, and presentation HLSL is
project-authored. No source, shader, generated table, binary, scene data, or
asset was imported from Q2RTX, a removed renderer, or repository history
before clean baseline `86241dd`. The later user-authorized behavioral
inspections did not import a generated Q2RTX package, renderer binary, shader,
or scene.

The DXR-only build produces DXIL in the build tree and stages only its
project-built diagnostic, ray-tracing, and presentation shader objects beside
enabled executables. It does not include, link, discover, load, or stage NVIDIA
Streamline or NGX files.

The Phase 4 material build reads the committed project-authored
`textures_pbr/*.png` sheets and the authoritative GLFT-selected Amiga assets:
wall WADs, the floor atlas, shared vector texture maps/palette, all loaded
vector models, referenced bitmap/lighted/additive/glare WAD/PTR/palette
frames, the backdrop, and the display palette. The three existing UI font
PNGs are included as presentation textures. `tools/export_pbr_asset_pack.py`
decodes those sources into 973 material identities and writes five separate
editable PNGs for each beneath the category-sorted
`assets/renderer_dxr/materials/` root.
That is 4,865 PNGs covering walls, floors, weapon/vector faces, enemies,
billboards/effects, environment, and UI. Unauthored normal, metalness,
roughness, and emissive maps are explicit generated assets, identified in
`materials.json`, rather than runtime-generated data. Non-vector unauthored
maps use neutral defaults. Source-vector faces use the exact source albedo,
normal `(128,128,255)`, metalness `0`, roughness byte `184` (nearest to `0.72`),
and manifest `specular_factor` `0.35`. `waterfile` is retained
in the manifest as non-color texture-coordinate animation data.

`tools/compile_pbr_asset_pack.py` validates the category paths and dimensions,
records file and decoded-pixel hashes, and embeds each exact compressed PNG in
the indexed `AB3PBR6` runtime package. Startup reads only its catalog; runtime
pixels are decoded from a material's embedded PNG payloads on first use. The
process does not invoke or consume the old Q2 package builder, its output,
packed channels, or material files.

The authoritative `shared_wall` IDs come from the wall texture load order in
`amiga/ab3d2_source/modules/res.s:Res_LoadWallTextures`, as published by
`game_bootstrap_make_wall_surface` in `SceneMaterial.source_asset_id`.
The wall mip identities and crop rectangles additionally come from
`amiga/ab3d2_source/hireswall.s:Draw_Wall`: word `+10` is the U origin and
bytes `+18`/`+16` are the U/V repeat masks published through
`SceneGeometry.texture_window`. The project-authored DXR scene compiler uses
those fields directly, rather than filenames or material guesses, to isolate
each window before generating its five semantic-aware software mip chains.
Floor and ceiling mip identity follows
`amiga/ab3d2_source/hires.s:Draw_Flats` and `draw_FloorLine`: `whichtile` is
added to `Draw_FloorTexturesPtr_l`, while the
renderer-neutral vertices publish the same fixed 64-by-64 logical repeat. DXR
therefore uses the complete bound 256-by-256 PBR flat image and that authored
UV scale; it does not infer a tile or scale from its filename.
The directional footprint, output-resolution LOD, bounded line filter, and
trilinear atlas sampling are project-authored and import no Q2RTX texture code,
shader, generated mip data, or material package.

After the longstanding native texture blur was isolated in the saved Level A
corridor, the approved Q2RTX comparison tree was inspected on 2026-08-27.
`src/refresh/vkpt/textures.c`,
`src/refresh/vkpt/shader/global_textures.h`,
`src/refresh/vkpt/shader/path_tracer_rgen.h`, and
`src/refresh/vkpt/main.c` establish only the target behavior:
linear anisotropic minification, two directional ray-cone gradients, gradient
texture lookup, and resolution-scale LOD compensation. The DXR implementation
was independently derived for this project's packed software-mip atlas. It
differentiates the camera-ray/triangle-plane intersection, computes the texel
footprint's singular axes, uses a project-selected eight-tap bound, and performs
manual repeat-aware line/trilinear sampling. No Q2RTX constant, equation, source
text, sampler layout, texture data, or generated asset was copied.
The 2026-08-29 performance pass retained that project-owned packed atlas and
added a one-texel repeat gutter around every software mip. Exact authored-
emitter proposals now use a project-authored D3D12 hardware-bilinear level-zero
lookup over those gutters. Reached surfaces use the same hardware bilinear
primitive while preserving the independently derived directional footprint,
software mip choice, explicit trilinear blend, and line-filter tap positions
above. The representation and sampler integration were derived in this
repository, and no Q2RTX texture representation, descriptor layout, sampler
code, or shader expression was copied.
Authored sheets without a demonstrated runtime binding remain unbound. Source
wall IDs 0 and 12 and every otherwise unauthored entry now have committed
neutral placeholder PBR maps, so the runtime has no decoded-source material
fallback. The user-authorized comparison demonstrated the `floor_0201` binding
and the two-emitter catalog. Metalness and emissive intensity are not inferred
from brightness at runtime.

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

The later user-directed weapon-material evidence was committed
`src/shaders/primary.rchit` at sibling commit
`0a350f8ebbf206db53fb191314cbee078ef2b281`. Its source-vector branch assigns
roughness `0.72`, metalness `0`, geometric normals, and specular factor `0.35`
while sampling exact source-vector albedo. `src/shaders/q2rtx_common.glsl` was
read only to establish what that scalar represented. No sibling GLSL, BRDF,
asset, material package, or channel convention was copied. This renderer keeps
its project-authored metallic-roughness implementation: the manifest factor
scales dielectric F0, is used consistently by path sampling and evaluation,
and feeds the NVIDIA-guided RR specular-albedo calculation. The generated
roughness PNG uses `184/255`, the nearest representable 8-bit value.

An earlier project-authored experiment multiplied world PBR emission by a
linear approximation of the source Gouraud shade response and described an
`authoredAmbientRadiance` fallback. The fallback was never called. The
2026-08-26 hallway audit removed both concepts: Gouraud/ZoneT values are raster
lighting retained in `SceneFrame` for OpenGL, not authored outgoing radiance.
All world polygon emitters now carry neutral vertex strength, and source-only
brightness changes no longer trigger DXR geometry/emitter uploads. Explicit
glare/additive strengths remain separate and unchanged. This restores the
implementation plan's existing clean-room rule and removes a non-authoritative
attenuation that could reduce the Level A emitter to a small fraction of its
manifest factor before indirect transport sampled it.

The additive-effect model is project-authored from the source's own blended
draw paths. `objdrawhires.s:draw_bitmap_additive`, `draw_bitmap_glare` and
`DOGLAREPOLY` add their result into the frame buffer, and the source has no
depth buffer for them to write, so an additive surface is light that never
occludes. `traceSegment` in `src/renderer_dxr/shaders/path_trace.hlsl` is the
path-traced statement of that: a ray passes straight through a
`DxrScenePrimitive::world_effect` triangle, adds its emission, keeps its
direction and throughput, and spends no bounce, because passing through is not
a scattering event. The reconstruction guides come from the first surface
behind the effect, which is the only surface in the pixel with a depth, normal
and albedo of its own. `AnyHit` drops the layer for visibility rays alone -
they are the only rays that skip the closest-hit shader - so an additive effect
casts no shadow. No MIS weight applies anywhere: `compile_emissive_triangles`
keeps every `world_effect` triangle out of the emitter list, so no next-event
estimator samples one.

Its strength needs no fitted constant. The packaged emissive channel already
carries the decoded source blend result, and no scene-linear exposure
multiplier changes transport, so the texel is the radiance. The one split that
is reproduced is `draw_bitmap_glare` adding a
blend-table result where `draw_bitmap_additive` adds the texel at full
strength: `source_glare_additive_strength` in `dxr_scene.cpp` holds a glare
bitmap at the same 0.8 that `renderer_opengl.c` does, and a `predoglare` vector
face keeps full strength because the OpenGL additive vector pass draws at an
opacity of one. Projectiles are traced with every other world bitmap and vector
for the same reason: the source draws them from these paths, and
`source_bitmap_scene_compile_world` already carries the contact bias a
depth-ordered scene needs.

`src/scene_geometry_compile.c` is a project-authored extraction of the native
port's current coordinate interpretation and polygon triangulation. Its
coordinate evidence remains `modules/transform.s:RotateLevelPts`,
`hires.s:Draw_Flats`, and the `SceneWorldPoint`/`SceneGeometry` producer
contract. OpenGL now calls this renderer-neutral implementation; no geometry
rule came from a removed renderer or generated Q2 scene.

`src/renderer_dxr/dxr_scene.cpp` consumes that shared geometry
and the public `SceneFrame` contract. It requires the preconverted five-PNG
binding for every world surface and exact source-map/UV/glare binding for every
companion face, creates a renderer-local five-channel material atlas, and
constructs project-authored vertex/material buffers and BLAS/TLAS resources.
The current ray shader uses the licensed
blue-noise/Owen-scrambled Sobol package recorded below, jittered primary rays,
explicit dimension-addressed Lambertian/GGX sampling,
explicit emissive/environment next-event sampling, MIS, and an analytic sky
gradient. The renderer-native PBR material package is consumed directly by
this runtime slice.

On 2026-08-21 the user explicitly requested comparison with NVIDIA's samples
after the project's initial temporal reservoir produced visible moving brown
dots. The behavioral references read were NVIDIA RTXDI's public
`Doc/Integration.md` and the RTXDI Library's `DI/Reservoir.hlsli`,
`DI/TemporalResampling.hlsli`, `DI/SpatialResampling.hlsli`,
`DI/SpatioTemporalResampling.hlsli`, and `Utils/BoilingFilter.hlsli`, from their
public `main` branches on that date. The FullSample DI initial, temporal,
spatial, and shading passes plus the runtime's `ReSTIRDI.cpp` defaults and sample
quality presets were also inspected. They established the staged pass sequence,
initial-candidate `M = 1` ownership and selection stratification, selected-only
initial visibility, the fixed low-discrepancy neighbor-offset-buffer contract,
surface-aware normalization contract,
emitter-history mapping requirement, temporal neighbor search, spatial material/
depth/normal validation, naive-sample discounting, disocclusion recovery, and
visibility-discard and ray-traced correction semantics. The
project HLSL was independently
rewritten around the published ReSTIR reservoir equations and the renderer's own
resources. No RTXDI source text, header, shader, table, library, binary, resource
layout, or build dependency was copied, included, linked, or staged. The result
uses the project's existing emitter alias table, vertex history, material atlas,
sample streams, root signature, and renderer-owned dispatch sequence; it is not
an RTXDI library integration.

After finer temporal noise remained, the current public FullSample Ultra preset
and `DI/InitialSampling.hlsli` behavior were inspected again on 2026-08-21. They
showed one BRDF sample and one infinite/environment sample beside the 16 ReGIR
local samples, plus secondary-surface direct resampling. The project independently
implemented the corresponding behavior around its analytic sky, existing GGX/
Lambertian sampler, and project ReGIR entries. The project ReGIR table cannot
evaluate the reverse per-cell PDF of an arbitrary BRDF-discovered mesh emitter,
so that overlap is rejected instead of substituting the unrelated global PDF;
BRDF/environment overlap retains an exact analytic density. The balance-mixture
equations and heterogeneous sample encoding are project code;
no NVIDIA source text, packed layout, bridge code, or shader resource was copied.

The comparison also showed that NVIDIA's Medium and Ultra quality paths feed
ReSTIR from spatially informed ReGIR proposals. The official RTXDI ReGIR host
configuration, regular-grid coordinate contract, presampling contract, local
light selection contract, initial sampling contract, FullSample application
bridge, triangle-volume target, and `UserInterface.cpp` quality presets were
therefore inspected on 2026-08-21 as behavioral references. They established a
camera-centered grid rebuilt each frame, corrected RIS entries, jittered cell
lookup, complete out-of-grid selection, and the filter-free Ultra preset's
16 initial candidates, four spatial samples, 16 disocclusion attempts,
ray-traced correction, and disabled boiling/final-visibility reuse.

The project implementation is independent. Its regular 16-cubed grid contains
512 project-layout entries per cell, each built from eight candidates drawn from
the existing complete global alias table. Its volume target is project-derived
from triangle area, the renderer's conservative maximum-emissive-texture bound,
a solid-angle cap, and an RMS receiver-volume distance.
Each entry stores only the project emitter index and mathematically required
inverse proposal probability. No NVIDIA shader text, fitted distance formula,
light hierarchy, constant table, resource layout, generated data, header,
library, or binary was copied, imported, linked, or staged.

The Phase 8 guide resources and motion/history implementation are
project-authored. The specular-albedo guide deliberately implements the compact
`EnvBRDFApprox2` integration formula published in section 4.2.1 of NVIDIA's
pinned Streamline v2.12.0 `ProgrammingGuideDLSS_RR.md`; both the HLSL and the
independent CPU check cite that exact source. No Streamline header, library,
plugin, sample shader, or binary is included, linked, loaded, or staged by this
ID-independent guide slice.

The same pinned guide's required-resource list and specular-hit-distance section
were rechecked after residual temporal noise. They list specular hit distance as
the alternative to specular motion vectors and do not list diffuse hit distance
as a DLSS-RR input. The project therefore stopped tagging its stochastic diffuse
diagnostic and independently replaced the stochastic/reprojected specular guide
with a deterministic primary-surface mirror-direction distance query.

## Streamline DLSS Ray Reconstruction integration

Phase 9 uses the public NVIDIA Streamline 2.12.0 API and production SDK without
importing NVIDIA sample application code or shaders. The implementation in
`src/renderer_dxr/dxr_streamline.{h,cpp}` follows NVIDIA's public
`ProgrammingGuide.md`, `ProgrammingGuideDLSS_RR.md`, and released headers for
manual hooking, custom-project identification, per-frame resource tagging,
constants/options, feature evaluation, resource release, and shutdown.

The application identifies itself with project-authored GUID
`6b0d5e3a-a774-4e9e-8937-e6ca29a6885e`, engine `CUSTOM`, and version `0.1.0`.
The numeric application ID remains zero. This follows the current
`sl::Preferences` contract, in which `applicationId` is optional and
`projectId`, `engine`, and `engineVersion` provide the identity for projects
without an NVIDIA-issued ID.

The verified input is NVIDIA's official `streamline-sdk-v2.12.0.zip` release,
SHA-256
`F5C0A3D870707DDDC3570FB4BCD3655CF48A8A68C3A9D342910CFA21B77DCF48`,
corresponding to public source tag `v2.12.0` / commit
`e8aaa6eaac968711fb62473d4ae8256dde20919b`. It is extracted outside the
repository. CMake pins the exact 24-header include set, production interposer
library, four runtime binaries, and three notice/licence files consumed by the
build. No NVIDIA binary is committed here.

Only the production `sl.interposer.dll`, `sl.common.dll`, `sl.dlss_d.dll`, and
`nvngx_dlssd.dll` are staged. Streamline modules must pass the SDK's embedded
dual-signature check; the NGX module must pass Windows Authenticode chain
validation with publisher `NVIDIA Corporation`. The static interposer import
path is delay-loaded so this verification completes before the first
Streamline call, and is audited so Streamline-enabled executables do not
directly import DXGI, D3D11, D3D12, or Vulkan. OTA/downloaded plugins are
disabled and runtime loading is restricted to the executable directory.

The reconstruction matrices, camera constants, guide interpretation, fixed
optimal input sizing, resource-state restoration, and raw diagnostic mode are
project-authored integration code. Streamline and NGX supply the proprietary
Ray Reconstruction implementation at runtime under their staged licences.

## Blue-noise/Owen-scrambled Sobol sampler

On 2026-08-18 the user explicitly supplied and approved the authors' project
page for *A Low-Discrepancy Sampler that Distributes Monte Carlo Errors as a
Blue Noise in Screen Space* by Eric Heitz, Laurent Belcour, Victor
Ostromoukhov, David Coeurjolly, and Jean-Claude Iehl (SIGGRAPH Talks 2019).
The project page is
`https://belcour.github.io/blog/research/publication/2019/06/17/sampling-bluenoise.html`.
The algorithm uses an Owen-scrambled Sobol sequence, an optimized per-pixel
ranking key, and an optimized per-pixel scrambling key; the lookup performs
the two XOR operations documented by the authors.

`data/renderer_dxr/blue_noise_spp256.bin` is a mechanical byte packing of the
`SOBOL`, `SCRAMBLING_TILE`, and `RANKING_TILE` arrays from the MIT-licensed
Rust transcription `Jasper-Bekkers/blue-noise-sampler` 0.1.0, commit
`b1720f637a3580c8711580873dcf84cce82f6504`. The pinned `src/spp256.rs` source
has SHA-256
`1713E8F1A593B9505860E8CAB9E47976434EBD17915B2B5FCEAD635E00E71AC2`.
The 327,680-byte renderer package has SHA-256
`3381BA037DB1940BD3A9A82C4C09A1EE145060268C8E08D8A3D02AD5895D69AB`
and is rejected by CMake if altered. The retained and staged licence is
`docs/third_party/blue-noise-sampler-MIT.txt`.

The HLSL lookup and renderer package loader are project-authored adaptations.
Each bounce owns eight fixed dimensions: two for environment sampling, three
for emitter selection/position, and three for BSDF lobe/direction. Specular hit
distance is now supplied by a deterministic mirror-direction query from the
primary surface, independently of the stochastic continuation dimensions.
This contract was checked against NVIDIA's public Apache-2.0
`nvpro-samples/vk_denoise_dlssrr` `primary_rgen.slang`; the project implementation
was written independently and no sample code was copied. The supplied tables
optimize eight dimensions, so later eight-dimension groups use fixed
tile translations, following Belcour and Heitz's published dimension-padding
guidance. After each complete 256-sample block, a deterministic whole-tile
translation selects a different per-pixel ranking/scrambling key. This retains
the reference lookup within every block while preventing the aligned full-tile
pattern from repeating every 256 presented frames. No temporal accumulation,
production radiance clamp, spatial filter, or alternate denoiser was added; the
retained diagnostic clamp defaults to zero/disabled.

The separate bounded diffuse-indirect channel uses project-authored HLSL and
host code. `rtx_max_bounces` counts the primary surface and up to seven real
continuations. Each continuation owns a separate eight-dimension sample group
and a disjoint polygon-light candidate stream; later terms retain every
preceding diffuse reflectance. Its first full-resolution reconstruction was derived and validated
inside this repository. After the explicit 2026-08-26 direction, the bounded
Q2RTX audit below identified the missing stage structure. The replacement keeps
full-resolution validated temporal history gathered through four bilinear taps,
represents incident luminance with standard first-order real spherical harmonics
plus two opponent-chroma values, computes a broad seven-stage low-resolution
lighting-change gradient, integrates guide-compatible 3-by-3 regions at one-
third resolution, applies a regional luminance bound and three guided wavelet
stages, and reconstructs with four bilateral taps. The project signal contains
only sparse polygon-light transport rather than Q2RTX's complete LF input, so
signed temporal confirmation prevents alternating Monte Carlo changes from
triggering anti-lag. The stage does not filter the fresh direct/specular signal
tagged for DLSS Ray Reconstruction.

### ReSTIR GI comparison path

On 2026-08-26 the user explicitly directed a ReSTIR GI implementation after the
exact raw indirect signal proved too sparse for DLSS Ray Reconstruction alone.
Only primary publications and NVIDIA's public integration contract were used:

- Yaobin Ouyang, Shiqiu Liu, Markus Kettunen, Matt Pharr, and Jacopo Pantaleoni,
  *ReSTIR GI: Path Resampling for Real-Time Path Tracing*, Computer Graphics
  Forum 40(4), 2021, DOI `10.1111/cgf.14378`; NVIDIA publication page:
  `https://research.nvidia.com/publication/2021-06_restir-gi-path-resampling-real-time-path-tracing`.
- NVIDIA RTXDI public ReSTIR GI integration and shader-API documentation:
  `https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/RestirGI.md` and
  `https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/ShaderAPI-RestirGI.md`.

The repository implementation was written independently. It uses a
project-defined secondary-area-measure formulation, a project-defined 32-byte
triangle/barycentric reservoir, the existing project sample stream and
low-discrepancy neighbor offsets, and the documented basic/biased reservoir
normalization. No RTXDI SDK source, header, shader, generated table, library,
binary, or data was copied, adapted, included, linked, or staged. The prior
user-authorized Q2RTX inspection established that Q2RTX's low-frequency ASVGF
path is not ReSTIR GI. A later user-directed parity audit established the
observable broad-continuation behavior. The ReSTIR refinement reuses the
already project-owned low-frequency sampler and independently derives and tests
its solid-angle density and directional-density ratio; no GPL expression,
reservoir implementation, layout, or shader text was copied or adapted.

### User-authorized Q2RTX behavioral audit

- Local checkout: `C:\Users\paula\Documents\Projects\Q2RTX`
- Exact commit: `f2526e9a165949f66e91e82f0d63aa7bb2567b4d`
- Checkout state during inspection: clean
- Licence of inspected source/shaders: GPL-2.0-or-later
- Files: `doc/client.md`, `src/refresh/vkpt/asvgf.c`,
  `src/refresh/vkpt/profiler.c`,
  `src/refresh/vkpt/path_tracer.c`,
  `src/refresh/vkpt/shader/global_ubo.h`, `src/refresh/vkpt/bsp_mesh.c`,
  `src/refresh/vkpt/material.c`, `src/refresh/vkpt/textures.c`,
  `src/refresh/vkpt/vertex_buffer.c`, `src/refresh/vkpt/main.c`, and
  `src/refresh/vkpt/shader/{asvgf.glsl,direct_lighting.rgen,
  indirect_lighting.rgen,utils.glsl,
  path_tracer_rgen.h,light_lists.h,
  asvgf_gradient_reproject.comp,asvgf_gradient_img.comp,
  asvgf_gradient_atrous.comp,asvgf_temporal.comp,asvgf_lf.comp,
  asvgf_atrous.comp}`.

Only observable algorithm boundaries and representation choices were recorded:
secondary-hit polygon-light NEE supplies a directional low-frequency diffuse
channel; multi-tap temporal reprojection, large-region change detection, regional
downsampling, deflicker, guided low-resolution filtering, and bilateral
reconstruction stabilize it. The later audit also established broad radial
continuation only in Q2RTX's first indirect pass, ordinary cosine sampling in
its second indirect pass, retained cosine-estimator throughput,
geometric-normal secondary NEE, bounding-rectangle/average-color polygon
emitters whose texture energy is conserved, and full default radiance for the
converted `floor_0101` surface. `path_tracer.c` showed that Q2RTX dispatches at
most two indirect passes. The project independently generalized that observable
depth contract to its already-public one-through-eight setting; it did not copy
Q2RTX's dispatch or shader structure. The inspection also showed that this is
not a ReSTIR-GI pipeline. No GPL implementation text or expression was copied or
adapted. No Q2RTX dependency, source, shader, table, data, binary, or asset is
present in the build or repository as a result.

The 2026-08-29 performance extension recorded only observable boundaries:
`profiler.c` uses frame-latent timestamp pairs for the complete frame and named
renderer stages; `path_tracer.c` schedules primary, direct, and at most two
indirect dispatches separately; `shader/global_ubo.h` defaults to one bounce
ray and exposes an optional half-resolution diffuse mode; the first
`shader/indirect_lighting.rgen` pass selects one diffuse or GGX continuation;
and `main.c` has an optional GPU-time-driven dynamic-resolution controller that
is disabled by default. The project performance plan is independently written
for its existing D3D12 frame contexts, `SceneFrame` resources, HLSL, and DLSS
Ray Reconstruction integration. It does not copy Q2RTX's Vulkan query code,
GLSL control flow, constants, data layout, or dynamic-resolution implementation.
The independently written S0-S3 production scheduler now uses that observed
shape through project-owned `PrimaryVisibility`, `ShadePrimary`,
`DenseMatureContinuation`, and `BurstContinuation` DXR exports: one stored
primary hit, one shared direct survivor, one inverse-probability-selected first
continuation lobe, a dense mature phase, and a capacity-proved GPU indirect
burst list. Its AB3D2 transport equations, material/emitter representation,
history classifier, work-list layout, HLSL, and D3D12 host scheduling remain
project-authored; no Q2RTX shader or host expression was copied or adapted.

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

No substantial renderer implementation was copied. The sampler's third-party
data and required MIT notice are the explicit, bounded exception documented
above.

## Project-owned exposure and presentation

The 2026-08-27 presentation revision meters the full-resolution linear-FP16
image returned by Ray Reconstruction. Its independently written compute stage
implements the published Eilertsen/Mantiuk/Unger minimum-contrast-distortion
equations with the observable Q2RTX stage order and defaults requested by the
user: a centre-weighted tent-filtered 128-bin histogram over `[-24, 8]` stops,
70th--90th percentile log exposure, seven display stops, a `-12`-stop noise
floor, Gaussian slope filtering, and elapsed-time curve adaptation. The
presentation shader applies the observed exposure bias and SDR knee and blends
the adaptive result equally with auto-exposed Reinhard. Exact black remains
black. The former pre-meter scene-linear exposure multiplier was replaced by
the comparator's post-curve `-5` through `0` EV control with a `-1` EV default.
`dxr_tone_mapping.h` independently mirrors the GPU contract for
deterministic CPU regression coverage.

The following project-authored presentation stage extracts bright energy with
a smooth luminance response, applies separable nine-tap filtering at half,
quarter, and eighth resolution in linear FP16, folds the broad levels back into
the finer ones, and composites the bounded result before histogram metering.
The 8-bit SDR path performs the existing exact sRGB encoding, then sources each
channel's sub-half-code quantization dither from separate optimized dimensions
of the already pinned and licensed blue-noise/Owen-scrambled Sobol package.
`dxr_post_processing.h` mirrors the extraction and quantization bounds for CPU
regression coverage. No additional noise table or third-party bloom code was
introduced.

The subsequent project-authored output stage detects the window's current
monitor with DXGI 1.6 and selects either the established 8-bit sRGB swap chain
or native FP16 scRGB. After tone mapping, the HDR scene is scaled by its fixed
scene target divided by scRGB's platform-defined 80-nit reference and receives
a luminance-preserving saturation adjustment. The default target is 800 nits
and default saturation is 100%. The former project-invented Hermite paper-white
shoulder and component peak clamp were removed. Both graphics PSOs are
recreated when the RTV format changes. Hidden validation remains explicitly
SDR. `dxr_post_processing.h` mirrors the HDR scene scale and saturation bounds
for deterministic CPU regression coverage.

The user-authorized Q2RTX checkout was inspected to establish presentation
stage ordering, SDR/HDR output spaces, and the feature gap (adaptive tone
mapping, bloom, output dithering, and scRGB negotiation). No Q2RTX source text,
shader, constant set, curve implementation, table, binary, or asset was copied
or adapted. On 2026-08-27 the user explicitly compared the two renderers and
requested Q2RTX's less crushed dark response. `tone_mapping.c`,
`tone_mapping_utils.glsl`, `tone_mapping_histogram.comp`,
`tone_mapping_curve.comp`, `tone_mapping_apply.comp`, and the related defaults
in `global_ubo.h` were inspected at the already recorded clean commit. The HDR
menu, `main.c`, `draw.c`, and `utils.glsl` were also inspected to establish the
observable opt-in HDR policy, 800-nit scene target, 300-nit UI target, 100%
saturation default, and the separation of scene and UI scaling. A repository-
wide control audit found no Q2RTX path-radiance clamp setting, so AB3D2's
matching default is disabled. The UI target is not exposed until the pending
DXR HUD/text compositor can consume it. The audit recorded observable stage
ordering and runtime defaults; no source expression was transplanted. The
rejected AB3D2 square-root-histogram
approximation was removed. Its replacement was independently implemented in
C++ and HLSL from the equations in Eilertsen, Mantiuk, and Unger, *Real-time
noise-aware tone mapping*, ACM TOG 34(6), 2015,
<https://doi.org/10.1145/2816795.2818092>, using the comparator's observed
configuration. No GPL source text, shader expression, table, binary, asset, or
generated output was copied or adapted; Q2RTX remains a read-only behavioral
oracle and is not a build dependency.

## Historical published-mathematics implementation

Phase 11 rebuilds the direct-lighting estimator from two published papers. Both
are references in the same sense as the Heitz sampler description: only the
published mathematics was used, and no implementation of either was consulted.

- Benedikt Bitterli, Chris Wyman, Matt Pharr, Peter Shirley, Aaron Lefohn, and
  Wojciech Jarosz, *Spatiotemporal reservoir resampling for real-time ray tracing
  with dynamic direct lighting*, ACM Transactions on Graphics 39(4), SIGGRAPH
  2020. Supplies the reservoir update rule, the unbiased contribution weight, and
  the temporal combination weights.
- Mark Jarzynski and Marc Olano, *Hash Functions for GPU Rendering*, Journal of
  Computer Graphics Techniques 9(3), 2020. Supplies the `pcg` integer hash used
  for the resampling candidate stream, which needs far more dimensions than the
  eight the pinned blue-noise tables optimize.

Before the explicitly requested 2026-08-21 comparison recorded above, NVIDIA's
RTXDI SDK had been excluded and the first reservoir version was written only
from the papers. The replacement remains project-authored HLSL and CPU support:
no RTXDI header, shader, generated table, library, or binary is included,
linked, or staged. Reservoir resampling is a sampling technique rather than a
denoiser; the noisy radiance input remains stochastic, so disabling Ray
Reconstruction still reveals visible noise.

## Toolchain evidence

The validated Windows build used the Windows SDK DXC `1.8.2502.11`; the
compiler executable SHA-256 was
`7C6918A0E2D4E437629FA8549F5CE800970494780F363BBBE1E3D3034F435AEE`.
CMake records the resolved DXC path, version, and hash for every enabled build;
the local path is configuration evidence and is never written to source.
