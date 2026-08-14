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
- renderer=opengl or rtx (opengl by default)
- rtx_target_fps=30 through 240 (60 by default)
- rtx_debug_view=final, albedo, normal, roughness, metalness, emissive,
  direct, indirect, specular, or variance (final by default)
- world_light_tessellation=1, 2, 4, or 8 (4 by default)

The native RTX option requires a Vulkan ray-tracing-capable GPU and driver.
It uses the supplied PBR textures, fresh per-pixel direct/indirect rays,
emissive-light visibility, ASVGF-style filtering, and temporal upscaling. The
RTX world and vector lighting ignores the original game's brightness and uses
only emissive PBR textures for direct light, indirect bounce, reflections, and
refractions. It reports a startup error when required Vulkan features or
declared PBR files are unavailable. The Web build always uses OpenGL/WebGL.

With always_run=1, hold Shift to walk. With always_run=0, hold Shift to run.

Keep ab3d2.exe, ab3d2.ini, and the data and fonts directories together. The executable
will report an error and stop if a required original game asset is missing.

Health and selected-weapon ammunition use the Alien Breed 3D I port's bitmap
HUD at the bottom right. Key indicators are not rendered yet. Source gameplay
messages use its bitmap text renderer. Menus and multiplayer are not included.
In-game messages appear at the top centre with a resolution-relative safe
margin, and scroll out at the original two-second-per-line cadence as newer
messages replace them.
