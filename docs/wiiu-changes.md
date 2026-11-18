# Wii U changes and implementation details

For the feature overview, requirements and installation instructions, see the [README](../README.md).

## Build, textures, display and controls

* Fixed external texture cache lookups so replacement textures resolve correctly when the entire pack is loaded at startup (`precache true` in `sm64config.txt`).

* Improved texture loading with `precache false` by adding selective preloading based on texture lists. Startup textures are preloaded before the first screen, and course-specific textures are preloaded during level transitions. Textures not included in the preload lists still fall back to on-demand loading during gameplay, preserving the previous behavior.

* Reduced ZIP texture-loading overhead by keeping the archive open while preserving the priority of loose files and other texture packs.

* Added support for building the game as a WUHB package.

* Added an application-local layout for `sm64config.txt`, `saves/`, `mods/`, and other external game files, keeping them inside the application's folder under `wiiu/apps/`. A build option is available to retain the previous SD layout.

* Added support for arbitrary application folder names under `wiiu/apps/`, instead of requiring a fixed folder name. This requires Aroma Beta 11 or newer.

* Added a guided Docker-based build script for compiling the Wii U version of the game and preparing texture packs.

* Fixed **Save and Exit** after collecting a star to return directly to the Wii U Menu.

* Added **Settings > Video** options for automatic, forced 720p, or forced 480p internal rendering, plus 16:9 or 4:3 aspect ratio. Changes can be applied without restarting the game.

* Added **Settings > Video > Frame Rate** option to switch between 30 FPS and 60 FPS without restarting the game. Limiting the game to 30 FPS can leave more rendering headroom for high-resolution texture packs, but it does not provide the same performance savings as a native 30 FPS build, since the game logic still runs at the 60 FPS rate.

* Reduced unnecessary rendering in the **pause > Settings** menu.

* Added an optional **Settings > HUD > Show FPS** counter, disabled by default and available in non-debug builds.

* Adjusted face-button mappings for the Wii U GamePad and Wii U Pro Controller. The Pro Controller and Wii Classic Controller D-pads now also work as the N64 D-pad, including in the debug level selector.

* Added a **Nonstop Stars** cheat that lets Mario remain in a level after collecting a regular star; grand stars and Bowser keys retain their normal behavior. The cheats menu can be opened by pressing L three times from the pause menu.

* Added optional Wii U load-time profiling and a log analyzer for diagnosing texture loads, level transitions, and frame-time stalls. Profiling is disabled in normal builds.

## GamePad and minimaps

The dedicated GamePad panel combines contextual status information with an interactive minimap and touch zoom. The runtime adapts Mark Street's [3DS minimap prototype](https://github.com/mkst/sm64-port/commit/5f0d3171fd1eee488f1e7d0b94f0f5f17918de17), extending it with Wii U rendering, per-map bounds, floor selection and game-state variants.

[SM64 Map Extractor](https://github.com/Takatoon/SM64-map-extractor) is an optional, separate generation tool. It renders maps from user-extracted game resources and includes geometry filtering rules adapted from [3DS Port Ultimate](https://github.com/Epic0522/Super-Mario-64-3ds-port---Ultimate/blob/ded2ad283290436687bc81cc4b52c9833e7b09c3/tools/minimap_extract_textured.py), including thresholds for removing overhead surfaces. The project also served as a technical and visual reference.

## Partial DynOS support and mod management

Compatible user-provided player and NPC packs can be selected through the MODS menu, together with texture packs. Apply Changes reloads the selected resources during gameplay. Hold Minus (-) and press D-pad Left/Right to cycle player models.

Support is partial. Compatible external Ogg Vorbis voices and static Character Select-style voice tables can follow the selected model; Lua code is not executed. Arbitrary skeletons and Character Select gameplay features are not supported. No model or voice packs are included.

Binary-format references include [Render96's reader](https://github.com/Render96/Render96ex/blob/f6e9e1b54f43b2caeffcf71644c6165f2652948c/data/dynos_gfx_load.cpp) and [CoopDX's binary compression implementation](https://github.com/coop-deluxe/sm64coopdx/blob/8cd6e5977d9f920d51ca71f2c61801d019ed79c6/data/dynos_bin_compress.cpp).

## Select any star

With cheats enabled, enable **Select any star** before entering a course's mission selector. All six mission stars become selectable, including missions not yet unlocked through normal progression. This changes mission selection without marking stars as collected. The 100-coin star remains separate from the six selectable missions.

## Camera fix

Fixed a vanilla-camera lock affecting Mario in Tall, Tall Mountain.
