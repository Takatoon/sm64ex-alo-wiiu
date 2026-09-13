# Wii U load-time profiling

The Wii U build contains optional instrumentation for measuring external texture
loading, level transitions, save writes, and frame costs in Vanish Cap Under the
Moat (VCutM). It is disabled by default and is not present in normal release
builds.

## Build a profiling version

The profiler requires both the Wii U target and external resources. With the
Docker workflow, enable it for one build from PowerShell:

```powershell
$env:WIIU_LOAD_TIMING_PROFILE = "1"
docker compose -f docker-compose.wiiu.yml run --rm --no-deps wiiu-dev bash docker/wiiu/build.sh
Remove-Item Env:WIIU_LOAD_TIMING_PROFILE
```

For a direct Make build, add `WIIU_LOAD_TIMING_PROFILE=1` together with
`TARGET_WII_U=1 EXTERNAL_DATA=1`.

The build stops with an error if profiling is requested for another target or
without external resources.

## Capture logs

Start the TCP logger before launching the profiling WUHB:

```powershell
.\tools\wiiu-tcp-log.ps1 -WiiUAddress "192.168.4.23" -BuildName "load-profile"
```

Replace the address with the current address of the console. Stop the logger
with Ctrl+C after returning to a stable game screen; exiting to the Wii U Menu
is not required.

Profiling records use the `LOADTIME_` prefix. They cover:

- full or selective startup precaching;
- per-level preload, initialization, and first-visible-frame timing;
- external textures loaded on demand, split into read, PNG decode, and GPU
  upload time;
- save-file open, write, and close time;
- texture misses during the first 180 frames after Save and Continue;
- 120-frame VCutM samples with frame-stage, draw-call, triangle, object, camera,
  and position data.

## Summarize a capture

Pass one or more saved log files to the included analyzer:

```powershell
docker compose -f docker-compose.wiiu.yml run --rm --no-deps wiiu-dev python3 tools/analyze_wiiu_load_times.py logs/wiiu/capture.log
```

The script can also be run directly with Python 3 when it is installed on the
host.

Passing one `precache=false` capture and one `precache=true` capture also emits
a per-level comparison table.

## Runtime cost

Profiling builds call the Wii U timer frequently and write detailed records to
the network logger. This can materially affect frame pacing, especially when a
texture is loaded on demand. Use these builds only for measurement; performance
and gameplay testing should use the default release configuration.
