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

- loads the authoritative `test.lnk` game database and `TEXT_FILE` narrative;
- unpacks (including stored and LHA-compressed `=SB=` records), parses, and validates the map, fly map, `twolev.bin`,
  `twolev.graph.bin`, and clip stream for every campaign level (`A`–`P`). The
  `TLBT` and `TLGT` headers follow `amiga/ab3d2_source/defs.i` exactly; both
  source 100x100 navigation maps plus the static door/lift/switch records are
  also decoded, along with each static object's documented type/animation
  selector bytes, without yet running AI or mechanism animation. Every source
  wall plus floor, ceiling, and water boundary is submitted as whole-level
  GPU-neutral geometry, without PVS or portal traversal. The retained scene
  refreshes its commands from the mutable graph after source door, lift, and
  water updates;
- defines a GPU-neutral frame command interface for cameras, materials,
  geometry, sprites, and HUD text. Material commands retain the source asset
  class and select shared versus per-level floor/wall overrides exactly as
  `modules/res.s:Res_LoadLevelData` does, carrying the selected source bytes
  and source palette bytes for later backend-owned conversion/upload. Wall
  palettes use the exact 2,048-byte `Draw_Wall` prefix; floor/ceiling/water
  commands retain the shared source texture palette. Texture-coordinate
  conversion is deliberately still unresolved;
- exposes the 30 source `ODefT` object definitions, both 20-frame six-byte
  object animation tables, and the 32 eight-byte bitmap metrics per object as
  endian-safe read views. Their mode-dependent bytes remain data only: sprites
  are not emitted until the original `ObjectHandler` update sequence has a
  replayable oracle fixture;
- exposes all 20 `AlienT` records used by `ItsAnAlien` and validates every
  loaded alien-slot type against that catalog, without starting an AI update;
- exposes all 20 `BulT` records, including their source animation/pop payload
  views and fixed six-byte frame records. The `Game_Begin` player/alien
  projectile pools and both player entities resolve to checked slots in the
  owned `ObjT` runtime array, without yet creating or updating a projectile;
- preserves the source `DEFGAME`/save-slot campaign record (a 70-byte,
  big-endian level and inventory payload). The native Load Position and Save
  Position menus use the original six-record, 420-byte `boot.dat` layout at a
  mutable path beside the executable (or `--save-path`), preserving every
  unedited raw record. The default starting ammunition class is read through
  the checked four-word `ShootT` record used by `newplayershoot.s`, rather
  than an ad-hoc GLFT byte offset;
- drives the source single-player main menu, including the two-page level
  selector and level start handoff. Master/slave multiplayer is explicitly
  unavailable; custom options and the two-page raw-key control rebinding menu
  change source-backed in-memory preference bytes. Native SDL events also feed
  the source-shaped raw-key map; the source operate, crouch, and fire branches
  consume it, while movement/collision remains pending. Preference persistence
  remains pending;
- opens a diagnostic SDL window whose title presents the current menu/level
  status and command count. It does not rasterize the game scene.

Single-player is the only intended PC mode. The original serial master/slave
multiplayer flow is intentionally not ported.

## Build

Requirements: CMake 3.16+, a C11 compiler, Git, and network access the first
time CMake fetches SDL2.

```sh
cmake -S . -B build/pc
cmake --build build/pc --config Debug
ctest --test-dir build/pc --output-on-failure
```

Run the `ab3d2` executable from its build output directory. Use Up/Down to
navigate, Enter or Space to activate a menu item, and the source menu's EXIT
entry (or the window close control) to quit. Escape preserves the main menu's
source no-op/cancel behavior. The native runtime fails explicitly if an
authoritative asset is unavailable.

Position saves are the original unversioned 420-byte `boot.dat` payload—not a
new native format. The default path is beside the executable; use
`--save-path <boot.dat>` to select another compatible file. A file must already
exist and be exactly 420 bytes: the port deliberately does not fabricate a
first-run template or modify the staged `data/` tree. The historical archive's
`boot.dat` is used only as a test fixture because its NEW GAME record is not
valid for the maintained A-P campaign.

## Port authority and source map

Gameplay and data behavior must be translated from the maintained source, not
from the first game port. The initial mappings are:

- startup asset ownership: `controlloop.s:Game_Start`;
- level bootstrap: `hires.s:Game_Begin` and `modules/res.s:Res_LoadLevelData`;
- level binary structures: `defs.i:TLBT`;
- future whole-level scene production: `hires.s:DrawDisplay`,
  `newaliencontrol.s:ViewpointToDraw`, and `objdrawhires.s`. It does not
  depend on `orderzones.s:Zone_OrderZones`, PVS errata, or portal traversal.

The future GPU backend will consume `src/scene_frame.h`; it must not depend on
Amiga framebuffer, C2P, copper, or software-rasterizer state. It may submit a
complete loaded level every frame; any visibility culling is an optional native
optimisation rather than a porting prerequisite.
