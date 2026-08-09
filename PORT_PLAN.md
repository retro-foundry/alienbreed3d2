# Alien Breed 3D II PC Port Plan

## Goal and fixed decisions

Port the maintained sequel source in `amiga/ab3d2_source/` to a native C
runtime for Windows, Linux, and macOS. The original source and media are the
authority for all game behavior and data formats.

- Single-player campaign only. Do not port the Amiga serial master/slave mode
  or replace it with local or network multiplayer.
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
  bitmap metric records as endian-safe read views, preserving mode-dependent
  frame bytes until the owning object/runtime routine is ported. Tests compare
  every decoded field and frame against `test.lnk`; this is catalog
  preparation only, not sprite emission or animation. The same layer decodes
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
  to GLFT definitions. Projectile/player slots remain data views only; no
  object, animation, or AI behaviour has been inferred or ported.
- [x] `src/level_mechanisms.*` decodes the `TLGT` door/lift streams as the
  exact `ZLiftableT` plus variable `ZDoorWall` sequence used by
  `newanims.s:DoorRoutine` and `LiftRoutine`, bounded by their source
  terminators. It also exposes the eight raw 14-byte switch records that
  `SwitchRoutine` traverses. Door/lift motion, switch interaction, and all
  associated graphics/audio changes remain deliberately unported.
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
  software-renderer approximation.
  Neither step uses PVS/portal traversal.
- [x] `src/player_runtime.*` ports the single-player `Plr_Initialise` spawn
  coordinates into both committed and input-side snap X/Y/Z state, its
  floor-relative standing and target heights, zone, and enemy flags. A loaded
  level now produces a camera and HUD command. Its exact non-spatial
  `plr_KeyboardControl` operate, crouch, and fire branches now consume the
  source raw-key state, including `$ff` latches and crouch-key consumption;
  they deliberately do not move the player. The source advances snap state
  before `hires.s:Plr1_Control` collision-validates and commits it, so native
  horizontal/vertical movement remains gated on that complete sequence.
  Sprite scene emission remains a distinct follow-up slice; walls, floors,
  ceilings, and water now submit source-defined material and geometry commands.
- [ ] Before resolving textured world geometry, establish the source-to-GPU
  texture-coordinate mapping for each primitive. `src/level_draw_graph.*` has
  now proven every active cursor boundary in the shipped streams: type 3 and
  other ignored tags advance by their tag word exactly as maintained
  `draw_zone_graph.s` does. `hireswall.s` establishes wall endpoints, vertical
  bounds, and material ID, but its screen-space perspective texture-coordinate
  calculation must not be replaced by invented UVs. Decode that mapping or
  capture an original-runtime fixture before clearing the unresolved-UV flag
  on wall commands; `Draw_Flats`' scale and floor-texture byte offset must
  likewise become an evidence-backed GPU mapping. Establish object/sprite
  semantics before emitting their scene commands. Do not use a software-renderer
  fallback.

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
  state boundary. Its non-spatial `modules/player.s:plr_KeyboardControl`
  operate/crouch/fire branches consume it; spatial movement remains absent
  until the collision/update sequence is validated.
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
     Horizontal movement still awaits the source collision/update order, not a
     generated trigonometric approximation.
   - Port menu controls and in-game messages before adding convenience inputs.
     Any optional modern binding must remain outside core simulation state.

5. **Objects, animation, AI, audio, and progression**
   - Port runtime object initialization, animation, doors/lifts/switches, and
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
   - Submit unprojected world geometry, source material IDs, sprite frames,
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
- For movement, collision, AI, projectile, timer, and visibility work, capture
  focused emulator-oracle fixtures: input sequence, routine entry/exit,
  relevant RAM window, and expected native-state result.
- Validate in layers: asset load, level bootstrap, input-to-control state,
  control-to-simulation state, simulation-to-scene commands, then GPU output.
  Do not use a rendered frame as the only parity check.
- Keep unported behavior absent and marked `TODO(port): <source>:<routine>`;
  never replace it with fabricated gameplay or a software renderer.

## Next evidence-backed milestone

The campaign bootstrap/whole-level scene milestone is complete: the native
executable enters the original single-player menu, selects every campaign
level, loads source-defined resources, initializes its source-backed
level/player state, and produces PVS-free camera/material/geometry/HUD commands
without pixels or multiplayer code.

The next milestone is source-backed dynamic single-player state. Before writing
native movement, collision, falling, interaction, projectiles, AI, animation,
or sprite-frame selection, capture a focused original-runtime oracle fixture
for each routine boundary. Follow the byte-exact capture contract in
[`docs/ORACLE_FIXTURES.md`](docs/ORACLE_FIXTURES.md): at a minimum, record an
input sequence, entry/exit RAM window, relevant registers/flags, and expected
state for:

- `modules/player.s:plr_KeyboardControl` plus `plr1control.s:Plr1_Fall`;
- `hires.s:Plr1_Control` plus the `objectmove.s:Obj_DoCollision`/`MoveObject`
  sequence; and
- `newanims.s:ObjectHandler` through the object frame consumed by
  `objdrawhires.s:Draw_Objects`.

The milestone is complete only when those fixtures replay against native tests,
the equivalent single-player routines update source-named state in the same
order, and resulting object/sprite/HUD commands use source asset IDs and frame
records. No PVS, portal traversal, software framebuffer, or multiplayer state
is required for that work.
