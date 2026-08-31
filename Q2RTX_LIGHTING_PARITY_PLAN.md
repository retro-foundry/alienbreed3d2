# Q2RTX Indoor Lighting and Performance Parity Handoff

Status: indoor-core direct/specular lighting implementation and validation are
complete. Primary direct diffuse/GGX, full-rate direct sampling, active RR
guides, real smooth specular, packed-F0 rough reconstruction, and radiance
isolations were implemented on 2026-08-28. Reference-scene, all-level, and
moving-lighting acceptance pass. Direct reservoirs remain dormant because
every measured temporal/spatial ReSTIR DI variant was less stable than fresh
RIS. Performance parity was reopened on 2026-08-29. The later 2026-08-29
clean-room correction established the current RR-only diffuse baseline: four
fresh, stratified cosine-weighted paths per internal pixel and no native GI
filter. Diffuse-energy parity is now reopened. Project-owned temporal
integration was evaluated at four and 64 presentations, then rejected as a
production default by two full-resolution user runs on 2026-08-31. Both
progressively retained rare, large indirect estimates until the initially clean
image filled with a stable bright-dot field. Production remains four fresh
current-frame paths and DLSS Ray Reconstruction as the sole temporal/spatial
reconstructor. After accepting that fresh result visually, the user requested
explicit INI choices to compare history lengths. The default remains one frame;
values `2..64` opt into the rejected experiment and are not parity acceptance.
The next production candidate must reduce variance in the genuine secondary
estimator; history length, radiance clamping, ambient fill, or another pre-RR
spatial filter may not conceal it. The linear-energy/Q2RTX comparison and
`DiffuseGiScale=0.75` versus `1.0` decision remain open.

Date: 2026-08-27

Last updated: 2026-08-31

## Temporal accumulation rejection (2026-08-31)

- Four-frame history first converged to the reported salt-and-pepper image. A
  64-frame/full-256-sample-block retry reproduced the same failure after one to
  two seconds: the image began clean, then dots appeared progressively and
  remained.
- Primary visibility jitter is exactly zero in production, and the frozen
  history diagnostics showed exact same-pixel acceptance. The failure is not a
  bilinear reprojection leak. Each pixel eventually receives a rare, large
  unbiased secondary-light estimate; feedback makes that outlier persistent,
  so DLSS-RR interprets it as stable image detail rather than current noise.
- A temporal mean cannot solve this raw-estimator distribution. Making the
  history longer increases the number of contaminated pixels before enough
  independent hits exist to converge them, while shortening it merely raises
  the permanent variance floor. Neighborhood clipping or a radiance ceiling
  would remove the very rare positive energy this task is trying to recover.
- Production therefore defaults to a one-frame window. At that value, slot-B
  and metadata compatibility resources are one texel and there is no
  full-resolution renderer-owned GI history allocation. The user-requested
  `rtx_gi_temporal_frames=1..64` diagnostic can explicitly allocate and enable
  the rejected history for A/B testing; `1` restores the accepted fresh image.
- Blue noise continues to advance normally through
  `SampleIndex * IndirectSamplesPerPixel + sampleOrdinal`. DLSS-RR receives four
  new stratified estimates on every presentation instead of a stable recycled
  dot field. The next measured candidate must improve the fresh secondary
  proposal or distribute additional genuine current samples more cheaply.
- Post-removal validation passed the warnings-as-errors Streamline Release
  shader/renderer build, all `32/32` Release CTest cases, combined and
  indirect-only saved-state smoke, and a 256-presentation indirect-only frozen
  run. The user then accepted the exact full-resolution fresh-only pose as
  looking good. The subsequent configuration slice restores the same rejected
  accumulator only when the INI or matching environment override is above one.
- The configuration slice passes the complete `32/32` Release suite and
  indirect-only saved-state GPU smoke at effective lengths `1`, `2`, `8`, and
  `64`. Parser tests accept `1..64`, reject `0/65`, and the renderer reports the
  applied INI value through its active-options boundary. These are execution
  checks, not visual acceptance of history values above one.

## Next low-cost fresh-estimator slice

Do not choose the next sampler by looking only at a tone-mapped screenshot.
First add hidden-validation counters/moments that separate:

- first diffuse rays that miss geometry;
- reached surfaces whose RIS candidate set contains no positive emitter sample;
- selected secondary-light samples rejected by visibility; and
- finite secondary contributions' mean, second moment, and upper percentiles.

Run those counters at four and 32 fresh paths with RR off. The 32-path result is
the oracle; it identifies whether the sparse dots originate in the first
cosine direction or in the reached-surface light proposal. In parallel, finish
the linear-HDR `DiffuseGiScale=0.75` versus `1.0` comparison so a mean-scale
error is not mistaken for sample loss.

If reached-surface light selection dominates, test proposal work before more
continuation rays: increase the unshadowed polygon RIS candidates, then test two
independent RIS survivors formed from disjoint candidate streams and average
their two visibility-tested estimates. The latter adds secondary shadow rays
but not primary-to-secondary continuation rays, so it is the most plausible
way to buy two genuine current light samples below the cost of doubling diffuse
SPP. It must remain unbiased and stay inside the five-percent frame-time gate.

If first-direction misses dominate, proposal work cannot help; measure a
fresh-path increase directly and reject any checkerboard/interleaved variant
that presents zero or stale radiance as a sample. Extra paths must use new
global sample ordinals. Preserve the four-sample radial/azimuthal strata and
independent polygon-light streams; never rewind a disoccluded pixel or reuse a
blue-noise value as temporal history.

## New-context handoff (2026-08-29)

- Continue on branch `new`. The implemented RR-only checkpoint spans commits
  `26df784` through `9c0af3c`; `origin/new` was synchronized and the working
  tree was clean when this handoff was prepared.
- The user's standing direction is to continue performance work, commit coherent
  slices as they land, and push accepted work. Use the Streamline Release save
  at `build/streamline/Release/savegame.bin` for captures and motion oracles;
  the Level A starting room is not the accepted comparison pose.
- The user's direction superseded the earlier ban on all renderer-owned GI
  history long enough to evaluate it. Four- and 64-presentation candidates both
  failed the 2026-08-31 production gate, so the rejection above remains
  authoritative for defaults. The later explicit request permits lengths
  `2..64` only as user-controlled diagnostics; `1` remains production.
  Do not reintroduce regional, deflicker, wavelet,
  ReSTIR-GI, current-frame spatial reuse, broad continuation, or
  projected-solid-angle triangle sampling. Do not copy or adapt GPL Q2RTX
  implementation details; its observable low-frequency temporal accumulation
  establishes the missing workload class, not this implementation.
- Current production GI traces four genuine fresh cosine-weighted diffuse paths
  per internal pixel, stratified radially and azimuthally, with standard
  uniform-area authored-triangle NEE. With `rtx_gi_temporal_frames=1`,
  `ReconstructIndirect` remodulates only that fresh directional/chroma signal
  before diffuse and rough-specular composition. `raw` and legacy `full` are
  compatibility aliases; the INI/environment value owns the diagnostic. The
  former broad GI filter passes and ReSTIR-GI code remain absent.
- The accepted saved RR Quality checkpoint at 853x480 tracing to 1280x720 is
  `8.0234/9.1955/17.7795 ms` frame median/p95/p99, `3.3736/4.0934 ms` burst
  median/p95, and `1.7582 ms` RR median. Indirect-only frozen
  delta/reprojected delta is `0.5182/0.5224`; Shotgun is `2.8855/0.6593`.
- Validation at handoff: the complete Streamline Release build succeeded, all
  32 CTest cases passed, ordinary saved-state RTX smoke passed, and saved-state
  indirect-only smoke passed with
  `AB3D2_DXR_INDIRECT_RECONSTRUCTION=full`.
- Next, complete the linear-HDR mean-energy oracle and matched Q2RTX receiver
  comparison at `DiffuseGiScale=0.75` and `1.0`, then attribute variance between
  the first diffuse direction and the reached-surface light proposal. Keep the
  four-fresh-path RR-only baseline fixed during that audit. Do not use history
  to conceal a transport-scale mismatch.
- Final parity remains open: the required five alternating matched-work Q2RTX
  and native trials, actual workload manifests, and the completion checklist in
  `Q2RTX_PERFORMANCE_PARITY_PLAN.md` have not been completed.

## Bounded temporal diffuse sample integration (rejected, 2026-08-31)

Q2RTX's displayed diffuse GI has passed through motion-reprojected
low-frequency history in
`src/refresh/vkpt/shader/asvgf_temporal.comp`; the prior native baseline had
only the four paths from the current presentation. That was a material sample-
count mismatch even before the two reconstruction systems were compared. Use
that observable stage ownership only. The candidate below is a small standard
running estimator built around the native renderer's existing data and must not
copy Q2RTX's filter equations, gradients, constants, or spatial passes.

Temporal integration reduces variance and lets a stable receiver present more
genuine path energy to RR, but it does not change the estimator's expected
value. This distinction matters because
`path_trace.hlsl::ReconstructIndirect` currently applies
`DiffuseGiScale`, whose production default is `0.75`, after decoding the
incident signal. Q2RTX's ordinary local-light bounce path has no matching global
`0.75` indirect multiplier. More accumulated samples cannot recover that
twenty-five-percent attenuation. Before judging the temporal candidate:

- capture a frozen, RR-off long-run mean with the temporal window forced to one
  and `rtx_indirect_samples=32` at both `rtx_diffuse_gi=0.75` and `1.0`;
- record mean/median luminance, non-black coverage, and fixed diffuse-receiver
  regions rather than judging one noisy frame;
- compare those values with the matched Q2RTX capture and the accepted authored
  emitter/material manifest; and
- if the high-sample `0.75` result remains low while `1.0` matches, promote
  `1.0` as the parity transport scale in a separate measured change. Do not use
  history length, exposure, ambient fill, or a fitted energy multiplier to hide
  a mean-transport mismatch.

### Rejected candidate record

The experiment kept four fresh `sampleDiffusePath` paths per internal pixel and
accumulated their directional/chroma signal at `ReconstructIndirect`. It used
exact primary-primitive validation, sub-half-pixel motion rejection, authored
emitter invalidation, and the existing motion/jitter convention. Four-frame and
64-frame caps were evaluated; neither is selectable now.

The sampler itself behaved as intended. The global index remained
`SampleIndex * IndirectSamplesPerPixel + sampleOrdinal`, every presentation
retained all four radial/azimuthal strata, and 64 presentations consumed a full
256-sample blue-noise/Sobol block without repetition. History rejection resumed
at the current global phase rather than rewinding the sequence.

That sequence diversity did not make temporal feedback suitable for this
estimator. Rare positive samples remained at their pixel long enough to become
a progressively denser stable dot field. The exact-pixel frozen-camera result
rules out bilinear propagation as the primary cause. Because clipping those
positive samples would bias away the missing diffuse energy, the candidate
failed the visual gate and was removed regardless of its low measured GPU cost.
### Initial four-frame result and rejected decision

- The implementation uses two `RGBA16_FLOAT` directional-SH textures, two
  `RG16_FLOAT` opponent-chroma textures, and two `R32_UINT` identity/count
  textures. It adds `20 bytes * internal pixel count`: `8,188,800 bytes`
  (`7.81 MiB`) at the saved RR Quality extent of `853x480`. The metadata keeps
  a 24-bit exact global primitive index and an 8-bit effective count; scenes
  beyond the identity range fail explicitly.
- No ray, root parameter, shader export, or dispatch was added. Primary/burst
  shading still traces exactly four current diffuse paths. The existing
  `ReconstructIndirect` dispatch performs the four-tap reprojection and count-
  weighted mean, and diffuse plus rough specular consume that one result.
- The first exact-primitive window sweep exposed an important rejection case.
  With unrestricted motion, window two measured `7.6111/1.3973` and window four
  `8.2886/1.4963` Shotgun delta/reprojected delta versus the one-frame control's
  `4.5457/0.7478`. Exact triangle identity alone cannot prove that a moving
  footprint is the same lighting point on a large triangle. The experiment then
  rejected history beyond half an internal pixel. Window four measured
  `4.5561/0.7509`, while its frozen combined delta improved from `0.5204` to
  `0.5156`.
- The same gated indirect-only A/B improved frozen delta/reprojected delta from
  `0.5182/0.5224` to `0.5096/0.5121`. Shotgun remained effectively unchanged at
  `2.8890/0.6582` versus `2.8855/0.6593`. A hash of exact emitter records,
  triangle positions, and emissive scale now rejects only the compact GI
  history whenever an authored emitter moves or changes power; the sparse-
  emitter recovery GPU test passes with that invalidation active.
- RR-off display diagnostics demonstrate the intended sample-retention effect
  at four fresh paths and scale `0.75`: settled non-black coverage rose from
  `360,825` to `729,845` pixels, presented mean luminance from `5.7528` to
  `11.5638`, and frozen delta fell from `8.7430` to `3.0949`. These are tone-
  mapped display values from a finite run, not proof that the unbiased linear
  mean changed. They show that real rare positive samples survive long enough
  to be displayed instead of disappearing between frames.
- The first 32-spp/window-one RR-off audit measured `528,601 / 9.9958` non-black
  pixels/mean display luminance at scale `0.75` and `555,587 / 11.8570` at scale
  `1.0`. Because tone mapping and sample coverage are nonlinear, this confirms
  the visible cost of the `0.75` attenuation but does not replace the required
  linear-HDR regional mean and matched Q2RTX capture. The production scale
  therefore remains `0.75` pending that separate decision.
- Five alternating 60-sample hidden-validation profiles on the RTX 3090 put
  window one at `7.6962/8.7469 ms` median-of-trial frame median/p95 and window
  four at `7.7702/9.2233 ms`. Median overhead is `0.96%`; p95 is `5.45%` higher
  but lies inside the measured per-mode run dispersion. `indirect_reconstruct`
  median rose from `0.0502` to `0.2744 ms`, far below the saved `4 -> 8` fresh-
  path cost. These hidden profiles include validation-only history atomics, so
  they are a conservative production-cost bound.
- Shader warnings-as-errors, the complete Streamline Release build, all `32/32`
  CTest cases, the sparse-emitter recovery test, combined saved-state smoke,
  and isolated-indirect saved-state smoke passed at the time. Those former
  temporal metadata/window tests have now been removed. The general blue-noise
  test still proves a complete 256-value permutation at a fixed pixel and
  dimension. The performance numbers in this subsection describe a rejected
  candidate and must not be presented as acceptance of its converged image.

## RR-only diffuse input baseline (restored production, 2026-08-31)

- At this checkpoint the production signal contained only current-frame path samples. There was no
  renderer-owned temporal radiance accumulation, regional integration,
  deflicker, wavelet pass, ReSTIR-GI reuse, or spatial radiance interpolation
  before DLSS Ray Reconstruction. `AB3D2_DXR_INDIRECT_RECONSTRUCTION=full`
  was only a compatibility alias for `raw`; the removed stage names were
  rejected at startup.
- That cleanup also removed the two obsolete 24-byte-per-pixel GI history
  buffers. `ReconstructIndirect` reads the raw directional/chroma textures
  directly, avoiding one packed write/read and its UAV barriers every frame.
- Increasing RR input from the accidentally forced one path to 1/2/4/8 genuine
  paths measured `5.7920/5.9823/7.9022/11.6293 ms` frame median in the saved
  Level A RR Quality sweep at 853x480 tracing and 1280x720 output. Four paths
  retained the useful quality of eight without its additional cost and became
  the default.
- Those four paths use ordinary cosine-weighted Lambertian continuation and
  uniform-area authored-triangle NEE. Their radial and azimuthal random values
  are stratified per pixel, continuation, and frame. This distributes genuine
  ray locations across the integration domain; it never fabricates or filters a
  radiance value for RR.
- The final stratified saved-game validation measured `8.0234 ms` frame median,
  `9.1955 ms` p95, `3.3736 ms` burst median, and `1.7582 ms` RR median on the
  RTX 3090. Frozen indirect-only delta/reprojected delta was
  `0.5182/0.5225`; Shotgun was `2.8855/0.6593`, with zero frozen saturated or
  >=16-code outlier pixels. This is a short hidden-validation checkpoint, not
  the still-open repeated Q2RTX matched-work benchmark.
- Historical projected-solid-angle, broad-continuation, native-filter, and
  ReSTIR-GI measurements below are retained only as rejected experiment
  evidence. They no longer describe selectable or compiled production paths.

## Performance parity reopening (historical baseline, superseded 2026-08-29)

The measurements and adaptive-history schedule in this section record the
state that initiated performance work. The new-context handoff and restored
RR-only section above describe current production behavior.

- The renderer does not yet have a valid current GPU-performance baseline.
  The earlier approximately `8.1 ms` Ultra Performance measurements predate
  full-rate primary direct sampling, the completed smooth/rough specular work,
  and the 16-path adaptive indirect burst. The later `96.77s` versus `92.80s`
  all-level smoke result is test wall time, not steady-state GPU frame time.
  The Shotgun `renderer_present` timer also includes frame-latency waiting,
  command recording, submission, and presentation. None of those numbers can
  establish Q2RTX performance parity.
- The approved Q2RTX checkout at commit
  `f2526e9a165949f66e91e82f0d63aa7bb2567b4d` has a non-blocking timestamp
  profiler in `src/refresh/vkpt/profiler.c`. `main.c`, `path_tracer.c`,
  `shader/global_ubo.h`, `shader/direct_lighting.rgen`, and
  `shader/indirect_lighting.rgen` establish the relevant observable workload:
  separate primary, direct, and indirect dispatches; one local-light shadow
  sample at the primary receiver; a default `pt_num_bounce_rays=1`; one first
  continuation selected between diffuse and GGX rather than two independent
  continuations; an optional `0.5` dense half-resolution diffuse mode; and an
  optional GPU-time-driven dynamic-resolution controller that is off by
  default.
- The current AB3D2 default performs materially different work: two independent
  primary RIS survivors, a deterministic mirror-distance guide ray, a separate
  GGX continuation, as many as two diffuse continuation surfaces at default
  depth three, and up to 16 fresh diffuse paths at missing, disoccluded,
  immature, or changing pixels. Stable mature pixels already rotate one fresh
  diffuse path through a 2-by-2 phase. A raw frame-time comparison without ray
  counts, internal pixel counts, and stage timings would therefore be
  misleading.
- Performance parity has two required results. The matched-work benchmark must
  show that the native DXR stages are no slower than Q2RTX outside measured
  run-to-run noise when both render the same source Level A pose and material/
  emitter set at the same internal pixel count and equivalent bounce/light
  work. The shipping
  benchmark must then bring the accepted default lighting path into the same
  steady-state frame-time envelope without reducing authored content, changing
  transport energy, hiding work behind extra queued frames, or weakening the
  locked visual and temporal acceptance oracles.

### Combined scheduler progress (2026-08-29)

- The S0/S1/S2 sequence splits primary visibility, shares one direct
  RIS survivor between diffuse and GGX, and selects one mutually exclusive
  first smooth/diffuse continuation with inverse-probability weighting. S3a
  adds a dense quarter-pixel launch for the already accepted mature rotating
  GI phase; burst/disoccluded pixels remain on the exact full primary path.
- In the current paired Release Level A hidden-validation profile, S3a reduced
  frame median from `14.6520` to `10.1687 ms`. Primary shading fell from
  `12.2726` to `6.1220 ms`; the extracted dense pass cost `1.2385 ms`. Paired
  final SDR captures stayed within one 8-bit code with mean absolute component
  difference `0.02447`, and the full moving/firing route retained zero
  unexpected scene rebuilds.
- At this implementation checkpoint it was not yet a production default or a
  performance-parity claim. The bounded S3 burst list is now implemented and
  moves new/disoccluded/changing pixels into an exact GPU-indirect dispatch.
  Across two alternating pairs it added `1.3-2.6%` median setup cost but reduced
  frame p95 by `29.0-30.2%` and p99 by `18.0-26.9%`; an unprofiled paired SDR
  capture stayed within one 8-bit code. S0-S3 remained off by default. The S4
  burst-ceiling sweep, locked repeated profiles, validation work counters, and
  direct Q2RTX run remain open in `Q2RTX_PERFORMANCE_PARITY_PLAN.md`; the
  lighting and temporal acceptance contract in this document is unchanged.
- The validated S0-S3 scheduler is now the ordinary adaptive-temporal production
  route rather than an environment-only experiment. A no-override Release
  Level A pair at `1280x720` reduced GPU frame median from the explicit
  monolithic control's `21.5461 ms` to `10.9554 ms` (`49.2%`) and p95 from
  `84.5120 ms` to `58.2606 ms` (`31.1%`) over the same changing 120-frame
  window. The complete route retained frozen stability `14.3395/14.3626`,
  moving delta `26.6886`, and zero unexpected walk rebuilds. Raw/ReSTIR,
  zero-history, and nonzero-clamp diagnostics retain their established control
  automatically; explicit `0|1` feature overrides remain for attribution.
  This promotion fixes the user-visible default but does not close performance
  parity: S3 burst continuation remains the dominant p95 stage.
- The first S4 ceiling sweep did not change that decision. Ceiling one reached
  `16.6009 ms` p95 in its single preliminary trial, but its confirmed-change
  temporal current weight is only `0.380` versus ceiling 16's `0.859`; settled
  and saved-motion captures do not isolate that recovery loss. Eight global
  light candidates with ceiling 16 reached `15.8996 ms` p95 but made the locked
  corridor visibly darker/less warm and raised saved-motion temporal outliers.
  Both shortcuts remain rejected pending the isolated indirect change oracle
  and complete-emitter local proposal required by the performance plan.
- The first complete-emitter local proposal is also diagnostic only. A
  six-global/two-ReGIR primary mix improves primary-shading median by about
  `10.9%` and frame p95 by about `10.9%` in one alternating pair, but saved
  frozen and Shotgun temporal deltas remain `9-14%` above the 16-candidate S3
  control. Production still retains 16 exact candidates while 1C moves to a
  16-point cheap proxy with exact textured evaluation only for the survivor.
- That 16-point proxy has now also been measured and rejected. It is unbiased
  and keeps exact selected textured emission, but primary-shading median moves
  only `1.2%`, p95 regresses `4.2%`, and the saved yaw/Shotgun deltas remain
  `8-13%` above S3. Its forced isolated lighting and six-channel tests pass,
  so rejection is based on variance and low leverage rather than broken energy
  accounting. The accepted lighting contract therefore still uses 16 exact
  global candidates; performance work moves to the conditional RR guide.
- The RR guide is now consumer-gated without changing lighting. RR-off
  force/skip final captures differ by at most one 8-bit code and saved-motion
  metrics are identical; RR Quality and the guide debug view retain the exact
  deterministic current-geometry ray. This removes `16-19%` of primary-shading
  median in the measured S3 pairs without weakening the lighting gates.
- Combining that gate with burst ceiling one reaches
  `6.3730/9.6050/10.5056 ms` frame median/p95/p99 and passes Level A, but the
  ceiling is not accepted: its first-frame confirmed-light-change weight is
  still `0.380` versus ceiling 16's `0.859`.
- The fixed-receiver indirect-only light-toggle oracle is now implemented in
  `ab3d2_renderer_rtx_indirect_recovery_test`. It matures dark history without
  changing the camera or receiver, moves the same dynamic authored emitter into
  its lit position without resetting history, and measures twelve reconstructed
  indirect-only frames. The emitter texture is deliberately sparse: one 2-by-2
  white quadrant and twelve black texels. Ceiling 16 reaches a `0.4926`
  normalized mean across response frames three through eight and `0.4247` on
  the fourth response frame. Ceilings 8/4/2/1 reach only
  `0.0835/0.0785/0.0403/0.0078`, or
  `16.95%/15.94%/8.17%/1.58%` of the control. All lower ceilings therefore
  fail the `90%` recovery gate; ceiling 16 remains the production quality
  contract and the fast ceiling-one profile remains attribution only.
- Exact emitter evaluation now uses one hardware-bilinear emissive sample from
  a repeat-guttered atlas tile instead of four explicit texel loads. The
  ceiling-16 sparse-emitter recovery remains `0.4725`, or `95.9%` of the
  software sampler's `0.4926`, and its fourth response frame is `0.4288` versus
  `0.4247`. All 16 proposals and the exact textured RIS target remain present;
  this is an accepted cost reduction rather than the rejected proxy estimator.
- Reached-surface base colour, normal, metalness, roughness, and emission now
  use the same gutter-safe hardware bilinear primitive while retaining the
  accepted directional footprint, software mip selection, explicit trilinear
  blend, and bounded anisotropic tap positions. Sparse recovery is unchanged,
  and RR-off saved-state frozen/Shotgun metrics remain
  `6.8729/6.7701` and `7.4038/7.1706` delta/reprojected versus the emitter-only
  checkpoint's `6.8752/6.7721` and `7.4052/7.1713`.
- Atlas dimensions now arrive once as integer root constants instead of a
  resource query in every hot evaluation. Keeping the reciprocal in HLSL
  preserves the prior deterministic first-frame hashes in both RR-off and RR
  Quality; passing CPU-computed reciprocals was explicitly rejected because it
  perturbed sample coordinates.
- The production adaptive scheduler now always traces the first diffuse
  continuation, then uses one blue-noise, throughput-adaptive Russian-roulette
  decision for the complete deeper suffix. A surviving suffix is divided by
  its exact probability, so the default second indirect surface remains an
  unbiased estimator rather than being removed or replaced by a fitted energy
  constant. This follows the observable workload boundary in Q2RTX
  `shader/indirect_lighting.rgen`, whose default is one continuation and whose
  deeper pass is optional, while retaining this renderer's accepted depth-three
  result.
- Acceptance uses the saved Level A corridor beside the Streamline executable,
  not the unrelated older save in the non-Streamline build directory or the
  Level A starting room. At the locked saved Shotgun frame, candidate/control
  comparisons measure `48.74 dB / 0.99730` PSNR/SSIM for RR-off combined,
  `52.79 dB / 0.99808` for RR-off indirect-only, and
  `48.42 dB / 0.99721` for RR Quality combined. RR Quality frozen delta is
  `0.5045/0.5045`; Shotgun delta is `4.7364/4.7331` and reprojected delta is
  `0.6775/0.6790`. At this pre-projected-sampling checkpoint, sparse-emitter
  recovery was `0.4725` with fourth response `0.4288`, because all first
  diffuse paths were then unchanged.

### Firefly diagnosis (historical)

- The reported bright blot was traced to a rare raw indirect sample being
  widened by the now-removed native regional and wavelet stages. The audit was
  useful evidence that a second denoiser must not precede DLSS RR.
- A projected-solid-angle emitter sampler was briefly tested as an estimator
  fix, then removed under the clean-room requirement. Production now uses
  standard uniform-area triangle sampling, standard cosine-weighted diffuse
  continuation, four per-pixel strata, and RR as the only reconstructor.
- The current accepted result and timings are recorded in the RR-only
  correction section above; all stage-boundary and ceiling-16 figures formerly
  in this section are rejected historical checkpoints.

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
- The moving Shotgun endpoint measured `4.8294`. The deterministic GPU
  reference scenes below are complete; the channel attribution below separates
  accepted direct lighting from the remaining indirect/rough-specular work.

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
- At this checkpoint these results left the production path unchanged. The
  later saved-corridor firefly audit below supersedes only the denoiser
  ownership and polygon-sampling decision; it does not revive the dormant
  screen-space reservoir or freeze reused samples.

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

### Moving-channel attribution (2026-08-28)

- A complete saved-state isolation sweep shows that primary direct light is no
  longer the source of the moving instability. Direct specular measures
  `2.4987/0.4711` and direct diffuse measures `2.1634/0.4958` for
  screen/reprojected delta; both reprojected results are at or below their
  frozen-channel noise floors.
- The untouched emission and smooth-specular channels likewise reproject to
  `0.3929` and `0.4000`. Indirect diffuse instead measures `3.1604/0.5890`,
  and reconstructed rough specular measures `3.9162/0.6047`. Those two
  correlated channels account for the remaining combined `4.8309/0.7022`
  endpoint; an immediately repeated combined run stayed within `0.0015/0.0001`
  of the locked capture.
- Moving direct-light acceptance is therefore closed. Further quality work
  belongs at the shared filtered directional-GI input used by indirect diffuse
  and reconstructed rough specular, not in direct reservoirs or additional
  primary shadow rays.

### Adaptive indirect burst follow-up (historical, superseded 2026-08-29)

- A stage-boundary sweep measured the indirect channel at `0.6676` raw,
  `0.7128` temporal-only, `0.6888` regional, `0.6533` deflickered, `0.6206`
  after wavelet one, `0.5999` after wavelet two, and `0.5890` after the full
  spatial reconstruction. Every accepted spatial stage reduces the
  motion-compensated error, so the diffuse filter and its converged result stay
  unchanged.
- The fresh-path ceiling used only for missing, disoccluded, immature, or
  confirmed-changing history now defaults to 16 instead of four. It fills the
  existing 32-sample history in two presentations; mature pixels still rotate
  one path through each 2-by-2 block, and primary direct NEE is not repeated.
- Sixteen paths improve the yawing indirect channel from `0.5890` to `0.5784`,
  rough specular from `0.6047` to `0.5888`, and combined from `0.7022` to
  `0.6798`; combined sampled reprojected outliers fall from `194` to `169`.
  A 32-path burst regresses combined to `0.6853`, so it is rejected. The
  all-level native smoke passes in `96.77s` versus `92.80s` at four paths,
  remaining inside the existing 120-second gate.
- A rebuilt production-default capture repeats at `0.6800` with `169` sampled
  reprojected outliers. It retains the left-wall engraving, floor pattern,
  panel edges, and rough-highlight shape without new blur or structured
  breakup.
- Four exact saved-motion poses at Shotgun update/subframe `1/1`, `3/4`, `6/4`,
  and `9/2` were inspected together. Panel edges and floor relief track the yaw,
  the left-wall highlight remains attached to geometry, and the evolving flash
  leaves no history trail. The valid motion-compensated endpoints are `0.6444`,
  `0.6278`, and `0.6800`; the first presentation correctly has no valid prior
  correspondence because weapon history is intentionally rejected. This closes
  moving indirect/rough-specular visual acceptance for the indoor core.

## Goal

Match the useful indoor lighting structure, diffuse-GI energy, and steady-state
performance envelope of Q2RTX while preserving authored response. Direct and
specular transport are complete; the active lighting work is to distinguish a
real mean-energy mismatch from four-path undersampling, then reduce the fresh
secondary estimator's variance without materially increasing traced work. This
is not an exposure change, ambient/shadow lift, temporal feedback, or unmeasured
quality cut.

The target pipeline is:

1. Primary visibility, material reconstruction, visible authored emission, and
   non-occluding additive layers.
2. Direct diffuse plus GGX specular lighting from authored local emitters.
3. Four fresh, stratified cosine-weighted diffuse paths per internal pixel,
   using uniform-area authored-triangle NEE and no renderer-owned temporal or
   spatial radiance reconstruction before RR.
4. Real first-bounce GGX specular transport for smooth materials.
5. Q2RTX-style rough specular derived from the current directional GI sample.
6. One combined noisy-HDR input with correct diffuse/specular guides for DLSS
   Ray Reconstruction, followed by the already accepted bloom, adaptive tone
   curve, exposure bias, and SDR/HDR presentation.
7. A profiled frame schedule whose traced-ray, reconstruction, post-processing,
   scene-update, and presentation costs match Q2RTX's performance envelope at
   equivalent internal resolution and work, while retaining the accepted
   result above.

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
- `shader/asvgf_temporal.comp` demonstrates the observable workload difference
  relevant to the reopened energy task: Q2RTX motion-reprojects and temporally
  accumulates its low-frequency directional lighting before final display. Its
  long history, gradient logic, filter constants, and spatial stages establish
  a workload difference, but the rejected native temporal experiment proves
  they are not directly transplantable to this estimator/RR boundary.
- `shader/global_ubo.h` and
  `shader/indirect_lighting.rgen::indirect_lighting` expose switches for local
  polygon/dynamic bounce lights but no general `0.75` multiplier corresponding
  to the native `DiffuseGiScale`; that scale therefore requires the explicit
  mean-energy audit above.
- `shader/asvgf_atrous.comp` reconstructs rough specular from the filtered
  first-order directional low-frequency signal. The blend begins at roughness
  `0.20` and reaches the reconstructed path at `0.30`.
- `shader/reflect_refract.rgen` is a separate special-material surface
  replacement stage for water, slime, glass, chrome, screens, cameras, and
  transparent materials. It is not the ordinary opaque GGX continuation.
- `shader/god_rays.comp`, `god_rays_filter.comp`, `physical_sky.comp`, and the
  caustic path are optional/content-dependent stages, not ambient fill.
- `profiler.c` and the `PROFILER_LIST` contract in `vkpt.h` use timestamp-query
  pairs for frame, instance geometry, BVH, primary, direct, indirect,
  reconstruction, bloom, tone mapping, and upscale costs without a same-frame
  GPU wait.
- `path_tracer.c::vkpt_pt_trace_primary_rays` and
  `::vkpt_pt_trace_lighting` dispatch primary, direct, and up to two indirect
  passes separately. The `0.5` indirect mode launches a dense half-height grid
  and maps it to alternating output rows instead of launching a full frame of
  inactive threads.
- `shader/global_ubo.h::pt_num_bounce_rays` defaults to one. The first
  `shader/indirect_lighting.rgen::indirect_lighting` continuation selects one
  diffuse or GGX direction and applies the matching selection probability; it
  does not trace independent diffuse and specular continuations for the same
  pixel.
- `main.c::drs_process` consumes the GPU frame timestamp, rejects non-world and
  invalid samples, and uses a short trimmed history to control an optional
  bounded resolution scale. Q2RTX leaves this controller disabled by default,
  so it is a frame-pacing tool after fixed-resolution efficiency, not evidence
  for claiming parity at a lower unreported resolution.

Pre-implementation AB3D2 lighting evidence retained for this handoff:

- At the start of this handoff,
  `src/renderer_dxr/shaders/path_trace.hlsl::RayGeneration` called
  `sampleDiffusePolygonLight` and `sampleDiffusePath`, so the live estimator was
  intentionally diffuse-only.
- At the same baseline, `writeDiffuseSurfaceGuides` wrote zero specular albedo,
  a fully rough guide, and zero specular hit distance even though the material
  system already supplied base color, normal, metalness, roughness, and
  specular factor.
- `evaluateBsdf`, `sampleBsdf`, `sampleGgxVisibleNormal`, `surfaceF0`, and
  `reconstructionSpecularAlbedo` already contain most of the required clean
  PBR and RR mathematics, but the live path bypasses them.
- `resampleDirectTemporal` and `SpatialShade` are dormant screen-space direct
  reservoir experiments. `dxr_pipeline.cpp::record` deliberately does not
  dispatch `SpatialShade`; do not revive it for this milestone.
- `ReconstructIndirect` owns the directional diffuse-GI composition point. It
  derives rough specular from the same fresh directional estimate as diffuse
  before the combined signal reaches RR, the sole temporal/spatial
  reconstructor.
- `environmentRadiance` is a hard-coded analytic gradient with no AB3D2 scene
  authority. It is dormant and must not be activated. The real backdrop is in
  `SceneEnvironment` and the packaged `environment_backdrop` material is
  currently non-emissive.
- `SceneVertex::source_light_level` includes legacy raster-lighting inputs,
  including some flash/torch/projectile contributions. Those values remain
  authoritative for the source/OpenGL raster path but are not physical
  radiance and must not be reinterpreted as path-traced ambient light.

The implementation history contains intentional tone-mapping, HDR, exposure,
and radiance-clamp changes. Inspect `git diff` before implementation and do not
revert or overwrite unrelated user changes.

## Invariants

- Preserve diffuse transport energy, the ReGIR proposal's complete emitter
  coverage, directional representation, and final diffuse-GI remodulation. The
  production one-frame mode must not reuse renderer-owned radiance before RR.
  Every presentation still traces the complete four-path current estimate when
  an explicit temporal diagnostic is selected.
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
  diffuse and specular with separate RR invocations. Apart from the explicit
  same-primitive temporal diagnostic, do not add regional, deflicker, wavelet,
  or ReSTIR-GI stages.
- Treat the 2026-08-28 lighting captures, isolated-channel sums, exact-black
  tests, and motion-compensated metrics as the performance phase's quality
  oracle. A faster candidate is rejected if it changes transport energy,
  material response, authored emitter coverage, or temporal behavior outside
  the established repeat-run variation.
- Report presentation extent, path-tracing extent, reconstruction-output
  extent, ray counts, bounce work, and RR mode with every timing. A lower input
  resolution or smaller workload is a separate preset result, not a code-level
  speedup.
- Performance timestamp collection must be frame-latent and non-blocking. Do
  not map unresolved data, wait for the GPU, or enable full diagnostic readback
  in an ordinary visible frame.
- The existing independent diffuse and smooth-GGX continuations remain the
  production baseline. The performance phase may evaluate Q2RTX's one-ray
  diffuse/GGX selection as an explicit A/B candidate, but it becomes production
  only if both performance and all lighting acceptance gates pass.

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

At the indirect composition boundary, extend `ReconstructIndirect` while its
fresh first-order directional signal is available:

1. Read the packed primary F0, actual roughness, shading normal, and primary
   view direction.
2. Compute the dominant incoming direction from the luminance SH vector using
   Q2RTX's `L0/L1` basis ratio.
3. Let the dominant-vector length define directionality. When it is below one,
   blend the mirror direction toward the normalized dominant direction,
   increase effective roughness toward one by `directionality^3`, and use the
   Q2RTX compensation scale `(roughness + 1)^3`.
4. Project the same incident field used by diffuse GI, evaluate GGX
   toward that dominant direction, and multiply by
   `smoothstep(0.20, 0.30, materialRoughness)`.
5. Add this reconstructed rough-specular radiance to the specular contribution,
   not to the diffuse incident field. Do not remodulate it with diffuse albedo.

Radiance-channel diagnostics isolate final contributions without changing this
boundary. RR-off and active-RR modes must derive rough specular from the same
directional input as diffuse: fresh at the production one-frame setting, or the
same accumulated signal during an explicit temporal diagnostic.

### 5a. Default native diffuse history off; retain user diagnostic

`rtx_gi_temporal_frames` accepts `1..64` and defaults to one. At one,
`ReconstructIndirect` returns slot A's fresh directional/chroma signal, while
slot B and metadata remain one-texel compatibility bindings. Values above one
explicitly allocate the ping-pong signal/metadata and run the rejected
same-primitive reprojection/count-weighted accumulator for user A/B testing.
`AB3D2_DXR_GI_TEMPORAL_FRAMES` overrides the INI for one run. `raw` and legacy
`full` are compatibility aliases and do not override the length. Four fresh
paths and their global blue-noise indices remain unchanged in every mode. The
mean-energy and raw-estimator variance audits are still open.

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
evaluation. RR-off remains the unbiased/noisy reference path; production must
not add an unrelated blur or feedback loop to imitate Q2RTX's ASVGF.

Do not dispatch `resampleDirectTemporal`/`SpatialShade`. Their screen-space
reservoir history, neighbor reuse, and hard-coded environment candidates are
not required for the Q2RTX indoor lighting decomposition and previously made
the active path harder to reason about. They are unrelated to the fresh
secondary-estimator work.

### 7. Add a non-blocking frame and stage profiler

Instrument before changing sampling or pass structure.

Implementation status (2026-08-29): the first frame-latent profiler is now in
`src/renderer_dxr/dxr_performance.{h,cpp}` and is opt-in through
`AB3D2_DXR_PROFILE`. It owns two non-overlapping query slices, resolves the
current slice into one readback buffer, and maps it only after the corresponding
existing frame fence has completed. The JSON summary reports adapter/driver,
all three extents, settings, history/rebuild state, median/p95/p99/maximum GPU
stage distributions, and CPU phase distributions. Missing optional stages are
reported as exact zero rather than timestamp-resolution noise. The remaining
stage-7 work is the finer BLAS/TLAS/refit split, validation-only ray/bounce
counters, per-sample records, and video-memory fields.

The same implementation removes hidden-smoke diagnostics from ordinary visible
frames. Validation still clears and copies the diagnostic UAV and retains all
coverage/non-finite atomics; visible play sets `validation_enabled=false`, so
those atomics and the finite-guide scan do not run. This changes no radiance,
ray schedule, history, reconstruction, or post-processing result.

Preliminary same-process A/B evidence on 2026-08-29 (Debug host build, RTX 3090,
driver 32.0.15.9186, Level A, 2568x1471 tracing/presentation, RR off, 120 warmup
and 120 measured visible frames) is:

| workload | GPU frame median / p95 (ms) | primary/radiance median / p95 (ms) |
| --- | ---: | ---: |
| validation work forced on | 55.3175 / 56.4252 | 52.2301 / 53.2041 |
| ordinary visible path | 52.2993 / 53.2368 | 49.1126 / 50.2319 |

The removal saves 3.0182 ms (5.46%) at the median complete frame and 3.1175 ms
(5.97%) in the measured hot dispatch in this preliminary run. A subsequent
Level A hidden smoke retained exact diagnostic coverage and passed. These two
single trials demonstrate that the validation separation is worthwhile; they
do not replace the five alternating 600-frame shipping/Q2RTX baselines below.

- Add a D3D12 timestamp query heap with one non-overlapping slice per in-flight
  frame. Resolve each completed slice into frame-owned readback storage and
  consume it only after that frame's existing fence has completed. Convert
  ticks with `ID3D12CommandQueue::GetTimestampFrequency`; never wait merely to
  obtain a profile.
- Mirror the useful Q2RTX categories while retaining project ownership:
  complete GPU frame, scene upload/instance work, static and dynamic BLAS,
  TLAS, ReGIR refresh, primary/radiance tracing, bounded diffuse continuation,
  current-frame indirect composition, DLSS Ray Reconstruction, bloom,
  histogram/tone curve, and presentation draw. Nested totals must reconcile
  with the complete frame within timestamp resolution.
- Record CPU frame-latency wait, scene compilation, command recording, queue
  submission, and `Present` separately. The existing `renderer_present`
  elapsed time remains an end-to-end latency observation, not a GPU-stage
  measurement.
- Add validation-only ray counters for primary segments, deterministic
  specular-guide rays, primary visibility rays, smooth-GGX continuations,
  diffuse continuations by depth, secondary BRDF proposal rays, and secondary
  visibility rays. Counters must be absent from the ordinary shader path; a
  profile collected with atomic ray counters is diagnostic evidence, not the
  performance result itself.
- Emit a machine-readable hidden-smoke report containing adapter/driver,
  extents, RR mode, active quality settings, history state, per-stage GPU and
  CPU samples, ray counts, scene rebuild/refit counts, and committed/local video
  memory. Do not add a user-facing brightness or quality setting merely for the
  profiler.

### 8. Establish matched Q2RTX and shipping baselines

Use the generated Q2RTX Level A package only as an external comparator; the
native renderer must continue consuming its own `SceneFrame` geometry and
renderer-native material package.

1. Build/install the comparator with `tools/build_q2rtx.py --lighting none` so
   Q2RTX receives a map generated from the same source level pairs, PBR sheets,
   and two authored emitter identities without converted zone/point lights.
   Record the native and converted triangle/emitter counts; they are comparable
   source scenes, not byte-identical acceleration structures.
2. Record the exact AB3D2 commit, Q2RTX commit, generated-package manifest,
   adapter LUID, driver, OS, power state, presentation extent, internal tracing
   extent, output mode, FOV, camera transform, and every renderer setting.
   Match internal traced pixel counts; similarly named Quality/Performance
   presets are not assumed to be equivalent.
3. Disable VSync/frame caps and Q2RTX DRS for throughput runs. Use the common
   Level A source spawn plus a locked corridor pose, settle reconstruction and
   pipelines for at least 120 unmeasured frames, then collect at least 600
   frames. Run five alternating AB3D2/Q2RTX trials rather than all trials of
   one executable first.
4. Report median, p95, p99, maximum, and run-to-run dispersion for complete GPU
   frame time and CPU submit/present time. Report stage times and rays per
   output/internal pixel beside the totals. A mean alone cannot accept a
   frame-pacing change.
   Use one pinned external GPU/frame trace for cross-executable total
   percentiles; Q2RTX's built-in profiler supplies its stage attribution, not a
   substitute set of differently collected total-frame percentiles.
5. Keep three distinct profiles: a static matched-work profile for backend
   efficiency, each renderer's normal shipping-default profile for the user
   experience, and the native moving Shotgun/disocclusion sequence for dynamic
   history, BLAS/TLAS, and hitch regression. Do not use the cheaper static
   comparator to hide a moving native regression.

The baseline report must be committed or summarized in this handoff before an
optimization is accepted. Old pre-lighting timings stay historical and must not
be substituted.

### 9. Bring the ray schedule toward Q2RTX's cost

Use the profiler and ray counters to order these experiments. Change one cost
domain at a time and rebuild the locked captures after each candidate.

- Recheck one versus two independent primary RIS groups. Q2RTX's local-light
  direct path spends one local visibility ray; AB3D2 currently spends two to
  stabilize sparse authored emitters. One group is accepted only if it produces
  a measurable GPU win and retains direct-diffuse/direct-specular moving,
  saturation, exact-black, and captured panel/left-wall behavior. The earlier
  unprofiled one-ray rejection is evidence to remeasure, not permission to
  assume a win.
- Implement a diagnostic Q2RTX-shaped first-continuation candidate: choose one
  diffuse or GGX continuation, apply that lobe's selection probability and
  existing Fresnel/throughput equations, and route the reached radiance into
  the existing diffuse or smooth-specular channel. Preserve the direct/fake
  specular complement, additive traversal, secondary local-light contract, and
  channel-sum test. Compare it against the independent two-estimator baseline;
  do not ship a noisier lobe lottery merely because it traces fewer rays.
- Sweep the adaptive indirect burst ceiling through `1, 2, 4, 8, 16` with the
  same motion-compensated oracle. Q2RTX's default is one full-resolution first
  continuation; AB3D2's larger burst exists for RR/custom-filter convergence.
  Select the lowest measured ceiling that preserves disocclusion recovery and
  the four accepted moving poses. Mature 2-by-2 scheduling remains unchanged
  unless a separate candidate proves better.
- Keep default depth three unless a new complete audit reverses the prior depth
  two rejection, which lost approximately eight percent of indirect energy.
  Bounce removal is a quality change, not a code optimization.
- Retain the deterministic mirror-distance ray while DLSS Ray Reconstruction
  uses specular hit distance. Remove or amortize it only after an independently
  valid specular-motion-vector path passes the same RR edge/smear tests; never
  derive the guide from stochastic radiance or old history.

### 10. Restructure only the measured hot path

Q2RTX's split dispatches are evidence for coherent work scheduling, not a
requirement to copy its Vulkan/GLSL structure. The native monolithic primary
dispatch should be split only if timestamps or shader profiling show that
divergent continuation/burst work dominates more than the extra G-buffer and
barrier traffic would cost.

- A/B a project-owned primary/guide pass, primary direct pass, and continuation
  pass against the current combined `RayGeneration`. Preserve one primary
  visibility result and the exact current-surface guide values; do not retrace
  the camera ray in later passes.
- If mature sparse continuation is hot, launch its scheduled pixels as a dense
  phase-sized dispatch with an explicit pixel mapping, analogous only in
  behavior to Q2RTX's dense half-resolution launch. If irregular disocclusion
  bursts are hot, evaluate a bounded GPU work list or separate burst layers.
  Define capacity and fail validation on overflow; do not silently drop paths.
- Specialize ray payloads, instance masks, and trace flags for shadow and
  deterministic-guide rays only when the profile attributes time to them.
  Preserve alpha-test, additive, weapon, billboard, vector, projectile, and
  geometric-normal behavior.
- Audit UAV barriers, transient resource lifetimes, descriptor rebinding, and
  surface formats with PIX/D3D12 validation. Remove only barriers proven
  redundant by the resource-state contract. Compress a target only after its
  guide/radiance tolerance test passes; do not trade signed range or history
  semantics for bandwidth by inspection alone.

### 11. Close scene, reconstruction, post, and frame-pacing costs

- Time full scene rebuild, per-frame vertex upload, dynamic BLAS refit, TLAS
  update, and history promotion separately. Static geometry/material/emitter
  identity must not rebuild in steady state. The known first appearance of a
  new material kind remains a cold event reported separately; the warmed second
  Shotgun burst must keep zero scene rebuilds.
- Report the proprietary DLSS Ray Reconstruction evaluation as its own stage.
  Optimize the resources and work on either side of it, but do not claim a
  native shader win by changing RR mode or input pixel count. Any SDK-mode
  comparison reports both input and output extents.
- Profile bloom, histogram/tone mapping, resource transitions, and the final
  presentation draw at the shipping mode. Retain the accepted Q2RTX-derived
  tone curve and bloom image. A reduced post chain is a preset candidate only
  when SSIM/PSNR plus the locked display captures pass.
- Preserve the frame-latency waitable-object path and report input-to-present
  latency beside throughput. Do not obtain a higher FPS result merely by
  increasing queued frames. Any change to frames in flight requires an explicit
  latency/throughput comparison and separate user acceptance.

### 12. Add optional Q2RTX-style dynamic resolution only after fixed parity

Q2RTX's DRS is optional and disabled by default. A corresponding native
controller is a final frame-pacing feature, not a substitute for the fixed-
resolution work above.

- Drive it from the completed GPU-frame timestamp, ignore invalid/non-world/
  reset samples, and use a short outlier-resistant history. Expose target FPS
  and bounded minimum/maximum scale only as deliberate rendering controls;
  retain off as the default unless the user later requests otherwise.
- Quantize/reconfigure in a way that does not allocate resources, flush the
  queue, rebuild the scene, or reset RR history every frame. Measure the cost of
  every actual scale transition and fail clearly if Streamline cannot support a
  requested extent.
- Report achieved scale beside every frame-time percentile. DRS acceptance
  requires stable pacing, no oscillation, and the same edge/history visual
  checks at the minimum accepted scale.

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
- Native indirect temporal configuration accepts only the one-frame value, and
  the obsolete temporal-frame environment variable cannot enable history.
- The 256-sample blue-noise/Sobol table remains a complete permutation at a
  fixed pixel and dimension. Four spp advance from the presentation's global
  sample index while each presentation still covers all four radial/azimuthal
  strata.

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
- a completely unlit surface that must remain exact black;
- a static diffuse receiver whose four-path estimates alternately hit and miss
  a sparse emitter, exposing raw-estimator hit rate, energy, and variance; and
- a moving foreground primitive, a disocclusion, and an emitter toggle, proving
  RR guides and current-frame radiance remain finite without a native trail.

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
direct-specular, and indirect-specular selections. Compare the four-spp RR-only
result with the 32-spp RR-off mean-energy oracle and matched Q2RTX
diffuse-receiver regions. Report `DiffuseGiScale` in every capture.

Acceptance requires:

- material-dependent highlights and reflections appear without a grey ambient
  floor;
- metalness removes diffuse response and colors specular response;
- rough materials transition smoothly from real to reconstructed specular;
- directly unlit black remains black;
- the accepted fresh-estimator candidate approaches the high-sample
  coverage/variance without changing its long-run mean; any remaining scale
  mismatch is resolved by the separate `0.75` versus `1.0` transport audit;
- a frozen camera does not progressively fill with persistent bright dots, and
  motion/disocclusion/weapon transitions do not create bright or dark trails;
- no new scene rebuilds or history resets occur during moving weapon,
  door/lift/water, billboard, vector, or projectile updates;
- RR guides remain stable during camera motion and do not create specular
  smearing at geometry edges;
- all Level A-P smoke, lifecycle, desktop-settings, material, and reconstruction
  tests pass;
- exposure, tone curve, bloom, SDR/HDR output, and the accepted indirect-GI
  diagnostics retain their pre-change results.

### Performance validation

- The profiler's complete-frame GPU interval reconciles with its non-overlapping
  top-level stage intervals, and a disabled profiler changes neither output nor
  frame time outside measured run-to-run variation.
- Any fresh-estimator candidate reports its actual diffuse paths, shadow rays,
  and proposal work per internal pixel and keeps total-frame median/p95 inside
  the five-percent gate after control dispersion.
- Performance reports contain the exact hardware/software manifest, settings,
  presentation/tracing/reconstruction extents, ray counts, warm-up/sample
  counts, median/p95/p99/maximum, and scene rebuild/refit counts. Reject any
  report that compares preset names without matching actual pixel counts and
  work.
- Run five alternating trials of the static matched-work Level A comparator.
  Performance parity is reached only when AB3D2's median and p95 GPU frame time
  are not slower than Q2RTX outside the dispersion measured by repeated control
  runs. If the confidence bands do not separate the results, record parity
  rather than a percentage win.
- Run the same five-trial protocol at shipping defaults. The accepted native
  default must enter the Q2RTX steady-state envelope while retaining all
  lighting gates. A matched-work win alone does not close the user-visible
  performance task.
- The native locked corridor, yawing Shotgun, disocclusion burst, warmed second
  Shotgun firing burst, and all-level smoke must have no new p95/p99 hitch,
  scene rebuild, history reset, non-finite guide/radiance value, or visible
  blur/trail. Cold atlas population is reported separately and is not averaged
  into steady state.
- Re-run shader warnings-as-errors, Release renderer build, Debug `29/29`
  CTest, native/Streamline GPU reference scenes, radiance-channel sum, locked
  captures, all-level smoke, and the source/package provenance audit for the
  final accepted performance configuration.

## Explicit non-goals for this handoff

- No authored-zone ambient or legacy Gouraud/ZoneT lighting in DXR.
- No physical sky or sun without scene data.
- No environment gradient or missed-ray fill.
- No direct screen-space temporal/spatial ReSTIR activation.
- No renderer-owned temporal radiance history in the production one-frame
  setting. The explicit `2..64` diagnostic may use only same-primitive
  reprojection: no outward history search, current-frame spatial radiance reuse,
  neighborhood clipping, regional pass, deflicker, or wavelet stage.
- No recursive reflections, water/glass refraction, caustics, or volumetrics in
  the indoor-core implementation.
- No new exposure, HDR, radiance-clamp, paper-white, or shadow controls.
- No reinterpretation of additive/glare effects as area, sphere, or point
  lights.
- No performance claim based only on all-level wall time, CPU
  `renderer_present` time, average FPS, a lower unreported render extent, or a
  different RR/upscale mode.
- No emitter pruning, distance/zone/PVS light cap, reduced material/geometry
  coverage, hidden radiance clamp, bounce-energy compensation, or reactivated
  screen-space reservoir presented as an optimization.
- No dynamic resolution or extra frames in flight used to conceal a slower
  fixed-resolution renderer.

## Handoff completion definition

The indoor-core work is complete only when direct GGX, real smooth specular,
rough reconstructed specular, and active RR specular guides are all present;
the diffuse mean-energy audit selects a justified transport scale; and an
accepted low-cost fresh estimator reaches the high-sample diffuse coverage
without a mean shift or persistent-dot failure. Isolated-stage captures must
sum to the combined result and the CPU, GPU-reference, and all-level validation
above must pass. Performance parity is complete only when the non-blocking
profiler and reproducible Q2RTX/native benchmark report exist, matched-work and
shipping-default median/p95 GPU results enter Q2RTX's measured envelope, the
moving native p95/p99 path has no new hitch, and every lighting oracle still
passes. A biased variance workaround, a partial direct highlight, or a faster
low-resolution preset without those paired results is not completion.
