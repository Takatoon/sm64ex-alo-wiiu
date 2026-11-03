# Minimap extractor

[SM64 Map Extractor](https://github.com/Takatoon/SM64-map-extractor) generates
minimap images from SM64 decompilation data and assets extracted from a
user-supplied ROM. Its renderer, map configuration, effects and Docker
environment are maintained in that repository.

This repository references the extractor at `tools/map-extractor` as a Git
submodule. The initial reference is commit
`28f0367e819ff545133628dc9a2337a362d39824`, with 30 level configurations
covering 64 maps. Git records the exact extractor commit with each game commit.

## Download

From the game repository root:

```sh
git submodule update --init tools/map-extractor
```

A new clone can include the submodule with:

```sh
git clone --recurse-submodules https://github.com/Takatoon/sm64ex-alo-wiiu.git
```

GitHub's source ZIP does not include the submodule contents. ZIP users can
download the [initially pinned extractor archive](https://github.com/Takatoon/SM64-map-extractor/archive/28f0367e819ff545133628dc9a2337a362d39824.zip)
and extract its contents into `tools/map-extractor`. For later game versions,
use the extractor commit linked by that version's submodule entry on GitHub.

## Local configuration

Copy `tools/map-extractor/config.toml.example` to
`tools/map-extractor/config.toml`. Set the Blender executable path and change
the source path for this nested directory:

```toml
[paths]
blender = "C:/path/to/blender.exe"
sm64_source = "../.."
rom = "roms/baserom.us.z64"
output = "output"
```

Keep the other sections from the example file. Paths are resolved relative to
the configuration file. Place your ROM at
`tools/map-extractor/roms/baserom.us.z64`, then run from the game repository root
using Python 3.11 or newer:

```sh
python tools/map-extractor/sm64_map_extractor.py validate
python tools/map-extractor/sm64_map_extractor.py all
```

The first command validates the map recipes without Blender or a ROM. The
second extracts assets and renders the maps. Images are written to
`tools/map-extractor/output/`. ROMs, local configuration and generated images
are ignored by Git. See the [extractor README](../tools/map-extractor/README.md)
for dependencies and rendering options.

## Docker

The extractor has a separate Docker environment that installs Blender and
its dependencies. From the game repository root in PowerShell:

```powershell
$env:SM64_SOURCE = (Get-Location).Path
docker compose -f tools/map-extractor/docker-compose.yml run --build --rm extractor
```

In a POSIX shell:

```sh
SM64_SOURCE="$PWD" docker compose -f tools/map-extractor/docker-compose.yml run --build --rm extractor
```

Place the ROM in the same extractor `roms/` directory described above. Docker
uses `config.docker.toml`; a local Blender installation is not required.

## Updating the reference

After publishing and reviewing a new extractor commit:

```sh
git -C tools/map-extractor fetch origin
git -C tools/map-extractor checkout --detach <reviewed-commit>
git add tools/map-extractor
git commit -m "Update minimap extractor reference"
```

For a release checkout, use `git submodule update --init` to restore the
recorded version. Avoid `--remote` when reproducing a release, because that
selects a newer upstream version instead of the recorded commit.

This reference makes the extractor available to developers. Automatic map
generation, packaging and the GamePad runtime integration are separate work;
the normal Wii U build currently does not invoke this tool.
