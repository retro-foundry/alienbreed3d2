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
  `TLBT` and `TLGT` headers follow `amiga/ab3d2_source/defs.i` exactly;
- defines a GPU-neutral frame command interface for cameras, materials,
  geometry, sprites, and HUD text;
- preserves the source `DEFGAME`/save-slot campaign record (a 70-byte,
  big-endian level and inventory payload) while keeping its host storage and
  interactive menu flow unported;
- opens a diagnostic SDL window whose title reports the command count. It does
  not rasterize the game scene.

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

Run the `ab3d2` executable from its build output directory. Use Escape or
close the window to exit. The native runtime fails explicitly if an
authoritative asset is unavailable.

## Port authority and source map

Gameplay and data behavior must be translated from the maintained source, not
from the first game port. The initial mappings are:

- startup asset ownership: `controlloop.s:Game_Start`;
- level bootstrap: `hires.s:Game_Begin` and `modules/res.s:Res_LoadLevelData`;
- level binary structures: `defs.i:TLBT`;
- future scene production: `hires.s:DrawDisplay`, `newaliencontrol.s:ViewpointToDraw`,
  `orderzones.s:Zone_OrderZones`, and `objdrawhires.s`.

The future GPU backend will consume `src/scene_frame.h`; it must not depend on
Amiga framebuffer, C2P, copper, or software-rasterizer state.
