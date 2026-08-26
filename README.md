# Alien Breed 3D II: The Killing Grounds — PC port

This is the native C port of the maintained Amiga source under
`amiga/ab3d2_source/`. It uses the same CMake/SDL2 desktop setup as the Alien
Breed 3D I PC port and targets Windows, Linux, and macOS.

The PC runtime stages the unmodified bytes from `amiga/media` beside the
executable as a lower-case `data/` tree. The Amiga volume path `AB3:Includes/test.lnk`, used by
`Game_Start` in `amiga/ab3d2_source/controlloop.s`, therefore becomes
`data/includes/test.lnk`.

## Desktop configuration

The first native build seeds an editable `ab3d2.ini` beside the executable
from `ab3d2.ini.template`; incremental builds preserve the edited file. The
direct-play executable reads it before creating its selected single-player
session. Supported keys are:

- `start_level=1` through `start_level=16` for Levels A--P (one-indexed);
- `infinite_health=0|1` restores the source inventory health limit after each
  completed source update;
- `infinite_ammo=0|1` retains the source fire/cooldown path but skips its
  empty-ammo check and ammunition debit;
- `all_weapons=0|1` grants every source gun and a full legal supply of each
  ammunition class when the session begins;
- `all_keys=0|1` treats every authored key in the level as collected, releasing
  its source door/lift locks. It defaults to `0`;
- `quicksave_load=0|1` enables first-port-compatible F5 quicksave and F9
  quickload through `savegame.bin` beside the executable. It defaults to `0`;
- `load_autosave=0|1` restores that `savegame.bin` at startup and enters
  gameplay without the initial level flavour text. It defaults to `0`, and an
  enabled load fails explicitly if the save is absent or invalid;
- `volume=0` through `volume=100` controls the master SDL mixer gain;
- `always_run=0|1` makes run the default controller mode. Hold Shift to walk;
  and
- `world_light_tessellation=1|2|4|8` controls presentation-only world-light
  subdivision. The default is `4`; `1` retains the strict source mesh; and
- `renderer=opengl|rtx` selects the desktop graphics backend. It defaults to
  `opengl`. A normal build retains the clean-room RTX fail-fast stub. A native
  Windows build configured with `AB3D2_ENABLE_DXR=ON` ray traces the
  `SceneFrame` world, bitmap billboards/effects, projectiles, animated world
  vector models, and companion weapon into a fresh, visibly noisy HDR image.
  It samples the renderer-native base-color, normal, roughness, metalness, and explicit
  emissive channels with a multi-bounce Lambertian/GGX path tracer, authored
  area emitters, environment lighting, shadow rays, MIS, and a pinned
  dimension-addressed blue-noise/Owen-scrambled Sobol sequence. Player 1's
  companion weapon is source-scale camera-relative PBR geometry in the same
  depth-ordered TLAS. It reflects, shadows, and is occluded by the world, and
  supplies HDR radiance plus every guide before Ray Reconstruction. Bitmap
  items/enemies, additive/glare effects, and 3D items/enemies likewise use
  their preconverted PBR maps inside the shared TLAS before reconstruction.
  Projectiles and particles are traced with them. An additive source effect -
  a glare, an additive bitmap, or a `predoglare` vector face - is light that
  never occludes: a ray passes through it collecting its emission, keeps its
  direction and throughput, spends no bounce, and takes its reconstruction
  guides from the surface behind. Visibility rays do not see it at all, so it
  casts no shadow. That matches the source's blended draw paths and the OpenGL
  backend's `glBlendFunc(GL_ONE, GL_ONE)` with depth writes disabled.
  World billboards occupy a fixed run of reserved instances rather than
  entering and leaving the scene, because a sprite joining the instance list is
  a layout change and therefore a full rebuild behind a GPU flush - around
  95 ms at 2560x1440. `object_scene_submit_active` publishes the live prefix of
  the ObjT array and skips vacated records, so without the pool a bullet
  appearing, an alien dying or an item being collected each cost one: walking a
  level with the trigger held measured 17 rebuilds over 400 frames before the
  pool and 1 after. What remains is a material kind entering the atlas for the
  first time, once per kind per level. HUD and text remain outside the DXR path.
  The Web build always uses OpenGL/WebGL.

The ray-traced backend takes its own presentation-only quality settings from the
same file. Every one is optional, and an absent key keeps the renderer's tuned
default, so the shipped template lists them commented out with their defaults:

- `rtx_samples_per_pixel=1` through `8` and `rtx_max_bounces=1` through `8`
  remain accepted configuration keys, but the current primary-visibility reset
  deliberately ignores them. It traces one camera ray and no lighting or
  continuation rays;
- `rtx_ray_reconstruction=quality|balanced|performance|ultra-performance|off`
  selects the DLSS Ray Reconstruction mode, which also sets the resolution the
  path tracer renders at before reconstruction upscales it. That makes it the
  largest single performance lever: at 2560x1440 the three fastest measured
  12.7, 10.4 and 8.5 ms a frame. The default is `quality`;
- `rtx_light_candidates=1` through `1024` and `rtx_reservoir_limit=0` through
  `65536` remain accepted for the retained estimator implementation, but no
  light-grid or reservoir shading dispatch runs in the current flat reset; and
- `rtx_radiance_clamp=200` and `rtx_ndf_trim=0.9` remain accepted but are
  inactive during the flat primary-visibility reset. `rtx_exposure=1` remains
  active and scales the flat HDR result before tone mapping.

`AB3D2_DXR_SPP`, `AB3D2_DXR_CANDIDATES`, `AB3D2_DXR_RESERVOIR_LIMIT`,
`AB3D2_DXR_RADIANCE_CLAMP`, `AB3D2_DXR_EXPOSURE`, `AB3D2_DXR_NDF_TRIM`, and
`AB3D2_DXR_RR_MODE` still override the file for one run, which is how a setting
gets swept without editing it. The hidden `--gpu-smoke` path deliberately reads
no `ab3d2.ini` at all, so its measurements stay independent of the host's
configuration.

`run_default` is accepted as an alias for `always_run`, matching the first
port. Boolean keys also accept `true`/`false`, `yes`/`no`, and `on`/`off`.
An explicit `--level A` through `--level P` command-line option overrides
`start_level`. `--world-light-tessellation 1|2|4|8` likewise provides a
one-run override for renderer validation. `--renderer opengl|rtx` overrides
the configured backend for one native run. `--skip-intro 1` enters the selected
level directly without requiring an autosave; it changes only the initial
flavour-text presentation.

Weapon selection retains the source controls: number keys `1`--`0` directly
select their owned weapon, while Backslash and the right mouse button advance
to the next owned weapon.

When `quicksave_load=1`, F5 captures the complete live level and F9 reloads
its source assets before restoring that state, including saves from another
campaign level.

When `load_autosave=1`, the saved campaign level and runtime state take
precedence over `start_level` and `--level`. Like the first port's Continue
route, only the initial story is skipped; later successful level transitions
still display their authored flavour text.

## Current native slice

The initial desktop target establishes the port boundary without reusing the
first game's software renderer:

The active direct-play path now runs the source single-player control,
weapon, object/mechanism (including source-held door/lift locks), worry, and
`ItsAnAlien` update ordering. Every
complete AI route enters through `newanims.s:ObjectHandler` only when its
source worry byte is set. Source death, successful collectable, and
destructible narratives enter the GPU-neutral small-screen message ring and
are submitted through retained renderer-neutral text commands. The visible
lines are independently centred below the first port's resolution-relative
safe top margin, retain the source
message-tag colours, and age through `c/message.c:Msg_Tick`'s exact one-line,
2000 ms null insertion until they are replaced or disappear. Failed inventory pickups retain their
source `Timer2` and EClock-deduplicated “cannot carry” notification. The SDL
active presentation path is selected behind the API-neutral `renderer.h`
boundary. OpenGL 2.1 / GLES 2 is the implemented native and Web backend. It
draws the complete loaded level without software
rasterization, PVS, portals, or zone ordering. It decodes the maintained
5-bit packed wall WAD strips, `floortile` logical tiles, `256pal`, object
WAD/PTR frame data, vector models, `rawbackpacked`, and `waterfile` into GPU
resources. OpenGL forward-renders source-textured geometry with smooth
source-driven light gradients and renders sky, animated water,
bitmap/glare effects, vector objects,
and Player 1's live companion weapon. OpenGL world and vector materials are
converted once to true colour with per-source-texel continuous linear-light
responses, so source brightness retains its authored hue shift without runtime
palette row selection. Before `Game_Begin`, the renderer-neutral resource
catalog hands every vector asset already loaded by `controlloop.s:Game_Start`
to the active backend. OpenGL walks `draw_PolygonModel`/`doapoly`'s immutable part and
face records, prepares the shared 256-entry light response once, uploads all
unique vector materials, reserves the largest authored face-conversion scratch
buffer, and completes deferred driver work before gameplay.
Vector drawing treats any later cache miss as an error instead of decoding or
allocating a model texture or per-face heap buffer inside a presentation frame.
Bitmap entities retain `objdrawhires.s:draw_Bitmap`'s exact
`draw_ObjScaleCols_vw` two-inputs-per-palette-row mapping. Its four
directionally lighted bitmap classes also preserve `draw_bitmap_lighted`'s
wrapped byte curve and non-positive-only `BrightToAdd+willybright` adjustment,
so items and bitmap enemies no longer select artificially dark palette rows in
bright spaces.
OpenGL is original source art with a continuous lighting presentation. Menus
and multiplayer are not included. The
detailed inventory below records the source-backed foundations; older
references to an unbound AI dispatcher are superseded by this live
integration.

`RenderView` owns the host-rate camera yaw. Horizontal mouse input updates it
immediately and samples that exact heading into Player 1 before the next 50 Hz
movement/fire update; source keyboard and inertial turns are committed back to
the same view after every completed tick. The camera and player therefore share
one direction while gameplay remains fixed-step. Camera pitch remains
presentation state derived from the source aim fields.
Mouse Y uses the source `PlrT_AimSpeed_l`/`STOPOFFSET` scale, so its initial
sight line agrees with source projectile aim while preserving the small-screen
`View_LookMin/Max` bounds and invert-mouse preference. Player movement,
collision, weapons, and action frames remain at the original 50 Hz; the
camera-space `Plr1_Use` companion follows the variable-rate camera while its
source-authored pose is interpolated. The renderer blends completed source-frame scene snapshots using the
50 Hz VBlank remainder. This uses SDL's high-resolution performance counter,
so the source companion weapon/action sequence advances only at its fixed PAL
cadence while camera, mutable world geometry, sprites, and source light
samples present smoothly at the host frame rate. The renderer-neutral
`SceneLighting` command also owns and blends both completed copies of
`CurrentPointBrights_vl` and `Zone_BrightTable_vl`, so backends cannot observe
the live 50 Hz tables stepping or overwriting the previous endpoint. World
surfaces separately retain the pre-object `allinzone` ambient field and the
next authored `newanims.s:brightanim` endpoint. That room lighting is blended
across the complete five-tick (100 ms) source interval instead of changing in
the first 20 ms and holding for the remaining four ticks. Flash, torch, and
projectile contributions remain independent adjacent-frame interpolants, and
none of this presentation state feeds player room brightness or AI. World
vector enemies retain
`ai_DoWalkAnim`/`ai_DoAttackAnim`'s discrete 50 Hz current
frame while renderer-neutral per-ObjT pose history blends compatible compiled-
model point tables across `DOALLANIMS`' complete authored five-tick (100 ms)
interval. Position, angle, and light retain ordinary completed-frame
interpolation; AI, movement, action timing, and ObjT state are not advanced at
presentation rate. The first-person gun
presentation deliberately holds each authored companion pose
for four 50 Hz ticks (4x its former duration); firing, ammunition, cooldowns,
projectiles, and all other gameplay still update on every source tick. The
source Assault Rifle is exempt because its two-tick automatic-fire restart
requires its companion action pose to advance on every tick. Native
direct play copies the first port's `display_init` desktop presentation: it queries the active desktop
bounds and creates the same normal shown/resizable OpenGL window there, without
any fullscreen or borderless SDL flag and without a monitor-mode change, then
renders directly to the complete SDL drawable. Its main
Windows build also carries the first port's `VS_DPI_AWARE OFF` manifest setting
so the normal window's non-client geometry uses the same desktop coordinate
space. The desktop presenter measures that normal frame, shifts it off the
top/left display edge, and expands the client area by the same inset so its
existing lower/right overscan still covers the taskbar—without changing window
style or monitor mode.
loop relies on the requested OpenGL swap interval rather than a fixed 16 ms
sleep, so a supported 120 Hz-or-higher display can present the interpolated
scene at its native refresh rate. The hidden GPU smoke test retains its
bounded 1280x720 validation window.

The headless regression starts a clean source-process runtime, selects each
authored Level A--P session, and runs six source VBlank-equivalent direct-play
ticks. It models the process-lifetime `DOALLANIMS` cadence rather than
resetting it at a level boundary, so every session crosses a real animation
pass. Its Level O case proves that the native workspace follows the source
terminator-delimited ObjT list beyond its fixed BSS prefix; Level G exercises
the source AI's direct control-point address after an unavailable navigation
route; and Level K exercises its ignored 68000 `DIVS.W` overflow result.

The shared-resource loader decodes the original `CSFX` Fibonacci sample
payloads exactly as the source file loader does. The desktop boundary exports
those decoded samples and the source-packed `music/packedtest` module as
committed WAVs, stages them beside the executable, and plays source-owned
events with listener-relative MakeSomeNoise-style priority, attenuation, and
stereo panning. `modules/player.s:plr_Fall` also supplies its original
floor-material footsteps through that path, including the source 4,096-unit
walk cadence and water-step special case. The eight host mixer slots preserve
the source's source-ID behavior: independent effects overlap, while a
`notifplaying` call either restarts or suppresses its own active source as the
original requests. The source default music toggle starts the looped module at
`Game_Begin`.

- loads the authoritative `test.lnk` game database and `TEXT_FILE` narrative;
- unpacks (including stored and LHA-compressed `=SB=` records), parses, and validates the map, fly map, `twolev.bin`,
  `twolev.graph.bin`, and clip stream for every campaign level (`A`–`P`). The
  `TLBT` and `TLGT` headers follow `amiga/ab3d2_source/defs.i` exactly; both
  source 100x100 navigation maps plus the static door/lift/switch records are
  also decoded, along with each static object's documented type/animation
  selector bytes, without yet running AI or mechanism animation. Every source
  wall plus floor, ceiling, and water boundary is submitted through
  GPU-neutral mesh instances, without PVS or portal traversal. The immutable
  world mesh is one static BLAS candidate; each source-controlled door, lift,
  and water controller is a separate dynamic candidate with a stable
  TLAS-style instance identity. Every live ObjT/ShotT bitmap, vector, and
  glare object likewise submits as one dynamic instance keyed by its selected
  source resource and source slot. OpenGL consumes the same instances with
  material subranges, while a future DXR backend can cache static BLASes and
  rebuild only dynamic BLASes without a producer-interface change. The
  retained scene refreshes its commands from the mutable graph after source
  door, lift, and water updates. Doors and lifts are a deliberate native-renderer
  simplification: every exact source `ZDoorWall.graphics_offset` keeps its own
  live `Draw_Wall` material, lighting, V-origin, texture window, source-8.8
  V density, and moving edge written by `DoorRoutine`/`LiftRoutine`; only a renderer-added
  door same-`EdgeT` counterpart uses its controller association. Each door's
  direct `Draw_Flats +2` movement plane is in that same controller mesh as its
  direct wall faces. During an intermediate source motion step it presents
  that plane at `DoorRoutine`'s `ASR.W #2`/`MULS #256` wall-boundary height,
  avoiding the sub-texel source-record difference becoming a 3D seam.
  `LiftRoutine`'s
  direct wall faces retain their live `+20` boundary while their source `+24`
  boundary remains fixed, alongside a plane snapped through the same source
  quantisation. This keeps
  adjacent shaft walls static while preserving each authored panel's texture motion without
  per-face height normalization or stretching. Where the complete graph has
  an unlisted, co-oriented shaft wall overlapping a direct lift face at a
  shared edge, the direct `LiftRoutine` face owns that edge and the shaft
  segment is clipped there; opposite-facing records remain the authored
  materials for the other side of the wall;
- defines a GPU-neutral frame command interface for cameras, lighting,
  environment, static/dynamic mesh instances, and source-object instances.
  It also carries retained bitmap-text commands with explicit font and layout
  identities, so UI production has no dependency on SDL, OpenGL, or a future
  renderer API. The current status producer submits health and selected-weapon
  ammunition only; key indicators are deliberately deferred. OpenGL/WebGL
  consumes the same renderer-independent glyph layout using the exact
  `health_digits.png`, `ammo_digits.png`, and printable-ASCII atlas from the
  Alien Breed 3D I port. Health is rounded to the same 0--100 percentage,
  ammunition is clamped to the same three-digit 999 limit, leading zeroes are
  suppressed identically, and the four-key row remains reserved in the
  bottom-right layout without being drawn. In-game flavour text uses that
  renderer-neutral atlas path at the top centre, below the first port's
  resolution-relative safe margin, while preserving AB3D2's
  five-slot message ring, four source tag pens, and 2000 ms expiry cadence.
  Level transitions use the same abstract text path to display the selected
  level's exact 16-line `TEXT_FILE` record over black. The group-fit layout,
  margins, crisp integer upscaling, eight-step fade, and 750 ms input guard
  match the first port; successful exits advance to the next authored level
  while preserving the source campaign inventory.
  Every live source object now emits an
  unprojected bitmap/vector/glare descriptor in source slot order, with its
  selected raw WAD/PTR/vector asset bytes, palette, frame data, source draw
  controls, role (world or Player 1's `ENT_NEXT_2` weapon), and live light.
  Lighting commands retain the source current-point and zone tables;
  environment commands retain backdrop, water frame, scroll, and palette data.
  Mesh surfaces retain the source asset
  class and select shared versus per-level floor/wall overrides exactly as
  `modules/res.s:Res_LoadLevelData` does, carrying the selected source bytes
  and source palette bytes for later backend-owned conversion/upload. Wall
  palettes use the exact 2,048-byte `Draw_Wall` prefix; floor/ceiling/water
  commands retain the shared source texture palette and the common
  `draw_Palette_vw` RGB palette. Wall commands carry their authored packed
  WAD window and source texel coordinates; floor, ceiling, and water commands
  carry the demonstrated signed `pastsides` scale mapping for the source's
  64x64 logical tile;
- exposes the 30 source `ODefT` object definitions, both 20-frame six-byte
  object animation tables, and the 32 eight-byte bitmap metrics per object as
  endian-safe read views. Object commands expose the source-selected mode and
  frame; live AI and moving-projectile paths consume those source-selected
  values without invented animation;
- exposes all 20 `AlienT` records used by `ItsAnAlien`, validates every
  loaded alien-slot type against that catalog, and supplies the exact setup
  input for the live worried-alien update;
  `Game_Begin`'s exact per-level alien/team workspace and damage initialization
  is owned separately, including the source's intentionally preserved trailing
  workspace words and boredom storage. The single-player `SETPLAYERS` alien
  gate is also preserved; it does not activate an AI routine;
- exposes all 20 `BulT` records, including their source animation/pop payload
  views and fixed six-byte frame records. The `Game_Begin` player/alien
  projectile pools and both player entities resolve to checked slots in the
  owned `ObjT` runtime array. Player 1's source `ENT_NEXT_2` companion weapon
  entity is also republished each update from `GLFT_GunObjects_l`, so firing
  updates its real object state. The bounded stationary impact state created by
  `plr1_HitscanSucceded` advances its original `ItsABullet` pop frames,
  source point-brightness call, and source-slot release. Live `firefive`
  projectiles now retain the source lifetime, fixed-point movement,
  floor/roof/wall response, direct target collision, and post-`MoveObject`
  point-brightness call; their source `ComputeBlast` damage, impulse, and
  flame-allocation paths are live, with projectile-impact sound requests
  emitted from the same source branches;
- preserves the source `DEFGAME`/save-slot campaign record (a 70-byte,
  big-endian level and inventory payload). The native Load Position and Save
  Position menus use the original six-record, 420-byte `boot.dat` layout at a
  mutable path beside the executable (or `--save-path`), preserving every
  unedited raw record. The default starting ammunition class is read through
  the checked four-word `ShootT` record used by `newplayershoot.s`, rather
  than an ad-hoc GLFT byte offset;
- starts the source default single-player session directly in Level A (or a
  selected `--level A` through `--level P`) while menu work is deferred.
  Master/slave multiplayer is explicitly unavailable. Native SDL events feed
  the source-shaped raw-key map, player movement/falling/static collision,
  collectables, doors, lifts, water updates, source PVST gameplay activation,
  and `hires.s:DOALLANIMS`' source-timed alien frame/action workspace update;
  the source AI's isolated player-memory, sight, facing, grounded-route,
  room-state, vertical-flight, and per-alien setup helpers, including the GLFT
  reaction timer; the source single-player weapon cooldown/ammunition/
  hitscan/projectile-launch/flight path, including source frame-local player
  noise for empty and successful triggers; the
  source directional point-brightness helper, alien torch caller, and
  `Anim_ExplodeIntoBits` fragment allocation are present for later behavior
  modes, projectile, and blast callers. `ai_JustDied`'s exact smaller-alien
  allocation branch, `STATS_KILL` counter/signal update, and alien-damage
  reactions are likewise prepared through its direct death helper. That helper
  emits its source `Msg_PushLine` narrative request for the later GPU-neutral
  message consumer. `modules/ai.s:ai_PauseBriefly` now composes the exact
  damage/death, walk-animation, source-frame-timer, torch, sight, front, and
  darkness branches; the live dispatcher invokes it when selected in source
  order. `modules/ai.s:ai_ProwlRandom`,
  `ai_ProwlRandomFlying`, and their shared `ai_ProwlFly` body now compose
  player-noise/team-memory routing, the caller-owned collision words, authored
  control-point movement, room/flight state, and the final sight/reaction
  branches. The live dispatcher owns its source-order narrative handoff.
  `modules/ai.s:ai_AttackCommon` now
  derives the exact source-width `SHOTTYPE`, `SHOTPOWER`, `SHOTSPEED`, and
  `SHOTSHIFT` state plus its hitscan/projectile branch from each `AlienT` and
  `BulT`; its attack modes execute through that dispatcher.
  `newaliencontrol.s:SHOOTPLAYER1`
  now preserves the hitscan-miss ray's source `GetRand` spread, repeated
  zero-extension `MoveObject` trace, and stationary player-shot impact-pool
  writes. `modules/ai.s:ai_AttackWithHitScan` now preserves its prior-frame
  `ObjRotated` chance test, Player 1 byte damage/`ai_CalcSqrt` impact impulse,
  miss handoff, animation, sight, torch, and mode transitions.
  `objectmove.s:CheckTeleport` now preserves its destination
  floor-relative collision probe, temporary teleport X/Z, and source-zone
  ownership handoff for charge and approach modes. `newaliencontrol.s:RunAround`
  now preserves its exact signed-word side-target adjustment for their side routes.
  `modules/ai.s:ai_DoWalkAnim` now also retains the raw post-call `a2`
  collision words, whose base changes when an auxiliary frame is active.
  `modules/ai.s:ai_Charge` and `ai_ChargeToSide` now compose their exact
  ground damage/death, attack animation, pre-dispatch teleport probe,
  response-speed heading, side target, two collision probes, movement,
  auxiliary-slot, melee, room-state, torch, sight, and final mode branches.
  `ai_ChargeFlying` and `ai_ChargeToSideFlying` now preserve their distinct
  airborne route: its 1000-unit descent limit, byte-only melee damage,
  vertical-state restore around room stats, and `ai_FlyToPlayerHeight` final
  attack branch. All charge modes run through the source dispatcher in
  `ObjectHandler` order. `ai_Approach`, `ai_ApproachToSide`,
  `ai_ApproachFlying`, and `ai_ApproachToSideFlying` now preserve their
  action-gated follow-up speed, source collision/movement path, ground
  reachability-before-sight order, mode-two timer/darkness gate, and the
  airborne fly-before-room-stat vertical restore. The live
  `modules/ai.s:AI_MainRoutine` default/response/follow-up dispatcher calls
  each source mode with explicit animation, lighting, map, observation,
  progression, random, and shared-workspace inputs; `ObjectHandler` owns the
  complete tick context and death-message handoff.
  `objectmove.s:CalcDist` and
  `HeadTowards` now preserve their two-step coarse distance, range backtrack,
  and speed proposal for that projectile firing helper. `newaliencontrol.s:FireAtPlayer1`
  now allocates and initializes its source alien-projectile state, including
  predictive lead and lateral launch offset. Vector-enemy shots also retain a
  renderer-neutral link to the authored firing frame: when the raw `SHOTYOFF`
  lies beyond that quarter-scale model's top, presentation translates the
  visible path to the model boundary while leaving the source `ShotT` origin,
  velocity, collision, and damage unchanged. This closes the Level P Mantis's
  authored `-300` launch-height gap without changing its 50 Hz rocket path;
  the link is preserved by quicksave format version 2. Its source animation-frame audio
  now uses the shared event path. `modules/ai.s:ai_AttackWithProjectile` now composes that projectile
  handoff with the source damage/death exit, attack animation, heading, memory,
  torch, and finished/sight transitions. `ai_DoTakeDamage`
  likewise completes the selected nonfatal reaction animation and heading
  branch with explicit source torch inputs. Enemy behavior, dynamic blast, and
  source-backed desktop audio are live;
- opens an SDL OpenGL window that draws the direct-play world with depth
  testing: source sky, complete authored geometry, smooth source light,
  animated water, source bitmap/glare/vector objects, the live view weapon,
  source messages, and the first-port health/ammunition bitmap UI.

Single-player is the only intended PC mode. The original serial master/slave
multiplayer flow is intentionally not ported.

## Build

Requirements: CMake 3.16+, a C11/C++ compiler, Python 3 with Pillow, Git, and
network access the first time CMake fetches SDL2.

```sh
cmake -S . -B build/pc
cmake --build build/pc --config Debug
ctest --test-dir build/pc --output-on-failure
# Opt-in real OpenGL context validation (hidden window, Levels A-P)
cmake --build build/pc --config Debug --target ab3d2_gpu_smoke
```

### Windows D3D12/DXR renderer and Ray Reconstruction

Set `renderer=rtx` in `ab3d2.ini`, or launch once with `--renderer rtx`.
OpenGL remains the default and an explicit RTX request never silently
substitutes it. The default, non-Windows, and Web configurations retain the
clear fail-fast stub. On native Windows, opt into the experimental renderer
with:

```powershell
cmake -S . -B build/dxr -A x64 `
  -DAB3D2_ENABLE_DXR=ON `
  -DAB3D2_DXC_EXECUTABLE="<path-to-dxc.exe>"
cmake --build build/dxr --config Debug
ctest --test-dir build/dxr -C Debug -R "dxr|rtx" --output-on-failure
```

For Visual Studio generators, the DXR-enabled solution selects `ab3d2` as its
startup project, launches it with `--renderer rtx`, and uses the executable
directory as its working directory. It also passes `--skip-intro 1`, so F5
enters the selected level directly. A solution generated with
`AB3D2_ENABLE_DXR=OFF` cannot enable DXR at runtime; its error reports the
missing build option explicitly.

`dxc.exe` is discovered from `PATH` when no explicit override is supplied.
The configure log records its version and SHA-256, and the project compiles
its HLSL as Shader Model 6.6 with warnings treated as errors. Debug
builds require the Windows Graphics Tools optional feature for the D3D12 debug
layer. `AB3D2_DXR_GPU_VALIDATION=ON` additionally enables the much slower
GPU-based validation mode.

The DXR backend creates a native SDL/`HWND` window without OpenGL,
selects a high-performance hardware adapter with feature level 12_0,
`ID3D12Device5`, and a nonzero DXR tier. It compiles opaque world surfaces and
Player 1's exact `ENT_NEXT_2` companion from the renderer-neutral `SceneFrame`,
uploads positions, UVs, material/primitive indices, and renderer-native
base-color, tangent-normal, metalness, roughness, emissive, and dielectric
specular material data, then
builds static/dynamic BLAS objects and one TLAS. The companion compiler applies
the source `rotate_object` pose, on/off state, face culling, and documented
one-quarter-level-unit model scale, then attaches those camera-local vertices
to the DXR camera's right/up/forward basis in an alpha-tested dynamic BLAS.
DXR keeps source part/face slots in file order because ray depth replaces the
source painter sort. Culled or disabled faces become exact zero-area triangles.
This fixed layout is important for the Shotgun: its authored firing poses vary
from 45 to 30 visible triangles. They now update only the camera-local vertex
buffer and refit its dynamic BLAS instead of rebuilding the scene/material
atlases, draining the GPU queue, and resetting Ray Reconstruction history.
Weapon and world instances use the same TLAS mask and nearest-hit query, so all
primary, secondary, and visibility rays see both. The PBR weapon can therefore
reflect, shadow, be shadowed by, and be occluded by the world while contributing
HDR radiance, depth, normals, motion, and every reconstruction guide.
`technolights` and the source
`floor_0101` panel use colored emissive masks at factor 200; other materials
remain non-emissive. Companion weapon vertices do not consume the source
`doapoly` directional flat/Gouraud response: non-emissive weapon illumination
comes only from traced incident radiance, while authored PBR emissive maps stay
at their material intensity. World vertices carry the source Gouraud shade
response for their surface, and that scales authored emission, so an emissive
panel in a zone whose `CurrentPointBrights_vl` words hold an
`Anim_BrightTable_vw` index pulses with `newanims.s:brightanim` - which is what
animates the `floor_0101` light panel the player starts beside in Level A. A
brightness-only frame rewrites the vertex and emitter buffers without refitting
any acceleration structure, so it never resets the temporal history.

The complete editable texture handoff is
`assets/renderer_dxr/materials/`: one zip-ready root with `walls`, `floors`,
`weapons`, `vector_models`, `enemies`, `billboards`, `effects`, `environment`,
and `ui` directories. They contain 978 material identities and five PNG maps
per material (4,890 PNGs total), alongside root-level `materials.json` and the
artist README. Each category is flat. Unauthored PBR channels are committed
generated maps for artists to replace. Authored wall-sheet panels are
center-cropped to the pixel aspect of their authoritative AB3D2 wall texture
before all five channels are resized together to four times that source extent.
Floors likewise use 256-by-256 replacements for their 64-by-64 source tiles.
The shared conversion applies the encoded normal-Z floor used by the Q2 package,
and the native shader performs repeat-aware four-tap filtering inside each
packed atlas rectangle. This keeps base-color, tangent-normal, metalness, and
roughness features in the same normalized UV domain as the source wall without
compressing extra sheet canvas into it or bleeding from an adjacent material.
Weapon and vector-model regions retain
their source albedo and use roughness 184/255 (the nearest PNG value to 0.72),
metalness 0, and editable `specular_factor` 0.35, matching the proven material
settings from the user-directed renderer comparison. Other unauthored maps use
the neutral defaults recorded in the manifest. The build validates every PNG and embeds
its exact compressed bytes in one runtime package. DXR reads only the package
catalog at startup and decodes a material's five PNGs when the live scene first
requires that binding. Active bitmap assets admit all frames of the selected
source mode together, so animation does not repack the atlas or reset RR
history. A missing/corrupt map or missing world/entity/weapon binding is fatal;
the runtime does not regenerate fallback textures.

The current renderer reset traces one pixel-centred camera ray per pixel with
zero frame-varying subpixel jitter and writes the first opaque surface's linear
base colour without direct lighting,
emission, environment radiance, shadow rays, continuation rays, or specular
guide rays. Misses are black. Source additive geometry remains non-occluding
but contributes no colour. Light-grid and spatial-reservoir dispatches are
skipped. A full-screen pass tone maps the flat HDR result to the three-frame
flip-discard swap chain; there is no project-authored temporal accumulation or
denoiser. The same primary dispatch writes separate
diffuse/specular albedo, world shading normal, linear roughness, linear depth,
dense scene motion, and specular-hit-distance resources in the formats recorded
by the implementation plan. The inactive specular and diffuse hit-distance
guides are zero.
A renderer-neutral history epoch resets camera and
geometry history across level/quickload discontinuities; topology-stable world
motion uses the previous vertex positions at the current hit barycentrics.
Non-projectile bitmap sprites and animated world vector objects now retain
stable renderer-neutral layouts, resolve their exact preconverted PBR maps on
demand, and update dynamic BLAS objects without repacking the global material
atlas. Alpha-tested bitmap/vector surfaces and emissive additive/glare effects
therefore participate in primary, secondary, shadow, and reflection rays and
write the RR guides before reconstruction. Source flat/Gouraud lighting does
not modulate the DXR entity or weapon materials. Transient projectile sprites
and HUD/text overlays remain outstanding.

Empty/non-world frames retain the diagnostic triangle. Resize,
minimize/restore, fences, DRED reporting, and orderly shutdown remain covered.
DXR shutdown hides the SDL window before its synchronous teardown, flushes the
GPU once while Streamline's proxy is live, and does not signal that proxy queue
again after `slShutdown`.
Use `ab3d2_renderer_rtx_foundation_test` for the hidden multi-thousand-frame
lifecycle check and run the game-content check with:

```powershell
.\build\dxr\Debug\ab3d2.exe --gpu-smoke all --renderer rtx
```

The RTX smoke renders each Level A--P frame twice and requires two nonzero
readback checksums. Without Streamline, the frozen flat-primary frames must
match exactly; any difference exposes unintended temporal movement. It also
requires nonzero GPU primary-hit coverage from the initial
and key-six Rocket Launcher companions, cumulative bitmap entity coverage, and
world-vector coverage. If a real active vector entity is occluded from a
level's initial camera, the test presents that exact command in an
occlusion-free diagnostic view without changing gameplay. An all-black image
now fails the readback check. Set
`AB3D2_DXR_DEBUG_LOG=1` to mirror DXR diagnostics to stderr during a run, and
set `AB3D2_DXR_CAPTURE_PPM` to an absolute `.ppm` path while using hidden GPU
smoke to save the latest presented frame. Weapon, bitmap-entity, and
vector-entity coverage come from a GPU UAV. Transient projectile, HUD, and text
coverage are not claimed at this milestone.
The ACES presentation pass uses exposure `1` by default, keeping ordinary
traced lighting above 8-bit display quantization. Set `AB3D2_DXR_EXPOSURE` to a
finite value from `0.001` through `100` for diagnostic exposure sweeps. The
CTest all-level invocation uses the production default without an override.

The RTX smoke then freezes the camera, view, and scene frame and presents
`AB3D2_DXR_STABILITY_FRAMES` frames (default 24, range 4--4096), reporting the
mean absolute per-component difference between consecutive presented frames on
the 0--255 display scale together with a count of pixels saturating tone mapping.
A converging renderer's difference falls towards a floor; a boiling one holds it
roughly constant. The value is reported, not bounded, because several levels
render almost nothing at their smoke camera and would pass any bound trivially.
On the first requested level it also selects and fires the real Shotgun through
the complete source input/gameplay sequence, presents all 48 animation updates,
reports mean/maximum presentation time, and requires the complete scene-rebuild
counter to remain unchanged after selection. It then walks and fires for 400
frames, reporting the moving-frame display delta and count of pixels changing by
at least 16 alongside the scene-rebuild count. These moving values are comparison
metrics rather than pass/fail thresholds. The sequence guards both the firing
hitch and the associated reconstruction-history quality drop.

`AB3D2_DXR_CANDIDATES` and `AB3D2_DXR_RESERVOIR_LIMIT` set the emitter
candidates resampled per primary hit and the maximum history-domain count
accepted from each reused previous-frame reservoir. Initial candidates collapse
to one temporal proposal, so increasing candidates does not shorten that history.
They default to 16 and 20. A positive limit
enables staged temporal and current-frame spatial reuse with material/depth/
normal validation, stratified initial selection, selected-only initial
visibility, fixed low-discrepancy neighbor offsets, naive-neighbor discounting,
and ray-traced bias correction. One analytic-environment candidate and one
independent BRDF candidate join the configured local candidates in the same
balance-heuristic reservoir. The BRDF strategy overlaps the analytic environment,
whose reverse PDF is exact; BRDF rays that hit a mesh emitter are rejected because
the stochastic ReGIR table cannot provide that arbitrary emitter's reverse
per-cell PDF. Mesh emitters remain completely and unbiasedly covered by the local
strategy. Before initial sampling, a camera-centered
16-by-16-by-16 ReGIR grid presamples 512 corrected light entries per cell from
the complete global emitter alias table. Its project-owned volume target uses
triangle area, a conservative emitted-radiance bound, and spatial solid angle;
surfaces outside the grid retain the complete global proposal. The filter-free
positive-history path uses four spatial neighbors and 16 disocclusion attempts,
matching NVIDIA's Ultra structure. `16 / 20` passed the moving-camera visual
check for the former local-emitter-only stage and the completed heterogeneous
signal was accepted in motion on 2026-08-21, so it remains the production default;
`4 / 128` mixes a low candidate count with a much longer history than NVIDIA's
presets and is no longer recommended. An explicit zero history limit retains
the history-off diagnostic. Secondary path vertices reuse two local samples
from their shared ReGIR cell, plus one environment and one environment-overlap
BRDF sample, before
selected-only visibility instead of returning to the former global one-sample
emitter path. Section 11 of
`DXR_RAY_RECONSTRUCTION_PLAN.md` records
why measurements from the former biased temporal approximation cannot be used to
tune the corrected implementation.

Set `AB3D2_DXR_DEBUG_VIEW` to `noisy`, `diffuse-albedo`, `specular-albedo`,
`normal`, `roughness`, `depth`, `motion`, `specular-hit-distance`, or
`specular-hit-distance-history` to present one reconstruction input directly.
Diffuse hit distance remains a diagnostic view but is not tagged to DLSS-RR;
Streamline 2.12 specifies specular hit distance as the optional reflection-motion
guide. `AB3D2_DXR_DEBUG_RANGE` sets the positive
linear visualization range for depth, motion magnitude, and hit distance.
Invalid motion/history pixels are magenta; surface/background guide alpha and
the raw values retain the documented shader sentinels rather than this display
colour.

Ray Reconstruction is a second, explicit build gate. Download and extract the
official NVIDIA Streamline 2.12.0 production release outside the repository,
then configure with the extracted root:

```powershell
cmake -S . -B build/streamline -A x64 `
  -DAB3D2_ENABLE_DXR=ON `
  -DAB3D2_ENABLE_STREAMLINE=ON `
  -DAB3D2_STREAMLINE_ROOT="C:/SDKs/Streamline/v2.12.0" `
  -DAB3D2_DXC_EXECUTABLE="<path-to-dxc.exe>"
cmake --build build/streamline --config Debug
ctest --test-dir build/streamline -C Debug -R "dxr|rtx|streamline" --output-on-failure
```

The build uses the committed project GUID
`6b0d5e3a-a774-4e9e-8937-e6ca29a6885e`, engine `CUSTOM`, and engine version
`0.1.0`; it does not require an NVIDIA-issued numeric application ID. The SDK
gate checks the exact official 2.12.0 headers, production library, signed
runtime binaries, and notices. The runtime verifies the staged NVIDIA
signatures before loading them, initializes Streamline before DXGI or D3D,
disables OTA plugins, and checks DLSS-RR support against the selected adapter
LUID.

The default `AB3D2_DXR_RR_MODE=quality` traces at Streamline's fixed optimal
input size and reconstructs into a full-resolution HDR output. `balanced`,
`performance`, and `ultra-performance` select the other supported modes;
`off` presents the raw noisy input for diagnosis. An explicit
`AB3D2_DXR_DEBUG_VIEW` also bypasses the reconstructed output and displays the
selected low-resolution guide. Streamline-enabled executables import the
static interposer path as a delay-load dependency instead of directly importing
DXGI/D3D, and package tests verify both that loader ordering and the minimal
runtime/licence set.

When `AB3D2_ENABLE_STREAMLINE=OFF`, Streamline and NGX are not linked, loaded,
discovered, or staged.
See [DXR renderer provenance](docs/DXR_PROVENANCE.md) and the
[implementation plan](DXR_RAY_RECONSTRUCTION_PLAN.md) for the clean-room
record.

Implementation provenance: the independently written device lifecycle
consulted `dxr-demo` commit `d08175a58e2737eb87b3cb81eb55416e3fa89ee9`
files `src/d3d12/Renderer.{hpp,cpp}`, `Context.hpp`, and `Frame.hpp` for the
approved swap-chain, frame-context, allocator, and fence concepts. No source
or shader was copied; all HLSL is project-authored. The plan-listed files from
approved `fisica-rt` commit `1784cba270676b8c49f85a9022041dc98528ab54`
were consulted only for DXR geometry/pipeline/camera/noise concepts. No external
runtime binary is staged.

The same renderer compiles to a preloaded WebGL build through Emscripten:

```sh
emcmake cmake -S . -B build/web -DBUILD_TESTING=OFF
cmake --build build/web
```

This produces `ab3d2.html`, JavaScript/Wasm, and the lower-case `data/`
preload package in `build/web`. Serve that directory through an HTTP server;
opening the HTML file directly will not satisfy browser asset-loading rules.

Run the `ab3d2` executable from its build output directory. It starts with
Level A's authored story text; use `--level B` through `--level P` to select
another campaign entry. After 750 ms, any key or mouse button dismisses the
text and starts play. Successful level exits show the next story and continue
the campaign. Escape or the window close control quits during gameplay. The
native runtime fails explicitly if an authoritative asset is unavailable.

Position saves remain the original unversioned 420-byte `boot.dat` payload.
When enabled, F5/F9 quicksave uses the port's versioned `savegame.bin` runtime
snapshot described under Desktop configuration.

## Port authority and source map

Gameplay and data behavior must be translated from the maintained source, not
from the first game port. The initial mappings are:

- startup asset ownership: `controlloop.s:Game_Start`;
- level bootstrap: `hires.s:Game_Begin` and `modules/res.s:Res_LoadLevelData`;
- level binary structures: `defs.i:TLBT`;
- whole-level scene production: `hires.s:DrawDisplay`,
  `newaliencontrol.s:ViewpointToDraw`, and `objdrawhires.s`. It does not
  depend on `orderzones.s:Zone_OrderZones`, PVS errata, or portal traversal.

`src/renderer.h` consumes `src/scene_frame.h` without exposing a graphics API
to game simulation. `src/renderer_opengl.c` implements OpenGL/WebGL, while the
clean-room RTX boundary supplies either the fail-fast stub or the opt-in
Windows D3D12/DXR renderer. Both backends use the same renderer-neutral scene
and render-view contract rather than
depending on Amiga framebuffer, C2P, copper, or software-rasterizer state. It
may submit a complete loaded level every frame; any visibility culling is an
optional native optimisation rather than a porting prerequisite.
