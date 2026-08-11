# Alien Breed 3D II: The Killing Grounds — PC port

This is the native C port of the maintained Amiga source under
`amiga/ab3d2_source/`. It uses the same CMake/SDL2 desktop setup as the Alien
Breed 3D I PC port and targets Windows, Linux, and macOS.

The PC runtime stages the unmodified bytes from `amiga/media` beside the
executable as a lower-case `data/` tree. The Amiga volume path `AB3:Includes/test.lnk`, used by
`Game_Start` in `amiga/ab3d2_source/controlloop.s`, therefore becomes
`data/includes/test.lnk`.

## Current native slice

The initial desktop target establishes the port boundary without reusing the
first game's software renderer:

The active direct-play path now runs the source single-player control,
weapon, object/mechanism (including source-held door/lift locks), worry, and
`ItsAnAlien` update ordering. Every
complete AI route enters through `newanims.s:ObjectHandler` only when its
source worry byte is set. Source death, successful collectable, and
destructible narratives enter the GPU-neutral small-screen message ring and
remain simulation-only; no HUD commands are submitted. Failed inventory pickups retain their
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
row selection. This is original source art with a continuous lighting
presentation—not a PBR conversion. HUD text, menus, and
multiplayer are not included. The
detailed inventory below records the source-backed foundations; older
references to an unbound AI dispatcher are superseded by this live
integration.

Mouse X remains the source controller's yaw input, while `RenderView` mirrors
its raw delta immediately for high-frame-rate presentation and reconciles the
next completed source tick without double-applying it. Mouse Y drives the
native `RenderView` pitch for real 3D mouse-look (clamped to +/-85 degrees and
respecting the source invert-mouse preference); both presentation adjustments
are isolated from source player state and do not replace the original aim/look
state. The renderer blends completed source-frame scene snapshots using the
50 Hz VBlank remainder. This uses SDL's high-resolution performance counter,
so the source companion weapon/action sequence advances only at its fixed PAL
cadence while camera, mutable world geometry, sprites, and source light
samples present smoothly at the host frame rate. Native direct play requests the active
desktop resolution and renders directly to the full SDL drawable. Its main
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

The shared-resource loader also now decodes the original `CSFX` Fibonacci
sample payloads exactly as the source file loader does. Source sound-event
routing and playback are deliberately deferred from the current
gameplay-first scope.

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
  simplification: every `Draw_Wall` record on a controlled `EdgeT` follows its
  canonical source mechanism record, and a lift's wall sides move rigidly from
  the live `Draw_Flats` plane. This produces a closed moving solid while still
  applying the live controlled `Draw_Wall` material, lighting, V-origin, and
  texture-window updates written by `DoorRoutine`/`LiftRoutine`, so the source
  panel motion remains visible;
- defines a GPU-neutral frame command interface for cameras, lighting,
  environment, static/dynamic mesh instances, and source-object instances.
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
  flame-allocation paths are live, while audio remains deferred;
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
  predictive lead and lateral launch offset; its separate audio calls remain
  absent. `modules/ai.s:ai_AttackWithProjectile` now composes that projectile
  handoff with the source damage/death exit, attack animation, heading, memory,
  torch, and finished/sight transitions. `ai_DoTakeDamage`
  likewise completes the selected nonfatal reaction animation and heading
  branch with explicit source torch inputs. Enemy behavior and dynamic blast
  are live; audio is deliberately deferred;
- opens an SDL OpenGL window that draws the direct-play world with depth
  testing: source sky, complete authored geometry, smooth source light,
  animated water, source bitmap/glare/vector objects, and the live view weapon.
  It intentionally draws no text or UI.

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

Run the `ab3d2` executable from its build output directory. It starts Level A
directly; use `--level B` through `--level P` to select another authored level.
Escape or the window close control quits. The native runtime fails explicitly
if an authoritative asset is unavailable.

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
