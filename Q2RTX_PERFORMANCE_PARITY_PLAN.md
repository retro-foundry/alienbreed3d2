# Q2RTX Performance Parity Execution Plan

Status: execution plan, 2026-08-29. The accepted lighting result remains the
quality oracle. S0-S3 is the production scheduler; performance parity is open.

This document is the performance companion to
`Q2RTX_LIGHTING_PARITY_PLAN.md`. It replaces a sequence of isolated
micro-optimizations with a small number of combined, measurable renderer
changes. The production renderer must keep the accepted image while moving its
actual ray schedule, light sampling cost, work layout, and material access cost
toward Q2RTX's. Reaching 60 FPS by lowering an unreported resolution, removing
energy, or weakening the validation scene is not parity.

## Authority and source boundary

The local Q2RTX source checkout is the behavioral and performance-architecture
reference:

- Q2RTX commit: `f2526e9a165949f66e91e82f0d63aa7bb2567b4d`.
- Native baseline commit before the current uncommitted performance work:
  `3020154dac91ef558e48b41fb0ff8d4fa303253b`.
- Refer to the external checkout as `<Q2RTX-root>` in committed documentation.
  Put its machine-specific absolute path in benchmark manifests, not source.
- Q2RTX is GPL-2.0. Use its observable pass structure, dispatch dimensions,
  settings, and estimator behavior as evidence. Do not copy its shader or host
  implementation into this project. Record any further inspection in
  `docs/DXR_PROVENANCE.md`.

The native lighting equations, authored AB3D2 material/emitter data, accepted
captures, exact-black rule, channel-sum rule, and Ray Reconstruction guide
contract remain authoritative for native output. Q2RTX is not permission to
invent or import Quake-specific sky, water, material, or light behavior.

## Definition of parity

Parity has two simultaneous gates.

1. Fixed-work parity: at the same output extent, internal traced extent, ray
   settings, camera class, VSync state, and fixed resolution, native median and
   p95 GPU frame time must be no slower than Q2RTX outside the dispersion of
   repeated control runs.
2. Shipping parity: the normal native configuration must sustain at least 60
   FPS and enter Q2RTX's measured shipping-default median/p95 envelope while all
   accepted lighting, motion, guide, and hitch tests pass.

For each percentile, the final budget is the stricter of `16.67 ms` and the
corresponding measured Q2RTX envelope boundary. A 60 FPS result is therefore
not sufficient when Q2RTX is materially faster. Conversely, a matched-work
stage win at a non-shipping setting does not close the user-visible task.

Report median, p95, p99, maximum, and run-to-run dispersion. FPS averages are
informational only. Report presentation, tracing, reconstruction, and output
extents beside every result.

## Current evidence and required reduction

The first visible A/B result was collected on an RTX 3090, driver
`32.0.15.9186`, Level A, `2568x1471`, RR off, with 120 warm-up and 120 measured
frames. It is useful for direction but is not the final baseline because it is
a Debug build and is shorter than the acceptance protocol.

| workload | GPU frame median / p95 | primary/radiance median / p95 |
| --- | ---: | ---: |
| validation atomics and finite scan forced on | 55.3175 / 56.4252 ms | 52.2301 / 53.2041 ms |
| ordinary visible path | 52.2993 / 53.2368 ms | 49.1126 / 50.2319 ms |

Removing visible diagnostic work saved `5.46%` of the frame and `5.97%` of the
hot dispatch. That change is valid, but it cannot close the gap:

- `52.2993 / 16.6667 = 3.14x` required total-frame speedup;
- `68.13%` of current frame time must be removed for 60 FPS;
- the primary/radiance dispatch is `93.91%` of the measured frame;
- only `3.1867 ms` currently lies outside that dispatch; and
- if all other work remains unchanged, the replacement for the hot dispatches
  has a combined budget of `13.48 ms`.

The next work must therefore reduce rays and expensive per-ray/per-candidate
shading, not spend a cycle polishing sub-millisecond post or scene stages.

## Why Q2RTX is cheaper

The following comparison is source-observed, not inferred from preset names.

| cost domain | native renderer now | Q2RTX reference |
| --- | --- | --- |
| pass layout | `path_trace.hlsl::RayGeneration` performs primary visibility, guide publication, direct RIS, smooth GGX, adaptive diffuse paths, and history output in one full-frame launch | `<Q2RTX-root>/src/refresh/vkpt/shader/path_tracer.h:27-85` describes separate primary, direct, and indirect stages; `path_tracer.c::vkpt_pt_trace_primary_rays` and `vkpt_pt_trace_lighting` dispatch them separately |
| primary ray | one full-rate segment | one full-rate primary stage writing a visibility/G-buffer representation |
| direct local light | default 16 textured candidates split into two RIS groups, followed by two selected visibility rays | the polygonal proposal examines at most `MAX_BRUTEFORCE_SAMPLING == 8` entries in one selected list partition; polygonal and dynamic proposals are combined before one selected local shadow ray in `path_tracer_rgen.h::get_direct_illumination` |
| first continuation | independent diffuse and GGX estimators can both run | default `pt_num_bounce_rays=1` selects one diffuse or GGX continuation with its selection probability in `indirect_lighting.rgen::indirect_lighting` |
| path depth | default maximum depth three: primary plus as many as two diffuse continuation surfaces | default one continuation; the optional high setting invokes a second diffuse continuation |
| stable indirect schedule | one fresh path on a rotating 2x2 phase, with zero paths on the other mature pixels; new/disoccluded/immature/changing pixels can burst to 16 paths | one first continuation per pixel at medium; low uses a dense half-height dispatch and remaps it to alternating rows |
| specular RR guide | a deterministic mirror-distance trace is executed while RR guides are active | no corresponding DLSS-RR guide trace in this Q2RTX checkout |
| material textures | a packed software mip atlas performs explicit four-tap bilinear loads, optional second mip, and up to eight anisotropic line samples per channel | bindless sampled textures use hardware `texture`, `textureLod`, and `textureGrad` in `shader/global_textures.h` |
| low-frequency filter | native project-owned temporal/regional/deflicker/wavelet chain, with low-resolution work at one third | ASVGF temporally folds LF into one-third resolution and filters LF there; SPEC is temporal-only |
| fixed resolution | current preliminary capture traced the full `2568x1471` extent | `viewsize=100`, DRS off, and FSR off are documented defaults; exact runtime values still must be captured rather than assumed |

This does not prove that any single Q2RTX decision will preserve the accepted
native image. It establishes the cost model that the combined candidates must
test.

## Execution rules

1. Work in combined vertical slices because ray choice, dispatch shape,
   G-buffer layout, and reconstruction inputs are mutually dependent. Do not
   land isolated half-measures that add buffers and barriers without removing
   the old work.
2. Keep internal diagnostic switches while developing a slice so its pieces can
   be A/B tested. Once accepted, ship one path and remove the rejected path and
   switches. Do not retain a silent slow fallback or a second lower-quality
   renderer.
3. Use Release GPU results for decisions. Debug runs remain correctness checks.
4. Keep fixed resolution, VSync, frame cap, camera, content, and reconstruction
   mode constant within an A/B. Never compare similarly named preset labels in
   place of actual internal extents and work counts.
5. Every invasive slice must reduce the measured hot path by at least `15%` or
   remove work required by the next accepted slice. The combined scheduling
   slice has a continuation gate of `22 ms` for all primary/direct/indirect/guide
   work at the preliminary full-resolution Level A profile. Final continuation
   requires `13.48 ms` or less unless later measurements reduce non-ray work.
6. No change is accepted on a static image alone. Locked motion, disocclusion,
   weapon, billboard, vector, projectile, animated material, and exact-black
   sequences are mandatory.
7. Do not compensate for removed rays with an arbitrary radiance multiplier,
   clamp, ambient term, emitter pruning, or bounce-energy constant. Any
   stochastic scheduling must carry its explicit selection probability and
   remain unbiased before temporal reconstruction.

## Milestone 0: establish the real two-renderer baseline

This milestone prevents a false target and should be completed before another
production optimization is accepted.

### Build and runtime manifest

- Build both renderers in optimized Release configurations with shader debug
  disabled and validation layers/D3D12 debug layer disabled.
- Record both commits, dirty state, compiler and SDK versions, GPU adapter/LUID,
  driver, OS build, clocks/power profile, presentation mode, frames in flight,
  frame cap, VSync, output mode, and all rendering settings.
- Record Q2RTX `viewsize`, `drs_enable`, `flt_fsr_enable`, `flt_taa`,
  `pt_num_bounce_rays`, reflection/refraction, caustics, volumetrics, and every
  setting that changes traced work. Capture the actual render extent from the
  runtime rather than relying on defaults.
- Record native output, trace, RR input/output, guide, indirect-filter, and
  bloom extents plus `rtx_light_candidates`, direct SPP,
  `rtx_indirect_samples`, `rtx_max_bounces`, reconstruction mode, RR mode, and
  radiance channel.

### Three baseline profiles

1. Fixed matched-work profile: same output and traced pixels, no DRS, no frame
   cap, no hidden upscaling. Use this to compare backend efficiency and rays per
   pixel.
2. Shipping-default profile: each renderer's normal quality configuration at
   the same output resolution. Report the actual internal extents. This is the
   user-facing result.
3. Native stress profile: locked camera yaw, disocclusion, first and warmed
   second Shotgun bursts, animated materials, and the all-level smoke route.
   This catches adaptive bursts, scene updates, history resets, and p99 hitches.

Use at least 120 unmeasured frames and 600 measured frames per run. Alternate
five native and five Q2RTX runs. Use one pinned external frame trace for
cross-executable total-frame percentiles and each renderer's internal profiler
for stage attribution.

### Add workload accounting before changing the estimator

Extend the existing frame-latent native profiler with per-stage counters that
are copied with the same delayed readback and are disabled outside profiling:

- primary, deterministic-guide, primary-shadow, smooth-GGX, diffuse-segment,
  and secondary-shadow rays;
- direct and secondary light candidates evaluated;
- pixels in missing, immature, changing, mature-scheduled, and mature-carry
  states;
- continuation paths by depth and the count terminated before each depth;
- material loads by channel, mip, and anisotropic tap count;
- dispatch dimensions, active threads, inactive early returns, and work-list
  occupancy if applicable; and
- BLAS/TLAS rebuild/refit counts, history resets, and RR evaluation extent.

Validation counters must not contaminate an ordinary profile. The profiler must
remain frame-latent and must never wait for the same frame.

### Milestone 0 gate

Commit or attach the complete manifests, raw JSON/CSV, stage totals, ray/work
counts, and locked poses. The summed non-overlapping stage intervals must
reconcile with total GPU time. Do not proceed from the preliminary Debug number
if the Release trace identifies a different dominant cost.

## Milestone 1: combined Q2-shaped scheduling slice

Implement this as one architecture branch with a control mode and feature bits
for attribution. The accepted production landing combines the winning pieces;
it does not ship a menu of experimental estimators.

### 1A. Split work without changing it

Create an exact-work control that replaces the current monolithic launch with:

1. primary visibility and compact surface/G-buffer output;
2. current-surface RR guide publication;
3. primary direct lighting;
4. scheduled first continuation plus optional later continuation; and
5. radiance/channel/history resolve.

Trace the camera ray once. Later stages reconstruct the surface from the stored
visibility/barycentrics/material identity and must not retrace it. The control
must produce the same ray counts and match the current radiance/guides within
storage tolerance. Its timing reveals the real buffer/barrier cost of the
split.

The native implementation should follow project conventions in
`src/renderer_dxr/dxr_pipeline.cpp` and
`src/renderer_dxr/shaders/path_trace.hlsl`; Q2RTX's Vulkan resource layout is
not to be copied.

Implementation checkpoint, 2026-08-29:

- S0 first existed behind the startup-only diagnostic switch
  `AB3D2_DXR_SPLIT_PRIMARY=1`; the default at this checkpoint remained the
  monolithic control.
- `PrimaryVisibility` traces the camera segment once and publishes its triangle
  index, full-precision barycentrics, crossed-additive-layer count, and additive
  radiance. `ShadePrimary` consumes that handoff and runs the unchanged guide,
  direct, smooth-GGX, diffuse, channel, and history work without a second camera
  trace.
- The profiler reports `primary_visibility` and `primary_shading` separately and
  writes `split_primary` into the JSON settings, preventing control/candidate
  samples from being mixed silently.
- Debug build, focused reconstruction/foundation/lighting-reference tests, and
  the complete Level A hidden smoke passed. The split route retained bitmap and
  additive coverage, both Shotgun bursts, 400-frame walking/firing stability,
  and zero unexpected scene rebuilds.
- A paired Release Level A hidden-validation profile at `1280x720`, 60 warm-up
  and 120 measured frames, measured the monolith at `17.9343 ms` frame median
  and `16.1034 ms` primary-shading median. S0 measured `17.6099 ms` frame median,
  `0.1219 ms` primary-visibility median, and `15.7409 ms` primary-shading median.
  This is only a control measurement: it is validation-contaminated, follows a
  changing smoke route, and its frame p95 regressed from `33.0120` to
  `35.4383 ms`, so it is not a Milestone 1 performance acceptance result.
- Final moving-frame SDR captures differed by a mean `0.02057` of an 8-bit code
  per component, with a maximum difference of one code. This clears the S0
  storage/rounding tolerance but does not replace the locked radiance/channel
  acceptance suite required for S3/S4.

At this checkpoint S0 remained a development foundation because it enabled the
following direct, continuation, and dense scheduling passes to reuse one
primary hit. It was not a shipping path until those pieces were combined and
the fixed Release profile cleared the gates below.

### 1B. Combine the ray-count reductions

On top of the exact-work control, test one integrated workload candidate:

- Use one primary local-light survivor and one visibility ray, carrying diffuse
  and direct GGX together as the current shared target already does.
- Select one first continuation lobe, diffuse or GGX, using a documented
  material/Fresnel probability. Divide throughput by that probability and route
  the contribution into the existing diffuse or specular channel. Do not trace
  the independent lobe as well.
- Launch mature rotating-phase continuations as a dense quarter-size dispatch
  with an explicit pixel mapping. Do not launch a full frame merely to return
  from three quarters of its threads.
- Put burst pixels into a bounded GPU work list or a small set of dense burst
  layers. Capacity is derived from the full internal pixel count and configured
  burst ceiling; overflow is a validation failure, never a dropped path.
- Sweep burst ceilings `1, 2, 4, 8, 16` inside this branch. Select the lowest
  ceiling that passes moving disocclusion and lighting-change gates.
- Keep maximum depth three for the first comparison. Then attribute depth-two
  work separately. A later continuation may be temporally interleaved only with
  an explicit `1 / p` estimator and proof that energy, variance, and recovery
  match; simply deleting the second surface or restoring its old approximate
  eight-percent loss with a multiplier is forbidden.

The control modes required during development are:

| mode | purpose |
| --- | --- |
| S0 | split passes, identical current rays and candidates |
| S1 | S0 plus one primary direct survivor |
| S2 | S1 plus one lobe-selected first continuation |
| S3 | S2 plus dense mature dispatch and bounded burst work |
| S4 | S3 plus accepted burst ceiling/depth scheduling |

Report each mode once to attribute the result, but judge the combined `S3/S4`
image and timing as the product candidate. This provides causality without
forcing five partial production landings.

S1 checkpoint, 2026-08-29:

- `AB3D2_DXR_SINGLE_PRIMARY_SURVIVOR=1` now enables S1 and is rejected unless
  S0 is also enabled. The profiler records this feature bit in every summary.
- All primary proposals enter one RIS stream. The selected point's exact
  material evaluation, diffuse and GGX contributions, normalization, and one
  shadow result remain shared. No lobe energy multiplier or visibility reuse
  from another frame was introduced.
- On the same Release `1280x720` Level A hidden-validation route used for S0,
  S1 reduced frame median from `17.6099` to `12.8654 ms` (`26.9%`) and primary
  shading median from `15.7409` to `10.9706 ms` (`30.3%`). Frame p95 improved
  from `35.4383` to `28.7643 ms`, though this changing route is still not the
  locked acceptance profile.
- The isolated lighting reference, six-channel sum, foundation test, complete
  Level A moving/firing route, both Shotgun bursts, entity/additive coverage,
  and zero-rebuild checks passed with S1 active. Stability changed from S0's
  `14.3572/14.3983` early/late display-delta pair to
  `14.3216/14.3499`; this is evidence against added divergence, not the final
  moving-image gate.
- An exploratory eight-proposal run reduced the S1 frame median again to
  `12.2324 ms` and primary shading to `10.1755 ms`, but it is not yet a code or
  default change. Sparse textured-emitter quality and proposal-load accounting
  in 1C must pass before that setting is accepted.

At this checkpoint S1 remained diagnostic and off by default. Its reduction
cleared the per-slice `15%` direction gate, but only the combined S3/S4 route
could become production.

S2 checkpoint, 2026-08-29:

- `AB3D2_DXR_SINGLE_CONTINUATION_LOBE=1` now enables S2, requires S1, and is
  rejected with the diagnostic ReSTIR-GI path until that reservoir can carry
  the added selection measure.
- When a smooth-GGX and diffuse first continuation coincide, a separate random
  dimension selects exactly one. Its probability uses current-view Schlick
  Fresnel, material diffuse reflectance, F0, and the accepted real-versus-fake
  specular roughness weight, clamped to `[0.05, 0.95]` when both lobes exist.
  The chosen contribution is divided by `p` or `1-p`; later diffuse burst paths
  remain unchanged. A CPU mirror pins the endpoint and inverse-probability
  contract.
- On the same Release hidden-validation route, S2 measured `12.1260 ms` frame
  median and `10.2252 ms` primary shading, reductions of `5.7%` and `6.8%`
  from S1. Relative to S0, the combined S1+S2 reductions are `31.1%` frame and
  `35.0%` primary shading. Frame p95 was `28.4066 ms` versus S1's
  `28.7643 ms`.
- The BRDF mirror, isolated lighting reference, six-channel sum, foundation
  test, complete Level A moving/firing route, both Shotgun bursts, coverage,
  stability, and zero-rebuild checks passed. Moving display delta was
  `26.8014`, effectively unchanged from S1's `26.7965`; static early/late
  stability was `14.3405/14.3644`.

S2 is required work for S3 but does not independently clear the `15%` slice
gate. It remained diagnostic and off by default; no energy compensation,
fallback lobe, or temporal visibility reuse was added.

S3a checkpoint, 2026-08-29:

- `AB3D2_DXR_DENSE_MATURE_CONTINUATIONS=1` requires S2, an adaptive temporal
  reconstruction mode, a nonzero reservoir limit, and the production zero
  radiance clamp. Unsupported combinations fail initialization; the mode was
  off by default at this checkpoint.
- A mature scheduled pixel marks the spare high bit of the exact split-primary
  payload and defers only its diffuse continuation. A quarter-pixel raygen
  launch maps `(x,y)` to `2*(x,y) + phase`, with the four `SampleIndex` phases
  covering the full image. Missing, disoccluded, immature, and changing pixels
  retain the configured burst ceiling in `ShadePrimary`, so this checkpoint
  has no work-list capacity, overflow, dropped path, or silent fallback.
- `DenseMatureContinuation` repeats S2's exact random lobe decision, diffuse
  sample index, inverse selection probability, material footprint, directional
  signal, and one-sample history contract. A smooth selection publishes a
  valid zero diffuse estimator with history length one. The full tracing extent
  now comes from the guide texture rather than the raygen launch dimensions,
  preventing dense dispatch size from changing authored material mips.
- On a paired Release Level A hidden-validation run at `1280x720`, 60 warm-up
  and 120 measured frames, S2 measured `14.6520 ms` frame median and
  `12.2726 ms` primary shading. S3a measured `10.1687 ms` frame median,
  `6.1220 ms` primary shading, and `1.2385 ms` dense continuation. This is a
  `30.6%` frame-median reduction and a `50.1%` primary-shading reduction;
  frame p95 improved from `30.7867` to `25.0592 ms`. The changing validation
  route is still not a locked shipping profile, and its S3a frame p99
  (`113.3134 ms`) did not improve over S2 (`103.8220 ms`).
- Paired final SDR captures had mean absolute component difference `0.02447`
  and maximum difference one 8-bit code. Release shader/native compilation,
  the BRDF/reconstruction/lighting-reference/channel-sum/foundation tests, and
  the complete Level A route passed. The route retained both Shotgun bursts,
  bitmap/additive coverage, 400-frame walking/firing, and zero unexpected scene
  rebuilds.

S3a clears the per-slice median direction gate and brings this preliminary
profile below 16.67 ms, but it is not S3 or performance parity. The bounded
burst schedule, validation-only work counters, locked static/motion trials,
five-run dispersion, and direct Q2RTX comparison remain required.

S3 bounded-burst checkpoint, 2026-08-29:

- `AB3D2_DXR_BOUNDED_BURST_CONTINUATIONS=1` requires S3a and remained off by
  default at this checkpoint. Primary shading appends one compact
  `(linear pixel, sample count)` entry for each new, disoccluded, immature, or
  changing pixel. A GPU-written
  `D3D12_DISPATCH_RAYS_DESC` then launches `BurstContinuation` through
  `ExecuteIndirect`, eliminating the full-frame divergent burst loop from
  `ShadePrimary`.
- Each in-flight frame owns an independent work buffer and indirect argument.
  Capacity is exactly the internal pixel count because the classifier can
  append at most once per pixel; the ray-work bound is that capacity multiplied
  by the configured indirect burst ceiling. Width begins at one sentinel thread
  so zero-work dispatches remain valid. A future capacity violation processes
  that pixel on the primary path and increments a validation counter; hidden
  validation fails explicitly, so overflow cannot silently drop radiance.
- The burst export reconstructs the exact split-primary hit and repeats S2's
  per-ordinal lobe decision, sample stream, inverse probability, directional
  accumulation, first-hit guide, mean, and effective history length. The
  indirect command is reset with a frame-owned copy/transition sequence and
  never requires a CPU readback or same-frame wait.
- Two alternating Release Level A pairs used `1280x720`, 60 warm-up and 120
  measured hidden-validation frames. In the first pair S3a/S3 frame median was
  `10.3959/10.6611 ms`, p95 `25.6482/17.9054 ms`, and p99
  `101.9704/83.5762 ms`. In the reverse-order pair it was
  `7.2650/7.3618 ms`, p95 `26.6489/18.9245 ms`, and p99
  `102.2108/74.7521 ms`. S3 therefore cost `2.6%` and `1.3%` at the median but
  reduced p95 by `30.2%` and `29.0%`, and p99 by `18.0%` and `26.9%`.
  Primary-shading p95 moved from `23.1380/23.7051 ms` to
  `7.0731/4.2168 ms`; explicit burst p95 was `12.5339/11.8002 ms`.
- The unprofiled paired final SDR captures differed by mean `0.31276` of an
  8-bit code per component and a maximum of one code. The complete Level A
  route passed twice with no work overflow or unexpected rebuild, retaining
  both Shotgun bursts and 400-frame walking/firing. The Release build, forced-S3
  foundation test, and authoritative default BRDF/reconstruction/material-mip/
  lighting-reference/channel-sum/foundation tests passed. Raw and zero-history
  reference modes intentionally reject S3's adaptive-history prerequisites.

At this checkpoint S3 was a verified scheduling foundation, not an accepted
production path. Its median setup cost did not clear the per-slice direction
gate, and burst p95 still exceeded the 16.67 ms frame budget in these
changing-route samples. S4 therefore had to sweep ceilings `1, 2, 4, 8, 16`,
retain the lowest ceiling that passed disocclusion/change recovery and every
lighting gate, and then run the locked repeated native/Q2RTX profiles.

S4 exploratory sweep, 2026-08-29:

| burst ceiling | frame median / p95 / p99 (ms) | burst p95 (ms) | static early / late delta | moving delta |
| ---: | ---: | ---: | ---: | ---: |
| 1 | `10.7145 / 16.6009 / 20.3788` | `7.5793` | `14.3031 / 14.3234` | `27.3305` |
| 2 | `9.7579 / 16.9265 / 26.9555` | `10.6154` | `14.3214 / 14.3374` | `27.2704` |
| 4 | `9.7159 / 26.2744 / 47.3452` | `19.8207` | `14.3352 / 14.3541` | `27.2316` |
| 8 | `10.3014 / 21.3121 / 70.8437` | `14.8525` | `14.3384 / 14.3611` | `27.2454` |
| 16 | `10.3410 / 19.3372 / 79.2778` | `12.6249` | `14.3406 / 14.3680` | `27.2603` |

- These are single sequential Release hidden-validation trials, not acceptance
  statistics. All used S3 at `1280x720`, 16 light candidates, 60 warm-up and
  120 measured frames, and all passed the full Level A route with zero overflow
  and rebuilds. The non-monotonic p95 ordering demonstrates why the five-run
  locked protocol remains mandatory. Ceiling one is the only trial below
  `16.67 ms` at p95.
- Settled corridor captures at ceilings 1/2/4/8 versus 16 had mean absolute
  8-bit component differences `0.6926/1.0179/0.6092/0.3365`; no normal-scale
  structural difference was visible. The locked saved yaw/Shotgun sequence at
  ceilings 1 and 16 was also close: shot reprojected delta
  `7.1722/7.1724`, reprojected outliers `15238/15251`, and identical sample
  counts. This does not measure a sudden indirect-light change in isolation.
- A fully confirmed gradient retains `3.436` old effective samples before the
  current burst. The exact temporal current weights for ceilings 1/2/4/8/16
  are therefore `0.380/0.494/0.630/0.760/0.859`. Ceiling one's apparently
  calmer settled delta buys materially slower first-frame lighting recovery.
  It was held for the locked indirect-only light-toggle oracle below; that
  oracle now confirms the lag and rejects the lower ceiling, so no default
  changed.
- Combining S3, ceiling 16, and eight global RIS candidates measured
  `9.0502/15.8996/62.0167 ms` frame median/p95/p99 and `9.4701 ms` burst p95.
  It clears the preliminary median/p95 budget without weakening burst recovery,
  but is rejected as a production setting: the deterministic corridor was
  visibly darker/less warm, and saved Shotgun delta rose from `7.4076` to
  `8.7470` with temporal outliers rising from `251244` to `284415`. A compact
  local proposal with the complete emitter mixture is still required; merely
  halving global proposals is not that algorithm.

No S4 ceiling is accepted by this sweep. Preserve 16 as the quality control,
add the isolated change-recovery oracle, and implement the 1C local proposal
before repeating ceilings and candidate counts.

### 1C. Stop evaluating expensive light data for every candidate

Ray-count changes alone may not close the gap because native direct RIS shades
16 candidates per full-rate primary pixel and repeats candidate work at
secondary vertices. Combine the scheduler change with a Q2-shaped local
proposal:

- Report time and texture loads separately for candidate generation, exact
  selected-sample evaluation, and visibility.
- Replace the large 16-by-16-by-16, 512-entry-per-cell refresh contract only if
  its lookup quality can be retained with a smaller local representation. Its
  refresh stage is currently cheap; the goal is cheaper shader-side proposal
  evaluation and better locality, not merely a smaller buffer.
- Build compact per-cell light lists or distributions from the complete
  authored emitter set. Never cap lights by distance, PVS, zone, or an arbitrary
  count.
- Target at most eight cheap proposal evaluations per receiver initially,
  matching Q2RTX's bounded polygonal shortlist. Preserve exact textured
  emission for the selected point and the correct proposal PDF. If an averaged
  emissive proxy cannot sample sparse/black texels without bias or unacceptable
  variance, reject it and use a hierarchical/integral distribution that can.
- Share the selected sample, exact material evaluation, and shadow result
  between direct diffuse and direct GGX. Do not reintroduce two lobe-specific
  shadow rays.
- Reuse the same compact local proposal at reached continuation surfaces while
  retaining the full global emitter distribution as its mathematically explicit
  mixture, not as a silent out-of-cell fallback.

First 1C compact-primary checkpoint, 2026-08-29:

- `AB3D2_DXR_COMPACT_LOCAL_PRIMARY=1` requires S3 and is off by default. It
  caps only primary RIS at eight exact candidates: six explicit draws from the
  complete global alias distribution plus two receiver-cell ReGIR draws with
  their stored inverse proposal probabilities. Continuation surfaces retain
  the configured 16 candidates. The profiler now reports both
  `light_candidates` and `primary_light_candidates`, plus the feature bit.
- The first one-global/seven-local mix was immediately rejected: frozen-camera
  late delta rose from the S3 control's `14.3629` to `16.4831`. Four/four
  improved it to `15.2457`. The retained six/two diagnostic measured
  `13.6388/13.9874` frozen early/late and `25.4353` moving delta in its
  unprofiled Level A run, with the complete route passing and no unexpected
  rebuild or overflow.
- One alternating 120-frame hidden-validation pair measured S3 control at
  `7.2655/20.0179/78.4800 ms` frame median/p95/p99 and the six/two diagnostic
  at `7.1340/17.8295/79.1992 ms`. Primary shading improved from
  `3.3802/4.4259 ms` median/p95 to `3.0116/4.1290 ms`; this is useful but not
  sufficient for the 60 FPS tail gate.
- The locked saved yaw/Shotgun route rejects the candidate for production.
  Versus the 16-global S3 control, frozen delta/reprojected delta rose from
  `6.8747/6.7689` to `7.8570/7.6407`, and Shotgun delta/reprojected delta rose
  from `7.4076/7.1724` to `8.2993/8.0966`. It is better than the rejected
  eight-global shortcut, but it still spends image stability for timing.

Keep this path only as attribution evidence. The next 1C candidate must retain
16 proposal points while making non-survivors cheap: use a conservative,
strictly positive emitter/geometry proxy for streaming, then perform exact
textured emission, normalization, and visibility only for the survivor. The
proxy must be proved nonzero wherever authored emission can be nonzero, and
sparse/black texel captures must pass before it can replace exact per-candidate
evaluation.

Second 1C proxy-primary checkpoint, 2026-08-29:

- `AB3D2_DXR_PROXY_PRIMARY_CANDIDATES=1` implements that experiment behind an
  S1-dependent, startup-validated switch that cannot be combined with the
  compact local candidate. Performance measurements combine it with S3; the
  narrower real dependency lets raw/zero-history lighting oracles exercise the
  primary estimator directly. It retains all 16 global proposal points. Candidate
  streaming uses geometry, the receiver BSDF, interpolated authored emission
  scale, and `selectionProbability * inverseArea`; the latter is proportional
  to the material's maximum authored emissive luminance and is therefore
  positive everywhere a compiled emitter can emit. Only the RIS survivor
  samples the exact emissive texture and spends a visibility ray. Its exact
  contribution is divided by the selected proxy target, so black texels remain
  valid zero-contribution samples rather than disappearing from the estimator.
  The forced isolated lighting reference and six-channel sum both pass with
  S1 plus the proxy active, including sparse/black texel and occlusion cases.
- The complete Level A route passes with no rebuild/overflow regression and
  reports `14.1436/14.1233` frozen early/late delta and `25.6527` moving delta.
  Those broad metrics do not accept it: the stricter saved oracle raises
  frozen delta/reprojected delta from the S3 control's `6.8747/6.7689` to
  `7.7411/7.6643`, and Shotgun delta/reprojected delta from
  `7.4076/7.1724` to `8.2095/7.7475`.
- One 120-frame hidden-validation profile measured
  `7.6938/18.5366/79.4825 ms` frame median/p95/p99 and
  `3.3398/4.6120 ms` primary-shading median/p95. Against the nearby S3 control
  (`7.2655/20.0179/78.4800 ms` frame and `3.3802/4.4259 ms` primary), median
  primary cost improves only `1.2%` while primary p95 regresses `4.2%`.

The proxy is rejected for production and does not justify more 1C tuning: on
this workload the emissive-atlas fetch avoided for non-survivors is not a large
enough share of candidate cost. Preserve both 1C diagnostics off by default
for attribution, keep the 16-exact-candidate S3 control as the quality path,
and move to 1D's full-rate deterministic RR guide ray.

### 1D. Make the RR guide ray conditional and reusable

The deterministic mirror-distance ray is one full-rate trace that Q2RTX does
not pay. It cannot simply be removed while RR consumes the guide.

- Measure it as a separate stage/ray class after the split.
- Classify pixels that actually carry a non-negligible real specular channel.
  Do not trace the guide for misses, invalid surfaces, or surfaces whose accepted
  specular reconstruction contract requires zero distance.
- When the chosen GGX continuation is sufficiently close to the deterministic
  guide contract, test reusing its current-geometry hit distance. Pixels that do
  not receive a qualifying sample may use a dense, material-gated guide launch.
- Accept reuse/amortization only if RR edge, smear, disocclusion, weapon, and
  moving-camera captures match the deterministic baseline. Do not use old
  history or a stochastic radiance hit as a plausible-looking substitute.

First 1D consumer-gated guide checkpoint, 2026-08-29:

- `writeSurfaceGuides` now traces `deterministicSpecularHitDistance` only when
  DLSS Ray Reconstruction or the `specular-hit-distance` debug view consumes
  the result. RR-off rendering writes zero to the otherwise unused texture.
  `AB3D2_DXR_FORCE_SPECULAR_GUIDE=1` restores the old full-rate ray as an exact
  attribution control, and profiler summaries record `specular_guide_active`.
- Two alternating 120-frame S3 pairs at `1280x720` measured primary-shading
  median/p95 at `3.5789/4.5415` and `3.4606/4.4257 ms` with the guide forced,
  versus `2.9005/3.9873` and `2.8959/3.6291 ms` when consumer-gated. The
  primary median reduction repeats at `18.9%` and `16.3%`; primary p95 improves
  `12.2%` and `18.0%`.
- Total-frame median moved from `7.3665/7.6005` to `7.0685/6.3756 ms` in the
  two pairs. Motion-route tail timing remains burst-dominated: p95 changed
  from `19.5415/18.1421` to `17.6865/18.9077 ms`, so this pass alone is not a
  60 FPS tail-parity claim.
- Paired final SDR captures differ by at most one 8-bit code with mean absolute
  component difference `0.27868`. Forced and gated saved-state metrics are
  identical: frozen `6.8747/6.7689` delta/reprojected and Shotgun
  `7.4075/7.1723`, with identical outlier and reprojected-sample counts. The
  changed hashes therefore reflect last-bit scheduling/compiler variation, not
  a structural or temporal image change.
- The Streamline Release build passed the complete Level A route in RR Quality
  at `853x480` traced to `1280x720`, reporting `specular_guide_active:true`.
  The standalone specular-hit-distance debug route also reports the guide
  active and passes. No stale/history substitute was introduced; RR continues
  to receive the exact current-geometry deterministic distance.

This consumer gate is accepted for RR-off rendering. It is source-consistent
with the observed Q2RTX pass structure, which has no DLSS-RR guide trace, while
retaining the native guide contract whenever the native RR path needs it.

Combined S4 timing check after the guide gate, 2026-08-29:

- S3 plus the accepted RR-off guide gate and burst ceiling one measures
  `6.3730/9.6050/10.5056 ms` frame median/p95/p99 in one 120-frame Level A
  hidden-validation profile. Primary shading is `2.8826/4.0053 ms` and burst
  continuation is `0.2422/4.3175 ms` median/p95. This is the first combined
  preliminary result with substantial margin below 16.67 ms at both p95 and
  p99; its `22.3392 ms` maximum includes the changing/rebuild route.
- The complete route passes with frozen early/late delta
  `14.2977/14.2645`, moving delta `26.6014`, zero unexpected walk rebuilds,
  and no burst-work overflow. These are consistent with the earlier settled
  and saved-motion ceiling sweep, but still do not exercise a confirmed
  indirect-light change at a fixed receiver.

Ceiling one therefore remained an unaccepted diagnostic despite the strong
timing. The indirect-only light-toggle recovery oracle below now resolves the
named evidence gap and rejects every lower ceiling.

Production-scheduler promotion, 2026-08-29:

- S0-S3 now activates automatically for the normal adaptive-temporal,
  nonzero-history, zero-clamp renderer. The five environment controls are
  explicit `0|1` A/B overrides rather than opt-in requirements. Raw/ReSTIR,
  zero-history, and nonzero-clamp diagnostic configurations retain the
  monolithic oracle unless compatible stages are requested explicitly;
  invalid dependency combinations still fail initialization.
- In one paired Release Level A 120-frame changing window at `1280x720`, the
  no-override production path reduced frame median from the explicit
  monolithic control's `21.5461 ms` to `10.9554 ms` (`49.2%`) and p95 from
  `84.5120 ms` to `58.2606 ms` (`31.1%`). Primary/radiance work moved from a
  `19.6439 ms` monolithic median to `0.1178 ms` primary visibility,
  `2.6982 ms` primary shading, `0.8018 ms` dense mature continuation, and
  `5.0637 ms` burst continuation medians.
- The no-override complete route passed with frozen stability
  `14.3395/14.3626`, moving delta `26.6886`, and zero unexpected walk rebuilds.
  Raw lighting/channel oracles and the ReSTIR Level A route also pass. The
  Streamline RR Quality route reports the same five production bits active at
  `853x480` tracing to `1280x720`, with `9.2116 ms` frame median; its
  `28.2545 ms` p95 remains burst-dominated.

This promotion repairs the user-visible configuration but does not complete
Milestone 1 or establish Q2RTX parity. The ceiling-16 burst tail is now the
next measured production bottleneck.

Indirect-light recovery oracle, 2026-08-29:

- `ab3d2_renderer_rtx_indirect_recovery_test` constructs a fixed camera and
  receiver with an off-camera secondary wall and one dynamic authored emitter.
  It matures the adaptive history while the emitter is too distant to
  contribute, then moves the same emitter into the lit position. The emitter
  identity, area, material, receiver, camera, and history epoch remain fixed;
  only the indirect radiance changes. The oracle reads the reconstructed
  `indirect` FP16 channel for twelve frames and compares ceilings
  `16/8/4/2/1` from identical sample zero. The 4-by-4 emitter is deliberately
  hostile to cheap material proxies: one 2-by-2 quadrant emits and the other
  twelve texels are black.
- Ceiling 16's normalized response over frames three through eight is
  `0.4926`, with its fourth captured frame at `0.4247` of settled radiance.
  The same six-frame metric is `0.0835/0.0785/0.0403/0.0078` for ceilings
  `8/4/2/1`: only `16.95%/15.94%/8.17%/1.58%` of the control. Settled ratios
  are `1.2990/0.9930/0.8655/0.2527`.
- The acceptance gate requires at least `90%` of ceiling 16's recovery and
  settled radiance within `10%`. Every lower ceiling is rejected. Production
  remains at 16; the `6.3730/9.6050/10.5056 ms` ceiling-one profile is not a
  shippable speedup.

The burst tail must therefore be made cheaper per ray or scheduled more
coherently without reducing confirmed-change samples. Milestone 2's
hardware-native material access and ray/payload specialization are now the
next production work; deleting burst paths is closed by measured evidence.

Retained-ray material attribution, 2026-08-29:

- Specializing diffuse continuation hits to omit normal and roughness atlas
  reads produced no repeatable timing gain and was removed; the shader compiler
  already eliminates the unused result fields.
- A second candidate kept all 16 continuation proposals and exact geometry but
  selected them with a cheap material-independent proxy, reading the exact
  emissive texel only for the survivor. It reduced the no-RR frame median from
  `10.5597` to `9.1356 ms` and p95 from `57.1009` to `43.7454 ms`; burst median
  fell from `5.3089` to `3.6987 ms`. With Streamline RR Quality, frame median
  fell from `9.2116` to `8.0098 ms` and p95 from `28.2545` to `21.4356 ms`.
- The candidate is mathematically unbiased, and broad Level A and saved-frame
  comparisons looked stable, but it failed the sparse-emitter recovery oracle:
  ceiling-16 recovery fell from the exact sampler's `0.4926` to `0.2776`, and
  its fourth response frame fell from `0.4247` to `0.0736`. The proxy was
  rejected and removed. Production continues to evaluate the exact emissive
  texture for every proposal.
- This result isolates repeated material access as meaningful cost without
  authorizing a lower-quality proposal distribution. Continue with
  hardware-native material sampling and exact payload/traversal specialization.

First hardware-material checkpoint, 2026-08-29:

- Every packed software mip now has a one-texel repeat-wrapped gutter. Primary
  and continuation emitter evaluations use a static D3D12 bilinear sampler for
  one exact level-zero emissive lookup instead of four explicit `Load`s. This
  changes neither candidate count nor proposal distribution, and visibility is
  still traced only for the exact RIS survivor. The existing bounded software
  mip/anisotropic filter remains in place for reached-surface material channels.
- Two 120-frame RR-off Level A controls measured frame median/p95 at
  `7.7428/18.9941` and `7.3044/18.2324 ms`. The guttered hardware path measured
  `6.5658/16.5215` and `6.9162/16.6094 ms`: average frame median improves
  `10.4%` and p95 `11.0%`. Primary-shading median improves from an average
  `3.1258` to `2.6947 ms` (`13.8%`). Burst p95 improves from `12.5896` to
  `11.2337 ms` (`10.8%`) and burst p99 from `71.7227` to `64.2946 ms` (`10.4%`).
- Streamline RR Quality at `853x480` tracing to `1280x720` passes the complete
  Level A route at `6.6777/13.3097/37.6417 ms` frame median/p95/p99, with
  `6.9053 ms` burst p95 and `1.7162 ms` RR median. The saved-state RR Quality
  route also passes.
- The sparse-emitter ceiling-16 recovery is `0.4725` with a `0.4288` fourth
  response frame, versus `0.4926/0.4247` for explicit software bilinear. The
  recovery ratio is `95.9%`, clearing the `90%` gate; ceilings 8/4/2/1 remain
  rejected. RR-off saved-state metrics remain within noise of the software
  path: frozen `6.8752/6.7721` delta/reprojected and Shotgun
  `7.4052/7.1713`, with zero unexpected walk rebuilds in Level A.
- Compacting the surface payload from 20 to 16 bytes by adopting Q2RTX's
  invalid-primitive hit sentinel passed all lighting tests but was removed: two
  candidate runs averaged `7.3765/18.8092 ms` frame median/p95 versus
  `7.5236/18.6133 ms` for two controls, and burst p95 regressed from `12.5896`
  to `12.9335 ms`. Source-level payload reduction without GPU-time improvement
  does not pass this plan's acceptance rule.

This first material slice is accepted. Full hardware-native surface filtering
and bindless/per-material representation remain open; Milestone 2 is not
complete.

Second hardware-material checkpoint, 2026-08-29:

- The gutter-safe hardware primitive now services all five reached-surface PBR
  channels. The accepted directional footprint, software mip selection,
  explicit trilinear blend, and bounded anisotropic tap positions are unchanged;
  only each four-`Load` bilinear operation becomes one hardware sample. The
  unused manual-bilinear shader route was removed rather than retained as a
  machine-dependent filtering path.
- Against the two emitter-only runs above (`6.7410/16.5655 ms` average frame
  median/p95), two 120-frame RR-off runs measure `6.5201/15.2803` and
  `6.3301/15.4095 ms`, averaging `6.4251/15.3449 ms`: another `4.7%` median and
  `7.4%` p95 reduction. Primary-shading median falls from `2.6947` to
  `2.2027 ms` (`18.3%`). Burst p95 falls from `11.2337` to `10.3935 ms`
  (`7.5%`), and burst p99 from `64.2946` to `53.9103 ms` (`16.2%`).
- Streamline RR Quality improves from the emitter-only checkpoint's
  `6.6777/13.3097/37.6417 ms` frame median/p95/p99 to
  `6.1148/10.8312/36.5381 ms`. Primary shading is `1.2385 ms` median, burst is
  `5.9148 ms` p95, and RR itself remains stable at `1.7280 ms` median.
- The sparse-emitter response is bit-for-bit unchanged from the first hardware
  checkpoint (`0.4725` recovery, `0.4288` fourth response frame). RR-off
  saved-state frozen/Shotgun metrics are `6.8729/6.7701` and
  `7.4038/7.1706` delta/reprojected. RR Quality saved metrics are likewise
  stable at `0.5053/0.5083` and `4.6986/0.6773`; both saved routes pass.

This extension is accepted. A real hardware-mip or bindless/per-material
representation may still reduce address arithmetic and explicit tap count, but
it must beat this now-hardware-filtered packed-atlas baseline.

### Milestone 1 gate

The combined primary/direct/indirect/guide stages must reach `22 ms` or less at
the preliminary full-resolution Level A profile, show no p95/p99 regression in
the motion profile, and pass every lighting/guide gate. If it does not, use the
feature-bit timings to reject the ineffective part before proceeding. Do not
optimize the new layout around a candidate that cannot plausibly reach the
`13.48 ms` final hot-work budget.

## Milestone 2: combine material, payload, and traversal cost reductions

After Milestone 1 has coherent passes, remove the expensive work repeated by
their rays.

### Hardware-native material sampling

The current software atlas can require four loads per bilinear mip, two mips per
trilinear sample, up to eight anisotropic samples, and as many as five material
channels. This is a likely multiplier on primary and continuation cost.

The accepted first slice replaced repeated level-zero emissive candidate
lookups; the second moved every reached-surface bilinear operation to the same
hardware primitive. The work below now refers to a real hardware-mip or
bindless/per-material representation and any separately proven channel
specialization, not the removed four-`Load` bilinear path.

- Prototype one bindless SRV per authored texture/channel with real mip chains
  and hardware anisotropic sampling, or another single D3D12 representation
  proven equivalent on the supported target. Verify descriptor-indexing support
  before committing the layout.
- Preserve cropped texture windows, wrapping, animation frame identity, palette
  conversion, alpha tests, normal orientation, metalness, roughness, emissive
  registration, and level-zero-only primitive behavior exactly.
- Generate mip chains from the same authoritative pixels and color-space rules.
  Compare every mip and crop boundary against the software-atlas oracle.
- Load only the channels required by a ray. Shadow/visibility rays must not
  evaluate PBR material channels; alpha any-hit needs only classification and
  alpha data. Secondary diffuse surfaces should not load primary-only RR guides
  or normal maps when geometric normals are the accepted contract.
- After equivalence and performance pass, remove the old production atlas
  sampling route. Do not keep a hidden representation fallback that changes
  filtering by machine.

### Payload and pipeline specialization

- Give primary, continuation, guide, and visibility rays the smallest payload
  and instance mask that preserves their accepted geometry behavior.
- Use accept-first-hit/skip-closest-hit flags only for visibility classes where
  alpha-test, additive, weapon, billboard, vector, projectile, and effect rules
  prove it safe.
- Keep surface reconstruction in coherent later passes. Avoid loading all
  vertices/material channels in hit shaders when a compact visibility result is
  enough.
- Inspect register pressure, occupancy, shader table traffic, divergent branches,
  and cache misses with PIX/Nsight. A source-level instruction reduction is not
  accepted without a GPU-time reduction.
- Audit barriers and formats after pass ownership is stable. Remove only barriers
  made redundant by an explicit resource-state proof. Compress a G-buffer or
  guide only after its round-trip tolerance and RR captures pass.

### Milestone 2 gate

At the fixed preliminary profile, all primary/direct/indirect/guide stages must
fit within the current `13.48 ms` hot-work budget and the total frame must be at
or below `16.67 ms`. If the measured Q2RTX envelope is faster, continue until
the repeated native confidence band enters that envelope. Material captures,
alpha coverage, animation, exact-black, radiance-channel sum, and all-level
smoke must remain unchanged.

## Milestone 3: reconstruction, post, and memory traffic

Do this only after the ray path is no longer overwhelmingly dominant.

- Keep the accepted native LF temporal/regional/deflicker/wavelet result as the
  oracle. Q2RTX's one-third-resolution LF filtering and temporal-only SPEC path
  are evidence for channel-specific work, not authority to remove native RR.
- Profile every reconstruction substage and its bandwidth. Fuse passes only
  when lifetime/barrier analysis proves it reduces traffic and preserves the
  selected intermediate diagnostics.
- Avoid repeatedly reading/writing full-resolution copies of data consumed only
  at one-third resolution. Keep full-resolution depth/normal validation at the
  reconstruction boundary.
- Report proprietary RR evaluation separately. Optimize its inputs and adjacent
  transitions; do not relabel a different RR mode or smaller input as a shader
  optimization.
- Profile bloom, exposure/histogram, tone map, and present only after ray work is
  within budget. Preserve the accepted tone curve, bloom, SDR/HDR output, and
  exposure behavior.

Gate: total fixed-profile GPU time must remain inside both the 60 FPS budget and
the Q2RTX envelope, while p95/p99 improve or remain within control dispersion.
No gain may come from extra queued frames; report input-to-present latency.

## Milestone 4: shipping RR resolution and frame pacing

The preliminary `52.30 ms` result was RR off at full traced resolution. That is
an important backend stress case, but it may not describe the intended shipping
configuration.

- Query and log Streamline's recommended input extent for every RR quality mode.
  Measure the actual normal shipping mode at its recommended fixed input extent,
  not at an assumed preset scale.
- Compare native fixed internal resolutions against Q2RTX at the same traced
  pixel count, and compare shipping modes separately at the same output extent.
- Confirm that extent changes do not rebuild static scene data, repack material
  resources, flush the GPU, or reset RR history except when the extent actually
  changes.
- Tune frame-latency waitable-object and frames-in-flight behavior only with a
  paired latency/throughput report. Higher queue depth is not a renderer speedup.

Optional Q2RTX-style dynamic resolution is last. Q2RTX's `drs_enable` is off by
default, so DRS cannot establish fixed parity. If added after parity, drive it
from completed GPU timestamps, use bounded scales and outlier-resistant history,
report every achieved scale, and keep it off by default unless explicitly
requested.

## Milestone 5: scene-update and hitch closure

Steady Level A profiling currently points at ray work, not scene construction.
Keep scene changes out of the critical path until evidence moves them there.

- Time vertex upload, dynamic BLAS build/refit, TLAS update, descriptor/material
  publication, light-list update, and history promotion separately.
- Static geometry, materials, mip resources, and emitters must not rebuild in
  steady state. A new material kind's first atlas/texture population remains a
  cold event reported separately.
- The warmed second Shotgun burst must have zero static scene rebuilds and no new
  resource allocation or queue flush.
- Optimize a scene stage only if it exceeds 5% of the relevant frame or causes a
  p95/p99 hitch. Do not trade a stable steady state for a faster average.

## Required quality gates for every combined slice

Run the established tests from `Q2RTX_LIGHTING_PARITY_PLAN.md`, including:

- CPU/unit, shader warnings-as-errors, Debug and Release builds;
- native and Streamline GPU reference scenes;
- Level A-P smoke, lifecycle, desktop-settings, material, and reconstruction
  coverage;
- locked Level A combined, indirect diffuse, direct specular, indirect
  specular, emission, and guide captures;
- radiance-channel sum and finite radiance/guide checks;
- exact-black unlit surface;
- diffuse/GGX material thresholds, metalness, normal maps, alpha-tested and
  additive geometry;
- camera yaw, disocclusion, door/lift/water animation, Shotgun first/warmed
  bursts, weapon, billboard, vector, and projectile motion; and
- RR specular hit-distance edge/smear tests.

For stochastic candidates, compare at least a fixed-seed single-frame result,
temporal convergence curve, settled image, and moving sequence. A lower settled
error that converges too slowly after disocclusion is a rejection.

## Performance report format

Each accepted or rejected combined slice gets one directory named with date,
native commit, Q2RTX commit, GPU, driver, and profile. It contains:

- complete native and Q2RTX setting manifests;
- raw frame/stage JSON or CSV for all alternating runs;
- presentation/tracing/reconstruction/output extents;
- ray, candidate, material-tap, active-thread, scene-update, and history-reset
  counts;
- median, p95, p99, maximum, dispersion, and confidence interval calculation;
- locked screenshots and moving capture identifiers;
- image/convergence comparison results; and
- a one-page decision stating accept, reject, or blocked by a named measurement.

Update the following documents only for an accepted production path:

- `README.md` for the actual renderer behavior and controls;
- `Q2RTX_LIGHTING_PARITY_PLAN.md` for the preserved quality contract and final
  measured status;
- `docs/DXR_PROVENANCE.md` for source evidence and clean implementation bounds;
  and
- this document for completed milestones and final parity results.

## Explicit non-solutions

- Do not claim the existing 5.5% diagnostic cleanup as performance parity.
- Do not lower resolution, change RR/upscale mode, enable DRS, or raise queued
  frames without reporting it.
- Do not remove the second diffuse surface, guide ray, or direct survivor and
  replace lost energy/stability with a magic constant.
- Do not cap, prune, or ignore authored emitters or geometry.
- Do not reactivate the rejected screen-space direct reservoir or experimental
  ReSTIR GI path merely because it already exists.
- Do not copy Q2RTX GPL shader code or its Quake-specific content behavior.
- Do not optimize ReGIR refresh, post, or scene code first while the traced hot
  path remains more than 90% of the frame.
- Do not leave placeholder counters, unbounded work lists, overflow drops,
  silent quality fallbacks, or permanent experimental renderer branches.

## Completion checklist

Performance parity is complete only when all of the following are true:

- [ ] Release native and Q2RTX matched-work and shipping manifests exist.
- [ ] Five alternating 600-frame trials exist for static and shipping profiles.
- [ ] Native median and p95 are at or below 16.67 ms and inside Q2RTX's measured
      envelope; p99 has no new hitch.
- [ ] Actual output, trace, reconstruction, and guide extents are reported.
- [ ] Ray/candidate/material/dispatch counts explain the achieved reduction.
- [ ] The monolithic hot path has been replaced by the accepted coherent work
      schedule; rejected experimental modes and old production paths are gone.
- [ ] No silent resolution, light, geometry, bounce-energy, clamp, or queue-depth
      shortcut contributed to the result.
- [ ] All lighting, exact-black, channel-sum, material, motion, RR-guide, smoke,
      lifecycle, and provenance gates pass.
- [ ] The warmed dynamic sequences allocate/rebuild/reset nothing unexpected.
- [ ] README, lighting handoff, provenance, and this plan describe the same final
      renderer and measured configuration.

Until every item passes, report the achieved frame time and remaining dominant
stage; do not label the work Q2RTX performance parity.
