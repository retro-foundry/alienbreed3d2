# Q2RTX Indoor Lighting Parity Handoff

Status: indoor-core implementation present; validation in progress. Primary
direct diffuse/GGX, full-rate direct sampling, active RR guides, real smooth
specular, packed-F0 rough reconstruction, and radiance isolations were
implemented on 2026-08-28. Reference-scene and all-level parity acceptance
now pass; moving-specular visual acceptance remains open.

Date: 2026-08-27

## Implementation progress (2026-08-28)

- Primary authored-emitter RIS now targets Fresnel-reduced diffuse plus the
  Q2RTX-weighted direct GGX contribution as one sample. Candidates are
  interleaved across up to two independent groups; each survivor, shadow
  result, and unbiased normalization feeds both lobes before the group mean.
  Metallic primary surfaces are no longer skipped merely because their diffuse
  reflectance is zero.
- The half-rate two-phase direct checkerboard was removed. Every primary pixel
  now evaluates its configured direct samples, eliminating a deterministic
  source of alternating direct-light noise before RR.
- RR now receives reconstructed specular albedo, material roughness in normal
  alpha, and a deterministic current-geometry mirror hit distance. Background
  remains zero; a surface mirror miss writes `SceneFarPlane`.
- `AB3D2_DXR_RADIANCE_CHANNEL` now isolates `emission`, `direct-diffuse`,
  `direct-specular`, `indirect`, `smooth-specular`, `rough-specular`, or the
  production `combined` sum.
- Every configured primary SPP now carries an independent GGX VNDF
  continuation when `rtx_max_bounces >= 2`. It accumulates crossed additive
  layers, reached-surface authored emission and diffuse local-light shading,
  applies the Q2RTX smooth/rough split and distance anti-flicker factor, and
  leaves misses black.
- Primary F0 is stored in a packed `R32_UINT` surface target. The fully filtered
  directional GI now supplies Q2RTX-style dominant-direction rough specular,
  with directionality roughening and compensation, directly into the specular
  lighting sum without diffuse-albedo remodulation.
- The accepted diffuse continuation and indirect reconstruction were not
  changed. Direct temporal/spatial reservoir reuse remains dormant as required
  by this plan; it has not been presented as completed ReSTIR work.

### Validation evidence (2026-08-28)

- Shader Model 6.6 compilation with warnings-as-errors and the Release renderer
  build passed.
- The complete Debug CTest suite passed: 29/29, including the DXR foundation,
  Streamline package checks, and the all-level RTX game smoke.
- The locked saved Level A `combined` capture measured frozen late delta
  `0.5049`, `793` saturated pixels, and zero >=16-code temporal outliers.
  Isolated `smooth-specular` measured `0.5129 / 0 / 0`; isolated
  `rough-specular` measured `0.5025 / 0 / 0` in the same tuple order.
- The moving Shotgun endpoint measured `4.8294`, so moving-specular visual
  acceptance remains open. The deterministic GPU reference scenes below are
  now complete.

### Direct stability follow-up (2026-08-28)

- A new activation of the dormant temporal/spatial direct reservoir path was
  measured and rejected rather than committed. Against the fresh estimator's
  moving combined delta `4.8305`, temporal plus spatial reuse measured
  `5.2146`, temporal-only `6.1642`, spatial-only `4.9955`, and a bounded
  one-frame temporal reservoir `5.2544`. Reuse also created tens of thousands
  of >=16-code moving differences. `SpatialShade` therefore remains dormant.
- Primary candidates were first interleaved across up to four independent fresh
  RIS groups, with one visibility ray and unbiased normalization per group.
  This retains the configured candidate-evaluation budget while replacing the
  single binary shadow outcome with a four-estimate mean at the default 16
  candidates. The later blue-noise sweep below consolidates production to two
  groups after measuring both variants.
- The locked combined result measures `0.5046 / 821 / 0` for frozen late
  delta, saturated pixels, and >=16-code frozen outliers, versus
  `0.5050 / 813 / 0` before the change. The moving endpoint is `4.8586`; this
  is not claimed as a scalar motion-delta win. Direct-specular captures show
  visibly reduced speckled breakup and RR trails at the central panel and right
  light while exact black remains black.
- The DXR foundation now includes a deterministic direct-light GPU reference.
  A visible non-emissive stone receiver is lit by an off-screen triangle
  cropped to a fully bright texel of the authored `technolights` emission mask.
  Readback asserts nonzero direct diffuse and dielectric GGX coverage for eight
  consecutive frames and zero non-finite combined radiance or mandatory RR
  guides. The native Debug and Streamline Release foundation runs pass. The
  earlier empty-scene phase still requires an exact-zero RGB checksum.
- The complete native Debug CTest suite passes `29/29`, including all GPU
  reference targets, the updated foundation, and the all-level RTX game smoke.
  The Streamline Release
  foundation also passes with the same direct-light and finite-guide contract.
- A separate `ab3d2_renderer_rtx_lighting_reference_test` uses an isolated,
  build-generated constant-PBR package; none of its synthetic bindings enter
  the production material catalog. It proves that visible non-emissive
  geometry with no emitter stays exact black, a material at `40/255`
  roughness has no direct GGX lobe, the lobe is active at `51/255` (`0.20`),
  and a `metalness=1` receiver has zero direct diffuse coverage but nonzero
  tinted specular coverage. `77/255` (approximately `0.30`) and fully rough
  dielectric cases also remain lit and finite. Native Debug and Streamline
  Release runs pass all cases over four consecutive frames each.
- The isolated test also distinguishes the smooth-GGX estimator from primary
  direct NEE through GPU readback. It requires a specular miss to remain exact
  black, an offset emitter behind the camera to appear with smooth-specular
  coverage and no direct-lobe coverage, and a reflected non-emissive wall to
  receive the separate emitter through reached-surface local-light sampling.
  A far side emitter first proves nonzero direct diffuse/GGX response; adding
  an off-screen blocker over its complete receiver cone then requires zero
  direct and smooth coverage plus an exact-black checksum. Native Debug and
  Streamline Release runs pass these cases.
- The same isolated package now includes a constant emissive additive-bitmap
  binding. A billboard behind the camera is invisible to primary and direct
  paths but must add radiance to the smooth reflected segment without
  occluding it. This exposed and fixed an emitter-count gate that previously
  suppressed every continuation ray in additive-only scenes even though
  additive geometry is correctly excluded from the polygon-emitter list. The
  native Debug and Streamline Release reference runs pass all four frames, and
  the complete native Debug suite passes `29/29`.
- A final synthetic scene activates visible additive emission, primary direct
  diffuse/GGX, diffuse continuation, real smooth specular, and reconstructed
  rough specular on the same deterministic sample-zero paths. Each of the
  seven diagnostic selections is rendered independently and copied from the
  actual `R16G16B16A16_FLOAT` noisy-HDR target before RR or post processing.
  All channels must contain positive stored radiance, and every combined RGB
  half-float must equal the sum of the six isolated values within their
  accumulated FP16 ULPs. The readback is opt-in and hidden-validation-only, so
  ordinary frames incur no copy or CPU synchronization. The dedicated native
  Debug and Streamline Release channel-sum invocations pass.

### Direct stability rejection matrix (2026-08-28)

- The locked saved-state baseline was re-confirmed after every experiment at
  approximately `0.5046 / 820 / 0` for frozen combined delta, saturated pixels,
  and >=16-code outliers, with a moving combined endpoint of `4.86`. Isolated
  moving direct diffuse remains `2.2286`; direct specular remains approximately
  `2.585`.
- More work alone did not improve reconstruction. Eight primary visibility rays,
  64 independent light candidates, two direct samples per pixel, and four fixed
  area samples for each emitter choice all increased the moving endpoint. A
  lobe-matched diffuse/GGX pair of RIS survivors likewise doubled visibility
  cost without reducing combined motion.
- Candidate stratification is not an accepted shortcut. Stratifying the emitter
  CDF or Latin-hypercube stratifying the two triangle coordinates increased raw
  and reconstructed motion. A 64-frame low-discrepancy lattice was effectively
  neutral and did not lower raw variance.
- Freezing primary direct samples proved that changing samples dominate the raw
  signal: frozen direct-specular delta fell from about `9.57` to `0.03`, and the
  reconstructed moving combined endpoint fell to `4.77`. It was rejected because
  screen-fixed samples produced visible camera-motion trails. Anchoring the same
  idea to primitive/material texels instead exposed structured grain and raised
  moving combined delta above `6`; a stable but under-sampled pattern is not a
  denoiser.
- The authored `wall_06_technolights` emission mask is sparse (about seven percent
  of level-zero texels are nonzero), but sparse-mask workarounds were not wins.
  Weighting emitter triangles by area times average texture power lowered frozen
  raw variance while raising moving combined delta to `5.07`; retaining the
  conservative maximum-radiance proposal is measurably safer. Extra per-emitter
  area candidates and coarse CDF changes were also rejected.
- RR guide and scene-policy probes were neutral: zeroing specular hit distance,
  allowing weapon pixels to retain history, and excluding the camera-attached
  weapon from world-light visibility all left the locked endpoint effectively
  unchanged. The active primary jitter is already exact zero and the saved run
  already uses Streamline's Quality preset. A radiance clamp of `10` was also
  neutral, showing that broad estimator variance rather than a few fireflies is
  the remaining problem.
- These results leave the production path unchanged. Do not revive the dormant
  screen-space reservoir, freeze its samples, increase ray count, or alter the
  conservative emitter distribution without a new oracle that beats both the
  moving metrics and the captured left-wall/panel behavior.

### Motion-compensated stability oracle (2026-08-28)

- The saved moving `delta` above compares identical display pixels while the
  validation camera deliberately yaws one mouse count per presentation. It
  therefore includes ordinary image translation and rewards blur. Negating the
  otherwise-correct motion vectors lowered that score to `4.65` while visibly
  smearing the left wall, so screen-space delta is retained only as a legacy
  diagnostic and is no longer the moving-lighting acceptance oracle.
- Hidden DXR validation now also reads the exact `R16G16_FLOAT` motion field
  supplied to Ray Reconstruction. It bilinearly maps output pixels to the
  render-resolution guide, follows its current-to-previous vector into the
  prior presented frame, rejects invalid or off-screen correspondences, and
  reports `reprojected`, `reprojected_outliers16`, and the sampled-pixel count.
  The metric evaluates every fourth pixel in each axis; its 1/16 validation
  grid keeps the all-level Debug smoke below its 120-second gate while retaining
  bilinear subpixel reprojection. Ordinary visible rendering still incurs no
  readback or synchronization.
- The locked production path measures `delta=0.5046` and
  sampled `reprojected=0.5074` for the frozen endpoint. During the yawing
  Shotgun endpoint it measures screen-space `delta=4.8599 /
  outliers16=77327`, but motion-compensated `reprojected=0.7057 /
  reprojected_outliers16=192` over `57261` valid samples. Future direct-light
  changes must improve the reprojected result without increasing screen-space
  trails or degrading the captured left-wall/panel detail.

### Blue-noise primary direct sampling (2026-08-28)

- The accepted fresh estimator now draws its primary emitter, triangle point,
  and reservoir-acceptance dimensions from the pinned spatiotemporal blue-noise
  sampler instead of an independent per-pixel hash. Dimensions 64--255 are
  reserved for the first 48 configurable candidates; a larger diagnostic
  candidate tail falls back to the unbounded hash stream before Sobol dimensions
  can wrap. Candidate count and unbiased RIS normalization are unchanged.
- Production uses two eight-candidate RIS groups at the default candidate
  budget. Compared with four four-candidate groups, this gives each survivor
  enough proposals to find sparse bright texels and reduces primary visibility
  from four rays to two. Frozen combined remains effectively unchanged
  (`0.5050/0.5072` at four groups and `0.5049/0.5071` at two, for
  screen/reprojected delta).
- Against the stride-sampled motion oracle, frozen combined remains effectively
  unchanged (`delta/reprojected 0.5046/0.5074` before and `0.5049/0.5071`
  after). The yawing combined endpoint improves from
  `4.8599/0.7057` to `4.8294/0.7021`; full-frame screen outliers fall from
  `77327` to `77056`, with `196` sampled reprojected outliers. The isolated
  direct-specular endpoint improves from
  `2.5863/0.4782` to `2.4987/0.4711`, and direct diffuse improves from
  `2.2285/0.4975` to `2.1634/0.4958` (screen/reprojected respectively).
- The captured Shotgun endpoint preserves sharp left-wall and panel detail with
  no structured grain or history trail. This is a sampling-distribution win,
  not temporal accumulation: direct screen-space reservoirs remain dormant.
- The corrected authored-emitter-only direct reservoir was also rechecked with
  the earlier full-resolution motion oracle. Temporal-only measured `1.0790`,
  temporal plus spatial reuse bottomed out at `0.7167`, and spatial-only reuse
  measured `0.7019`, all worse than the fresh estimator's `0.6871`. The spatial
  variants additionally used substantially more visibility rays and shifted
  the saturated-pixel population. No tested ReSTIR DI mode is accepted.

## Goal

Match the useful indoor lighting structure of Q2RTX while preserving the
current diffuse indirect-GI result, which is already accepted. The next work is
material-dependent direct and specular transport, not another exposure change
and not an ambient/shadow lift.

The target pipeline is:

1. Primary visibility, material reconstruction, visible authored emission, and
   non-occluding additive layers.
2. Direct diffuse plus GGX specular lighting from authored local emitters.
3. The existing accepted low-frequency diffuse indirect GI, unchanged.
4. Real first-bounce GGX specular transport for smooth materials.
5. Q2RTX-style reconstructed rough specular from the filtered directional GI.
6. One combined noisy-HDR input with correct diffuse/specular guides for DLSS
   Ray Reconstruction, followed by the already accepted bloom, adaptive tone
   curve, exposure bias, and SDR/HDR presentation.

No ambient term is part of this work. Exact black must remain black when no
authored emission or traced light path reaches a surface.

## Authority and inspected evidence

Q2RTX is a bounded renderer reference, not permission to invent AB3D2 content.
Use AB3D2 scene commands, geometry classifications, and PBR material metadata
to decide what exists in the scene. Use the Q2RTX files below for stage order,
BRDF behavior, channel separation, and filtering behavior.

Q2RTX evidence under `<Q2RTX-root>/src/refresh/vkpt/`:

- `shader/path_tracer.h` describes the four main path-tracing stages:
  `primary_rays.rgen`, recursive `reflect_refract.rgen`,
  `direct_lighting.rgen`, and one or two `indirect_lighting.rgen` passes.
- `main.c`, around the calls to `vkpt_pt_trace_primary_rays`,
  `vkpt_pt_trace_reflections`, `vkpt_pt_trace_lighting`, and
  `vkpt_asvgf_filter`, establishes the executed order.
- `shader/direct_lighting.rgen::direct_lighting` evaluates polygon, dynamic,
  and sun lighting into distinct high-frequency diffuse and specular channels.
- `shader/path_tracer_rgen.h::get_direct_illumination` and
  `::get_sunlight` use shadow visibility, GGX direct specular, Fresnel energy
  partitioning, and caustic visibility where enabled.
- `shader/brdf.glsl` supplies the Q2RTX reference equations for Schlick
  Fresnel, GGX, Smith masking, visible-normal sampling, reflectivity, and final
  diffuse/specular composition.
- `shader/indirect_lighting.rgen::indirect_lighting` makes the first
  continuation diffuse or GGX-specular, shades the reached surface, and keeps
  the specular result separate. Later continuations are diffuse.
- `shader/asvgf_atrous.comp` reconstructs rough specular from the filtered
  first-order directional low-frequency signal. The blend begins at roughness
  `0.20` and reaches the reconstructed path at `0.30`.
- `shader/reflect_refract.rgen` is a separate special-material surface
  replacement stage for water, slime, glass, chrome, screens, cameras, and
  transparent materials. It is not the ordinary opaque GGX continuation.
- `shader/god_rays.comp`, `god_rays_filter.comp`, `physical_sky.comp`, and the
  caustic path are optional/content-dependent stages, not ambient fill.

Current AB3D2 evidence:

- `src/renderer_dxr/shaders/path_trace.hlsl::RayGeneration` currently calls
  `sampleDiffusePolygonLight` and `sampleDiffusePath`, so the live estimator is
  intentionally diffuse-only.
- `writeDiffuseSurfaceGuides` writes zero specular albedo, a fully rough guide,
  and zero specular hit distance even though the material system already
  supplies base color, normal, metalness, roughness, and specular factor.
- `evaluateBsdf`, `sampleBsdf`, `sampleGgxVisibleNormal`, `surfaceF0`, and
  `reconstructionSpecularAlbedo` already contain most of the required clean
  PBR and RR mathematics, but the live path bypasses them.
- `resampleDirectTemporal` and `SpatialShade` are dormant screen-space direct
  reservoir experiments. `dxr_pipeline.cpp::record` deliberately does not
  dispatch `SpatialShade`; do not revive it for this milestone.
- `ReconstructIndirect` owns the accepted filtered, directional diffuse-GI
  composition point. It is the correct place to derive rough specular from the
  already filtered incident field.
- `environmentRadiance` is a hard-coded analytic gradient with no AB3D2 scene
  authority. It is dormant and must not be activated. The real backdrop is in
  `SceneEnvironment` and the packaged `environment_backdrop` material is
  currently non-emissive.
- `SceneVertex::source_light_level` includes legacy raster-lighting inputs,
  including some flash/torch/projectile contributions. Those values remain
  authoritative for the source/OpenGL raster path but are not physical
  radiance and must not be reinterpreted as path-traced ambient light.

The working tree already contains intentional tone-mapping, HDR, exposure, and
radiance-clamp changes. Inspect `git diff` before implementation and do not
revert or overwrite those changes.

## Invariants

- Do not change the accepted diffuse continuation distribution, ReGIR proposal,
  indirect history, directional representation, deflicker, wavelet filters, or
  final diffuse-GI remodulation.
- Keep `rtx_radiance_clamp=0` as the production default. A nonzero diagnostic
  clamp must operate on completed finite path samples; it is not a brightness
  control.
- Keep the current Q2RTX-derived adaptive tone curve, `rtx_exposure_bias=-1`,
  bloom, SDR encoding, HDR scRGB scale, 800-nit default scene target, and
  100-percent HDR saturation unchanged.
- Do not add zone ambient, constant fill, minimum illumination, AO brightening,
  shadow lifting, paper white, a second exposure stage, or a missed-ray color.
- Do not infer emission from base-color brightness. Only explicit emissive maps
  and factors enter the emitter distribution.
- Additive/glare geometry remains visible and non-occluding but does not become
  a light emitter unless its material contract explicitly gives it radiometric
  behavior.
- Keep primary visibility pixel-centered and preserve existing history-reset,
  motion, BLAS/TLAS, alpha-test, weapon, billboard, vector, and projectile
  behavior.
- Send the final lighting sum through Ray Reconstruction once. Do not denoise
  diffuse and specular with separate RR invocations.

## Implementation plan

### 1. Freeze and capture the baseline

Before changing shading, save the current locked-camera Level A corridor in
combined and indirect-only modes and record the all-level smoke/stability
metrics. These are regression oracles for the accepted diffuse GI and display
pipeline.

Add diagnostic radiance selections for:

- visible emission/additive layers;
- direct diffuse;
- direct specular;
- indirect diffuse;
- real smooth indirect specular;
- reconstructed rough indirect specular;
- combined.

These are diagnostic stage isolations only. Do not add new production `.ini`
quality or brightness controls.

### 2. Add full direct local-light BRDF shading

Keep the current complete authored-emitter distribution, per-cell ReGIR
proposal for continuation vertices, and fresh RIS normalization. The primary
candidate budget is interleaved across up to two independent groups, each with
one final visibility ray. Change only the primary receiver evaluation:

- Split the existing `BsdfEvaluation` logically into diffuse and specular
  contributions while retaining their matching mixture PDF.
- Use the existing project material contract:
  `diffuseReflectance = baseColor * (1 - metalness)`,
  `dielectricF0 = 0.04 * specularFactor`, and
  `F0 = lerp(dielectricF0, baseColor, metalness)`.
- Evaluate Lambertian diffuse with Fresnel energy removal and GGX/Cook-Torrance
  specular using the shading normal. Reject directions below the geometric
  normal and offset/trace visibility from the geometric normal.
- Use `alpha = roughness * roughness`, the existing Smith functions, Schlick
  Fresnel, and the existing `rtx_ndf_trim=0.9` contract.
- Match Q2RTX's direct-specular transition:
  `directSpecularWeight = smoothstep(0.16, 0.20, roughness)`.
- Stream RIS candidates using the luminance of diffuse plus weighted specular,
  then return both contributions from each group's selected sample and the same
  group-local unbiased normalization. Average the group estimates. Do not
  select a diffuse-only survivor and attach an unrelated specular value
  afterwards.
- Continue using the diffuse-only local-light evaluator at existing diffuse GI
  continuation vertices. The user has accepted that signal and it must remain
  unchanged.

Dynamic emissive meshes/billboards already enter the triangle-emitter path when
their packaged material is explicitly emissive. Do not create Q2RTX-style
sphere/beam lights from non-radiometric AB3D2 sprite brightness or additive
effects.

### 3. Activate stable material and RR specular guides

At every opaque primary surface:

- write the existing diffuse-reflectance guide;
- write `reconstructionSpecularAlbedo(surfaceF0, roughness, NdotV)` instead of
  zero;
- write the actual material linear roughness instead of `1`;
- retain the current shading normal, linear depth, and dense motion contracts;
- store raw material F0 in one internal packed `R32_UINT` surface-parameter
  target for the later rough-specular reconstruction. This is not an RR tag;
- trace a deterministic mirror direction and write the world-space hit
  distance to `SpecularHitDistance`. A surface whose mirror ray misses writes
  `SceneFarPlane`; a background pixel with no surface keeps zero.

The deterministic guide is independent of the stochastic radiance sample. Do
not manufacture a hit distance by reprojecting old radiance. Existing history
storage may remain for diagnostics, but the current guide must be computed from
current geometry every frame.

### 4. Add real smooth-specular continuation

When `rtx_max_bounces >= 2`, trace one companion GGX continuation for the
primary surface without replacing or probabilistically discarding the accepted
diffuse-GI continuation:

- Sample the GGX visible-normal distribution using the existing VNDF sampler,
  `alpha = roughness^2`, and `rtx_ndf_trim`.
- Because this is an independent specular estimator rather than Q2RTX's 50/50
  lobe lottery, use the specular estimator's own PDF and do not apply the
  original `1 / 0.5` lobe-choice correction.
- Use Q2RTX's real/fake division:
  `fakeSpecularWeight = smoothstep(0.20, 0.30, roughness)` and multiply the real
  path by `1 - fakeSpecularWeight`.
- Apply Q2RTX's default distance/roughness anti-flicker factor:
  `1 / max(1, hitDistance * roughness * 0.02)`.
- Accumulate authored additive layers crossed by the specular ray without
  letting them occlude it.
- On a geometry hit, accumulate explicit surface emission plus diffuse direct
  local-light shading at that reached surface, multiplied by the complete GGX
  throughput. Do not add secondary direct specular in this first slice; Q2RTX
  also calls the reached-surface local-light evaluator with its specular output
  disabled for this path.
- If the ray directly reaches a triangle already represented in the analytic
  emitter distribution, multiply that direct emissive hit by
  `1 - directSpecularWeight` so it complements rather than duplicates the
  primary direct-light estimator.
- A miss contributes zero in the indoor-core milestone. Do not call the
  hard-coded analytic gradient.
- Do not recurse specular paths or implement water/glass surface replacement in
  this stage.

`rtx_samples_per_pixel` repeats this complete companion estimator. The added ray
cost is intentional: preserving the accepted diffuse-GI sampling rate takes
precedence over reproducing Q2RTX's noisier lobe lottery.

### 5. Reconstruct Q2RTX-style rough specular from accepted GI

After the existing temporal/regional/wavelet indirect filtering, extend
`ReconstructIndirect` while its filtered first-order directional signal is
available:

1. Read the packed primary F0, actual roughness, shading normal, and primary
   view direction.
2. Compute the dominant incoming direction from the luminance SH vector using
   Q2RTX's `L0/L1` basis ratio.
3. Let the dominant-vector length define directionality. When it is below one,
   blend the mirror direction toward the normalized dominant direction,
   increase effective roughness toward one by `directionality^3`, and use the
   Q2RTX compensation scale `(roughness + 1)^3`.
4. Project the same filtered incident field used by diffuse GI, evaluate GGX
   toward that dominant direction, and multiply by
   `smoothstep(0.20, 0.30, materialRoughness)`.
5. Add this reconstructed rough-specular radiance to the specular contribution,
   not to the diffuse incident field. Do not remodulate it with diffuse albedo.

Raw/diagnostic indirect modes must preserve their named stage boundaries. The
production `full` mode performs rough-specular reconstruction from the fully
filtered directional signal; `raw` may show the corresponding unfiltered
reference but must not silently run production filters.

### 6. Compose once and retain dormant experiments as dormant

The final noisy HDR value is the sum of:

- visible authored emission;
- non-occluding additive radiance;
- direct diffuse;
- direct GGX specular;
- accepted reconstructed diffuse GI;
- real smooth-specular continuation;
- reconstructed rough specular.

Validate the sum for finite values, apply a nonzero diagnostic radiance clamp
only at the completed-sample boundary, and then invoke the existing single RR
evaluation. RR-off remains an unbiased/noisy reference path; do not add an
unrelated blur to imitate Q2RTX's ASVGF.

Do not dispatch `resampleDirectTemporal`/`SpatialShade`. Their screen-space
reservoir history, neighbor reuse, and hard-coded environment candidates are
not required for the Q2RTX indoor lighting decomposition and previously made
the active path harder to reason about.

## Content-gated stages after indoor parity

These are real Q2RTX lighting stages, but they are not the next fix for the
indoor corridor and must remain absent until their AB3D2 inputs are explicit.

### Environment and sky

- Upload the real `SceneEnvironment` backdrop and map misses to that authored
  image when `sky_enabled` is set.
- Treat it as illumination only when the environment material has an explicit
  nonzero emissive map/factor; build an importance distribution from that
  radiance.
- Do not invent a physical sky, sun direction, sun power, portal lights, or
  environment intensity from the backdrop's base color.
- Remove the hard-coded `environmentRadiance` gradient when the real contract
  replaces it; do not retain it as a fallback.

### Water reflection/refraction

- Preserve `SCENE_GEOMETRY_PRIMITIVE_WATER` in the DXR vertex/material
  classification instead of collapsing it to an ordinary world primitive.
- Use `SceneEnvironment.water_frame`, `water_scroll`, and the original
  `waterfile` data for the animated surface input.
- Then add a special reflection/refraction surface-replacement stage before
  ordinary direct lighting, using Q2RTX's water path only where it does not
  conflict with the authoritative AB3D2 animation/geometry data.
- Do not classify ordinary surfaces as glass, chrome, slime, screens, or
  security cameras without explicit material metadata.

### Caustics and volumetrics

- Add shadow-ray caustics only after water transmission and medium tracking are
  validated.
- Add god rays/participating media only after `SceneFrame` carries an explicit
  medium boundary, density/extinction data, and a real directional light.
- Until those inputs exist, leave these stages absent and document the missing
  evidence rather than inserting plausible constants.

## Tests and acceptance

### CPU/unit tests

- Dielectric F0, metallic F0, zero metallic diffuse, and specular-factor
  behavior match the existing PBR material contract.
- GGX distribution, Smith masking, VNDF sampling, and PDFs remain finite at
  grazing angles and for the minimum roughness.
- Direct-specular weight is zero at/below `0.16` and one at/above `0.20`.
- Fake-specular weight is zero at/below `0.20` and one at/above `0.30`.
- Direct-emitter and specular-hit complement weights sum to one.
- Packed primary F0 round-trips within the chosen quantization tolerance.
- Specular hit distance distinguishes background, geometry hit, and surface
  mirror miss exactly.

### GPU reference scenes

Create small deterministic scenes containing:

- diffuse and dielectric surfaces under one emissive triangle;
- a metal surface that has no diffuse lobe;
- roughness values around `0.16`, `0.20`, and `0.30` plus a clearly rough
  material;
- an off-axis emissive surface visible only through a reflection;
- a reflected non-emissive wall lit by another local emitter;
- occluded direct light, additive geometry on a reflected segment, and a
  specular miss;
- a completely unlit surface that must remain exact black.

The combined image must equal the sum of the isolated radiance channels within
the storage format's tolerance. All radiance and guides must remain finite.

### Game validation

Run at minimum:

```powershell
ctest --test-dir build -C Debug --output-on-failure
./build/dxr/Debug/ab3d2.exe --gpu-smoke all --renderer rtx
./build/streamline/Release/ab3d2.exe --gpu-smoke save --renderer rtx
```

Also capture the locked Level A corridor with combined, indirect-diffuse,
direct-specular, and indirect-specular selections. Compare against the frozen
pre-change indirect-only capture: the accepted diffuse GI must not change.

Acceptance requires:

- material-dependent highlights and reflections appear without a grey ambient
  floor;
- metalness removes diffuse response and colors specular response;
- rough materials transition smoothly from real to reconstructed specular;
- directly unlit black remains black;
- no new scene rebuilds or history resets occur during moving weapon,
  door/lift/water, billboard, vector, or projectile updates;
- RR guides remain stable during camera motion and do not create specular
  smearing at geometry edges;
- all Level A-P smoke, lifecycle, desktop-settings, material, and reconstruction
  tests pass;
- exposure, tone curve, bloom, SDR/HDR output, and the accepted indirect-GI
  diagnostics retain their pre-change results.

## Explicit non-goals for this handoff

- No authored-zone ambient or legacy Gouraud/ZoneT lighting in DXR.
- No physical sky or sun without scene data.
- No environment gradient or missed-ray fill.
- No direct screen-space temporal/spatial ReSTIR activation.
- No recursive reflections, water/glass refraction, caustics, or volumetrics in
  the indoor-core implementation.
- No new exposure, HDR, radiance-clamp, paper-white, or shadow controls.
- No reinterpretation of additive/glare effects as area, sphere, or point
  lights.

## Handoff completion definition

The indoor-core work is complete only when direct GGX, real smooth specular,
rough reconstructed specular, and active RR specular guides are all present;
the accepted diffuse GI and display pipeline remain unchanged; isolated-stage
captures sum to the combined result; and the CPU, GPU-reference, and all-level
validation above passes. A partial direct highlight without the specular
continuation/reconstruction and guide work is not completion.
