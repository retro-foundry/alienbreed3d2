ALIEN BREED 3D II: THE KILLING GROUNDS - PC PORT

Run ab3d2.exe. The game starts in single-player mode with the authored level
story. After a short input guard, press any key or mouse button to begin.
Successful exits show the next level story and continue the campaign.

CONTROLS

- W / S: Move forward / backward
- A / D: Sidestep left / right
- Left / Right Arrow: Turn
- Mouse: Look and turn
- Left Mouse Button or Ctrl: Fire
- F: Operate doors, lifts, and switches
- 1 through 0: Select an owned weapon
- Backslash or Right Mouse Button: Next owned weapon
- C: Crouch
- Space: Jump
- L: Look behind
- F5: Quicksave (when quicksave_load=1)
- F9: Quickload (when quicksave_load=1)
- Escape: Quit

SETTINGS

Edit ab3d2.ini beside the executable before starting the game. It supports:

- start_level=1 through 16
- infinite_health=0 or 1
- infinite_ammo=0 or 1
- all_weapons=0 or 1
- all_keys=0 or 1
- quicksave_load=0 or 1
- load_autosave=0 or 1 (restore savegame.bin at startup and skip initial flavour text)
- volume=0 through 100
- always_run=0 or 1
- renderer=opengl or rtx (opengl by default; rtx requires an opt-in DXR build)
- rtx_output=auto, sdr, or hdr (auto by default; hdr requires Windows HDR)
- rtx_hdr_peak_nits and rtx_hdr_paper_white_nits accept 80 through 10000
- world_light_tessellation=1, 2, 4, or 8 (4 by default)

The default build reports that DXR was compiled out and never silently
substitutes OpenGL. A native Windows build configured with
AB3D2_ENABLE_DXR=ON ray traces opaque game-world geometry into a fresh, visibly
noisy image on supported hardware. It currently uses decoded source albedo and
one stochastic Lambertian environment sample. Sprites, vector objects, weapon,
HUD, and text are not drawn yet. The enabled build also creates and stages a
hashed, renderer-native set of separate PBR textures from textures_pbr; runtime
sampling of those PBR channels remains to be implemented. The Web build always
uses OpenGL/WebGL.

With always_run=1, hold Shift to walk. With always_run=0, hold Shift to run.

Keep ab3d2.exe, ab3d2.ini, and the data and fonts directories together. The executable
will report an error and stop if a required original game asset is missing.

Health and selected-weapon ammunition use the Alien Breed 3D I port's bitmap
HUD at the bottom right. Key indicators are not rendered yet. Source gameplay
messages use its bitmap text renderer. Menus and multiplayer are not included.
In-game messages appear at the top centre with a resolution-relative safe
margin, and scroll out at the original two-second-per-line cadence as newer
messages replace them.
