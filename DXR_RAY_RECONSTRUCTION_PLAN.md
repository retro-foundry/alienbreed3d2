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
working experimental world/entity DXR path tracer described below; an
`AB3D2_ENABLE_DXR=OFF` build still contains the fail-fast stub by design.
Following explicit user direction on 2026-08-17, the sibling
`alienbreed3d2-rtx-renderer` source was inspected only to recover its emissive
material catalog and mask behavior. Generated Q2RTX package output remains
excluded; the result is rebuilt into the renderer-native material format.
Following separate explicit user direction on 2026-08-20, the narrowly scoped
weapon files recorded under Approved references were inspected to replace the
planned post-tone-map companion overlay with primary DXR geometry. The user
then directed a comparison of the old weapon PBR appearance; its committed
source-vector material constants were recorded as evidence. No sibling renderer
source or shader was copied into this tree.

The implementation has reached the Streamline DLSS Ray Reconstruction
milestone. The foundation was validated on a GeForce RTX 4080 in Debug and
Release, and the current PBR scene path was validated in Debug on a GeForce
RTX 3090, with Windows SDK DXC 1.8.2502.11 (SHA-256
`7C6918A0E2D4E437629FA8549F5CE800970494780F363BBBE1E3D3034F435AEE`). The
Phase 2 D3D12/DXR lifecycle remains the foundation. Phase 4 now exports every
game color-texture identity selected by the authoritative startup and content
tables into the committed, category-sorted, zip-ready
`assets/renderer_dxr/materials/` tree. Its 973 materials cover 13 bound and one
archived wall, 20 floor tiles, 625 vector-face regions (including 341 weapon
regions), 310 bitmap/
lighted/additive/glare variants, the backdrop, and three UI atlases. Every
material has separate base-color, tangent-normal, metalness, roughness, and
emissive PNGs: the original 13 project-authored PBR sheets and source-authored
emission are retained, while unauthored channels are explicit generated PNGs.
Weapon/vector defaults preserve the demonstrated roughness 0.72, metalness 0,
and specular factor 0.35; other generated channels remain neutral.
The package therefore contains 4,865 editable PNGs plus `materials.json` and
an artist README. The build validates every PNG, records deterministic file and
decoded-pixel hashes, and embeds the exact compressed PNG bytes in one
`AB3PBR6` runtime package. Startup parses only the package catalog; each
material's five PNG payloads are decoded on first use by the live scene. A
missing binding, malformed package range, corrupt PNG, or dimension disagreement
is fatal instead of invoking a hidden source-texture fallback. `waterfile` is
recorded separately as non-color UV animation data; water geometry uses its
selected floor PBR material.

The implemented Phase 5/6 slice shares the tested native world-coordinate
conversion and concave X/Z ear clipping with OpenGL, compiles opaque
`SceneFrame` world surfaces plus non-projectile bitmap/glare commands and world
vector models, resolves their required preconverted PNGs, uploads
positions/UVs/material indices and atlases, and builds default-heap BLAS/TLAS
resources. Stable static world meshes and dynamic door/lift/water, billboard,
vector-entity, and weapon meshes use separate BLAS objects. Renderer-neutral
bitmap compilation preserves `draw_Bitmap` placement, attachment, clipping,
flip, and mode semantics in a fixed six-vertex layout. Renderer-neutral world
vector compilation preserves `rotate_object` transforms, animation
interpolation, part order, and sector clipping while allocating three stable
triangle slots for every source fan triangle. Hidden, clipped, or inactive
records become zero-area slots, so ordinary animation only uploads vertices,
material indices, and emitter data before refitting the affected dynamic BLAS.
Material atlases are retained and the TLAS updates without a queue flush.
Transient projectile sprites, water-specific transmission/refraction, HUD, and
text remain excluded. The player companion weapon uses the same stable-vector
approach:
the source `rotate_object` pose is converted to camera-local level units using
the documented one-quarter-level-unit model scale, attached to the DXR camera
basis, and stored in its own alpha-tested dynamic BLAS. Weapon and world
instances share one TLAS mask and one nearest-hit query, so all primary,
secondary, and visibility rays can see both. Each companion face resolves its
exact source map/UV/glare identity to the corresponding editable five-PNG PBR
set. Its noisy radiance, real world depth, guides, and motion are written before
Ray Reconstruction. The source Shotgun firing sequence was measured at
45, 41, 36, 30, 38, then 45 visible triangles. Treating those source culls as
layout changes previously rebuilt the complete scene/material atlases, drained
the GPU queue, and invalidated reconstruction history. The camera-local compiler
now retains every source part/face slot in file order and represents a culled or
disabled face as an exact zero-area triangle. Firing therefore stays on the
existing vertex upload/dynamic-BLAS refit path without a queue flush or global
history reset; projected OpenGL/source behavior continues to omit those faces.
The camera-local weapon compiler also fixes every retained vertex's source-light
scalar at neutral one and does not evaluate `objdrawhires.s:doapoly`'s
directional flat/Gouraud response. PBR base color, normals, roughness,
metalness, specular factor, and authored emission remain intact; all incident
weapon illumination is traced. The projected OpenGL compiler continues to
apply the original directional lighting.

Phase 7 now writes one fresh un-denoised `R16G16B16A16_FLOAT` sample per pixel
and presents it with a full-screen tone-map pass. The path integrator evaluates
energy-consistent Lambertian diffuse plus Cook-Torrance GGX specular, samples
GGX visible normals with the matching mixture PDF, follows up to three surface
hits, and performs environment and area-emitter next-event sampling with shadow
ray paths and power-heuristic MIS. Emissive triangles come from the explicit
`technolights`, the source `floor_0101` emissive textures, and authored
`predoglare` companion faces. The first two are scaled to scene-linear radiance
by their manifest factors; companion glare uses its exact decoded source colour
at unit emission. All use a global
area-times-average-luminance distribution; source Gouraud/ZoneT lighting is not
consumed. CPU tests cover
the material equations, lobe probability, sampler/PDF agreement, normal
transform, deterministic sample sequence, PDF mass, and finite throughput.
The former linearly seeded xorshift stream has been replaced with the pinned
256-spp Heitz et al. Owen-scrambled Sobol sampler. Eight explicit dimensions
per bounce remove branch-dependent sample consumption, and translated optimized
tiles pad later dimension groups. The specular hit-distance guide now traces a
deterministic mirror direction from the primary surface every frame; a miss
reports the far-plane distance. Each completed 256-sample block applies
a deterministic tile translation so the reference package does not repeat the
same aligned screen-space pattern every 256 presented frames.
The visible Level A capture has also been checked through the hidden readback
path. Phase 8 now allocates and writes the seven required RR guide resources at
the primary hit. Diffuse/specular albedo, world shading normal, perceptual
roughness, linear view depth, dense scene motion, and deterministic specular
hit distance use the formats in the frame contract below. A renderer-owned
history retains the previous camera basis, per-frame Halton jitter, dimensions,
history epoch, and a GPU copy of the previous vertices; motion is
`previousPixel - currentPixel` in pixel units. `SceneFrame.history_epoch`
invalidates history across level loads and quickloads, while scene rebuilds and
resizes also reset it. Phase 9 integrates the pinned Streamline 2.12.0
production runtime using manual hooks and a stable custom project GUID. On a
GeForce RTX 3090 it initializes NGX, selects a fixed optimal low-resolution
input for the chosen quality mode, evaluates DLSS-RR, and presents the
full-resolution reconstructed HDR output. The raw-noise mode and every Phase 8
guide view remain available for diagnosis. Non-projectile bitmap billboards,
items, bitmap enemies, additive/glare effects, vector items, and animated 3D
enemies are now PBR geometry in the same pre-RR TLAS. Their exact active
material frames are decoded lazily; animation retains the atlas and
reconstruction history. Source flat/Gouraud lighting is neutralized for these
entities and the companion weapon, leaving illumination to the path tracer.
Raw per-guide readback statistics and ID overlays, transient projectile
sprites, water transmission/refraction, HUD, and text overlays remain
outstanding.

Phase 11 then attacked the reconstructed image's temporal stability, which had
never settled the way TAA does. Its first result is a measurement: the hidden
smoke freezes the camera, view, and `SceneFrame`, presents
`AB3D2_DXR_STABILITY_FRAMES` frames, and reports the mean absolute per-component
difference between consecutive presented frames. Fixed-phase jitter, non-degenerate
sky guides, and a reprojected specular hit-distance guide moved the Level A plateau
from 1.3596 to 1.1829 and, more importantly, made the image keep settling instead of
flatlining immediately. An alias table replaced the linear emitter
cumulative-distribution walk. The first temporal reservoir lowered raw variance
but used biased `1 / M` normalization, one exact history tap, unstable compact
light identities, and no spatial/disocclusion stage, so its negative measurements
do not characterize a complete ReSTIR estimator. Section 11c now records a
surface-aware ray-traced bias correction, validated emitter history, temporal
neighbor search, and staged current-frame spatial/disocclusion reuse. The symptom-level weight clamp and
boiling filter were removed. The corrected `16 / 20` path passed its interactive
moving-camera visual check on 2026-08-21 and remains historical evidence for
the now-dormant screen-space direct-light reservoir path. Finer temporal noise
reported later led to the heterogeneous completion in section 11c, which the
user accepted in motion on 2026-08-21. The active stripped diffuse renderer
keeps 16 fresh RIS candidates and instead defaults its independently
reconstructed low-frequency history to 256 samples.
`AB3D2_DXR_RESERVOIR_LIMIT=0` remains the history-off diagnostic; read section
11 before tuning the estimator.

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
       exact camera-relative ENT_NEXT_2 companion geometry
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
  -> multiscale linear-HDR bloom
  -> exposure and tone mapping
  -> output encoding and SDR blue-noise dithering
  -> transparent presentation effects, HUD, and text
  -> DXGI swap chain
```

The original Phase 9 target was a physically coherent noisy image denoised only
by NVIDIA Ray Reconstruction. Later Level A measurements showed that one rare
diffuse continuation per pixel is a distinct low-frequency signal that RR does
not reconstruct reliably by itself. The user therefore directed a dedicated
diffuse-only reconstruction path. That scoped path must remain separate from
fresh direct/specular radiance; it does not authorize a general pre-RR blur,
reflection denoiser, or TAA replacement.

The target is a stable and convincing PBR presentation of the native AB3D2
scene. Q2RTX is a bounded behavioral comparator only where an explicit
user-authorized inspection is recorded below; it is not the authority for
AB3D2 content, gameplay, or exact pixels.

## Non-negotiable clean-room boundary

The following rules apply to every implementation commit:

1. Do not inspect, copy, diff, port, link, regenerate from, or use Q2RTX source,
   shaders, renderer assets, or generated data except for a read-only,
   user-authorized behavioral audit whose exact revision and files are recorded
   in this plan and `docs/DXR_PROVENANCE.md`. No Q2RTX source text may be copied.
2. Do not recover renderer code from this repository's older commits. The history necessarily records the removal, but those commits are prohibited implementation material. Start from `86241dd` or a descendant and use only files present in that clean tree.
3. Do not add a Q2 BSP, PVS, cluster, ZoneT lighting proposal, light cap, importance workaround, temporal-reprojection hack, or Q2 material convention to the new backend.
4. Keep a provenance record for every imported source file, shader, generated table, binary, and third-party dependency. Copying a substantial MIT-licensed implementation requires retaining its copyright and licence notice.
5. Run a source and packaged-artifact audit before each renderer milestone. The new DXR directories must contain no Q2RTX-derived text, binary, or data.
6. AB3D2 source simulation and content remain the behavioral authority. The renderer may interpolate presentation data already exposed through `SceneFrame`; it must not invent or modify gameplay state.

The raw, committed images in `textures_pbr/` are project-authored inputs and may be used. Do not consume the generated `q2rtx_pbr/` package, Q2 `.mat` files, WAL files, Q2 roughness/metalness packing, or Q2 texture path conventions. Create a renderer-native material build from the source sheets.

The 2026-08-20 user-authorized sibling inspection is a scoped exception to the
removed-renderer restriction only for the weapon evidence listed below. It does
not authorize Q2RTX code, packages, algorithms, assets, or any other historical
renderer path.

On 2026-08-26 the user explicitly requested a detailed Q2RTX renderer breakdown
and then directed continued investigation of its lower noise. That authorizes
the separate, read-only Q2RTX audit recorded under Approved references. It does
not authorize importing GPL source, shaders, binaries, BSPs, material files, or
generated assets. The implementation in this repository remains independently
written against project resources.

## Approved references and exact revisions

### `Q2RTX`: user-directed low-frequency reconstruction audit

Local path: `C:\Users\paula\Documents\Projects\Q2RTX`

- Inspected commit: `f2526e9a165949f66e91e82f0d63aa7bb2567b4d`
- Licence of inspected shader/source files: GPL-2.0-or-later
- Inspection date and authority: 2026-08-26, after the user requested a
  detailed renderer breakdown and directed continued investigation of Q2RTX's
  lower temporal noise.
- Files inspected: `doc/client.md`, `src/refresh/vkpt/asvgf.c`,
  `src/refresh/vkpt/global_ubo.h`, `src/refresh/vkpt/bsp_mesh.c`,
  `src/refresh/vkpt/material.c`, `src/refresh/vkpt/textures.c`,
  `src/refresh/vkpt/vertex_buffer.c`, `src/refresh/vkpt/main.c`, and the
  shaders `asvgf.glsl`, `indirect_lighting.rgen`, `utils.glsl`,
  `path_tracer_rgen.h`, `light_lists.h`, `asvgf_gradient_reproject.comp`,
  `asvgf_gradient_img.comp`, `asvgf_gradient_atrous.comp`,
  `asvgf_temporal.comp`, `asvgf_lf.comp`, and `asvgf_atrous.comp`.

The audit established behavior and stage boundaries: diffuse continuation plus
polygon-light NEE at the secondary hit, a separate directional low-frequency
signal, bilinear/bilateral multi-tap temporal history, broad low-resolution
lighting-change gradients, one-third-resolution regional integration, explicit
regional deflicker, three guided wavelet stages, and bilateral reconstruction.
It also established that this path is not ReSTIR GI.
The later transport audit established that the low-frequency diffuse
continuation deliberately broadens its radial distribution to power `0.4`
while retaining cosine-estimator throughput, and that secondary polygon NEE
uses the geometric normal. Its polygon emitters use a bounding rectangle and
one texture-derived average color; because that average includes black texels
inside the rectangle, it preserves the emissive texture's integrated energy
rather than creating extra light. The converted AB3D2 map supplies full default
radiance to `floor_0101` and a `0.9` BSP surface factor to `technolights`; it
does not carry native Gouraud shade rows into either polygon-light power.
No GPL source text, shader, table, binary, asset, or generated output was copied,
adapted, linked, staged, or committed. The HLSL and host implementation here
were written independently for the existing D3D12 resources; the real
first-order spherical-harmonic basis used is standard published mathematics.

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

### `alienbreed3d2-rtx-renderer`: user-directed companion-weapon evidence

Local path: `C:\Users\paula\Documents\Projects\alienbreed3d2-rtx-renderer`

- Inspected commit: `0a350f8ebbf206db53fb191314cbee078ef2b281`
- Working-tree state at inspection: modified `src/renderer_vulkan_rtx.c` and
  `src/shaders/compose.comp`; those uncommitted user changes were read-only and
  were not altered.
- Committed evidence inspected:
  `src/renderer_vulkan_rtx_view_weapon.{c,h}`
  (`renderer_vulkan_rtx_view_weapon_prepare`) for its call to the shared exact
  companion compiler and source material/alpha data, and
  `src/renderer_vulkan_rtx_world_vectors.{c,h}` for the pattern of adding
  source vector triangles to a dynamic acceleration-structure input.
- At the user's subsequent request to reproduce the old weapon's PBR material,
  committed `src/shaders/primary.rchit` was inspected for the vector-material
  assignments only. It demonstrates exact source-vector albedo, geometric
  normals, roughness `0.72`, metalness `0`, and specular factor `0.35` for both
  weapon and world vector faces. `src/shaders/q2rtx_common.glsl` was read only
  to distinguish that scalar from a metalness value; none of its BRDF or GLSL
  implementation was copied.
- Uncommitted evidence inspected: the two modified files above only for the
  stated intent to retire the late companion count and identify primary weapon
  pixels. The diff contained no definition of `PRIMITIVE_VIEW_WEAPON` and no
  working primary-geometry producer, so it was not an implementation to port.
- AB3D2 authority used for the implementation:
  `amiga/ab3d2_source/hires.s:Plr1_Use`,
  `objdrawhires.s:draw_PolygonModel`, `rotate_object`, `PutinParts`, `doapoly`,
  and `predoglare`, together with the current clean-tree
  `source_vector_scene_compile_view_weapon`,
  `source_vector_model_world_offset`, and `SceneViewWeaponProjection`
  contracts.

The current D3D12 implementation is original to this tree. It reuses the
project-owned source compiler. The initial implementation reversed the DXR
primary projection and gave the dynamic companion BLAS a foreground-only mask;
following explicit user direction on 2026-08-20, that cleared-depth design was
replaced. `source_vector_scene_compile_view_weapon_camera` now retains the
source pose/part/face decisions while converting `rotate_object` eye values to
uniform camera-local level units (`x/256`, `y/256`, `-z/2`). This is the same
one-quarter-level-unit authored model scale documented by the original
full-screen path. DXR attaches those vertices to the camera basis, assigns the
same instance mask as the world, performs a single nearest-hit query, and emits
a two-word in-world-primary GPU diagnostic. No sibling C or GLSL file, Q2RTX
material convention, or generated package was copied or linked.

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

The initial implementation was written from the papers only. On 2026-08-21 the
user explicitly requested a comparison with NVIDIA's samples after visible brown
dots persisted. NVIDIA RTXDI's public integration document and its temporal,
spatial, fused-spatiotemporal, reservoir, and boiling-filter HLSL were then read
as behavioral references. The project shader remains independently written from
the published reservoir equations and the observed pass contracts: no RTXDI
header, shader, table, library, or binary is copied, included, linked, or staged.
`docs/DXR_PROVENANCE.md` records the exact scope. Resampling is a sampling
technique, not a denoiser: the path-traced input remains a stochastic estimate
and disabling Ray Reconstruction must still reveal visible noise.

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
  export_pbr_asset_pack.py           source assets -> sorted artist PNG package
  compile_pbr_asset_pack.py          validate/hash/embed PNGs + runtime catalog
assets/renderer_dxr/materials/
  README.md                          artist handoff and archive instructions
  materials.json                     source identity/channel manifest
  walls/                             category directories; each remains flat
  floors/
  weapons/
  vector_models/
  enemies/
  billboards/
  effects/
  environment/
  ui/
    *_base_color.png                 five maps per material in its category
    *_normal.png
    *_metalness.png
    *_roughness.png
    *_emissive.png
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
- Add a true object-space vector-model compiler for DXR world objects. The
  projected helper remains available for the OpenGL source presentation. DXR's
  companion path instead preserves the same source pose/part/face decisions and
  converts the original eye axes into camera-local level units at the documented
  one-quarter model scale before attaching them to the camera basis.

### PBR materials

Create a renderer-native manifest keyed by stable source material identity. It must describe, independently for each material:

- Linear base color/albedo texture.
- Tangent-space normal texture and normal strength.
- Linear roughness texture or scalar.
- Metalness texture or scalar.
- Optional emissive texture, tint, and physically documented radiance scale.
- Alpha mode and cutoff.
- UV transform.

`tools/export_pbr_asset_pack.py` follows the authoritative GLFT load order and
animation/model tables, decodes all renderer color-texture identities, and
merges the committed `textures_pbr/*.png` sheets where authored maps exist. It
writes five conventional, separately editable PNGs per identity into nine
class directories. Each category remains flat. Unauthored maps are generated
explicitly as tangent normal `(128,128,255)`, metalness `0`, and emission `0`,
preserving the base alpha. Non-vector roughness defaults to `1`. Weapon and
world-vector roughness uses byte `184` (the nearest 8-bit value to `0.72`) and
their manifest `specular_factor` is `0.35`, retaining the proven old-renderer
material settings until artists replace the maps or scalar. Additive/glare source art has an
explicit emissive map and unit factor. `materials.json` records class,
dimensions, source provenance, exact world/vector/bitmap binding, alpha mode,
color spaces, generated-channel list, and non-color source assets.

Authored wall panels are center-cropped to the authoritative source wall's
logical pixel aspect before all five channels are resized together to four
times that source extent. The packed WAD's one or two unused final columns are
not part of that extent. The `chevrondoor` replacement additionally carries an
explicit piecewise horizontal landmark registration, applied identically to all
five channels after Lanczos resizing. Its source boundaries at U 24, 43, 84,
and 103 then agree with the PBR artwork, keeping Level A's U 0..15 jamb window
inside the pipe panel. Floors are similarly exported at 256 by 256 for each
64-by-64 source tile. The source dimensions and any registration landmarks are
recorded in the manifest rather than inferred from a generated renderer
package. A shared image helper applies the Lanczos crop, resize, registration,
and encoded normal-Z floor used by the Q2 package. Tests require every world
output to be exactly four times its declared logical source dimensions.

Native shading uses a manual repeat-aware four-tap filter for every PBR channel.
All four loads stay inside the material's packed atlas rectangle, including at
UV seams, so filtering cannot leak a neighboring material. Alpha-test coverage
retains its exact point lookup. Walls, floors, and ceilings now receive
software mip pyramids at scene compilation. Each wall identity includes the
exact source U origin, U period, and V period from `Draw_Wall`, and its five channels are
cropped to that window. Each floor/ceiling identity uses the complete
256-by-256 PBR image for the fixed 64-by-64 source tile selected by
`Draw_Flats`. Their levels
are vertically packed below level zero. sRGB color/emission are reduced in
linear light, tangent normals are averaged and renormalized, and scalar
channels are reduced linearly. The ray shader converts its hit ray cone through
the authored triangle UV gradients and trilinearly blends adjacent levels.
Water and non-world material classes remain on the prior level-zero
path by design.

`tools/compile_pbr_asset_pack.py` rejects missing, extra, malformed, renamed,
or wrong-sized PNGs and private/absolute provenance paths, hashes files and
decoded pixels, and embeds the exact compressed PNG bytes in one indexed runtime
package. It must not invoke the old Q2RTX package builder or inherit its channel
packing and naming. The C++ library parses metadata for all 978 identities, then
decodes only the five PNGs for a material when the live world or companion first
resolves that binding. World and companion compilation require a matching
binding; there is no runtime texture synthesis or decoded-source fallback.

Decode base-color textures from sRGB to linear before BRDF use. Treat normal,
roughness, metalness, and emissive scalar data according to their declared
color spaces. Do not infer metalness or emission from pixel brightness at
runtime.

For a metallic-roughness model:

- `diffuseReflectance = baseColor * (1 - metalness)`.
- `dielectricF0 = 0.04 * specularFactor`, with manifest default `1`.
- `F0 = lerp(dielectricF0, baseColor, metalness)`; the factor therefore changes
  dielectric response without attenuating authored metallic reflectance.
- Feed linear roughness to Ray Reconstruction, while the microfacet BRDF may use its squared alpha convention internally.
- Compute the RR specular-albedo guide from material specular color, roughness, and view angle using the pinned NVIDIA RR guidance; do not simply write raw metalness or `F0` without validation.

### Emitters and environment

Build renderer-owned emissive triangle records from explicitly authored emissive material entries. Weight a complete global sampling distribution by triangle area and emitted luminance, use it to build the renderer-owned ReGIR proposal, and trace visibility rays against the TLAS. Receivers outside the finite grid sample that same complete distribution. This replaces every old spatial-light-list concept; there is no BSP, PVS, cluster, ZoneT, arbitrary light cap, or incomplete fallback.

Use the `SceneEnvironment` backdrop/sky through a documented lat-long or equivalent environment mapping and build an importance distribution when the environment is emissive. Source additive/glare sprites are visual effects, not light emitters, until the material manifest explicitly assigns radiometric behavior.

## Noisy path tracer

Current staged status (2026-08-26): after reducing the executed DXR path to
flat primary visibility, diffuse polygon-light transport has been reintroduced
without the former full PBR estimator. The camera ray remains pixel-centred
with zero frame-varying subpixel jitter and preserves flat base colour as an
inspectable material guide rather than adding it to HDR as self-emission.
Directly visible authored emission is shown. For each SPP sample, the primary
diffuse surface streams `CandidateCount` samples from the complete global
authored-emitter alias distribution through fresh RIS, converts area density to
solid-angle density, and traces visibility only for the survivor.
`rtx_max_bounces` now counts real surface depth: one evaluates the primary
surface only, while each value from two through eight traces one additional
diffuse continuation. The first continuation uses the broad low-frequency
geometric-normal distribution established by the Q2RTX audit; every later
continuation uses ordinary cosine sampling. Before the primary dispatch, the
existing camera-centred ReGIR grid is rebuilt from the complete alias table.
Every opaque indirect hit evaluates fresh polygon RIS from its own world-space
cell and multiplies later terms by all preceding diffuse reflectances. This
supplies Q2RTX's essential local-light-list proposal behavior while retaining
the stored categorical inverse probability. Environment lighting,
GGX/specular transport, authored zone ambient, and screen-space direct-light
ReSTIR reservoirs remain dormant. ReGIR supplies only a current-frame light
proposal. Fresh RIS uses the unbiased
`weightSum / (candidateCount * selectedTarget)` normalization and is not reused
across frames or pixels. Source additive layers are visible, non-occluding, and
accumulated along every segment, but are not area-light candidates.

The sparse diffuse suffix now has a dedicated low-frequency reconstruction
path. All indirect-light evaluations use geometric normals; only the first
continuation deliberately covers more grazing directions than an ordinary
cosine sample. The path tracer stores the complete configured suffix without
primary albedo as first-order directional luminance plus two opponent-chroma
values. Full-resolution history is gathered
from four bilinear taps through dense scene motion, rejected on depth/geometric-
normal disagreement, and maintained as a bounded running average. One current/
history luminance pair per 3x3 region is blurred through seven unguided wavelet
stages. Because this stripped polygon-only estimator is sparser than Q2RTX's
complete LF input, signed temporal confirmation rejects alternating Monte Carlo
changes before the resulting gradient shortens history and raises the current-
frame weight. Each guide-compatible 3x3 full-resolution region is then
integrated into one anchored value in a
one-third-resolution working image. An explicit regional deflicker bound runs
before three guided 3x3 wavelet passes at low-resolution steps 1, 2, and 4. A
four-tap bilateral reconstruction projects the directional field onto the
full-resolution primary geometric normal; primary albedo is then restored and
direct radiance is added. This reconstructs the bounded polygon-light suffix;
it neither invents ambient light nor implements ReSTIR GI. After the single
Ray Reconstruction evaluation, three separably blurred FP16 bloom scales fold
bright energy back into one full-resolution linear-HDR composite. A dedicated
compute stage meters that exact composited result through a noise-weighted
128-bin log-luminance histogram. Exact black is excluded and local-neighbour
consistency downweights isolated reconstructed fireflies. The 2nd--99th
percentile interval reports the occupied span, while the 10th--90th percentile
interval drives bounded elapsed-time exposure with a faster response to
highlights than darkness. The saved Level A calibration remains authoritative:
its `0.014` scene-linear key and quadratic `0.02` toe keep reconstructed
near-black transport below visible grey, followed by a monotonic rational
highlight shoulder. `rtx_exposure` remains an explicit multiplicative bias.
Metering, adaptation, curve constants, the dark-corridor failure distribution,
and guide rejection rules have CPU regression coverage, while hidden GPU smoke
reports the target and adapted exposure plus the measured luminance span.

The former equal-weight 3x3 cascade made every successful secondary path visible
as a square lattice that appeared and faded in dark areas. On the same frozen
Level A save over 32 frames, replacing that impulse response reduced the
indirect-debug mean frame delta from `0.1094` to `0.0660`; the final
Ray-Reconstruction presentation fell from `0.1080` to `0.0938` while retaining
the corridor fill. Those are retained historical measurements of the former
full-resolution B-spline stage. The current directional reduced-resolution
stage measured `0.0468` in the indirect debug view, a further 29% reduction.
Four-tap temporal reprojection with the 256-sample LF default then measured
`0.0347` on the same 32-frame saved Level A view. Applying the unconfirmed
gradient directly worsened that result to `0.0862`; signed confirmation restores
static-scene convergence while retaining a response to persistent same-direction
lighting changes. The corresponding composed RR result measured `0.1125`,
essentially unchanged from the direct/high-frequency-dominated result below.
The composed RR result measured `0.1098`--`0.1125` in repeated runs; its three
16-level temporal outliers did not increase, but automatic-exposure convergence
and unfiltered high-frequency/direct noise keep that whole-image number above
the former `0.0938` run. This is reconstruction of the explicitly separated
diffuse-indirect signal, not a filter on the fresh direct/specular input
supplied to DLSS Ray Reconstruction.

A matched 2026-08-26 reduction experiment then kept the same single final
DLSS-RR evaluation while stopping the LF path at explicit stage boundaries.
The 32-frame saved Level A indirect-view results were `1.5917` for exact raw,
`0.1181` for temporal-only, `0.1408` after regional integration, `0.1012` after
deflicker, `0.0780` after one guided wavelet, `0.0537` after two, and `0.0347`
after all three. Raw appeared stable after RR only because nearly all corridor
fill disappeared. Its corrected current-frame RGB path bypassed directional SH
projection as well as every temporal/spatial LF stage, measured `0.0906` after
RR, and exposed a starfield of rare nonzero indirect samples. Eight complete
samples per pixel revealed more corridor detail after RR (`0.2755`) but remained
substantially darker than the LF reconstruction. Temporal-only and one-wavelet
captures retained visible speckles or blotches. Two wavelets came closest, but
the isolated LF still
showed blotches and its moving Level A comparison was marginally worse than the
full path: late delta `0.3548` versus `0.3504`, saturation `76463` versus
`76213`, and walked-frame delta `16.0135` versus `16.0068`. No useful GPU-time
reduction separated the two results. The complete regional deflicker and all
three guided wavelets therefore remain the production path. The startup-only
`AB3D2_DXR_INDIRECT_RECONSTRUCTION` diagnostic preserves every measured stage
boundary without adding a second RR pass.

On 2026-08-26 a complete project-owned `restir` comparison mode was added at
the user's direction. Four current-frame continuation candidates use the
established broad `0.4` radial distribution over the primary geometric-normal
hemisphere and are streamed into one
initial reservoir without repeating primary direct lighting. A hit is stored as
triangle identity,
full-precision barycentrics, secondary outgoing radiance, effective sample
count, and finalized basic-resampling weight. The common sample domain is
secondary surface area: `p_A = p_broad cos_y / r^2`. Reconnection applies
`cos_x cos_y / (pi r^2)` multiplied by `p_broad / p_cosine`, which reproduces
the comparator's deliberate directional kernel while retaining unit response
to constant incident radiance. One motion-reprojected reservoir and four
low-discrepancy spatial reservoirs are guide validated, re-evaluated at the
current primary surface, and visibility tested before the selected result is
published. A final fresh visibility ray precedes primary-albedo remodulation.
The pass sequence is current sample -> temporal reservoir -> spatial reservoir
-> combined noisy HDR -> the existing single DLSS-RR evaluation; it runs none
of the SH, gradient, regional, deflicker, or wavelet stages. The implementation
uses the documented low-cost basic/biased correction and makes no unbiased-mode
claim.

This successfully changes the raw path's coverage rather than merely hiding
its sparsity. On the same captured 32-frame saved Level A corridor, exact `raw`
measured display delta `0.0865` with no saturated pixels, `restir` measured
`0.2103` with `2103` saturated pixels, and production `full` measured `0.1107`
with `3654` saturated pixels. The ReSTIR capture visibly lit corridor floor and
wall regions that were black in `raw`, but retained patchy single-reservoir
variation instead of matching the smooth LF result. On the moving Level A
smoke, ReSTIR late stability was `0.5122` versus `0.3500` for `full`; its second
48-frame Shotgun burst averaged `11.555 ms` versus `11.066 ms`, about 4.4%
slower, and the complete Levels A--P GPU smoke passed. ReSTIR GI therefore
remains the explicit comparison path rather than replacing `full`: reservoir
resampling improves path discovery, but it is not itself the smooth diffuse
reconstruction that DLSS-RR failed to supply from the sparse raw signal.

The initial version above traced one new GI continuation per configured SPP.
After the user's interactive check requested denser discovery, the ReSTIR path
was given a floor of four GI-only candidates while ordinary primary direct
lighting retained the configured SPP count. On the same captured saved view,
this reduced delta from `0.2103` to `0.1715` with no 16-level outliers. Moving
Level A late delta fell from `0.5122` to `0.4119`; the second Shotgun burst rose
from `11.555 ms` to `13.416 ms`, about 16%. For comparison, four complete SPP
reached `0.1081` saved and `0.2949` moving but cost `16.731 ms`, about 45% over
the one-candidate result because it needlessly repeated direct NEE as well.
Four GI-only candidates are therefore the ReSTIR floor; configured SPP above
four remains an explicit quality-for-cost option that raises both channels.

The subsequent hallway-energy audit removed an unrelated attenuation before
the estimator: world PBR emission had been multiplied by the source raster
Gouraud shade, despite this plan's requirement that DXR ignore Gouraud/ZoneT
lighting. World polygon lights now use neutral vertex emission strength, and a
brightness-only source frame is an unchanged DXR scene. The dormant
`authoredAmbientRadiance` fallback and its uncalled arbitrary ambience were
deleted. In the same saved Level A indirect-only ReSTIR smoke, the converged
exposure-meter mean rose from approximately `0.000013` to `0.000016`; a
32-frame quality-RR run measured display delta `0.0719`, zero saturated pixels,
and zero pixels changing by at least 16 display-code values. The broad ReSTIR
kernel changes where that energy is discovered rather than multiplying it, so
the fixed-view global mean is not expected to measure the doorway redistribution.

The following `rtx_max_bounces` audit replaced the dormant-depth branch with a
bounded loop and disjoint blue-noise/light-candidate streams per continuation.
On the same frozen Level A indirect-only ReSTIR/RR save, depth one was exactly
black as expected; depths two, three, and five produced distinct checksums and
display deltas `0.0911`, `0.0818`, and `0.0775`. Their display-space mean
luminance rose from `24.8076` at depth two to `26.8999` at depth three and
`27.4315` at depth five. This demonstrates that later paths execute and deliver
energy, but it does not make bounce count a coverage repair. The dark
right-hand hallway face has valid non-black diffuse albedo and a valid
geometric normal. Raising complete SPP from one to eight changed its measured
display-region mean only from approximately `17.10` to `17.14`. Its normal is
incompatible with the adjacent brighter faces under the current ReSTIR GI
spatial similarity gate, so those reservoirs cannot cross the corner. A deeper
suffix only begins after a useful first continuation has been discovered; it
cannot correct that first-vertex proposal/reuse limitation.

`AB3D2_DXR_RADIANCE_CHANNEL=indirect` is the startup-only isolation test for
this signal. It clears primary visible emission, additive radiance, and direct
polygon NEE only at the final composition boundary, after the unchanged path
trace has produced the secondary GI estimate and all reconstruction guides.
The isolated, remodulated secondary diffuse result therefore enters the same
automatic exposure and single DLSS-RR evaluation as the combined frame. This
is deliberately separate from `AB3D2_DXR_DEBUG_VIEW=indirect`, which presents
the incident-light diagnostic buffer directly and bypasses DLSS-RR.

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

Use specular hit distance for the first complete integration, not specular motion vectors. Trace a deterministic mirror direction from the primary surface and report its world-space distance (or the far plane on a miss), avoiding a separate virtual-reflection motion pipeline. Supply the exact additional world/view camera matrices named by the pinned `sl_dlss_d.h`. Once baseline quality is validated, specular motion vectors may be evaluated as a measured alternative, never as two simultaneously ambiguous inputs.

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
10. Composite linear-HDR bloom, meter and tone map that post-RR presentation,
    apply the selected output encoding/dither, submit, present, and only then
    promote current history to previous.

Ray Reconstruction does not support changing its input resolution in-place without reconstruction reinitialization. Use a fixed input size for the selected mode; on resize or mode change, flush, release RR resources, recreate the size-dependent targets, and set reset for the first valid frame.

Call `slIsFeatureSupported(sl::kFeatureDLSS_RR, adapterInfo)` for the selected adapter LUID. Report driver/OS/hardware/plugin failures verbatim enough to diagnose them. Do not claim RR is running unless evaluation succeeds and the expected plugin/version is logged.

## Transparency, sprites, water, weapon, and HUD

Establish the opaque RR path before adding ambiguous presentation layers:

- Opaque and alpha-tested world geometry participates in primary rays, depth, normals, materials, motion, and TLAS visibility.
- Non-projectile bitmap billboards use alpha-tested, camera-facing geometry in
  the TLAS. Bitmap items/enemies and their lighted animation modes resolve the
  exact `(asset, frame, mode)` five-map PBR identity. Additive/glare commands
  use alpha-tested emissive surfaces in the same TLAS, so they are visible to
  primary, secondary, reflection, and visibility rays and reach RR with valid
  geometry guides. They remain excluded from sampled area emitters because the
  source commands describe visual effects rather than radiometric lights.
- Transient projectile/contact sprites remain outside the DXR scene. Adding
  them requires a topology-stable slot policy that does not reintroduce the
  firing-time scene rebuild and reconstruction-history reset already removed
  from the weapon path.
- The camera-space companion weapon is implemented as pre-RR ray-traced world
  geometry rather than a post-RR overlay. Its exact `ENT_NEXT_2` source pose is
  converted to camera-local level units at the documented quarter-unit model
  scale, assigned the five preconverted PNGs selected by each face's exact
  source map offset, UV bounds, and glare flag, alpha tested in any-hit, and
  stored in a dynamic BLAS. It shares the world's instance mask and nearest-hit
  query; primary, secondary, and visibility rays therefore provide ordinary
  depth occlusion, mutual shadows, and PBR reflections. Current/previous
  camera-relative vertices produce valid depth, normals, materials, motion, and
  all mandatory RR guides before reconstruction. Authored `predoglare` faces
  map their decoded colour to unit surface emission but are excluded from the
  world's emitter sampling distribution; they do not use a post-tone-map
  additive blend.
- HUD/text remain presentation overlays after RR and tone mapping.
- Water surfaces already resolve their selected floor five-PNG PBR set and have
  valid moving geometry history. `waterfile` only animates texture coordinates
  and is not a color texture. A water-specific rough dielectric model remains
  pending; defer transmission/refraction until its ray type, absorption,
  nested-medium behavior, guides, and motion tests are specified.
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

Current status: the complete category-sorted artist package described above is
committed: 973 material identities and 4,865 directly loaded PNG maps. Exact
bindings cover every wall/floor slot, bitmap frame/mode, and vector/weapon face
region; the backdrop and UI atlases are packaged as unbound
presentation assets. The old-renderer-compatible colored `technolights` mask
and source-authored `floor_0101` panel are explicit emissive textures with
factor 200; `brownspeakers` and `technotritile` are non-emissive. Source
`floor_0201` is also bound to its authored PBR sheet. Missing/corrupt maps and
bindings fail loudly. Weapon and vector-face assets use their exact source
albedo plus generated roughness `184/255`, metalness `0`, and manifest
`specular_factor` `0.35`; the scalar is carried through `AB3PBR6`, the scene
material buffer, the clean-room BRDF, and the RR specular-albedo guide. Material
debug spheres/planes remain later work.

- Maintain deterministic full-source export, manifest schema, hashes, and
  inventory/golden tests.
- Upload base color, normals, roughness, metalness, and explicit emissive textures.
- Add material debug spheres/planes.
- Include required MIT/project/asset notices; include no Q2 package output.

### 5. `Compile SceneFrame geometry for DXR`

Current status: opaque static and door/lift/water world geometry,
non-projectile bitmap billboards/items/enemies/effects, animated vector
items/3D enemies, and the exact camera-relative companion weapon are
implemented with shared coordinate conversion, stable material indices, and
bounded three-frame dynamic GPU upload. Bitmap commands retain a fixed
two-triangle quad across clipping, visibility, and frame changes. World vector
commands retain source part/face order and fixed clipped-triangle capacity
across animation and active-part changes. Each entity category retains
current/previous vertices and refits only its dynamic BLAS when its pose,
placement, attachment, or material frame changes. The weapon follows the same
policy when its source pose or camera-relative world position changes.
Its DXR compile retains a fixed source-record layout across on/off, near-plane,
and backface culling; inert faces are zero-area slots. The actual Shotgun and
Assault Rifle action sequences have layout-hash regressions, and the hidden GPU
smoke presents 48 real Shotgun updates while asserting that the full scene
rebuild count does not change after selection. Camera-local weapon vertices
also carry neutral source light, with a regression proving that a fully dark
source Gouraud input remains fully dark in the projected/OpenGL mesh but cannot
modulate the DXR mesh.

- Add shared tested world-coordinate conversion and triangulation.
- Add renderer-neutral object-space vector geometry where needed.
- Upload static and dynamic meshes, material indices, stable identities, and previous/current transforms.
- Render raster/compute debug views before acceleration structures.

### 6. `Build and validate DXR acceleration structures`

Current status: opaque, two-sided static/dynamic world BLAS objects, dynamic
alpha-tested billboard/vector/companion BLAS objects, and one shared TLAS are
built and primary visibility is traced for all Levels A--P. Moving
door/lift/water vertex ranges retain PBR/emissive atlases, refit only changed
dynamic BLAS objects, and update the TLAS in place without the old per-frame
queue flush. The D3D debug-layer foundation test exercises this update path.
GPU diagnostics count primary-hit pixels separately for bitmap and vector
entities. Transient projectile acceleration structures, auxiliary guide
readback statistics/ID overlays, and PIX validation remain later work.

- Add default-heap BLAS/TLAS resources, scratch allocation, barriers, build/update policy, and shader tables.
- Trace primary visibility into IDs, normals, depth, and albedo.
- Validate all game levels, moving doors/lifts/water, sprites, and vector objects in PIX.

### 7. `Add the clean-room noisy PBR path tracer`

Current status: the historical complete estimator remains implemented but is
inactive apart from the direct and bounded diffuse polygon-light stage recorded
above. The executed shader covers world geometry, non-projectile bitmap/vector
entities, and the PBR companion weapon. Alpha-tested surfaces participate in
primary, continuation, and visibility traversal; additive/glare geometry is
visible but excluded from the area-emitter distribution. Later presentation
classes and transient projectiles remain incomplete.

- The pinned 256-spp blue-noise/Owen-scrambled Sobol sampler is implemented
  with explicit dimensions for every environment, emitter, BSDF, and specular
  guide decision. Deterministic tile translation between 256-sample blocks
  prevents an aligned long-run screen-space repeat. Add the small GPU material
  reference scenes.
- Produce fresh un-denoised `R16G16B16A16_FLOAT` radiance each frame.
- Keep source Gouraud/ZoneT/PVST data unused in this pass.

### 8. `Generate complete Ray Reconstruction guides`

- Current status: all seven mandatory guide textures are separate, named, and
  written by the world/entity/companion path tracer. Primary misses write zero albedo,
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

- Current status: implemented for the world/entity/companion pipeline. Quality,
  balanced, performance, and ultra-performance modes use Streamline's fixed
  optimal input dimensions and a full-resolution output. The renderer submits
  all required Phase 8 guides, matrices/constants, options, frame token, and
  viewport, restores presentation command-list state after evaluation, and frees
  resources across resize/shutdown. `AB3D2_DXR_RR_MODE=off` is the raw-noise
  diagnostic path. Interactive shutdown hides the window immediately, makes the
  GPU idle before releasing Streamline resources, and tears the already-idle D3D
  device down without a second proxy-queue signal after `slShutdown`.
- Add pinned SDK detection, licence staging, secure plugin load, feature checks, optimal fixed resolution, options, tags, constants, and evaluation.
- Restore command-list state after evaluation and validate with NVIDIA/PIX tooling.
- Add an RR-off raw-noise mode for diagnosis only, not a shipping denoiser fallback.

### 10. `Complete DXR presentation and regression coverage`

- Current status: exposure/tone mapping and shared-depth pre-RR PBR world,
  billboard, vector-entity, effect, and companion geometry are implemented.
  Hidden DXR smoke reads per-category primary coverage and a fresh-radiance
  checksum from a GPU UAV; the 2026-08-20 Level A
  run passed, and a diffuse-albedo capture confirmed the source-scale lower-view
  surface supplies real world depth and reconstruction guides. The configured
  exposure bias is `1`; a dedicated post-Ray-Reconstruction compute stage now
  meters the actual full-resolution linear-FP16 reconstructed image with a
  noise-weighted 128-bin histogram. Its 10th--90th percentile interval drives
  elapsed-time exposure adaptation; the 2nd--99th interval diagnoses the
  occupied span. The saved Level A corridor owns the `0.014` scene-linear key
  and quadratic `0.02` toe, so reconstruction residue remains dark instead of
  being redistributed across the display. Hidden GPU diagnostics expose both
  exposure values and the metered percentile range. CTest no longer supplies
  an exposure override, so its Level A--P smoke exercises the production
  presentation and both companion assertions directly. Three separably blurred
  FP16 scales now extract and composite bright energy in linear HDR before that
  histogram and tone curve.
  The explicit SDR path retains manual exact sRGB encoding and uses the pinned
  blue-noise/Owen-scrambled Sobol package for sub-half-code final 8-bit
  quantization dithering. Visible auto-mode windows now inspect the current
  monitor through `IDXGIOutput6`: Windows advanced colour selects an FP16 scRGB
  swap chain and `RGB_FULL_G10_NONE_P709`, while other monitors retain the SDR
  swap chain. The pipeline rebuilds both diagnostic and final-present PSOs when
  a cross-monitor move changes the RTV format. The HDR branch stays linear,
  anchors diffuse white to 200 nits by default, maps the tone-curve highlight
  shoulder to the display-reported peak, and performs hue-preserving peak
  compression without sRGB encoding or 8-bit dither. Auto mode falls back to
  SDR if FP16 scRGB presentation is rejected; an explicit HDR request fails
  clearly. Hidden validation remains forced SDR and its temporal-blue-noise
  regression allows only sub-code display variation. Transmissive/alpha-blended
  presentation, HUD, text, and optional NVIDIA transparency guides remain.
  `rtx_output=auto|sdr|hdr`, `rtx_hdr_peak_nits`, and
  `rtx_hdr_paper_white_nits` expose that contract through `ab3d2.ini`; matching
  `AB3D2_DXR_OUTPUT`, `AB3D2_DXR_HDR_PEAK_NITS`, and
  `AB3D2_DXR_HDR_PAPER_WHITE_NITS` environment overrides take precedence for
  one-run validation. The parser bounds both nit values to 80--10000 and rejects
  paper white above an explicitly configured peak.
- Add transmissive/alpha-blended presentation, HUD, text, and optional NVIDIA
  transparency guides if captures prove they are needed.
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
- The initial specular hit-distance guide was a reprojected running estimate
  blended towards a stochastic path-lobe measurement. The completed guide now
  traces a deterministic mirror direction from the primary surface every frame;
  a miss reports the far plane. Streamline 2.12 does not list diffuse hit
  distance as a DLSS-RR input, so that stochastic diagnostic is no longer
  tagged. These changes stabilize geometry guides, not radiance.
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

#### 11c. Correct spatiotemporal ReSTIR direct lighting — heterogeneous signal
implemented, measured, and visually accepted

The first temporal reservoir was not equivalent to the NVIDIA samples the user
was comparing against. It always finalized reuse with `1 / total M`, retained a
compact emitter index across scene updates without checking that the index still
named the same light, sampled one exact prior pixel, and had no spatial or
disocclusion reuse. An arbitrary inverse-PDF ceiling and a post-shading boiling
filter were later tried against the resulting brown dots. The filter made at most
a tiny visible improvement and the ceiling changed the estimator, so both have
been removed rather than retained as symptom-level fixes.

The clean-room implementation now has the correctness pieces that comparison
identified:

- The 48-byte double-buffered reservoir carries the selected direct-light sample,
  finalized inverse-PDF weight, history-domain count, and its owning surface's world
  position, shading and geometric normals, material index, and exact UV. Those
  fields reconstruct the prior BSDF domain without adding another G-buffer.
  A local sample stores its emitter and canonical triangle coordinates; an
  analytic-environment sample uses a reserved light identity and stores its
  packed world-space direction in the same sample word.
- A geometry-only scene update invalidates renderer history if compact emitter
  slots stop naming the same triangle or if that triangle's area PDF changes.
  A proposal-probability change alone remains compatible because each reservoir
  stores the inverse PDF that generated its selected sample. Otherwise
  `PreviousVertices` supplies the prior light
  position and emissive scale when evaluating a temporal normalization term.
- Motion reprojection first tries the exact prior pixel and up to eight randomized
  matches within the four-pixel temporal search footprint. Matching uses the
  previous position's linear depth, exact material identity, depth within 10%,
  and shading/geometric normal cosines of at least 0.5, matching RTXDI's default
  temporal thresholds rather than the former arbitrary `0.5% / 0.966` rejection.
- Initial candidates are finalized into one current-frame proposal, matching
  RTXDI's `M = 1` ownership rule. Candidate count therefore improves the initial
  proposal without multiplying the current frame's temporal influence or
  shortening the configured history length. Emitter-selection uniforms are
  stratified across those candidates, matching the sample pass instead of
  allowing a random candidate set to cluster on the same light buckets.
- Primary integration and temporal reuse write a dedicated scratch field. After
  a UAV barrier, a second ray-generation dispatch performs current-frame spatial
  reuse and final shading into the published history buffer. A history-bearing
  center samples one neighbor within 32 pixels; a fresh center makes eight
  disocclusion-recovery attempts. Valid neighbors with `M <= 2` are discounted
  rather than spread, matching RTXDI's naive-sampling threshold. A randomized
  cyclic start walks a fixed, project-authored 256-point low-discrepancy disk,
  satisfying the neighbor-offset-buffer contract without importing NVIDIA data.
  Spatial neighbors require exact material identity, depth within 10%, and
  shading and geometric normal cosines of at least 0.5.
- Finalization evaluates the selected light at the current surface and at every
  accepted prior surface. Its numerator is the target at the domain that supplied
  the selected sample and its denominator is the represented-count-weighted sum
  over all participating domains. Temporal correction checks visibility at the
  previous owner for every selected sample (the higher-quality presets disable
  the temporal visibility shortcut); spatial
  correction traces visibility at each accepted neighbor. This is the
  ray-traced surface-aware mode used by NVIDIA's higher-quality presets, not the
  former biased `1 / M` approximation.
- Initial RIS weights are unshadowed. Its selected sample alone traces the
  optional initial-visibility ray; an occluded selection loses identity and
  weight but retains `M = 1`. This matches NVIDIA's pass contract and avoids the
  former per-candidate visibility proposal. The spatial/shading stage performs
  final visibility again, so both fresh and reused reservoirs obey the same
  publication rule.
- The final shadow ray clears an occluded sample's light identity and weight but
  keeps its represented count and owner surface, so the empty proposal domain
  still participates in the next basic normalization. There is no reservoir
  weight clamp, radiance average, blur, or boiling-filter pass.

The first accepted revision still kept environment and BRDF strategies outside
the reservoir and covered authored area emitters only. Residual temporal grain
reported after that acceptance exposed this as a signal-coverage gap rather
than a reason to extend history. The primary initial pass now streams one
cosine-hemisphere analytic-environment candidate and one independently traced
BRDF candidate beside the configured local candidates. A BRDF miss stores its
world-space environment direction. A BRDF ray that reaches a mesh emitter is
rejected: the stochastic ReGIR table does not expose the reverse per-cell PDF
for an arbitrary discovered emitter, and substituting the global alias-table
PDF created rare oversized weights. Mesh emitters remain completely covered by
the local strategy. The environment strategies use an exact balance-heuristic
mixture PDF, and the ordinary continuation ray no longer double-counts those
direct paths.

On 2026-08-21 a second explicit comparison had also identified another
architectural mismatch: NVIDIA's Medium and Ultra sample presets feed initial
ReSTIR DI from a camera-centered ReGIR proposal. The project builds a regular
16-by-16-by-16 world grid
before the primary dispatch. Each cell holds 512 independent RIS entries; each
entry resamples eight candidates from the complete global alias table and stores
the selected emitter plus its inverse proposal probability. A UAV barrier makes
the rebuilt grid visible to initial sampling. Jittered cell lookup reduces hard
cell boundaries, and an out-of-grid receiver deliberately samples the complete
global distribution.

The project-owned volume target contains the physical quantities a spatial
light proposal needs without importing NVIDIA code or data: conservative emitted
luminance, triangle solid angle, and an RMS receiver-volume distance. The RIS
correction changes only which local candidate is proposed. Local and analytic-
environment strategies share the physical unshadowed direct integrand; the
environment also has the exact count-weighted BRDF-overlap density. Moving the
camera or grid therefore cannot change the estimated integral. There is no light
cap, reservoir-weight ceiling,
boiling filter, or post-shading blur in this path.

Secondary vertices do not allocate another pair of full-resolution history
reservoirs. Each instead draws two local candidates from the camera-centered
ReGIR cell shared in world space, one analytic-environment candidate, and one
BRDF candidate that safely overlaps environment misses. RIS selects one sample before visibility,
and the continuation ray carries indirect transport only. This closes the
former one-global-emitter-sample and binary environment-hit paths at later
bounces without adding roughly two more screen-sized 48-byte buffers.

The pinned Streamline 2.12 DLSS-RR guide lists specular hit distance, not
`kBufferTypeDiffuseHitDistance`, as the alternative to specular motion vectors.
The integration no longer tags its stochastic diffuse diagnostic. Specular hit
distance is now a deterministic mirror-direction query from the primary
surface instead of alternating between a stochastic lobe sample and reprojected
history. These are guide corrections only; no radiance is accumulated or
filtered by them.

The same comparison showed that `4 / 128` was not a coherent NVIDIA quality
preset. The filter-free Ultra structure uses 16 initial local-light samples,
four spatial samples, 16 disocclusion-boost attempts, ray-traced temporal and
spatial correction, no final-visibility shortcut, and the library's default
history length of 20. The positive-history path now uses those structural
spatial counts, and `16 / 20` replaces `4 / 128` as the visual-acceptance
configuration. NVIDIA's current integration documentation, ReGIR sampling and
presampling contracts, initial/temporal/spatial reservoir contracts, and sample
preset configuration were read as behavioral references after the user
explicitly requested the comparison. The shader here was written independently
around the published reservoir equations and project resources; no NVIDIA
header, shader, table, library, binary, or data is included or copied.

The alias proposal now uses conservative emitted-radiance bounds: triangle area,
the material's maximum emissive-texel luminance, and the maximum authored vertex
emission scale. This prevents a hot texel or triangle corner from inheriting a
probability based on a much dimmer average and supplies the complete build
proposal for the ReGIR grid above. A proposal-only probability change keeps
history because the stored inverse PDF already records the distribution that
generated the reservoir; identity or area changes still invalidate it.

Release shader/app builds and the focused reconstruction, sample-stream, and
scene-update tests pass in both build configurations. The earlier 192-frame
measurement, before the conservative emitter bound, gave a late Ray
Reconstruction delta of `1.4320` at `4 / 128` versus `1.2035` at `1 / 0`.
That regression identified proposal quality, not reservoir history length, as
the remaining estimator problem.

In a matched 96-frame Ray Reconstruction smoke after the bound correction,
`4 / 128` measured `7.0736` early and `1.2062` late, versus `7.5256` and
`1.2134` at `1 / 0`. Raising the candidate count to eight produced `7.0269`
and `1.2068`, so it supplied no material improvement. The late-frame count of
pixels whose maximum RGB component changed by at least 16 was `7399.8` at
`4 / 128` versus `7241.2` at `1 / 0`: the former large mean regression is gone,
but this tail remains 2.2% higher. That frozen-camera diagnostic was therefore
not used by itself to decide visual acceptance.

With ReGIR and the filter-free Ultra spatial counts active, a matched 96-frame
Level A Ray Reconstruction sweep at `16 / 20` measured `6.9608` early and
`1.1988` late, versus the already-recorded `7.5256` and `1.2134` at `1 / 0`.
That is a 7.5% startup improvement and a 1.2% steady-state improvement. The
large-delta tail was `7422.2` pixels versus `7241.2` at `1 / 0`, however, so the
frozen-camera metric does not prove that the visible moving dots are gone. The
old `4 / 128` combination measured `6.9953`, `1.2103`, and `7452.5` with the new
grid/stage, confirming that the NVIDIA-like count/history combination is the
better setting. On 2026-08-21 the user then performed the required interactive
moving-camera check at `rtx_light_candidates=16` and
`rtx_reservoir_limit=20` and reported that it looked good in motion. That closes
the original brown-dot swimming artifact without a weight clamp, boiling filter,
radiance average, or blur. A later report of finer temporal noise triggered the
heterogeneous primary/secondary completion above. Its first integrated `16 / 20`
96-frame run measured `5.1552` early, `1.7047` late, and `19701.0` large-change
pixels; the moving sequence measured `11.8601` and `172607.6`. That exposed a
wrong reverse-PDF substitution when a BRDF ray discovered a ReGIR mesh emitter.
Restricting that overlap to the analytically evaluable environment improved the
same measures to `5.0604`, `1.5685`, `18059.5`, `11.7100`, and `169291.6`.
Reprojected and deterministic specular hit-distance guides differed by only
`0.0024` in the late metric; the deterministic guide was marginally better and
matches the pinned Streamline contract directly. Reducing the history limit to
eight worsened the frozen and moving measures (`1.6693` late and `11.9937`
moving), so `16 / 20` remained the production default for that then-active
ReSTIR path rather than extending or shortening its history. The user then
reported the completed signal looked much better in motion and accepted
`16 / 20` for that path. An
explicit zero history limit retains the history-off diagnostic.

#### 11d. Historical biased temporal approximation and measurements

The former approximation was implemented with the environment left out of the
reservoir. Mixing the emitter and cosine-hemisphere strategies in one reservoir
requires the mixture source pdf to stay unbiased, and the cosine strategy's pdf
for an emitter-sampled direction is not computable without an extra ray. Emitters
alone are the dominant, clearly diagnosed noise source, so they were measured
first.

- The former 32-byte reservoir per render-resolution pixel, double buffered on the
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

The 2026-08-21 follow-up found two correctness defects behind the reported
positive-history artifacts and the failure of higher SPP to improve the visible
swimming:

- Renderer-owned reservoir and specular-hit-distance history used the
  jitter-free Streamline motion vector directly as a previous-buffer address.
  They now add the current jitter and subtract the previous jitter before the
  point sample. Streamline still receives the original jitter-free motion with
  `motionVectorsJittered=false`; only the renderer's private pixel history is
  corrected. `tests/dxr_reconstruction_test.cpp` pins a phase change that must
  cross into the adjacent previous pixel.
- With more than one path sample, only sample ordinal zero had valid primary
  motion and could advance the temporal reservoir, but every later historyless
  ordinal overwrote the stored result. Ordinal zero now exclusively publishes
  the pixel's one temporal reservoir; later ordinals remain independent fresh
  lighting/path estimates and contribute only to the per-frame radiance
  average.

These were correctness fixes rather than evidence that the earlier stability
conclusion had reversed. The later corrected ReGIR/ReSTIR measurements and
moving-camera acceptance result are recorded in 11c.

That moving-camera check was performed with four candidates and a history cap of
128. It confirmed that positive temporal reuse still left brown emitter samples
swimming as dots over otherwise neutral surfaces. Stochastic nearest reservoir
reprojection was then tested, but it produced no material improvement and made
the reconstruction slightly blurrier, so it was removed. Reservoir addressing
therefore remained an exact point reprojection in that implementation.

One production temporal-ReSTIR correction from that test was retained: a final
sample rejected by its shadow ray is invalidated before storage, so its
unshadowed importance cannot dominate later frames. That correction alone did
not close the visual issue.

A dedicated post-temporal boiling-filter compute pass was then tried, following
the placement and weight test used by
[RTXDI's direct-lighting boiling filter](https://github.com/NVIDIA-RTX/RTXDI-Library/blob/main/Include/Rtxdi/DI/BoilingFilter.hlsli).
The path-tracing ray-generation shader cannot synchronize a tile, so the pass
runs immediately afterwards over 16-by-16 groups. It compares each finalized
reservoir's inverse-PDF weight (`unbiasedWeight`) with the group's nonzero mean
and empties weights above eleven times that mean (filter strength 0.5). It runs
only when the history cap is positive.

It was a history-only correction, but the user observed at most a tiny
improvement. It and the arbitrary 20x inverse-PDF clamp were removed when the
underlying normalization and missing reuse stages were corrected in 11c.

**Historical result from the biased implementation, not acceptance evidence.**
With a frozen Level A camera and
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
and drift, which is what reads as boiling. Reducing estimator variance therefore
appeared to be the wrong lever for this renderer's output stability. Because the
tested estimator omitted basic bias correction and spatial/disocclusion reuse,
that conclusion does not apply to the implementation in 11c.

Resampling was then tested at four candidates and a history cap of 128. Those
were the values measured best
among the *enabled* configurations rather than best overall. Within them the
candidate count dominates and the cap is nearly irrelevant:

| Candidates | Cap 64 | Cap 128 | Cap 320 | Cap 640 |
| --- | --- | --- | --- | --- |
| 4 | 1.4120 | **1.4046** | 1.4088 | 1.4121 |
| 8 | 1.4097 | 1.4171 | 1.4214 | 1.4254 |
| 16 | 1.4455 | 1.4508 | 1.4467 | 1.4465 |
| 32 | 1.4771 | 1.4750 | 1.4693 | 1.4674 |

At that tested configuration the reconstructed image measured 1.4006 against 1.1896 for
the single-sample estimator, so the metric still prefers resampling off by 18%.
The raw path measures 13.83 with a ratio of 0.6517 against 29.44 and 0.9904, so
it is 53% better and converging for the first time.
`AB3D2_DXR_CANDIDATES=1 AB3D2_DXR_RESERVOIR_LIMIT=0` recovers the single-sample
estimator exactly, for comparison.

The former temporal acceptance test drew from an optimized blue-noise dimension rather
than the hash stream. That test decides whether a pixel keeps its history, so it
determines where stale samples sit on screen, and drawing it from the hash lets
neighbouring pixels hold their history in clumps. The change measures 1.4046 to
1.4006, which is inside the run-to-run spread of roughly 0.005, so it is retained
on principle and not on evidence: the stability metric is temporal and cannot
observe the spatial clumping this addresses. That property needs an eye on the
image.

The former conclusion that spatial reservoir reuse was contraindicated was
premature: the implementation being measured was not the comparable ReSTIR
estimator. Section 11c now implements current-frame spatial reuse with
ray-traced correction;
its own moving-camera result, not this historical table, decides acceptance.

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

A 2026-08-21 visual check found that raising SPP still swam after the history
overwrite and jitter reprojection fixes. That check exercised the biased
temporal estimator documented in 11d and is superseded by the accepted 11c
local-emitter implementation. The later heterogeneous 11c implementation also
removes the independent one-sample environment term from primary and secondary
surfaces; it still requires visual revalidation.

The all-level smoke's stability numbers should not be turned into a regression
threshold until the near-black levels are understood, because a level that renders
nothing passes any stability bound trivially.

#### 11f. Implemented comparison and deferred work

- ReSTIR GI for the indirect channel is implemented as the explicit `restir`
  comparison documented above. It resamples secondary path vertices through
  initial, temporal, and spatial reservoirs, independently of Q2RTX's inspected
  secondary-NEE reconstruction. Its measured single-reservoir variation keeps
  it out of the production `full` default.
- Dropping the forced `ePresetD` on every quality level.

## Test matrix

### CPU/unit tests

- World-coordinate and camera basis conversion against existing source/OpenGL expectations.
- Polygon triangulation, tangent generation, UVs, winding, and stable identity mapping.
- PBR sheet extraction, color-space declarations, manifest parsing, hashes, missing/corrupt assets, and deterministic rebuilds.
- BRDF energy sanity, finite output, PDFs, material guide values, and random-sequence reproducibility.
- Linear-HDR bloom extraction response, SDR blue-noise quantization bounds, and
  scRGB paper-white/peak mapping.
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
- `--gpu-smoke save` restores the executable-local save and adjacent desktop
  settings, freezes that exact scene/camera, and presents 32 frames. This is the
  acceptance view for corridor-fill changes; it replaces the former synthetic
  single-wall inspection without modifying the user's save.
- The dedicated foundation test does not require game content. The RTX
  all-level smoke separately renders each world/entity frame twice. An isolated
  pass may correctly return black when that view sees no authored source or
  sampled emitter connection; across Levels A--P at least one level must
  produce authored radiance and at least one frozen pair must differ as the
  indirect sample sequence advances while primary visibility remains
  pixel-centred. It additionally requires nonzero GPU
  primary-hit coverage for the initial and key-six Rocket Launcher companion;
  on the first requested level it also times the complete Shotgun firing
  animation and rejects any scene rebuild after weapon selection. It requires
  nonzero bitmap primary-hit coverage and uses an occlusion-free diagnostic
  view of a real active source vector command when an initial level camera
  cannot see one, then requires nonzero vector primary-hit coverage. It does
  not claim UI, projectile, or complete transparent/transmissive coverage. The foundation's synthetic
  scene also retains one camera/geometry state for 32 presented frames and
  requires its directly visible authored emitter to remain deterministic before
  exercising material-window and geometry motion.
- The 2026-08-20 Release all-level run reported 4,822 visible Level A bitmap
  primary pixels and 11,198/654,905 pixels from real vector-entity probes in
  Levels D/P. Its 48-frame Level A Shotgun sequence averaged 9.858 ms, peaked
  at 11.283 ms, and retained the two expected scene builds without a firing
  rebuild. The subsequent full 26-test Debug suite passed, including
  D3D12-validation-clean foundation and all-level game smokes.
- Adapter DXR-tier checks and clear unsupported-device diagnostics.
- After phase 3, adapter-LUID consistency, signed-plugin loading, `presentCommon()` execution, and clear unsupported driver/plugin/RR diagnostics.
- BLAS/TLAS correctness for static, updated, rebuilt, appearing, and disappearing instances.
- Guide buffer format, extent, state, range, finite-value, clear-value, and coverage assertions.
- Fixed-scene captures at RR input scales corresponding to quality and performance modes.
- Small camera translation/yaw and dynamic object motion with no unexplained guide collapse.
- Level A through P smoke runs, including water, doors/lifts, non-projectile
  bitmap sprites, vector models, glare, and weapon. Projectile, text, and HUD
  coverage are separate outstanding requirements.
- Streamline evaluation success, resource release/recreation, frame-token consistency, and history reset after mode changes.

### Existing project regressions

- Native Release build and CTest with DXR disabled.
- Native OpenGL renderer smoke and gameplay/source-runtime tests.
- Material/converter unit tests.
- Emscripten/Web build and smoke.
- A package/source scan proving no prohibited Q2RTX/GPL-derived renderer code, data, shader, sidecar, or generated asset was introduced.

## Acceptance criteria

The renderer is ready for normal use only when all of these are true:

- It is implemented from the clean baseline, the recorded MIT references, the
  scoped user-authorized sibling evidence above, AB3D2 project code/assets,
  public graphics specifications, and the pinned NVIDIA SDK/documentation.
- The current explicit `renderer=rtx` creates the documented experimental raw
  world/entity/companion D3D12/DXR backend on supported Windows/DXR hardware
  and fails
  clearly elsewhere. It is not yet the complete renderer. Normal-use
  completion additionally requires the open scene/PBR/guide/presentation work.
  The world/entity/companion path does provide working Streamline DLSS-RR support on
  compatible hardware.
- The raw path-traced input is visibly noisy and physically coherent; disabling RR reveals no hidden temporal/spatial denoiser.
- Every mandatory RR input is present at the correct resolution, format, range, space, and frame, with dense camera/dynamic motion and correct reset behavior.
- Small camera movement does not erase lighting or reflection information from the reconstructed result.
- Near and far surfaces receive lighting according to traced visibility and material response, without BSP/PVS/zone lists, arbitrary light caps, or distance fallbacks.
- AB3D flat/Gouraud lighting does not affect DXR entity or weapon radiance;
  their incident lighting is traced, while OpenGL behavior remains unchanged.
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
- Additional spatial passes beyond the one current-frame pass in 11c: measure the
  accepted implementation before paying for more ray-traced neighbor domains.
- Promoting ReSTIR GI over `full`, or adding further spatial iterations, remains
  deferred until a measured variant beats the production path in both saved and
  moving reconstruction quality.

This plan intentionally leaves no compatibility path to the removed renderer. If a required behavior is missing, extend the clean renderer and its API-neutral `SceneFrame` evidence rather than reviving old code or data.
