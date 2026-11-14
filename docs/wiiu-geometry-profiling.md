# Wii U geometry optimization trial (2026-11-14)

This trial targets sustained rendering of the external Mario Galaxy player
model. Exclude model changes, level loading and the first frames after either
from performance comparisons. Keep the level, player position, camera,
resolution, texture packs and other models unchanged.

## Baseline

The preceding `SM64 Geometry Profile` capture is saved locally in
`../sm64ex-alo/logs/wiiu/geometry-session-20261114.log`.
Static-camera Galaxy windows 11–17: median 51.31 FPS, 7.18 ms display-list
processing, including 1.36 ms vertex processing and 2.48 ms backend submission.
Galaxy did not fill the 256-triangle buffer. Backend submission measures the
whole call, including validation, copies and GX2 calls; it is not GPU time.

## Changes

- Prepare the aspect ratio, geometry flags and changed light directions once
  per vertex load. Preserve alternating light colors, integer accumulation,
  texture generation and fog behavior.
- On Wii U, reuse triangle material setup across triangle and vertex commands.
  This skips repeated shader information queries, render-state comparisons,
  texture/sampler checks and texture-dimension calculations.
- Invalidate material preparation on every other display-list command,
  including calls and returns. Start each `gfx_run` invalid. Rectangles also
  invalidate before and after temporarily overriding viewport/filter state.
  Texture-cache rollover still retries both texture bindings before publishing
  valid preparation. A failed import never marks preparation valid.
- Keep buffer capacity, draw order and GPU cache publication unchanged.

A proposed cache of projected vertex coordinates was **discarded**: host
`-ffast-math` comparison detected differences in face rejection. The existing
face-rejection arithmetic is retained.

## Verification

`tools/tests/check_wiiu_geometry.py` compiles the actual before/after vertex,
triangle and rectangle functions against a recording backend. It exercises
240 scenarios with lighting, generated UVs, fog, negative/zero W, front/back
culling, shader inputs, both texture units, texture-cache rollover, capacity
flushes, partial vertex reloads, material changes and rectangle overrides.

Inside the build container, using the local pre-change snapshot:

```sh
python3 tools/tests/check_wiiu_geometry.py --reference build/geometry-validation/gfx_pc_before.c
python3 tools/tests/check_wiiu_geometry.py --reference build/geometry-validation/gfx_pc_before.c --fast-math
```

Both comparisons pass with **byte-identical** recorded vertex attributes and
draw batches. Shader information queries fall from 101,342 to 706 (strict
math) and 101,364 to 706 (fast math). These are synthetic workload counts,
not predicted console speedups. This host test does not execute GX2 or the
full display-list interpreter; hardware validation is still required.

## Hardware comparison

Build title: `SM64 Geometry Optimized`.
Build name: `sm64-geometry-opt-20261114`.
Same options as baseline: 60 FPS, debug menu, DynOS, minimap and load timing.

`MODEL_GEOMETRY revision=2` identifies the new capture. Existing timings are
retained, plus `avg_material_prepares` per presented image. Compare at least
20 seconds with Galaxy and a static camera, then 20 seconds moving the camera.
Original Mario remains the control. Actual FPS improvement is pending this
console comparison.

## First optimized console capture

Confirmed the launch of `sm64-geometry-opt-20261114.wuhb` and revision 2 logs.
The receiver snapshot is saved locally as
`../sm64ex-alo/logs/wiiu/geometry-optimized-session-20261114.log`.

Galaxy windows 31–41 have the same reported player/camera coordinates
(-992,659,2189 / -828,776,2754), yaw 2956. Medians:

| Measurement | Value |
| --- | ---: |
| Presentation rate | 39.630 FPS |
| Total image interval | 24.988 ms |
| Vertex processing | 1.275 ms |
| Backend submission (included in display-list time) | 2.653 ms |
| Display-list processing | 7.206 ms |
| Finish-render wait | 9.047 ms |
| Presentation wait | 7.720 ms |
| Loaded vertex entries | 6,732 |
| Draw calls | 99 |
| Rendered triangles | 2,486 |
| Capacity flushes | 0 |
| Material preparations | 116 |

The preceding baseline static camera was at a different position and rendered
different scene geometry. Do not interpret the FPS difference as an optimization
gain or regression. Repeated ~33 ms spikes include ~15.5 ms presentation waits;
missing a 60 Hz refresh deadline is consistent with these measurements. The
finish-render timer measures waiting inside the backend, not an isolated GPU
execution query. Separating normal/interpolated images and backend work from
waiting would help explain the remaining deadline misses.

## Phase/backend measurement build

Title: `SM64 Galaxy Phase Profile`.
Build name: `sm64-galaxy-phases-20261114`.
`MODEL_GEOMETRY revision=3` identifies this instrumentation.

The first draw uses interpolated transforms and includes the game update/audio
work. `patch_interpolations()` then restores current transforms for the second
draw. Labels follow this actual order, not an assumption that interpolation is
drawn second. Non-HIGH_FPS builds label their single draw `current`.

Every 120 presented images the log also emits, for each phase:

- `MODEL_PHASE`: sample count, intervals >=25 ms, total, game/setup-before-run,
  display list, vertices, backend submission, audio, post-submit, finish and
  presentation times; maximum total/finish times; draws and triangles.
- `MODEL_GX2`: geometry validation, storage selection/allocation, CPU copy,
  attribute publication, attribute binding/draw command calls, final VBO cache
  publication, `GX2DrawDone`, VBO recycling, TV copy/flush, DRC preparation/copy,
  swap/context reset, presentation loop and video-mode handling. Also reports
  VBO bytes per image, allocations, submitted draws and vsync-wait call counts.

GX2 counters use the native clock and convert summed ticks only when reporting.
They measure CPU call durations, including any blocking inside the calls; they
are **not GPU timestamp queries**. DRC timing includes minimap preparation and
scan-copy submission. No new waits or GPU synchronization were added. Model or
level changes clear both phase accumulators. A dropped image clears the window.

The combined FPS calculation now uses the actual number of measured intervals:
subsequent windows contain 120 intervals, whereas the initial window contains
119. Earlier builds always used 119, underreporting steady rates by ~0.8%.
Do not attribute this reporting correction to faster rendering.

`python3 tools/tests/check_wiiu_geometry_profile.py` exercises the actual backend
functions against a fake clock and recording GX2 calls: timing partitions,
invalid batch rejection, publication before draw, aligned allocation and reuse
only after completion. The geometry equivalence check with `--fast-math` still
passes with byte-identical output. Actual console performance awaits a fresh
capture with Galaxy, a static camera and an angle below 60 FPS for 20–30 seconds.

## First phase capture results

Saved receiver snapshot:
`../sm64ex-alo/logs/wiiu/galaxy-phases-session-20261114.log`.
The tested model is choice 4 (Galaxy). Windows 27–44 have fixed reported player
and camera coordinates (-1328,260,4664 / -1387,364,5073), yaw -1500, and a
median 39.963 FPS. Per-phase medians:

| Measurement | Current | Interpolated |
| --- | ---: | ---: |
| Vertices | 1.306 ms | 1.319 ms |
| Whole display list | 7.833 ms | 7.864 ms |
| VBO memcpy (included above) | 2.618 ms | 2.617 ms |
| Attribute publication | 0.169 ms | 0.163 ms |
| Bind/draw command calls | 0.029 ms | 0.028 ms |
| DrawDone wait | 8.724 ms | 8.235 ms |
| DRC preparation/copy | 0.372 ms | 0.395 ms |
| VBO bytes/image | 374,033 | 374,008 |

There are no new allocations or capacity flushes in that stable range. Geometry
work is similar between phases; the first/interpolated phase additionally
includes update and audio work. No evidence of an exclusively expensive
interpolation path appears in these samples.

The user notes this castle view historically caused slowdowns, also with the
Sunshine model, before the conditional KILL_ENABLE fix reduced them. That fix
is still present in `gx2_shader_gen.c`. These samples do not isolate model GPU
cost from the scene: do not assign the entire DrawDone wait to the model or
infer a GPU regression from this capture alone.

An actionable next experiment is eliminating the intermediate geometry copy
by writing into backend-owned VBO storage. Preserve per-draw publication and
reuse only after completion, then compare in hardware: the measured memcpy
duration is a candidate saving, not a guaranteed FPS gain. No such direct-write
change is included in the phase measurement build.

## Direct VBO write trial

Per the user's request, this replaces the existing
`sm64-galaxy-phases-20261114.wuhb` under the same build folder and keeps the
title `SM64 Galaxy Phase Profile`. Revision **4** log records distinguish it
from the preceding measurement-only build (revision 3).

The frontend reserves aligned, backend-owned space only when starting an empty
batch, after all material/texture setup that could flush. It writes the same
attributes into this region. Submission recognizes this uncommitted region,
commits only the actual byte length and skips memcpy. Reserve capacity is the
existing maximum batch size; the triangle limit remains 256. Committed data is
never overwritten until `GX2DrawDone` completes, including when the block pool
grows. CPU attribute publication remains before every draw and at frame end.

If reservation fails, the frontend retains the original CPU staging/copy path.
A failed large reservation restores the block cursor so a smaller fallback
batch can still use available storage. Other rendering backends retain their
existing path. No normal/interpolated frames were combined and no waits were
removed.

`MODEL_GX2` additionally reports `direct_draws` and `copied_draws` for the whole
phase sample. Reservation time is included in storage counters and whole-DL
time, even though it now occurs before submission. Compare whole-DL and total
times, not only submission or memcpy time: moving work must not be mistaken
for removing it.

Validation passes:

- Actual frontend before/after functions produce byte-identical recorded
  geometry across 240 scenarios, including reservation failures.
- Actual backend functions pass address/undefined-behavior sanitizers with
  direct and copied batches, partial commits, alignment, malformed inputs,
  block-pool growth, failed allocations, publication ordering, preservation of
  in-flight data and reuse after completion.

Next hardware test: Galaxy, the same expensive castle view, 20–30 seconds
without moving the camera. A faster CPU submission path does not remove the
castle scene's GPU cost or guarantee 60 FPS.

## First direct-write capture

Saved locally as `../sm64ex-alo/logs/wiiu/galaxy-direct-session-20261114.log`.
Revision 4 confirms the new path. Stable Galaxy windows 17–29 report player
(-1194,260,4028), camera (-1378,316,4339), yaw -5559, and median 39.963 FPS.
Across these windows: 214,514 direct draws and zero copied draws. No new
allocations appear in that range; memcpy time is zero.

| Median per image | Current | Interpolated |
| --- | ---: | ---: |
| Vertex processing | 1.318 ms | 1.323 ms |
| Submission call | 0.276 ms | 0.281 ms |
| Whole display list | 7.806 ms | 7.796 ms |
| Finish-render wait | 8.965 ms | 8.420 ms |
| Presentation wait | 14.779 ms | 0.002 ms |
| Rendered triangles | 2,861 | 2,858 |

The copy has been eliminated, but this capture does not establish an FPS gain.
The preceding stable capture used a different camera/player position and
rendered less geometry (~374 KB vs ~400 KB per image). The residual display-list
time is now ~6.21 ms, so a smaller submission timer alone must not be reported
as an equivalent whole-frame saving. Writing directly may move memory costs
into attribute emission, but that is a hypothesis, not an isolated measurement.
Both presentation phases miss the pair's 60 Hz budget; neither phase's ~18 ms
or ~32 ms CPU duration is itself the screen's refresh interval.

Cross-compiled before/after objects were inspected in the scratch validation
folder. Whole-function instruction counts do not show an increase (1,031 vs
1,011 for gfx_sp_tri1); this does not measure dynamic instructions, cache misses
or hardware performance. No compiler-overhead cause is established.

Useful next work: controlled copy/direct A/B at the same camera without a
restart, and investigate attribute packing/memory writes while retaining the
known castle GPU cost as a separate factor. No new game build was produced
during this capture analysis.

## Automatic same-camera A/B build

Revision **5**, same WUHB filename and title as revisions 3/4. The test is
enabled only in the existing external-data Wii U timing-profile build.

Once gameplay has a player, with no pause/options menu or dialog, the controller
waits for 3 seconds of stable scene conditions. It then runs four 20-second
blocks: **copy, direct, copy, direct**. The first second of each block is
excluded. Method changes happen between completed simulation/render pairs, so
current and interpolated images within a pair always use the same method.
GPU synchronization, batch capacity, interpolation, shader code and drawing
order stay the same between methods.

Moving Mario >4 units on any axis, camera/focus >16 units, or camera yaw >128
angle units relative to the anchor restarts stabilization. Level, area, model,
dimensions, GamePad mode or filtering changes also reset the run. Menus,
dialogs, a >0.5-second gap (e.g. HOME), or a backwards clock invalidate an
active run. After all four blocks it stops measuring and leaves direct writes
enabled. Moving to a new scene starts a new run after stabilization. A reset
after an already completed run does not invalidate that completed result.

Each `MODEL_AB_WINDOW` maps the normal 120-image reports to a unique run,
one-based block and `method=copy|direct`. No report combines methods, warmup
images or scene resets. `block_end` reports accepted frames, full reported
windows and the leftover partial window, which is discarded. `event=complete`
identifies a full four-block comparison; unfinished/reset runs should be
excluded from paired comparisons.

Keep Mario and the camera still for **about 90 seconds** after finding the
desired view. Both capped-60-FPS scenes and slower scenes are useful: compare
whole preparation/finish times and not only the capped presentation rate.
The logger identifies the individual method; no user action is needed to
toggle it. The game continues its normal animations during the test.

`check_wiiu_geometry_ab.py` passes address/undefined-behavior sanitizers for
settling, all four deadlines, exact initial sample exclusion, completion,
scene/menu resets, long gaps and clock rollback. The geometry equivalence
test with fast math also still passes.

## First completed A/B result

Snapshot: `../sm64ex-alo/logs/wiiu/galaxy-ab-session-20261114.log`.
Run 4 completed all four blocks (`event=complete`), Galaxy, level 6 area 1,
1920x1080, player (-946,0,1902), camera (-946,172,2157). Each block has nine
full 120-image reports after warmup (1,080 images). No invalid geometry reports
were found in the capture.

Compare adjacent pairs because visible triangle count changed over time even
with fixed reported camera/player coordinates:

| Median whole-DL preparation/submission | Copy | Direct | Reduction |
| --- | ---: | ---: | ---: |
| Blocks 1/2 (~2,520 triangles/image) | 7.332 ms | 6.952 ms | 0.380 ms / 5.18% |
| Blocks 3/4 (~1,946 triangles/image) | 6.121 ms | 5.835 ms | 0.286 ms / 4.67% |

Both methods remain at the display cap (~59.44 reported FPS with logging
overhead), as expected in this lighter scene. Vertex processing stays around
1.42 ms. Copy/direct path counters confirm the correct method in each block;
there are no new allocations or triangle-capacity flushes. Memcpy disappears,
but the **net** whole-DL benefit is ~0.3–0.4 ms, not the entire old memcpy timer
(~1.9–2.5 ms). Do not equate the removed copy timer with a full-frame saving.

Exterior runs 1/2 had complete copy blocks followed by partial direct blocks
with higher observed presentation rates, but both runs reset before completing
the four-block protocol. Run 3 has only a partial copy block. These are
exploratory evidence, not a confirmed exterior FPS comparison. A new exterior
run can use the same WUHB and controller; no additional build is required.

## Sleep interrupted the exterior comparisons

Snapshot: `../sm64ex-alo/logs/wiiu/galaxy-ab-sleep-session-20261114.log`.
The user observed Mario falling asleep and the camera moving. Exterior runs
5–7 reset before completing the four-block comparison. In run 7 the player
position remains (-624,801,1251) but a later anchor changes from camera
(-727,940,1697) to (-687,934,1527). The detector correctly avoids continuing
the same comparison across that view change.

Exploratory adjacent copy/direct block medians: run 6, 8.244 -> 7.776 ms whole
DL; run 7, 8.327 -> 7.851 ms. Both stay around 39.96 FPS. This is consistent
with a ~5.7% preparation reduction but is not a completed four-block exterior
result. Run 5 has no measured direct block and cannot be paired.

The same-name A/B build now marks block starts `keep_awake=1`. Only in Wii U
external-data timing-profile builds, `act_idle` resets the sleep cycle timer
while the benchmark is initialized (including stabilization and completed
state). Normal head-turn animations and real input/gameplay cancellations
continue. No controller input is synthesized. Remaining awake after completion
prevents sleep from changing the view and spuriously restarting a finished run.
Other builds keep the original sleep behavior.

The controller test now also compiles and executes the actual `act_idle`
function with and without the profile defines. It verifies 10,000 idle steps
without sleeping during the benchmark, all three head-turn animations,
unchanged input/low-health cancellations and normal sleeping outside the
benchmark. Address/undefined-behavior sanitizers pass.

## Completed initial-camera comparison with sleep disabled

Snapshot: `../sm64ex-alo/logs/wiiu/galaxy-ab-awake-session-20261114.log`.
Parsed results: `../sm64ex-alo/logs/wiiu/galaxy-ab-awake-analysis-20261114.json`.
The latest launch has `keep_awake=1` and reaches `event=complete run=1 blocks=4`
at console uptime 00:03:19.091 without a reset. Galaxy, exterior castle,
1920x1080; all full windows report the same player (-1328,260,4664) and camera
(-1328,521,5354), yaw 0. Roughly 2,560 triangles/image remain visible throughout.

| Block | Method | Full reports | Median FPS | FPS across reported intervals | Median whole DL |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | Copy | 7 | 46.405 | 46.137 | 7.594 ms |
| 2 | Direct | 9 | 59.444 | 59.172 | 7.250 ms |
| 3 | Copy | 7 | 46.405 | 46.010 | 7.584 ms |
| 4 | Direct | 9 | 59.444 | 59.499 | 7.255 ms |

Each report spans 120 images. Across-report FPS combines equal-image windows
using their interval durations, rather than taking an arithmetic rate average.
Both direct blocks are near the 60 Hz presentation cap; the adjacent copy
blocks remain around 46 FPS. The reversal repeats in the same scene, unlike
the preceding comparisons between different views. This is positive evidence
for retaining direct writes in this tested workload, not a universal 60 FPS
guarantee for every view or model.

Whole-DL savings are 0.344 ms (4.53%) and 0.329 ms (4.34%). The larger FPS change
is consistent with crossing refresh deadlines: presentation waits in the
current phase fall from roughly 9.2 ms with copies to roughly 1.1 ms direct.
Do not describe it as a 28% reduction in geometry or total GPU work. Vertex
processing remains ~1.3 ms, and an isolated GPU duration was not measured.

All 209,416 submitted draws in copy blocks use copies; all 269,228 draws in
direct blocks use direct storage. No allocations or invalid-geometry/exception
reports appear in the analyzed run. Normal gameplay builds should remove the
automatic A/B toggling while retaining direct writes, since this experimental
build restarts comparisons after moving to a new scene.

## Local attribute writer experiment

Revision **6**, `experiment=local_writer`; same WUHB name/title as before.
Both A/B sides now **always use direct VBO storage**. Blocks 1/3 report
`method=baseline` (the preceding direct-write implementation); blocks 2/4
report `method=local_writer`.

The experimental writer advances a local float pointer while assembling a
triangle and updates the global batch length once at the end. It makes no
floating-point or material changes. The original and experimental writers
include the same `gfx_triangle_emit.inc.h` attribute body with different store
expressions. The profiling build keeps both paths for a runtime comparison;
non-profile Wii U builds compile only the local writer after dead-code
elimination. Other backends use the original expression.

No `restrict` promise is used in this trial. Controller deadlines, initial
sample exclusion, camera checks and prevention of sleeping are unchanged.
Retain the direct/copy counters as a check that both sides really use direct
storage; a fallback allocation failure can still select the existing copy path.

Verification before hardware testing:

- Both writers match the saved pre-change actual renderer byte-for-byte over
  240 geometry/material scenarios with the Wii U fast-math compiler option.
- Both pass address/undefined-behavior checks against the same reference.
- The four-block controller and actual idle/sleep behavior checks pass.
- Host sanitizer tests use non-PIE executables to avoid their container startup
  failures; this does not change the Wii U compilation flags.

The preceding WUHB was retained locally as
`build/geometry-validation/galaxy-ab-awake-before-local.wuhb` and the reference
source as `build/geometry-validation/gfx_pc_before_local_writer.c`.
No hardware speedup is claimed until a completed same-camera revision-6 run
is captured. Test for about 90 seconds without moving or pausing as before.

## Local writer hardware result

Snapshot `../sm64ex-alo/logs/wiiu/galaxy-local-writer-session-20261114.log`,
analysis `../sm64ex-alo/logs/wiiu/galaxy-local-writer-analysis-20261114.json`.
Revision 6 run 3 completed all four blocks without a reset. Galaxy, 1920x1080,
player (-1280,260,4511), reported camera (-1394,319,4858). Each block has six
full 120-image reports. Geometry counts stay essentially equal (~2,660
triangles and 7,014 loaded vertex entries per image).

| Block | Writer | Median whole DL | Median FPS |
| --- | --- | ---: | ---: |
| 1 | Baseline direct | 7.5245 ms | 39.9635 |
| 2 | Local cursor | 7.4945 ms | 39.9600 |
| 3 | Baseline direct | 7.5190 ms | 39.9605 |
| 4 | Local cursor | 7.4935 ms | 39.9600 |

The 0.0255–0.0300 ms whole-DL difference (~0.34–0.40%) is very small and has no
practical presentation-rate effect. Do not claim a meaningful performance gain
from this experiment or extrapolate it to faster models/views. Finish-render
waiting remains ~8.9 ms combined, vertex work ~1.3 ms, and backend submission
~0.27 ms. All 411,704 reported draws use direct storage, with zero copied draws
or new allocations. This is separate from the earlier confirmed direct-storage
benefit; the copy-removal optimization remains supported by that earlier test.

The local-cursor idea should not remain a performance priority. A subsequent
experiment can target repeated material-attribute preparation (e.g. uniform
color conversion), with its own correctness and same-camera A/B checks.

## Prepared material attributes experiment

Revision **7**, `experiment=prepared_material`; same WUHB filename/title.
The four blocks compare **baseline direct storage** (1/3) and **prepared
material constants with direct storage** (2/4). The local-cursor experiment is
removed from the active paths: both now use the same global batch-length store
expression, so this comparison isolates material preparation.

When the existing conservative material cache is rebuilt, its attribute cache
is invalidated. The experimental path lazily prepares RGB/alpha mappings and
normalizes primitive/environment colors and fog RGB once per material state.
Per-vertex shade colors and fog factors remain dynamic. LOD is evaluated once
per triangle only when a shader input uses it; its original intermediate
8-bit quantization is preserved. Zero/default input mappings retain zero.

This avoids choosing a temporary color pointer and converting uniform colors
again for each vertex. Material changes (including same-combiner color changes),
rectangles, frame changes and texture binding retries still use the existing
invalidation rules. Positions, texture coordinates, stride, batch size, GPU
cache publication, interpolation and synchronization are unchanged.

`avg_uniform_prepares` in `MODEL_GEOMETRY` counts actual attribute preparations
per image; baseline should show zero. Their cost is included in whole-DL time,
not hidden outside the measurement. Both paths still expose direct/copy draw
counters as a storage-path check.

Verification compares actual functions against the saved original direct
renderer across 240 scenarios with all input mapping kinds, input counts 0–4,
RGB/alpha variants, lighting, fog, generated UVs, near/mid/far LOD, partial vertex
loads, cache rollover, full batches and rectangles. Primitive, environment,
fog and combiner changes are made inside each draw sequence, including a
mid-sequence fog toggle. Baseline and prepared outputs are byte-identical with
Wii U fast-math settings; prepared output also matches under address/UB
sanitizers. Controller/awake behavior tests pass. No hardware gain is claimed
before capturing a completed revision-7 comparison.

Previous local-writer WUHB retained at
`build/geometry-validation/galaxy-local-writer-before-material.wuhb`.
For the test keep Galaxy and the camera still ~90 seconds, preferably in the
same slower view; all four 20-second blocks and warmup exclusion remain active.

## Prepared-material hardware results

Snapshot `../sm64ex-alo/logs/wiiu/galaxy-prepared-material-session-20261114.log`,
parsed blocks `../sm64ex-alo/logs/wiiu/galaxy-prepared-material-analysis-20261114.json`.
Revision 7 runs 1 and 2 both complete all four blocks. The scene stays fixed
within each run; both test Galaxy at 1920x1080. All reported draws use direct
storage. `avg_uniform_prepares` is zero on baseline and ~136/image in the
initial view, ~185/image in the slower view, confirming the preparation path.

| View / pair | Whole DL baseline -> prepared | Median FPS baseline -> prepared |
| --- | --- | --- |
| Initial, blocks 1/2 | 7.256 -> 7.164 ms | 57.542 -> 59.444 |
| Initial, blocks 3/4 | 7.263 -> 7.166 ms | 57.074 -> 59.444 |
| Slow, blocks 1/2 | 7.582 -> 7.509 ms | 39.960 -> 39.960 |
| Slow, blocks 3/4 | 7.589 -> 7.499 ms | 39.960 -> 39.960 |

The initial view reports player (-1328,260,4664), camera (-1328,521,5354),
~2,562 triangles/image. The slower view reports the same player, camera
(-1477,356,5044), ~2,682 triangles/image. There are nine full reports in each
initial-view block except block 3 (eight); six in each slower-view block.
Across-report interval rates in the initial view are 57.284/59.281 and
56.577/59.281 FPS, confirming a small capped-rate benefit in both pairs.

The net preparation savings are ~0.07–0.10 ms (~1–1.3%), not a large speedup.
Finish-render waiting in the slow view stays ~8.76–8.79 ms and its presentation
rate does not change. These results support at most a modest preparation
improvement; this experiment does not solve the 40 FPS view. Avoid comparing
absolute FPS with preceding builds because this binary has different A/B code
layout and instrumentation. The paired within-build comparison is the control.

## RGBA8 color format experiment

Revision **8**, `experiment=packed_colors`. The same-name WUHB now compares
`float_colors` against `rgba8`, twice each, on the existing four-block schedule.
Both sides keep direct VBO writes and prepared material attributes. Position
and padded UV fields stay unchanged; fog and color inputs use four bytes in
the compact layout. No inverse conversion from normalized floats is used:
the original shade/primitive/environment bytes and quantized LOD are emitted.

Each shader group has two immutable fetch programs sharing its existing vertex
and pixel programs. The compact fetch uses UNORM_8_8_8_8 without endian swap for
explicit RGBA byte stores. The frontend invalidates its bound shader pointer
when the selected layout changes, so normal material binding selects the
matching fetch program in an acquired foreground context. VBO byte lengths and
validation strides use the selected layout. The GamePad's own float renderer
keeps using its original fetch program. Both fetch allocations are released
and allocation failures follow existing cleanup paths.

CPU checks pass: 240 real-frontend scenarios decode to byte-identical normalized
attributes against the committed baseline, with fast math and sanitizers;
40 combinations of fetch-layout metadata and all 256 byte values pass;
RGBA words whose float bit patterns are NaNs are correctly treated as colors,
not invalid positions; VBO lifetime, texture-cache rollover, foreground/shutdown
and A/B/awake tests pass. Historical optimization commits were syntax-checked
with the actual PowerPC compiler. The current full Wii U build links and
packages successfully.

This is an **experiment pending hardware visual and performance validation**.
No RGBA8 speedup or pixel-exact GPU output is claimed yet. Non-profile builds
default to float colors. See [the status and next steps](wiiu-optimization-status.md)
for the commit breakdown, measured improvements and deferred ideas.

## First RGBA8 hardware capture

Snapshot `../sm64ex-alo/logs/wiiu/galaxy-packed-colors-session-20261114.log`,
parsed results `../sm64ex-alo/logs/wiiu/galaxy-packed-colors-analysis-20261114.json`.
The user reports no visible color change in the tested scenes. This is initial
visual feedback, not exhaustive validation of every material/scene.

Run 1 completes all four blocks in the fixed initial exterior view. Median
whole-DL times are 7.223/6.201/7.230/6.210 ms (float/RGBA8/float/RGBA8).
This is a repeated ~1.02 ms / 14.1% reduction. Vertex bytes per image decrease
from ~363 KB to ~273 KB, approximately 25%, while triangle counts stay ~2,560.
FPS across reported intervals is 58.214/59.499/58.213/59.499, near the display cap.

Run 2 tests a slower fixed view (player -1016,313,3185; reported camera
-1244,410,3672). Its first pair completes: 7.177 -> 6.172 ms whole DL and
40.055 -> 55.166 FPS across measured intervals. Counts stay ~2,527 triangles;
VBO bytes fall from ~357 KB to ~268 KB. The next float block also completes
(7.175 ms / 39.980 FPS), followed by a partial RGBA8 block (6.173 ms /
54.983 FPS), but a scene reset interrupts that fourth block at 13.9 seconds.
Treat this second pair as supporting partial evidence, not a completed second
four-block comparison. A third run starts after the camera/player move.

No invalid-geometry/exception reports or new VBO allocations appear in the
analyzed blocks. Direct storage is used throughout. Finish-render waiting in
the slower view remains around 9 ms; do not interpret its duration as isolated
GPU time. The tested slower view improves substantially but is not locked to
60 FPS. Further scene/model/transparency/fog visual checks remain appropriate
before making compact colors the non-profile default.

## Manual visual comparison controls

Revision **9** defaults to manual control in the timing-profile build. The TV
HUD displays F (float) or R (RGBA8), regardless of the FPS setting. The options
menu redraws this marker after the cached-background boundary, including when
the FPS counter is disabled or still initializing its first timing sample.

Tap/release Minus to toggle; holding it does not repeat. Minus plus left/right
remains the DynOS model shortcut and suppresses the color toggle. Physical
input is collected before DynOS consumes the D-pad chord on GamePad, Pro,
Classic and Wii Remote/Nunchuk. Missing/disconnected devices do not synthesize
a release toggle. Simultaneous device toggles are merged into one action.

Controller events queue the request, which is applied at the next completed
simulation/render-pair boundary. The HUD reports the applied backend format,
not a pending request. Start in RGBA8; retain the selection across scene/menu
changes. There is no automatic restart or timeout. `MODEL_COLOR` records each
selection and `MODEL_COLOR_WINDOW` identifies collected measurement windows;
changing format clears accumulated samples so methods are not mixed.

The previous automated protocol is retained behind the explicit compile-time
`WIIU_GEOMETRY_AUTO_AB` define. No such define is used for this visual-test WUHB.
The standalone manual-control tests exercise actual selector functions, held
keys/chords/disconnects, no timer-driven changes and pair-boundary application.
Actual overlay functions are tested with FPS disabled, a warmup FPS sample and
the cached options background. Geometry equivalence and cache rollover checks
also pass. The native Wii U build compiles and packages with the same filename.
