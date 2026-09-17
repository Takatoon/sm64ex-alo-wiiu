# Super Mario 64 Wii U (sm64ex-alo)

This is a Wii U-focused fork of
[AloUltraExt's sm64ex-alo](https://github.com/AloUltraExt/sm64ex-alo).
It adds a Docker-based build workflow, external texture-pack support, and Wii U
gameplay and display improvements. The original project's documentation is
preserved below.

## Wii U fork

Builds are created from a user-supplied Super Mario 64 ROM; neither the ROM nor
copyrighted game assets or HD texture packs are distributed here.

### Wii U changes

* Fixed external texture cache lookups so replacement textures resolve
  correctly during full precaching.
* Reduced ZIP texture-loading overhead by keeping an archive handle open while
  preserving the priority of loose files and other texture packs.
* Added selective texture preloading for startup screens and level transitions.
  Textures not covered by the preload tables still load on demand.
* Added a guided Docker build tool for the Wii U game, texture-pack conversion,
  or both. It offers texture-size profiles, 30/60 FPS builds, and an FTP-ready
  SD card output.
* Packaged the game as a WUHB with configurable application name, title, icon,
  and boot images. External-resource builds embed their base resources in the
  WUHB; user texture packs go in `mods/` beside it.
* Added an optional application-local layout for `sm64config.txt`, `saves/`,
  and `mods/`. The previous SD-root layout remains available at build time.
* Fixed **Save and Exit** after collecting a star to return directly to the Wii U
  Menu.
* Added **Settings > Video** choices for automatic, forced 720p, or forced
  480p internal rendering, plus 16:9 or 4:3 aspect ratio. Changes can be
  applied without restarting the game.
* Reduced unnecessary rendering in the Wii U pause/options menu.
* Added an optional **Settings > HUD > Show FPS** counter, off by default and
  available in non-debug builds.
* Adjusted face-button mappings for the Wii U GamePad and Wii U Pro Controller.
  The Pro Controller and Wii Classic Controller D-pads now also work as the
  N64 D-pad, including in the debug level selector.
* Added a **Nonstop Stars** cheat that lets Mario remain in a level after
  collecting a regular star; grand stars and Bowser keys retain their normal
  behavior.
* Added optional Wii U load-time profiling and a log analyzer for diagnosing
  texture loads, level transitions, and frame-time stalls. Profiling is off in
  normal builds.

### Requirements

* Docker Desktop configured to use Linux containers.
* A legally obtained Super Mario 64 ROM. ROMs and copyrighted game assets are
  not included in this repository.
* Aroma Beta 11 or newer when using the default application-local storage
  layout.

### Quick start

Clone this fork, place the required user files under `user-assets/` as described
in the [Wii U Docker guide](docs/wiiu-docker.md), and run:

```bat
git clone https://github.com/Takatoon/sm64ex-alo-wiiu.git
cd sm64ex-alo-wiiu
build-wiiu.cmd
```

The completed SD card structure is generated under:

```text
build/dist/wiiu/
```

Copy that directory to the root of the SD card. The resulting application uses
the following layout by default:

```text
SD:/wiiu/apps/<name>/
├── <name>.wuhb
├── sm64config.txt
├── saves/
└── mods/
```

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
