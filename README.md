# Super Mario 64 Wii U (sm64ex-alo)

This is a Wii U-focused fork of
[AloUltraExt's sm64ex-alo](https://github.com/AloUltraExt/sm64ex-alo).
It adds a Docker-based build workflow, improves external texture-pack loading, and includes various Wii U-specific enhancements and fixes. The original project's documentation is preserved below.

### Wii U changes

* Fixed external texture cache lookups so replacement textures resolve correctly when the entire pack is loaded at startup (`precache true` in `sm64config.txt`).

* Improved texture loading with `precache false` by adding selective preloading based on texture lists. Startup textures are preloaded before the first screen, and course-specific textures are preloaded during level transitions. Textures not included in the preload lists still fall back to on-demand loading during gameplay, preserving the previous behavior.

* Reduced ZIP texture-loading overhead by keeping the archive open while preserving the priority of loose files and other texture packs.

* Added support for building the game as a WUHB package.

* Added an application-local layout for `sm64config.txt`, `saves/`, `mods/`, and other external game files, keeping them inside the application's folder under `wiiu/apps/`. A build option is available to retain the previous SD layout.

* Added support for arbitrary application folder names under `wiiu/apps/`, instead of requiring a fixed folder name. This requires Aroma Beta 11 or newer.

* Added a guided Docker-based build script for compiling the Wii U version of the game and preparing texture packs.

* Fixed **Save and Exit** after collecting a star to return directly to the Wii U Menu.

* Added **Settings > Video** options for automatic, forced 720p, or forced 480p internal rendering, plus 16:9 or 4:3 aspect ratio. Changes can be applied without restarting the game.

* Reduced unnecessary rendering in the **pause > Settings** menu.

* Added an optional **Settings > HUD > Show FPS** counter, disabled by default and available in non-debug builds.

* Adjusted face-button mappings for the Wii U GamePad and Wii U Pro Controller. The Pro Controller and Wii Classic Controller D-pads now also work as the N64 D-pad, including in the debug level selector.

* Added a **Nonstop Stars** cheat that lets Mario remain in a level after collecting a regular star; grand stars and Bowser keys retain their normal behavior.

* Added optional Wii U load-time profiling and a log analyzer for diagnosing texture loads, level transitions, and frame-time stalls. Profiling is disabled in normal builds.


### Requirements

* [Docker Desktop](https://www.docker.com/products/docker-desktop/) — free for personal use and non-commercial open-source projects.
* A legally obtained Super Mario 64 ROM. ROMs and copyrighted game assets are
  not included in this repository.
* A texture pack (optional). Texture-pack support has been tested with [SM64 Reloaded HD](https://github.com/GhostlyDark/SM64-Reloaded/releases).
* Aroma Beta 11 or newer when using the default application-local storage
  layout.

### Quick start

Clone the repository: 

```sh 
git clone https://github.com/Takatoon/sm64ex-alo-wiiu.git 
cd sm64ex-alo-wiiu 
```

Place your own files in the following locations:

```text
user-assets/
├── rom/<your legally obtained SM64 ROM>   # any filename; region auto-detected
├── texture-packs/<original texture pack>.zip   # optional; converted by the build script
└── wuhb/                                   # optional WUHB artwork
    ├── icon.png                            # 128×128, RGBA
    ├── boot-tv.png                         # 1280×720, RGB
    └── boot-gamepad.png                    # 854×480, RGB
```

Place the original, unmodified texture-pack ZIP in `user-assets/texture-packs/` before running the build script. The script will convert it using the selected quality profile and place the converted pack in the appropriate output folder.

`user-assets/base/` is generated automatically; do not palce a `base.zip` there manually. 

The ROM, texture packs, and WUHB artwork are not included in this repository.

On Windows, run:

```bat
git clone https://github.com/Takatoon/sm64ex-alo-wiiu.git
cd sm64ex-alo-wiiu
build-wiiu.cmd
```

On Linux and macOS: 

```sh 
docker compose -f docker-compose.wiiu.yml build --quiet wiiu-dev 
docker compose -f docker-compose.wiiu.yml run --rm --no-deps wiiu-dev \ bash ./build-wiiu-textures.sh 
```

The guided build script lets you choose the game build, texture-pack conversion, or both, along with the available build options.

The completed SD card structure is generated under:

```text
build/dist/wiiu/
```

Copy the contents of `build/dist/wiiu/` to `SD:/wiiu/`. 

By default, the application uses the application-local layout:

```text
SD:/wiiu/apps/<name>/
├── <name>.wuhb
├── sm64config.txt       # created on first launch
├── saves/
└── mods/               # converted texture-pack ZIPs go here
```

The application folder can use any name under `wiiu/apps/`. This requires Aroma Beta 11 or newer. 

A build option is also available to retain the previous SD-root layout. 

When using the combined game-and-textures workflow, the build script places the converted texture pack in `mods/`. Compatible texture-pack ZIPs can also be copied into that folder manually later. 

Texture loading behavior can be configured with `precache` in `sm64config.txt`: 

* `precache true` loads the complete texture pack before the game starts. This results in a longer initial loading time, but avoids additional texture-loading pauses when entering levels. 

* `precache false` uses selective preloading. The game starts faster, and the textures required by each level are loaded the first time that level is entered. This typically adds around 1–2 seconds to the first load of a level, depending on the level. Textures not included in the preload lists still fall back to on-demand loading when needed. 

Both modes are supported, so the choice depends on whether you prefer a longer initial load or shorter startup with small per-level loading times. `sm64config.txt` is created automatically on first launch.

See the [build guide](docs/wiiu-docker.md) for the complete workflow, the
[storage layout documentation](docs/wiiu-storage-layout.md) for runtime paths
and legacy compatibility, and the
[profiling guide](docs/wiiu-load-profiling.md) for optional measurements.

### Credits

This fork builds on [sm64ex-alo](https://github.com/AloUltraExt/sm64ex-alo),
the [SM64 decompilation project](https://github.com/n64decomp/sm64),
[sm64ex](https://github.com/sm64pc/sm64ex), and the Wii U work by
[AboodXD](https://github.com/aboood40091). Please refer to the original
projects and this repository's commit history for their respective authors and
contributions.

## Original sm64ex-alo documentation

The following sections describe features and build instructions inherited from
the upstream project. For this fork's Wii U workflow, use the guide above.

## Changes
 * N64 Building - Support for it was removed in sm64ex
 * Based of the latest refresh (since sm64ex is stuck on 12)
 * Puppycam 2 (sm64ex still has Puppycam 1)
 * Quality of life fixes and features (QOL_FIXES=1 and QOL_FEATURES=1 respectively)
 * Mouse support for desktop targets (MOUSE_ACTIONS=1)
 * Simple debug options menu (EXT_DEBUG_MENU=1)
 * Kaze's more objects patch (PORT_MOP_OBJS=1)
 
## Backends included
 * Same ones as in [sm64ex](https://github.com/sm64pc/sm64ex/tree/nightly) (macOS - Raspberry Pi Series - Windows - Linux), etc.
 * [Nintendo 64](https://github.com/n64decomp/sm64) along with some slight [HackerSM64](https://github.com/HackerN64/HackerSM64) changes.
 * [Nintendo Wii U](https://github.com/aboood40091/sm64ex/tree/nightly) (by AboodXD)
 * [Nintendo 3DS](https://github.com/mkst/sm64-port) (by Fnouwt, mkst)
 * [Nintendo Switch](https://github.com/fgsfdsfgs/sm64ex/tree/switch) (by Vatuu, fgsfdsfgs, KiritoDev)
 * [Android](https://github.com/VDavid003/sm64-port-android/tree/ex/nightly) (by VDavid003)

## Patches
 * Some misc patches for this repo are available [here](https://github.com/AloXado320/sm64ex-alo-patches) (more incoming)

## Building
 ### Clone the repository:

 ```sh
 git clone https://github.com/AloUltraExt/sm64ex-alo
 cd sm64ex-alo
 ```
 
 **Note:** On Unix systems you may need to do this before doing any changes:
 
 ```sh
 git config core.fileMode false
 chmod -R 775 .
 ```
 
 ### Copy baserom(s) for asset extraction:
 
 For each version (jp/us/eu/sh) for which you want to build an executable, put an existing ROM at `./baserom.<VERSION>.z64` for asset extraction.
 
 By default it builds the US version.

<details>
  <summary>To build for N64, click here.</summary>
 
  **Note:** Only tested in WSL, works on (Debian / Ubuntu) as well, other distros untested.

  #### Install build dependencies:
  ```sh
  sudo apt install -y binutils-mips-linux-gnu build-essential git pkgconf python3 gcc-mips-linux-gnu
  ```

  #### Build:
  ```sh
  # if you have more cores available, you can increase the -j parameter
  make -j4 TARGET_N64=1 
  ```
 
  #### ROM location:
  ```sh
  build/us/sm64.us.f3dzex.z64
  ```

</details>

<details>
  <summary>To build for Android, click here.</summary>
 
  **Note:** Only Termux build is supported.
 
  #### Install Termux
 
  Install the app from F-Droid [here](https://f-droid.org/en/packages/com.termux/)
 
  Make sure you use this version, as the Google Play version is outdated.

  #### Install build dependencies
  ```sh
  pkg install git wget make python getconf zip apksigner clang binutils which libglvnd-dev
  ```

  #### Copy in your baserom:

  Do this using your default file manager (on AOSP, you can slide on the left and there will be a "Termux" option there), or using Termux.
  ```sh
  termux-setup-storage
  cp /sdcard/path/to/your/baserom.z64 ./baserom.us.z64
  ```

  #### Install external dependencies
  ```sh
  cd platform/android/ && ./getkhrplatform.sh && ./getSDL.sh && cd ../..
  ```
 
  #### Build
  ```sh
  # if you have more cores available, you can increase the -j parameter
  # On Termux, TARGET_ANDROID=1 is defined automatically in Makefile
  make -j4
  ```

 #### Copying and Installing apk:
 
 Do this to move the apk to the root of your storage then open it using a file manager.
  ```sh
  cp build/us_android/sm64.us.f3dex2e.apk /sdcard/sm64.us.f3dex2e.apk
  ```
 
</details>

 * To build for sm64ex platforms, [click here](https://github.com/sm64pc/sm64ex/blob/nightly/README.md).
 * To build for Wii U, [click here](https://github.com/aboood40091/sm64-port/blob/master/README.md). (TARGET_WII_U=1)
 * This fork's WUHB-adjacent save, config, and mod directories are documented in [docs/wiiu-storage-layout.md](docs/wiiu-storage-layout.md).
 * To build for 3DS, [click here](https://github.com/sm64-port/sm64_3ds/blob/master/README.md). (TARGET_N3DS=1)
 * To build for Switch, [click here](https://github.com/fgsfdsfgs/sm64ex/blob/switch/README.md). (TARGET_SWITCH=1)
