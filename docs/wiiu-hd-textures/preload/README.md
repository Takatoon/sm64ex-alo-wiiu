# Conservative Wii U preload manifests

## Result

`preload-manifests.json` is generated exclusively from this `sm64ex-alo`
checkout, the compiled `base.zip`, and an external texture-pack ZIP. It
contains:

- a global bundle for Mario, transformations, the HUD, common events, and
  hardware-verified front-end resources;
- one specific bundle for each of the 30 levels that has a `level.yaml` file;
- separate bundles for the intro and menus, the ending, and conditional system
  resources;
- metadata for every image from `base.zip`, overlaid with any matching or
  additional entries from the selected external pack;
- PNG size and estimated RGBA8 memory metadata;
- a coverage audit and a list of unused candidates.

The generator treats `script.c` as the final authority because five differences
were found between the supporting metadata and the resources actually loaded by
the game. For example, `wmotr` loads the `sky` bank although `level_defines.h`
specifies `generic`; `castle_inside/script.c` loads `common0` although its
`level.yaml` file does not declare it.

## Event coverage

The generated portion of the global preload bundle contains 321 textures and
represents 16.36 MiB of RGBA8 memory:

- `group0`: Mario, metal/vanish/wing transformations, bubbles, walking and burn
  smoke, dust, ripples, splashes, and particles.
- `common1`: coins, stars, caps, power meter, flames, explosions, doors, pipes,
  trees, and global objects.
- `segment2`: HUD, fonts, and common interface resources.

The generator then merges the 52 front-end paths listed in
`tools/wiiu_preload_overrides.json`. These paths were observed as runtime cache
misses during the Wii U hardware session identified in that file. Keeping the
measured additions separate from source-derived dependencies makes the final
table reproducible without treating all 179 conservative front-end candidates
as startup requirements. The resulting global bundle contains 373 textures and
represents 18.20 MiB of estimated RGBA8 memory.

Level-specific groups cover local events. `group12`, used by Bowser, includes
his 14 fire textures, the bomb, impact smoke, impact ring, and yellow sphere.
Levels that load the `effect` bank include its 15 textures.

## Level coverage

The level-specific manifests contain between 73 and 192 textures. Representative
cases are shown below. The figures describe decoded RGBA8 memory rather than
measured loading time.

Representative cases:

| Level | Level-specific textures | RGBA8 |
|---|---:|---:|
| Bowser 2 | 142 | 23.3 MiB |
| Bowser 1 | 120 | 22.1 MiB |
| Whomp's Fortress | 188 | 14.7 MiB |
| Bob-omb Battlefield | 175 | 12.6 MiB |
| Castle Grounds | 160 | 11.4 MiB |
| Castle Inside | 114 | 10.3 MiB |
| Princess's Secret Slide | 73 | 6.9 MiB |

## Coverage audit

For the current Wii U pack, the generator inspected all 2,009 images supplied
by `base.zip`; the external ZIP replaces 1,830 of them. Every base image is
assigned to a conservative gameplay, front-end, ending, or conditional-system
bundle.

The compiled Wii U table omits only conditional PC crash-screen and
touch-control resources that are not used by this platform. Complete skybox
source images are also omitted when present in an input pack: the game requests
the tiles generated under `gfx/textures/skybox_tiles/`, not the source images
under `gfx/textures/skyboxes/`.

## Regeneration

```powershell
python tools/generate_wiiu_preload_manifests.py `
  --root . `
  --base-zip build/us_wiiu/sm64ex_res/base.zip `
  --pack-zip path/to/external-texture-pack.zip `
  --output docs/wiiu-hd-textures/preload/preload-manifests.json `
  --unused-output docs/wiiu-hd-textures/preload/unused-texture-audit.json `
  --compiled-output src/pc/gfx/wiiu_level_preload_tables.inc.h
```

The build consumes `src/pc/gfx/wiiu_level_preload_tables.inc.h` directly. The
command above generates the manifest and the compiled table in one invocation.
It loads `tools/wiiu_preload_overrides.json` by default; use `--overrides` only
to validate an alternative versioned override file. Do not edit the generated
header directly.
The table can also be regenerated independently from an existing manifest with
`--manifest-input`. The Wii U build neither includes nor parses the JSON files.

## Runtime policy

With `precache false`, startup synchronously loads the global bundle—including
the verified intro, title, menu, and star-selector additions—and
`LEVEL_CASTLE_GROUNDS` after GX2 initialization and before the first visible
frame. Each subsequent world loads its specific bundle during its existing
entry transition. No texture work is performed by the star selector because
distributing PNG loads across its updates caused visible stutter in hardware
testing. Paths absent from the manifest retain on-demand loading as a fallback.
