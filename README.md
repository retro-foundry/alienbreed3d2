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
- `quicksave_load=0|1` enables first-port-compatible F5 quicksave and F9
  quickload through `savegame.bin` beside the executable. It defaults to `0`;
- `volume=0` through `volume=100` controls the master SDL mixer gain;
- `always_run=0|1` makes run the default controller mode. Hold Shift to walk;
  and
- `world_light_tessellation=1|2|4|8` controls presentation-only world-light
  subdivision. The default is `4`; `1` retains the strict source mesh.

`run_default` is accepted as an alias for `always_run`, matching the first
port. Boolean keys also accept `true`/`false`, `yes`/`no`, and `on`/`off`.
An explicit `--level A` through `--level P` command-line option overrides
`start_level`. `--world-light-tessellation 1|2|4|8` likewise provides a
one-run override for renderer validation.

Weapon selection retains the source controls: number keys `1`--`0` directly
select their owned weapon, while Backslash and the right mouse button advance
to the next owned weapon.

When `quicksave_load=1`, F5 captures the complete live level and F9 reloads
its source assets before restoring that state, including saves from another
campaign level.

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
active presentation path is an OpenGL 2.1 / GLES 2 renderer behind the
API-neutral `renderer.h` boundary, so the same scene producers can later feed
a DirectX backend. It draws the complete loaded level without software
rasterization, PVS, portals, or zone ordering. It decodes the maintained
5-bit packed wall WAD strips, `floortile` logical tiles, `256pal`, object
WAD/PTR frame data, vector models, `rawbackpacked`, and `waterfile` into GPU
resources. It forward-renders source-textured geometry, smooth source-driven
light gradients, sky, animated water, bitmap/glare effects, vector objects,
and Player 1's live companion weapon. World and vector materials are converted
once to true colour with per-source-texel continuous linear-light responses,
so source brightness retains its authored hue shift without runtime palette
row selection. Before `Game_Begin`, the renderer-neutral resource catalog
hands every vector asset already loaded by `controlloop.s:Game_Start` to the
active backend. OpenGL walks `draw_PolygonModel`/`doapoly`'s immutable part and
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
This is original source art with a continuous lighting presentation—not a PBR
conversion. Menus and multiplayer are not included. The
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

Requirements: CMake 3.16+, a C11 compiler, Git, and network access the first
time CMake fetches SDL2.

```sh
cmake -S . -B build/pc
cmake --build build/pc --config Debug
ctest --test-dir build/pc --output-on-failure
# Opt-in real OpenGL context validation (hidden window, Levels A-P)
cmake --build build/pc --config Debug --target ab3d2_gpu_smoke
```

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

Position saves are the original unversioned 420-byte `boot.dat` payload—not a
new native format. The gameplay-first executable does not expose its
interactive save/load flow yet.

## Port authority and source map

Gameplay and data behavior must be translated from the maintained source, not
from the first game port. The initial mappings are:

- startup asset ownership: `controlloop.s:Game_Start`;
- level bootstrap: `hires.s:Game_Begin` and `modules/res.s:Res_LoadLevelData`;
- level binary structures: `defs.i:TLBT`;
- whole-level scene production: `hires.s:DrawDisplay`,
  `newaliencontrol.s:ViewpointToDraw`, and `objdrawhires.s`. It does not
  depend on `orderzones.s:Zone_OrderZones`, PVS errata, or portal traversal.

`src/renderer.h` consumes `src/scene_frame.h` without exposing OpenGL to game
simulation. `src/renderer_opengl.c` is the current OpenGL/WebGL backend; a
future backend must keep that public scene and render-view contract rather than
depending on Amiga framebuffer, C2P, copper, or software-rasterizer state. It
may submit a complete loaded level every frame; any visibility culling is an
optional native optimisation rather than a porting prerequisite.
