# Wii U storage layout

The default Wii U build keeps all mutable data beside the running WUHB or RPX:

```text
sd:/wiiu/apps/sm64/
├── sm64.wuhb
├── sm64config.txt
├── saves/
│   └── sm64_save_file.bin
└── mods/
    ├── texture-pack.zip
    └── translation.zip
```

The application obtains the executable path from Aroma through
`RPXLoader_GetPathOfRunningExecutable`. The directory is therefore not tied to
the WUHB filename or to `wiiu/apps/sm64`: moving the complete application
directory preserves the relationship between the executable and its data.

This mode requires Aroma Beta 11 or newer and a launch through
RPXLoadingModule. Path discovery is mandatory. If it fails, the application
shows an English fatal-error message and does not use an alternative location.
This prevents saves and configuration from silently becoming split across two
directories.

The WUHB's embedded resources remain read-only at:

```text
/vol/content/sm64ex_res/base.zip
```

External ZIPs and loose overrides are mounted from `mods/` with higher priority
than the embedded base resources.

`base.zip` is required and embedded only when building with
`EXTERNAL_DATA=1`. With `EXTERNAL_DATA=0`, all base resources remain linked
into the RPX, the packager creates a WUHB without a content directory, and no
`mods/` directory is staged or created at runtime.

## Legacy layout

Set `WIIU_LEGACY_PATHS=1` only when a build must retain the previous layout:

```text
sd:/sm64config.txt
sd:/sm64_save_file.bin
sd:/sm64ex_res/
```

For the Docker workflow:

```sh
WIIU_LEGACY_PATHS=1 docker compose -f docker-compose.wiiu.yml run --rm wiiu-dev
```

From Windows Command Prompt:

```bat
set WIIU_LEGACY_PATHS=1 && docker compose -f docker-compose.wiiu.yml run --rm wiiu-dev
```

No flag is needed for the default WUHB-adjacent layout.

## FTP-ready output

After packaging, the Docker build also creates a clean SD tree:

```text
dist/
└── wiiu/
    └── apps/
        └── <WUHB_BASENAME>/
            ├── <WUHB_BASENAME>.wuhb
            ├── mods/       # EXTERNAL_DATA=1 only
            └── saves/
```

Upload `dist/wiiu` to the SD root by FTP. Compiler intermediates remain under
`build/` and do not need to be transferred. The packaging script updates the
WUHB without deleting existing files from the staged `mods/` and `saves/`
directories.

`sm64config.txt` is intentionally not generated on the PC. The application
creates it with the current defaults on its first launch. The application also
creates `saves/` if it was omitted by the FTP client, and creates `mods/` when
the build uses `EXTERNAL_DATA=1`.

Legacy builds do not initialize `librpxloader` and do not require executable
path discovery. There is no runtime fallback between the two layouts.
