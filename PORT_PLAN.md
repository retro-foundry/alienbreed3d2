# Alien Breed 3D II PC Port Plan

## Goal and fixed decisions

Port the maintained sequel source in `amiga/ab3d2_source/` to a native C
runtime for Windows, Linux, and macOS. The original source and media are the
authority for all game behavior and data formats.

- Single-player campaign only. Do not port the Amiga serial master/slave mode
  or replace it with local or network multiplayer.
- The active development path enters the default single-player session and
  Level A directly. Do not spend implementation time on menus until the game
  loop, dynamic world state, and renderer handoff are playable end to end.
  The desktop executable also accepts `--level A` through `--level P`, which
  uses the source campaign selection handoff without exposing menu behavior.
- Use the Alien Breed 3D I PC port as a desktop-build, SDL2, logging, and
  packed-asset reference only. Do not import its gameplay, level assumptions,
  procedural test data, or software renderer.
- Keep rendering behind `src/scene_frame.h`. Producers submit cameras,
  materials, geometry, sprites, and HUD text; no producer may depend on Amiga
  framebuffers, copper lists, C2P, or a specific modern graphics API.
- The current SDL window is diagnostic-only. A modern GPU backend is a later
  replacement for that consumer, not a software-rendering interim step.
- The future GPU renderer may draw a complete loaded level in one submission.
  Preserve source geometry and gameplay state, but do not port PVS errata,
  portal traversal, or zone-order rendering merely for visibility culling.
  Those are optional native renderer optimisations, not parity requirements.

## Current completed foundation

### Latest milestone: PVS-free whole-level scene production

- [x] `src/game_link.*` provides a bounds-checked view of every `GLFT` table
  from `defs.i`. It now decodes all 30 `ODefT` records, both source 20-by-6-
  byte object animation tables, and all 30-by-32-by-8-byte `GLFT_FrameData_l`
  bitmap metric records as endian-safe read views. Tests compare every decoded
  field and frame against `test.lnk`; `src/object_scene.*` now consumes the
  source-selected frame records for active scene commands, while gameplay
  animation remains owned by its original update routine. The same layer decodes
  every four-word `ShootT` entry; `DEFAULTGAME` obtains its initial ammunition
  class through that checked source record, without implementing firing or
  projectiles. All 20 42-byte `AlienT` entries are likewise decoded and every
  loaded alien slot is validated against that catalog, without inferring AI
  behaviour or updating an alien. All 20 300-byte `BulT` records now expose
  their exact leading longword values and source animation/pop payloads, with
  all 20 six-byte frame records in each payload validated against `test.lnk`.
  No projectile creation, timing, collision, or rendering is inferred from
  them.
- [x] Staged `ab3:`, `tkg1:`, `tkg2:`, and `sfx:` resource paths resolve to
  source assets without case-sensitive host assumptions. The `sfx:` entries
  have an exact one-for-one `media/ab3dsfx/samples/` mapping, verified for all
  59 slots scanned by `Res_LoadSoundFx`.
- [x] `modules/res.s:Res_LoadLevelData` level-music step now runs before each
  Level A-P map/data bundle. Tests cover the GLFT layout, known table entries,
  path resolution, and all campaign music loads.
- [x] `src/game_resources.*` preserves the raw assets queued by
  `controlloop.s:Game_Start`: sounds, walls, floor/texture maps/palette,
  sprite WAD/PTR/palettes, vector models, and backdrop. It does not import
  software-renderer interpretation.
- [x] `Res_LoadLevelData` optional floor, properties, errata, and `wall_0-F`
  files are loaded when present. PVS errata remains uninterpreted and out of
  scope for GPU visibility because complete levels are drawn at once.
- [x] `src/game_session.*` ports the single-player `DEFAULTGAME` inventory,
  selected-level handoff, and completed-level inventory persistence from
  `controlloop.s`. Its first ammunition class comes directly from the GLFT
  shoot-definition table; no multiplayer state is represented.
- [x] `src/game_inventory.*` preserves the `Inventory`/`InventoryConsumables`
  word order used by `c/game_properties.c`, the fallback caps used when the
  optional global `ab3:Includes/game.props` is absent, its `32000` cap
  sentinel handling, single-player collectability rule, saturated consumable
  addition, and item-bit merge. `src/game_link.*` now decodes every matching
  `GLFT_AmmoGive_l`/`GLFT_GunGive_l` object grant. Tests compare all 30 grants
  to `test.lnk` and exercise the source cap logic. This is a pure data/helper
  boundary: it does not yet decide when an `ObjT` is collectible or mutate an
  object slot.
- [x] `controlloop.s:DEFGAME` and `game_LoadPosition` now share a tested
  70-byte big-endian campaign-record codec (level counter plus `InvCT`/`InvIT`).
  Selecting an absent optional `levels/level_X/deflev.dat` follows the source
  path back through `DEFAULTGAME`; malformed definitions fail explicitly.
- [x] `src/game_menu.*` drives the single-player `game_ReadMainMenu` command
  flow from SDL keys: source-style cyclic navigation, play/`game_DoneMenu`,
  two-page `DEFGAME` selection (including its register-restored A-P level
  index), exact load/save position menu routing, and exit. The master/slave
  branch is explicitly unavailable, as required for this port, and the status
  presenter identifies actions that have no implemented native subsystem.
- [x] `src/game_preferences.*` preserves the eight custom-options bytes and
  the seven `not.b` toggles from `controlloop.s:customOptions`; the menu now
  changes this source-backed in-memory state. `src/game_controls.*` also
  preserves all 18 persisted `AssignableKeys_vb` bytes, the source defaults,
  and `CHANGECONTROLS`' two-page raw-key rebinding flow through SDL physical
  key capture. `src/game_input.*` now reproduces `hires.s:key_interrupt`'s
  `KeyMap_vb`/one-shot `lastpressed` state; player control consumption remains
  pending. Preference persistence remains pending; source-format position
  save/load is available when the user supplies a compatible `boot.dat`.
- [x] `src/game_save.*` now preserves the exact unversioned 420-byte
  `game_LoadPosition`/`game_SavePosition` payload: six 70-byte big-endian
  campaign records, with source record zero retained as the load menu's NEW
  GAME entry and source records one through five as writable positions. The
  menu reads the entire file before opening either position screen, modifies
  only the selected user record on save, and rewrites the complete payload as
  the source does. It stores that mutable `boot.dat` beside the executable by
  default (or at `--save-path`), never in staged media. A missing, truncated,
  or non-420-byte file fails explicitly; no slot is synthesized.
- [x] `amiga/ab3d2_old_source/boot.dat` is a checked 420-byte historical
  source-format regression fixture. Its record zero selects level 16, outside
  the maintained source's A-P range, so it is deliberately not staged or used
  as a native first-run seed. The test suite proves its raw layout, rejects
  that unsafe NEW GAME load through the maintained session validator, and
  proves save/load preserves it byte-for-byte outside the edited user record.
- [x] `src/level_runtime.*` resolves the `Game_Begin` TLBT/TLGT table bases,
  including the source's byte-16 `ZoneT` offset table and per-zone draw-graph
  offset table, plus every `ZoneT` in all campaign levels into endian-safe
  native views. It decodes every `Lvl_PointsPtr_l` `Vec2W` through the
  source's inclusive `TLBT_NumPoints` final index, the eight-byte control-point
  records used by source AI navigation, and the `EdgeT` collision records
  reached by each zone's primary edge-index list exactly through the first
  negative source marker; source extended-edge markers remain distinct for the
  later collision port. It also exposes every lower/upper draw-graph stream
  root from `TLGT_ZoneGraphAddsOffset_l`; these roots are complete-level scene
  inputs, not PVS traversal state.
  It also exposes the ten leading fixed 160-byte message payloads exactly as
  `newaliencontrol.s` and `modules/ai.s` pass them to `Msg_PushLine`; no text
  formatting or display behavior has been inferred.
  `src/level_navigation.*` now resolves both 100x100 `twolev` maps through
  `objectmove.s:GetNextCPt`, including its `only-see` high bit, but does not
  invent a caller or AI route behavior.
  The maintained `Game_Begin` EdgeT-span calculation is retained as raw state
  only because shipped offsets do not form a table extent. It does not consume
  PVS lists or portals.
- [x] `src/level_runtime.*` also exposes the bounded 64-byte `ObjT` slot and
  object-point tables that `Game_Begin` establishes. `TLBT_NumObjects` is
  retained as the source's inclusive final object-point index. The separate
  `ObjT` record list is explicitly validated through
  `newanims.s:ObjectHandler`'s `-1` terminator, including the documented
  `EntT_Type_b` and `EntT_WhichAnim_b` overlay bytes used to join object state
  to GLFT definitions. `src/object_runtime.*` copies the entire active list,
  its terminator slot, and all object points into owned big-endian mutable
  storage when a level loads. This makes the source mutation boundary explicit
  without applying any update. The original `Game_Begin` player-shot,
  alien-shot, player-one, and player-two offsets now resolve to checked ranges
  in that owned `ObjT` array, including all 20 slots in each projectile pool.
  Alien AI, projectile flight/collision, and audio remain unported; the
  bounded object paths listed below retain their own source-owned updates.
  `src/object_observation.*` independently translates the source
  `CalcPLR1InLine` workspace over that mutable ObjT/object-point state using
  the original sine table and fixed source capacities. It is refreshed after
  the source-order object/mechanism update for the next shot decision and
  uses neither PVS nor portals. `src/player_shoot.*` now translates
  `newplayershoot.s:Plr1_Shot`'s closest eligible target selection and
  fixed-point vertical auto-aim calculation from that workspace, including
  AUX handling, target flags, sight gate, and tie selection. Its bounded
  `plr1_HitscanSucceded` companion now allocates the source impact ObjT from
  the player-shot pool, copies its source point, and applies byte-sized damage
  plus impact direction. `src/object_projectiles.*` then translates the
  `ItsABullet` stationary pop branch for that status: its source bitmap/glare/
  additive descriptor, frame advance, and `FREE_ENT` release are dispatched
  in `ObjectHandler` slot order. `src/player_shoot.*` also now translates
  `newplayershoot.s:firefive`, which creates a non-hitscan volley directly in
  the source player-shot pool: exact centred firing angles, speed/vertical
  clamp, launch coordinates, and projectile bytes are covered by regression
  tests. `src/object_movement.*` now translates the zero-extension
  `objectmove.s:MoveObject` workspace used by
  `newplayershoot.s:plr1_HitscanFailed`: primary edge contact, source height
  opening tests, edge flag writes, exit-first contact coordinates, and bounded
  joined-zone/layer transitions all retain the source word/long arithmetic.
  Its byte-layout regression covers both a solid exit-first impact and a
  passable joined-zone crossing. Extended-edge movement is deliberately not
  implied by this helper. These helpers are not wired into input yet: source
  cooldown, ammunition, hit probability, miss-effect spawn, moving-projectile
  update, and sound still belong to the remaining `Plr1_Shot` path.
  `src/game_random.*` now retains `objectmove.s:GetRand`'s
  exact seeded 16-bit rotate/add sequence for the upcoming probability and AI
  paths. `GameBootstrap` owns and initializes that `Rand1` state once per
  source game session, so it persists across campaign-level loads; it is not
  consumed until the owning shot/AI routine is fully translated.
- [x] `src/level_runtime.*` exposes the source `ZoneT+48` signed-terminated
  `PVST` records for gameplay-only consumers. Every record and target is
  validated across all A-P levels. `src/object_visibility.*` now directly
  translates `objectmove.s:CanItBeSeen`: the source target-zone PVST lookup,
  signed left/right clip-point tests, and joined-zone crossing height/layer
  tests. Its focused byte-layout fixture covers a visible authored target,
  PVST rejection, clip rejection, and same-zone layer handling. This is an
  alien/gameplay helper only; it does not make the complete-level GPU renderer
  depend on PVS, portal traversal, or renderer clipping.
- [x] `src/object_scene.*` translates the non-raster `ObjT` descriptor boundary
  used by `objdrawhires.s:Draw_Objects` and `draw_Object`. Each live source
  slot produces one unprojected `SceneSprite` command in source slot order:
  bitmap, vector-model, and signed-graphics glare paths remain distinct;
  WAD/PTR/vector bytes, the exact selected palette, source frame number, and
  `GLFT_FrameData_l` bitmap metrics are attached for backend-owned conversion.
  It also preserves the source word-coordinate position, height scale,
  brightness, yaw, AUX offsets, upper-zone bit, and bitmap flip/light/additive
  controls. The command producer deliberately does not carry over current-zone
  selection, layer passes, depth sorting, clip rectangles, PVS, portals, or a
  software rasterizer: it submits every live object for the future complete-
  level GPU renderer. Tests compare every emitted descriptor to the mutable
  source slot and selected raw assets.
- [x] `src/level_mechanisms.*` decodes the `TLGT` door/lift streams as the
  exact `ZLiftableT` plus variable `ZDoorWall` sequence used by
  `newanims.s:DoorRoutine` and `LiftRoutine`, bounded by their source
  terminators. It also exposes the eight raw 14-byte switch records that
  `SwitchRoutine` traverses. Door/lift motion, switch interaction, and all
  associated graphics/audio changes remain deliberately unported.
- [x] `src/level_dynamic_state.*` owns byte-exact mutable copies of each
  loaded `twolev.bin` and `twolev.graph.bin`, then exposes the same
  `LevelRuntime` view over those copies to current simulation. This is the
  source mutation boundary required by `newanims.s:DoorRoutine` and
  `LiftRoutine`: they change `ZoneT`, `EdgeT`, and graphics records in place.
  No mechanism behavior, visibility ordering, or scene rebuild is implied yet;
  static whole-level production intentionally remains immutable until those
  routines are ported and tested.
- [x] `src/level_draw_graph.*` follows the maintained
  `draw_zone_graph.s` cursor for every lower/upper stream: fixed wall records,
  variable flat/water records, object selectors, and header-only ignored tags
  through the signed-byte terminator. It exposes the complete `Draw_Wall`
  record plus every `Draw_Flats` height, source point word (including its high
  flag bits), skipped word, scale, floor-texture byte offset, and brightness
  offset; it validates each reference in all campaign levels.
  `src/level_static_scene.*` expands every source wall into triangle-list
  world geometry and submits floor, ceiling, and water records as source-order
  polygon boundaries. Materials explicitly distinguish wall texture indexes
  from floortile byte offsets and identify the active shared versus per-level
  override source; each command carries the exact selected source bytes plus
  the palette bytes selected by the source renderer: the 2,048-byte wall
  prefix from `hireswall.s:Draw_Wall` or the shared floor texture palette from
  `Res_LoadFloorsAndTextures`. Conversion/upload remains backend-owned. Every
  surface intentionally flags UVs as unresolved rather than fabricating a
  software-renderer approximation. The complete-level allocation is retained
  while `level_static_scene_apply_runtime` refreshes its geometry and material
  IDs from the mutable source draw graph after door, lift, and water updates;
  it rejects a source topology change rather than silently substituting native
  geometry.
  Neither step uses PVS/portal traversal.
- [x] `src/player_runtime.*` ports the single-player `Plr_Initialise` spawn
  coordinates into both committed and input-side snap X/Y/Z state, its
  floor-relative standing and target heights, zone, and enemy flags. A loaded
  level now produces a camera and HUD command. The source raw-key branches for
  operate, crouch, fire, forward/back, turn, run, force-sidestep, sidestep,
  jump, and keyboard look/centre-view now feed the same snap-state order used by
  `modules/player.s:plr_KeyboardControl` and `plr1control.s:Plr1_Fall`.
  `hires.s:Plr1_Control` commits that state through source fixed-point
  arithmetic, teleports, floor/roof transitions, and the primary plus extended
  static `EdgeT` sequences from `objectmove.s:MoveObject`; there is no PVS or
  portal-rendering dependency. Its `TmpX/Y/Z/Height/Clicked/Fire/Gun/Used`
  snapshot now retains the source game-loop values consumed by the first object
  interaction and weapon paths, then clears the one-tick use and click pulses.
  Static collision also records the source
  `0x0100` player-contact bit in each mutable `EdgeT_Flags_w`, so mechanism
  routines can consume that signal rather than a native proximity shortcut.
  `src/player_entity.*` now publishes the `hires.s:Plr1_Use` fields required by
  the live shared `ObjT` state: the player-one type, point position, zone,
  centre height, current angle, targetability, and upper-zone flag. It runs
  after `Plr1_Control`'s translated spatial update and before `ObjectHandler`.
  Damage response, audio, and weapon-sprite selection remain with their
  original routines and are not substituted. The single-player loop also
  performs `hires.s`/`macros.i:FREE_ENT` on the player-two slot and clears its
  sight byte each tick, so the shared object list and scene producer retain no
  multiplayer entity state.
  Reaching the loaded level's authored `Lvl_ExitZoneID_w` (compared to
  `ZoneT_ID_w`, rather than a native zone-table index) now follows
  `hires.s:game_main_loop` into the direct-mode end-level handoff and preserves
  the single-player campaign inventory; it does not fabricate a replacement
  completion screen or menu.
  `src/object_collectables.*` ports the single-player `ItsAnObject`/
  `Collectable`, `Plr1_CheckObjectCollide`, and `Plr1_CollectItem` subset for
  a same-zone/layer candidate: floor/roof placement, source word-coordinate
  hit test, `GLFT_AmmoGive`/`GunGive`, saturated inventory update, and removed
  slot sentinel. The Level B slot-20 health fixture validates this exact path.
  `src/mechanism_runtime.*` now ports the single-player `newanims.s:DoorRoutine`
  slice: source `ZLiftableT` position/velocity and open timers, `ZoneT_Roof_l`,
  per-door graphics displacement records, door-state bits, source raise masks,
  player-in-door safety opening, and `EdgeT_Flags_w` consumption. It operates
  entirely on the cloned level bytes, leaves PVS unused, and emits neither
  audio nor pixels. Its `LiftRoutine` companion now mutates source `ZoneT`
  floor height, lift wall/graphics displacement, per-lift source height table,
  trigger edges, and the `FloorSpd_w` handoff consumed by the next player-fall
  update. Its trailing `DoWaterAnims` pass now follows the source's 21
  controller records after the lift `999` marker, including motion-bound
  reversal and every authored graphics/`ZoneT_Water_l` target; it neither
  renders pixels nor requires PVS. Dynamic `Obj_DoCollision`,
  `src/object_activatables.*` ports the `ItsAnObject`/`Activatable` subset:
  source floor/ceiling placement, default/action six-byte frame records,
  player collision, operate-to-toggle state, active timeout, and inventory
  grant attempt. `src/object_passives.*` ports the source destructible
  damage-threshold/hit-point transition and action animation, plus the
  worry-gated decoration placement/default animation. It deliberately leaves
  object locks, destructible narrative messages, and AI worry selection out
  of scope until their owning systems exist. Dynamic `Obj_DoCollision`,
  switches, enemies, moving projectiles, and sounds remain absent until their
  owning routines are ported. Active source object render descriptors are
  emitted; the only projectile update is the source's non-moving hitscan-
  impact pop state, so no movement behaviour is inferred.
  `SceneCamera.look_offset` now carries
  the source small-screen look value for the future GPU backend. Walls, floors,
  ceilings, and water continue to submit source-defined material and geometry
  commands.
- [x] The desktop entry point now starts the source default single-player
  session directly in Level A rather than routing through the native menu. The
  minimal status presenter exposes live level, zone, and camera coordinates;
  this is a gameplay-first temporary path, not a replacement menu or renderer.
- [ ] Before resolving textured world geometry, establish the source-to-GPU
  texture-coordinate mapping for each primitive. `src/level_draw_graph.*` has
  now proven every active cursor boundary in the shipped streams: type 3 and
  other ignored tags advance by their tag word exactly as maintained
  `draw_zone_graph.s` does. `hireswall.s` establishes wall endpoints, vertical
  bounds, and material ID, but its screen-space perspective texture-coordinate
  calculation must not be replaced by invented UVs. Decode that mapping before
  clearing the unresolved-UV flag
  on wall commands; `Draw_Flats`' scale and floor-texture byte offset must
  likewise become an evidence-backed GPU mapping. The active-object sprite
  descriptor boundary is now evidenced and emitted; a backend still needs to
  decode WAD/PTR/vector contents without a software-renderer fallback.

- CMake builds `ab3d2` with SDL2 on the three desktop platforms.
- `tools/stage_media.py` copies the authoritative `amiga/media` bytes into an
  executable-local, lower-case `data/` tree. This replaces Amiga
  case-insensitive filename assumptions without changing any asset content.
- `src/asset_io.*` implements mandatory asset failure and the `=SB=` packed
  record boundary from `modules/file_io.s:io_LoadCommon` and
  `io_HandlePacked`, including stored and LHA-compressed records.
- `src/game_bootstrap.*` maps `controlloop.s:Game_Start`, `SETPLAYERS`, and
  `modules/res.s:Res_LoadLevelData` for the game link, narrative file, and
  Level A–P map/flight-map/data/graphics/clip bundles.
- `src/game_menu.*` maps the single-player branches of
  `controlloop.s:game_ReadMainMenu`, `levelMenu`, `levelMenu2`, and
  `game_DoneMenu` onto SDL key actions, without creating a multiplayer path.
- `src/game_preferences.*` maps the `Prefs_CustomOptionsBuffer_vb` defaults and
  `customOptions` toggles from `controlloop.s`; it intentionally does not yet
  define a native preference-file format.
- `src/game_controls.*` maps `AssignableKeys_vb` and `CHANGECONTROLS` from
  `controlloop.s`; its raw-key state remains in-memory until a source-compatible
  host preference-file policy is defined.
- `src/game_input.*` maps `hires.s:key_interrupt` into a pure native raw-key
  state boundary. `src/player_runtime.*` consumes it through the single-player
  keyboard/fall/static-collision sequence; source object collision and
  interaction are still pending.
- `src/level_bootstrap.*` decodes the big-endian `TLBT` and `TLGT` headers used
  by `hires.s:Game_Begin`.
- `src/level_navigation.*` maps the walk/fly link lookup in
  `objectmove.s:GetNextCPt`; no AI or movement routine consumes it yet.
- `src/level_mechanisms.*` maps `newanims.s`'s door, lift, and switch source
  data streams; no dynamic mechanism routine consumes them yet.
- `src/scene_frame.*` and `src/renderer_stub.*` establish the renderer seam.
- `tests/level_data_test.c` validates the staged data and every campaign level.

## Implementation sequence

1. **Game-link and resource catalog**
   - Decode `GLFT` in `defs.i` from `includes/test.lnk`, including the
     fixed-size level, music, wall, object, vector, sound, floor, texture,
     bullet, gun, alien, and object-definition tables.
   - Resolve the Amiga volume-qualified resource names in that table against
     the staged media tree, preserving their source asset identity for the
     future material/sprite/audio systems.
   - Load the level-specific music and optional floor, wall, properties, and
     PVS-errata assets exactly when the corresponding source routine does.

2. **Single-player control flow and persistent state**
   - Translate `Game_Start`, `DEFAULTGAME`, `game_ReadMainMenu`, `SETPLAYERS`,
     and level-enter/return sequencing from `controlloop.s`.
   - Port only the single-player branches; make a request for master/slave mode
     fail explicitly.
   - Translate the source preferences and progression formats from
     `c/game_preferences.c` and `c/game_progress.c`. Keep any future native
     preference/progress files separate from source assets and version their
     format. Preserve `data/game_data.s` position saves as their exact
     unversioned six-record `boot.dat` payload; do not substitute a native
     format or generate a template.

3. **Level runtime and source data**
   - Materialize the `TLBT`, `TLGT`, `ZoneT`, `EdgeT`, door, lift, switch,
     control-point, object, and clip structures as endian-safe native views or
     owned structures when gameplay or whole-level scene production requires
     them. Do not materialize `PVST` data for rendering.
   - Port level initialization in `hires.s:Game_Begin`. Do not port PVS
     errata, portal visibility, or `Zone_OrderZones` as a rendering dependency;
     the GPU path may draw the whole level at once.
   - Retain zone/edge data only where gameplay or a later optional culling
     optimisation demonstrably needs it.

4. **Input, player, movement, and interaction**
   - Map native SDL events to the source control bytes; port the single-player
     paths in `plr1control.s`, `modules/player.s`, `objectmove.s`, and
     `newplayershoot.s` without changing fixed-point scales, timer ownership,
     collision order, aim behavior, or pickup rules.
   - `src/game_math.*` now loads and validates the exact 16,384-byte
     `amiga/media/includes/bigsine` payload that `data/tables_data.s` incbins;
     it preserves `AMOD_A` address wrapping and big-endian sine/cosine reads.
     Horizontal movement, falling, and static `MoveObject` collision now use
     that table and the maintained fixed-point update order, not a generated
     trigonometric approximation. The current interaction scope includes the
     tested collectable, activatable, destructible, and decoration paths,
     `DoorRoutine`, `LiftRoutine`, and the source edge-gated next-weapon
     selection. `CalcPLR1InLine` now publishes its source-shaped object
     observation workspace without a renderer dependency, while
     `objectmove.s:CanItBeSeen` supplies the separate PVST/clip/joined-zone
     gameplay visibility query an alien update will consume.
     `object_movement.*` independently preserves the zero-extension
     `MoveObject` trace needed by `plr1_HitscanFailed`; next is that miss
     effect's object-pool write, followed by source `Obj_DoCollision`, the
     remaining alien/projectile-flight `ObjectHandler` paths, and the rest of
     `newplayershoot.s`.
     Source object render descriptors are now emitted independently of those
     pending simulation branches.
     `SwitchRoutine` remains absent:
     the maintained `objmoveanim` loop comments out its call, so it must not
     be activated as a native gameplay change.
   - Keep optional modern bindings outside core simulation state. The native
     menu is intentionally not on the gameplay-first launch path for now; do
     not extend it while the direct Level A path is the active milestone.

5. **Objects, animation, AI, audio, and progression**
   - Port runtime object initialization, animation, switches/water animation, and
     collision from `newanims.s`, `objectmove.s`, and `newaliencontrol.s`.
   - Port AI from `modules/ai.s` using its GLFT definition tables; do not
     approximate enemy state machines or constants.
   - Load and play original sound/music data through a native audio backend;
     preserve source event/timing decisions while replacing Paula hardware.
   - Wire source-backed game events to progression and achievements only after
     their triggering routines are ported.

6. **Scene production and GPU renderer**
   - Translate the non-raster scene construction path around
     `newaliencontrol.s:ViewpointToDraw`, `DrawDisplay`, and `objdrawhires.s`
     into `SceneFrame` commands; do not carry over the software renderer's PVS
     or portal traversal.
   - Preserve the validated active draw-graph cursor rules (including ignored
     tag-only records). Decode each primitive's material mapping only after
     its source coordinate use is demonstrated; never infer UVs or flat-tail
     meaning from neighbouring geometry.
   - Submit unprojected world geometry, source material IDs, active source
     object/sprite descriptors and frames,
     camera state, and HUD/message intent. The simulation must not emit pixels.
     A renderer is allowed to draw all loaded level geometry every frame.
   - Select and implement the modern GPU API separately, consuming only the
     public scene-frame contract. It owns resource upload, shaders, projection,
     culling implementation, presentation, and diagnostics.

## Validation rules

- Every native gameplay routine cites its authoritative Amiga routine/table in
  source comments and tests.
- Expand asset tests from header validation to all GLFT entries, all 16 levels,
  optional-asset presence, decompressed sizes, and malformed-input failures.
- For movement, collision, AI, projectile, timer, and visibility work, derive
  focused source-level regressions from the maintained routine, source-named
  fields, and byte layouts.
- Validate in layers: asset load, level bootstrap, input-to-control state,
  control-to-simulation state, simulation-to-scene commands, then GPU output.
  Do not use a rendered frame as the only parity check.
- Keep unported behavior absent and marked `TODO(port): <source>:<routine>`;
  never replace it with fabricated gameplay or a software renderer.

## Next gameplay milestone

The campaign bootstrap/whole-level scene milestone is complete. The native
executable now enters the source default single-player session directly in
Level A, loads source-defined resources, runs source-backed player movement,
falling, teleport/zone transitions, and static edge collision, and produces
PVS-free camera/material/geometry/HUD commands without pixels or multiplayer
code. `--level A` through `--level P` provides direct source campaign-level
selection while menu work is deferred; this allows the gameplay loop to enter
authored populated levels such as B without a temporary native menu.

The next milestone is source-backed dynamic world state: initialize and update
objects, apply `Obj_DoCollision`, complete the remaining source-order object
handling, activate switches, and create projectiles. The door/lift and bounded
object slices are complete. `src/object_scene.*` now emits the raw render
descriptor for every live source object without renderer visibility logic,
while `src/object_handler.*` now
preserves `newanims.s:ObjectHandler`'s `ObjT` iteration order, terminator, and
`ObjT_ZoneID_w` to `EntT_ZoneID_w` copy for the translated collectable,
activatable, destructible, and decoration branches. The destructible/decorative
path has no inferred AI worry, narrative, or lock behavior; the stationary
hitscan-impact projectile dispatch is now present, while alien and moving-
projectile dispatch remain unported. The reusable `CanItBeSeen` query now
retains source gameplay PVST/clip/height behavior but is deliberately not
wired until the owning alien path is translated. `firefive` now creates the
source non-hitscan launch state, but its later `ItsABullet` movement/collision
path is still absent, so it too remains unwired. `object_movement.*` now
provides the exact zero-extension `MoveObject` path for
`plr1_HitscanFailed`, including its exit-first wall contact and joined-zone
state; its object-pool miss effect has not yet been connected. Translate each
remaining bounded slice directly from the maintained source and add
source-derived regressions for its state changes and ordering.

The milestone is complete when the equivalent single-player routines update
source-named state in the same order, direct source-derived tests cover their
bounded behavior, and resulting object/sprite/HUD commands use source asset
IDs and frame records. No PVS, portal traversal, software framebuffer, or
multiplayer state is required for that work.

The immediately preceding preparation step is complete: the runtime decodes
the source's object inventory grants and reproduces its inventory-limit helpers,
owns byte-exact mutable `ObjT`/object-point storage, and applies the translated
collectable, bounded activatable, destructible, and decoration paths in
`ObjectHandler`'s source slot order. This does not port PVS/worry selection,
locks, narrative audio/messages, alien behaviour, or projectile flight. The
missing systems must use the maintained source's mutable `ObjT` initialization,
worry, animation, and update ordering rather than a generalized object update.
