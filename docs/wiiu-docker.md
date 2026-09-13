# Wii U Docker environment

This environment uses the official `devkitpro/devkitppc:20260503` image and
includes devkitPPC, WUT, the Wii U port libraries, SDL2, Python, Pillow, NumPy,
Make, and all tools required to build the game and convert texture packs. The
host only needs Docker Desktop configured to use Linux containers.

Optional load-time instrumentation is documented in
[Wii U load-time profiling](wiiu-load-profiling.md).

## User files

Place user-provided files under the following directory at the repository root:

```text
user-assets/
├── base/                         # generated automatically
├── rom/<ROM with any file name>
├── texture-packs/<pack>.zip
└── wuhb/
    ├── icon.png                  # PNG, 128x128, RGBA
    ├── boot-tv.png               # PNG, 1280x720, RGB
    └── boot-gamepad.png          # PNG, 854x480, RGB
```

The ROM must be a legally obtained copy. The wizard identifies it by its
contents, so its file name does not matter and its region is detected
automatically. Texture packs must be ZIP files inside
`user-assets/texture-packs`; the wizard lists every ZIP found there. WUHB
artwork is optional. Local files under `user-assets` are excluded from Git.

## Running the wizard

On Windows:

```bat
build-wiiu.cmd
```

The wizard can also be run directly inside the container:

```sh
docker compose -f docker-compose.wiiu.yml build --quiet wiiu-dev
docker compose -f docker-compose.wiiu.yml run --rm --no-deps wiiu-dev \
  bash ./build-wiiu-textures.sh
```

The first menu offers three workflows:

1. **Wii U game only**: build only the game.
2. **Texture pack only**: convert a pack without rebuilding the game.
3. **Wii U game + texture pack**: build and prepare the complete result.

The third option is the default and is recommended for the first run.

### Build options

When building the game, the wizard only asks for:

- 60 or 30 FPS;
- external texture pack support in game-only mode;
- application-folder or legacy SD-root storage;
- the application directory and WUHB file name;
- the title displayed by Aroma.

The default application name and title are `sm64ex-alo` and `Super Mario 64`.
The enhanced camera, quality-of-life improvements, and original hidden-cheats
behavior remain unchanged. Cheats start disabled and hidden; the original port
can reveal them by pressing L three times while the options menu is open. The
number of parallel build jobs is selected automatically.

External texture support allows the game to load textures from the SD card but
increases loading times. It is enabled automatically in the combined workflow
because it is required. When disabled in game-only mode, the built-in textures
remain inside the WUHB and the game loads faster.

The recommended storage layout keeps settings, saves, and texture packs beside
the WUHB under `SD:/wiiu/apps/<name>/`. The legacy layout uses the original
shared locations instead:

```text
SD:/sm64config.txt
SD:/sm64_save_file.bin
SD:/sm64ex_res/
```

Legacy mode is available for compatibility and sets `WIIU_LEGACY_PATHS=1`.

### Converting without rebuilding

Texture conversion needs original game data prepared by a build with external
texture support. The wizard saves this data automatically under
`user-assets/base`; users do not need to locate or copy `base.zip` themselves.

If the data is missing when **Texture pack only** is selected, the wizard
offers to build the game and pack, select existing prepared data, or cancel.
This workflow does not ask for a ROM, region, frame rate, title, or WUHB images.

## Output

All results are written under `build/dist`:

```text
build/dist/
├── logs/
├── reports/
├── texture-packs/<converted-pack>.zip
└── wiiu/
    └── apps/<name>/
        ├── <name>.wuhb
        ├── BUILD-INFO.txt
        ├── mods/<converted-pack>.zip
        └── saves/
```

In a combined legacy build, the converted pack is written to
`build/dist/sm64ex_res/` instead of the application `mods/` directory.

Verbose output is stored in `build/dist/logs`; the wizard displays only a
progress indicator while each operation runs.

To install the game, copy the complete `build/dist/wiiu` folder to the root of
the SD card. The final application must be located at
`SD:/wiiu/apps/<name>/`. Do not copy only the contents of `wiiu` directly to
the SD root. For a combined legacy build, also copy
`build/dist/sm64ex_res` to the SD root.

The wizard does not include or overwrite `sm64config.txt`, preserving existing
settings and save data. Reduced texture packs are intended for selective
loading (`precache false`).

## Automation

The same workflows are available through `--game-only`, `--textures-only`, and
`--game-and-textures`. View every automation option with:

```sh
bash ./build-wiiu-textures.sh --help
```

The lower-level build script remains available for manual builds:

```sh
docker compose -f docker-compose.wiiu.yml run --rm --no-deps wiiu-dev \
  bash docker/wiiu/build.sh
```

The resulting RPX is written to
`build/<region>_wiiu/sm64.<region>.f3dex2e.rpx`.

## Wii U paths

The WUHB, configuration, and save-data layout is documented in more detail in
[wiiu-storage-layout.md](wiiu-storage-layout.md).
