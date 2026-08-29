# Q2RTX Performance Parity Execution Plan

Status: execution plan, 2026-08-29. The accepted lighting result remains the
quality oracle. Performance parity is open.

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
