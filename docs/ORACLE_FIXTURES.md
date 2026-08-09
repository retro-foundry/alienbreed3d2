# Dynamic oracle fixture contract

The original Amiga execution is the authority for movement, collision, falling,
object updates, projectile updates, timers, and sprite selection. Do not port
one of those routines from source inspection alone when its observable state
depends on preceding routines or 68000 register/flag behaviour.

`amiga/ab3d2_source/Makefile` can build a debug (`FLAVOR=dev`) executable with
debug information. Capture fixtures from that maintained source and the same
staged game media used by the PC test suite. Record the executable checksum,
source revision, emulator model/CPU configuration, Kickstart/Workbench inputs,
and frame timing in every fixture manifest. A fixture without that provenance
cannot be used as a parity oracle.

## Fixture packet

Store each fixture beneath `tests/oracles/<routine-case>/` with a JSON manifest
and byte-exact binary dumps. The packet must contain:

- `manifest.json`: source identity, executable checksum, emulator configuration,
  routine entry address/symbol, and the input timeline in source raw-key values;
- `before.bin` and `after.bin`: the contiguous RAM ranges named in the manifest,
  preserved as raw big-endian bytes;
- `registers-before.json` and `registers-after.json`: D0-D7, A0-A7, SR/CCR, and
  PC at the routine boundary; and
- `expected.json`: a compact list of the source-named values the native test
  must compare after replaying the captured input.

The manifest must name every RAM range as a symbol plus byte offset and length,
not just a resolved emulated address. It must also state the call order used to
reach the boundary, including `Anim_TempFrames_w` and any interrupt/frame tick
that changed state before capture. Do not normalize signed values, clear unused
bytes, or convert a source sentinel into a host null value.

## Required first fixtures

### Player keyboard, fall, and collision

Capture one fixture for each of these entry/exit sequences:

1. `modules/player.s:plr_KeyboardControl` followed by
   `plr1control.s:Plr1_Fall`;
2. `hires.s:Plr1_Control` through its call to
   `objectmove.s:Obj_DoCollision`/`MoveObject`; and
3. a boundary case for each branch that changes a player snap position, angle,
   vertical velocity, floor/roof state, crouch state, or current zone.

Include the complete source player state block, its source object slot, the
current zone, referenced `EdgeT` sequence, player input/raw-key state, relevant
level points, and the player globals read by the captured routines. The input
timeline must include key press/release frames, not only the final key bitmap.

### Objects, animation, and sprites

Capture `newanims.s:ObjectHandler` from the first object slot through the
object state consumed by `objdrawhires.s:Draw_Objects`. Cover at least one
active alien, collectable, activatable/destructable object, decoration, and any
projectile/auxiliary slot observed in the selected level.

Include the complete `ObjT` list through its `-1` terminator, its object-point
table, affected zones/mechanism data, the relevant `GLFT` definition and
animation records, `Anim_TempFrames_w`, and all changed object slots. If a
case reaches a sprite frame, record the selected object resource index, frame
metadata, and palette/resource identity in `expected.json`; do not infer that
mapping from a screenshot.

### Projectiles and audio events

After player movement/collision fixtures replay successfully, capture
`newplayershoot.s:Plr1_Shot` and the `newanims.s:ItsABullet` branches it
reaches. Include the selected `ShootT`/`BulT`, ammunition inventory, free shot
slots, object-point table, collision inputs, sound-event globals, and all
resulting slot/timer changes. A visual or audible result alone is insufficient.

## Acceptance rule

For each native dynamic routine, add a test that initializes native state from
`before.bin`, replays the manifest's source input/timing sequence, and compares
the explicitly named `after.bin` ranges and `expected.json` values. Any
unexplained difference is a porting bug or missing fixture coverage; it is not
permission to substitute a new rule, PVS traversal, software rendering, or
multiplayer state.
