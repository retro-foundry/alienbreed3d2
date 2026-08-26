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

## Project-authored implementation

The files under `src/renderer_dxr/`, `src/renderer_rtx.cpp`, and
`tests/renderer_rtx_foundation_test.c` were written for this repository from
the clean baseline. All diagnostic, ray-tracing, and presentation HLSL is
project-authored. No source, shader, generated table, binary, scene data, or
asset was imported from Q2RTX, a removed renderer, or repository history
before clean baseline `86241dd`. The later emissive compatibility comparison
did not import a generated Q2RTX package, renderer binary, shader, or scene.

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

The per-vertex scaling of that authored emission by the source Gouraud shade
response is project-authored and takes its evidence from the maintained Amiga
sources, not from the sibling renderer, which has no equivalent. The row
coordinate is `hires.s:goursides`/`dofloorGOUR` and
`hiresgourwall.s:drawwallPACK*G`'s `source_light_level - 300`, and the animated
Gouraud that drives it is `newanims.s:brightanim` through
`Anim_BrightTable_vw`. The response is linear in that row coordinate, and the
darkest row keeps a `1/rows` residual rather than reaching zero. Two things
support the residual. The shipped art keeps one: the mean display luminance of
the shared floortile at offset `0x0101`, the emissive floor panel Level A opens
beside, is 167 through shade row 0 and 10 through row 30. And these panels are
the room's only light in this renderer, so extinguishing them would leave the
path tracer nothing to reconstruct. The remaining curvature between those
endpoints is not reproduced: the OpenGL forward path fits per-texel exponent
and floor maps from the same shade table, and the PBR material package carries
no equivalent.

That same response is read a second time, as the level's authored ambience.
`authoredAmbientRadiance` in `src/renderer_dxr/shaders/path_trace.hlsl` gives a
world flat or wall strip an outgoing radiance of `baseColor * emissiveScale`,
and only a secondary ray gathers it: a primary hit shades from traced lighting
alone, so a directly visible surface receives the authored level as fill from
whatever surrounds it rather than as a term of its own. This too is
project-authored, and the sibling renderer has no equivalent. Its unit is not
fitted. `hires.s:goursides` and `hiresgourwall.s:drawwallPACK*G` draw a texel at
its own display value through shade row zero, which fixes what "fully lit" means
in the source's authored lighting: the surface leaves exactly its albedo. A
Lambertian surface leaves `albedo * E / Pi`, so row zero is `E = Pi`, and the
scale of one in `AuthoredAmbientScale` reproduces the source's own brightness
instead of selecting a level. Because primary rays ignore it, what reaches the
image is the product of two albedos, roughly a tenth of the authored level for
this art, which is an order of magnitude below what the `0x0101` panel's 200
radiance delivers to the geometry around it. Metalness is not factored out: base
colour is a metal's specular tint rather than a diffuse albedo, but a rough metal
under ambient light does return
close to its base colour, and a `1 - metalness` factor would only black out
metal-panelled rooms. Only `DxrScenePrimitive::world` is read. Billboards,
vector models and the view weapon each write a shade of one so their own
emissive materials survive `doapoly`'s Gouraud modulation having been dropped
from PBR entities, so they are left to gather this from the world around them
like any other incident light.

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
carries the decoded source blend result, and exposure is one, so the texel is
the radiance. The one split that is reproduced is `draw_bitmap_glare` adding a
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
from triangle area, the renderer's conservative maximum-emissive-texture and
authored-vertex bound, a solid-angle cap, and an RMS receiver-volume distance.
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
radiance clamp, spatial filter, or alternate denoiser was added.

The separate one-bounce diffuse-indirect channel uses a project-authored
low-frequency reconstruction stage. Its separable cubic B-spline weights,
threefold step sequence, depth/geometric-normal guide tests, spatial-before-
temporal ordering, and history layout were derived and validated inside this
repository. No Q2RTX or other third-party reconstruction shader was inspected,
copied, linked, or staged for this change. The stage does not filter the fresh
direct/specular signal tagged for DLSS Ray Reconstruction.

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

The 2026-08-26 automatic-exposure and tone-mapping revision was designed and
implemented from this renderer's own Level A HDR diagnostics. It uses a sparse
64-bin log-luminance histogram, weighted 10th--98th percentile metering,
elapsed-time asymmetric adaptation, and a luminance-preserving quadratic toe
with an asymptotic rational shoulder. No external game renderer source, shader,
constant set, or tone-mapping implementation was inspected or adapted for this
revision. `dxr_auto_exposure.h` mirrors the shader contract for deterministic
CPU regression coverage.

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
