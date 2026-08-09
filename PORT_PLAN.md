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

### Latest milestone: GLFT catalog and level music

- [x] `src/game_link.*` provides a bounds-checked view of every `GLFT` table
  from `defs.i`, preserving big-endian definition data for the subsystem that
  will own it later.
- [x] Staged `ab3:`, `tkg1:`, and `tkg2:` resource paths resolve to their
  source assets without case-sensitive host assumptions. Legacy external
  volumes, including `sfx:`, are rejected until their original distribution
  mapping is evidenced; no similarly named asset is substituted.
- [x] `modules/res.s:Res_LoadLevelData` level-music step now runs before each
  Level A-P map/data bundle. Tests cover the GLFT layout, known table entries,
  path resolution, and all campaign music loads.
- [ ] Load optional floor, wall, and properties assets when their exact source
  ownership is ported. PVS errata is explicitly out of scope because the GPU
  renderer will draw complete loaded levels.

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
- `src/level_bootstrap.*` decodes the big-endian `TLBT` and `TLGT` headers used
  by `hires.s:Game_Begin`.
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
   - Translate the source preferences, progression, and saved-game formats
     from `c/game_preferences.c`, `c/game_progress.c`, and `data/game_data.s`.
     Keep native files separate from source assets and version their format.

3. **Level runtime and visibility data**
   - Materialize the `TLBT`, `TLGT`, `ZoneT`, `EdgeT`, `PVST`, door, lift,
     switch, control-point, object, and clip structures as endian-safe native
     views or owned structures.
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

## Near-term acceptance criteria

The next milestone is complete when the native executable can enter and return
from the original single-player menu, select any campaign level, load all its
source-defined resources, initialize an equivalent level/player state, and
produce a source-backed `SceneFrame` without pixels or multiplayer code.
