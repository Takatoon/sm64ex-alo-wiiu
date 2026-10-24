# Optional DynOS player packs on Wii U

This implementation starts from the public Takatoon main commit
`ec4820f7f99506a580948b40e1de1ae4290918f3`. It does not depend on the local
compiled Swag/Sunshine/Render96/DS/Galaxy experiments or their renderer changes.

Build with `DYNOS=1 EXTERNAL_DATA=1`, or use the public build wizard:

```sh
bash build-wiiu-textures.sh --game-only --external-textures enabled --dynos enabled
```

The feature defaults to zero. Docker Compose forwards `DYNOS`. The enabled
configuration uses the engine's system-heap allocator for a permanent graph
arena. Use a clean build or a separate `BUILD_DIR_BASE` after changing flags.

## Install models without rebuilding

Copy original, unconverted DynOS binaries into the application folder:

```text
SD:/wiiu/apps/sm64-dynos-test/
    sm64-dynos-test.wuhb
    dynos/packs/DS Mario/mario_geo.bin
    dynos/packs/DS Luigi/luigi_geo.bin
    dynos/packs/Sunshine Mario/sunshine_geo.bin
    mods/your-texture-pack.zip
```

Alternatively, put a model ZIP directly in `mods/`. Its virtual paths can be
`PackName/mario_geo.bin` or `dynos/packs/PackName/mario_geo.bin`. ZIPs nested
inside `dynos/packs/` are not automatically mounted: use loose folders there.
Do not zip a ZIP. File names supported by this first selector are
`mario_geo.bin`, `luigi_geo.bin`, `wario_geo.bin`, `waluigi_geo.bin`,
`toad_player_geo.bin` and `sunshine_geo.bin`. A different Mario-compatible
replacement can be renamed `mario_geo.bin` without altering its contents.

Restart the application after installing/changing packs. In the pause Options
menu, open Settings > **DYNOS PLAYER MODEL**. ORIGINAL retains native Mario.
Packs are selected individually as Mario costumes; there is no change to
physics, hitbox, voice or gameplay. This public-base implementation does not
include the experimental crouch/D-pad selector. On Wii U, hold physical
Minus and press D-pad Left/Right to cycle player appearances, including ORIGINAL.
This works with GamePad, Pro Controller, Classic Controller and Wii Remote
with Nunchuk. Each press changes once; holding the direction does not repeat.
The chord consumes its direction to avoid changing camera/menu at the same time.
Selection is saved after the requested model has loaded, before the next frame.

The WUHB does not need to contain the model or its textures. Embedded PNGs in
the BIN supply the model's own textures, independently of any texture mod.
The base game's external data is still embedded in the WUHB. No large texture
pack is required. User texture packs should be compatible with the public
renderer; experimental DDS/GPU profiling changes are not included here.

## Initial compatibility scope

- Little-endian legacy DynOS actor streams and CoopDX `DYNOSBIN` zlib streams.
- Embedded PNGs and internal texture aliases, integer or F32VTX vertices, F3DEX2 display lists and standard model
  geometry commands/callbacks. Pointer references are resolved after reading
  the records; no file-supplied machine address is executed.
- Player appearances use Mario's 20-joint native animation hierarchy. NPCs
  are checked against their original actor's joint order and parents. All switch/hand/eye/cap/LOD
  alternatives must retain the same joint order/parentage. Connected meshes
  work by preserving the original vertex-cache loads and transforms.
- Common two-texture loads are adapted to the port's two slots. Independent
  secondary texture coordinates are rejected when sampled. Secondary loads
  unused by an explicitly defined material combiner are omitted to preserve
  the renderer's shared UV bounds.
- CoopDX light-copy commands use fixed standard palettes selected by the slot
  name. Original material lights are otherwise retained. Lua-defined palettes
  and character identities are not interpreted (a Yoshi in a Waluigi slot still
  inherits that slot's default palette/name).

Embedded animation tables are read/skipped, not installed. There is no Lua,
Character Select execution, new actions, custom voices, runtime C-source
compiler or custom actions. Named actor replacements, including supported
NPCs and native cap actors, use the original actor animations. Newer raw/external
texture records, unsupported callbacks/commands and a different animation
hierarchy are rejected. F32VTX decimal coordinates are decoded from IEEE754
little-endian bytes and rounded to the nearest native signed 16-bit position
(half values away from zero, at most half a game unit error per axis). The
format marker is not a vertex. NaN, infinity and out-of-range coordinates
are rejected, including under the Wii U fast-math optimization flags. The
supplied Sunshine and Sunshine Jacket player BINs pass the reader unchanged.

An incompatible selected pack displays ERROR in the choice list, logs its
reason and renders original Mario. You can continue to the next choice. The
failed pack is not saved as the active model.

## Memory, rendering and persistence

Models are discovered at startup, sorted and deduplicated. The saved preference
is a stable hash of the virtual path, not a changing menu index. Missing saved
packs select original Mario. At most 64 candidate player files are listed.

The selected BIN is loaded on demand before building the next game frame. Each
used model owns its vertices, commands, layouts and encoded PNGs until shutdown;
permanent graph nodes survive level-pool resets. Retaining previously used
models also protects queued/interpolated display lists and texture-cache keys.
The mirror uses the same selected graph. Object model pointers used by game
logic are not replaced. GPU/cache shutdown precedes model release on platforms
that perform explicit renderer shutdown; Wii U OS exit releases the process.

The reader caps input/decompressed data at 32 MiB, individual PNG dimensions at
2048x2048, and estimated model resources (including decoded texture and graph
allowances) at 64 MiB. Resident models share a 128 MiB estimated budget. These
are resource limits, not speed guarantees. PNG decoding/upload occurs on first
draw/cache miss; initial selection may briefly stall. Moving geometry into a
BIN does not reduce its rendering cost: Render96 can still be too expensive.

## Format/reference investigation

The implementation follows the binary record/pointer/texture separation used
by [Render96's DynOS reader](https://github.com/Render96/Render96ex/blob/f6e9e1b54f43b2caeffcf71644c6165f2652948c/data/dynos_gfx_load.cpp)
and checks the compression, pointer and geometry conventions against
[CoopDX](https://github.com/coop-deluxe/sm64coopdx/blob/8cd6e5977d9f920d51ca71f2c61801d019ed79c6/data/dynos_bin_compress.cpp).
It is an independent, deliberately limited integration, not a wholesale port
of either repository's UI, script runtime or graphics engine.

Wii U requires native reconstruction: Gfx bitfields keep numeric values,
vertex coordinates/UVs are decoded from little-endian scalars, and GeoLayout
BBH/HH/full-word fields are packed separately for PowerPC. A blanket byte swap
would corrupt callbacks, halfwords or scale commands.

The filesystem integration also fixes an existing public-base directory walk
return-type mismatch: a successful Boolean walk was being returned as enum
INTERRUPTED, discarding enumeration results. Folder/ZIP regression coverage
checks the actual virtual filesystem, not a mock listing.

## Validation and hardware trial

Native tests:

```sh
python3 tools/tests/test_dynos_model.py
python3 tools/tests/test_dynos_fs.py
python3 tools/tests/check_dynos_packs.py PATH_TO_BIN_OR_ZIP ...
```

Fourteen reader tests cover uncompressed/compressed streams, truncation and size
limits, invalid pointers/callbacks, cycles, all switch alternatives, native
vertex decoding, ambient-light padding and palette-copy light-state refresh and callback-node compatibility. The filesystem test discovers and
reads both a loose player BIN and one inside a deflated ZIP.

Real-file reader checks accepted the four VC21 player binaries, all five Swag
players and Render96 Mario (10 models), without C conversion. Sunshine and
Sunshine Jacket additionally pass after the F32VTX/texture-alias extension.
These checks establish parsing and
structural compatibility, not visual correctness. A complete Wii U build
links the runtime, menu, mirror and embedded-texture provider. No full host
game execution is claimed: the optional dummy-backend host build encountered
public-base segment/TLB linker dependencies.

On Wii U test loose-folder detection first, selecting DS Mario/DS Luigi,
original fallback, saved selection/restart, run/jump/swim, hands/held objects,
cap effects, castle mirror and several warps. Then test the same BIN in a ZIP.
Capture `DYNOS_INIT`, `DYNOS_LOADED` or `DYNOS_REJECT` messages if a pack is
missing or rejected. Hardware appearance, texture upload and performance
validation remain pending.

The complete public-base regression suite passes all 27 tests. Modified core
translation units also compile for Wii U with DYNOS=0. Bash syntax and Git
whitespace checks pass. The test distribution includes two unchanged VC21
BINs (Mario and Luigi) and the original Sunshine player BIN; it includes no large texture mod.

Final test WUHB SHA256: `35f24861a1a3d2d1c50be97b7b79ecbbe1c4d523b0a713bb2a693dd6b26b493b`.

Startup crash fix: the Wii U TCP log identified an invalid pointer in the ZIP directory walk called by dynos_init. Zero-initialize the archive root, including its child/sibling links. A poisoned-allocation regression fails before this fix and passes afterwards. Hardware retest remains pending.

Player palette fix: retain the directional copy's player part for its paired ambient copy. VC21 Mario uses source indices whose ambient command otherwise rounds into the next body part, mixing pants blue with shirt red. The regression uses the real pants/shirt command sequence, including consecutive pairs. Luigi uses explicit material lights and does not use this copy path. Visual confirmation of the new build remains pending.

The decimal-vertex extension applies to all compatible DynOS player models,
not to a Sunshine-specific conversion. Tests cover compressed F32VTX data,
rounding, truncation, invalid coordinates under fast-math, forward texture
aliases and cycles, and used versus unused secondary texture coordinates.

## Automatic NPC/actor packs (no priority menu)

Keep player appearances and actor replacements in the same pack if desired:

```text
dynos/packs/Low Poly Promo/actors/
    mario_geo.bin
    luigi_geo.bin
    wario_geo.bin
    peach_geo.bin
    toad_geo.bin
    bowser_geo.bin
    bowser2_geo.bin
```

The `actors/` subfolder and pack name are optional. Loose BINs directly under
`dynos/packs/` still work, and mounted ZIPs use the same virtual paths. A
manifest is not required. Player slots remain choices in the existing menu;
known non-player native actor files apply automatically when their original
model is registered by the level script. Restart after adding/removing packs.
Original object behavior, collision, sound and animations remain in use.

The registry covers 225 exported actor geometry symbols from this public port;
this is a name registry, not a promise that every available mod is compatible.
`bowser2_geo.bin` maps to the native Bowser geometry without shadow. Regenerate
the registry with `python3 tools/generate_dynos_actor_registry.py` after adding
new native actor symbols. Unknown names are not installed as replacements.
Unsupported geometry/callbacks and incompatible animation hierarchies fall
back to the original. The player-only 20-joint test is not used for NPCs.

For overlapping files with different virtual paths, the first compatible path
in lexicographic order wins for that actor; invalid earlier candidates are
logged and skipped. Existing filesystem mount precedence determines which
file is read if different mounted packs expose exactly the same virtual path.
There is no activation/priority menu or per-pack configuration in this version.
Remove or move a pack outside the mounted folders to disable it, then restart.

Model graphs and embedded textures are retained across level transitions.
Per-level graph bindings are cleared before level memory is released and
recreated when native models are registered. Held objects also use the selected
actor graph. Validation compares joint parentage to the native graph, checks
switch and LOD alternatives, and permits a whole-actor empty shadow state such
as Bowser's. It rejects partial skeleton skips. Existing stack/memory budgets
still apply. Only known native geometry callbacks are resolved.

The Low Poly Promo Peach, Toad and both Bowser BINs pass independent reader
checks against the original source hierarchies (21, 20 and 44 joints). Runtime
tests exercise failed-first-pack fallback, deterministic selection, rebinding
after level unload, model-ID reuse and shortcut selection/persistence. Native
graph tests cover switches, LODs, empty shadow states and cycles. Platform
services and graph allocation are stubbed in the runtime test; Wii U visuals,
NPC cutscenes and controller hardware still require console verification.
Logs: `DYNOS_INIT version=2`, `DYNOS_NPC_BOUND`, `DYNOS_REJECT`, and
`DYNOS_PLAYER_SHORTCUT`. The TCP log receiver is not stopped by this feature.

With DynOS enabled, Wii Remote Minus is reserved for the model modifier rather than the legacy N64 L binding.

## Victory hand diagnostics

The celebration still uses native MARIO_ANIM_STAR_DANCE and hand state 2.
A native host probe using the actual geo interpreter and original hand selector
selects the peace-hand display list in both DS Luigi and Sunshine, including
metal/transparent branches. A live Wii U capture on 2026-10-24 also confirms
Sunshine selects child 2 with hand state 2 at celebration timers 40 and 62.

Following the resolved native G_DL/VTX/triangle commands reveals an asset
limitation in the supplied BINs: Sunshine
`mario_004_switch_option_left_hand_peace_mesh_layer_1` produces exactly the
same 172 triangles and vertex positions as the closed hand
`mario_000_displaylist_004_mesh_layer_1`. The earlier compiled Sunshine
conversion independently contains identical vertex/triangle arrays for these
two variants. DS Luigi has 60 triangles in each variant; its peace variant
is the same fist with a different orientation (corresponding triangle vertices
fit a rigid transform within 1.62 model units, consistent with integer rounding).
Changing the hand selector cannot create missing extended fingers. These
packs need an authored peace-hand mesh to display the V sign. The separate
wrist seam report has not been diagnosed by these checks.

This diagnostic build emits DYNOS_STAR_HAND at three phases during a star
celebration (not every frame). Fields include model path, action timer, hand
side, shared and Mario-specific hand states, selected child, actual number
of children, first display-list symbol and current animation ID. Startup
reports star_hand_trace=1. Callback selection and animations are unchanged;
the wrapper calls the native hand selector before recording the result.

## SwagSkeleton95 Replacements compatibility audit

The locally supplied v1.0 ZIP was inspected on 2026-10-24. It contains 67 BINs;
57 pass the current reader against the native actor animation hierarchies.
Reader acceptance is not a substitute for visual verification on Wii U.

| Actor | Native joints | Pack joints | Current limitation |
| --- | ---: | ---: | --- |
| black_bobomb_geo | 13 | 14 | Additional joint |
| bobomb_buddy_geo | 13 | 14 | Additional joint |
| chuckya_geo | 30 | 31 | Additional joint |
| scuttlebug_geo | 42 | 43 | Additional joint |
| skeeter_geo | 28 | 18 | Reduced hierarchy |
| bowser_key_cutscene_geo | 2 | Unresolved | Unbalanced geometry close |
| klepto_geo | 26 | 26 | Unsupported callback 615 |
| mr_blizzard_geo | 9 | 9 | Unsupported palette copy |
| mr_blizzard_hidden_geo | 9 | 9 | Unsupported palette copy |
| ukiki_geo | 23 | 23 | Unsupported palette copy |

The last four skeletons match their native parent/order signatures in a
structural inspection that does not execute callbacks or graphics. Their
material/callback failures remain enforced by the production reader.
This branch does not implement custom bones or replace actor animations.
No third-party model packs, textures, ROMs or compiled game data are included.
