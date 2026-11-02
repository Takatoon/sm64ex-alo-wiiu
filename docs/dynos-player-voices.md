# External DynOS player voices

DynOS-enabled Wii U builds (`DYNOS=1 EXTERNAL_DATA=1`, or `--dynos enabled`
with external textures enabled in the build wizard) can load player voices
without rebuilding the game. No separate `CUSTOM_PLAYER_VOICES` build option
is required. Voices follow the applied player model, including Minus + D-pad
Left/Right changes. Vanilla Mario and **Enable Mods = No** use native voices.

## Pack layout

Keep the Lua data beside the pack's `actors/` directory:

```text
dynos/packs/players/My Character/
    main.lua
    actors/mario_geo.bin
    sound/jump.ogg
    sound/hoo.ogg
    sound/star.ogg
```

With the default Aroma Beta 11 layout, `dynos/` is beside the WUHB under
`SD:/wiiu/apps/<name>/`. Legacy builds exclusively use
`SD:/sm64ex_res/dynos/`; `SD:/dynos/` is not searched. Player BINs directly in
the pack folder also work. Model ZIPs can use the same pack-relative layout;
put the ZIP in application-local `mods/`, or in `SD:/sm64ex_res/` for legacy
builds. See the [model pack guide](dynos-player-packs.md) for supported BIN names.
Voices are separate files and are not read from the model BIN.

## Static Lua data

For example, `main.lua` can contain:

```lua
local MODEL = smlua_model_util_get_id("mario_geo")
local VOICES = {
    [CHAR_SOUND_YAH_WAH_HOO] = { "jump.ogg", "jump.ogg", "hoo.ogg" },
    [CHAR_SOUND_HERE_WE_GO] = "star.ogg",
}
_G.charSelect.character_add_voice(MODEL, VOICES)
```

The reader extracts data; none of these calls or other Lua code is executed.
The model name must match the BIN basename without `.bin`. Explicit
`character_add_voice` associations allow different models in one pack to use
different voice tables. If there are no registrations, one unambiguous static
voice table can apply to the pack. Multiple unassociated tables or ambiguous
registrations fall back to native audio rather than guessing a character.

Supported values are literal filenames, arrays of up to 16 literal filenames,
simple variable aliases, and `audio_sample_load("filename.ogg")` with a static
filename. Both `filename.ogg` and `sound/filename.ogg` refer to the pack's
`sound/` folder. Filenames are preserved; no manual sound-ID mapping file or
filename convention is needed. Parent traversal and absolute paths are rejected.

Direct `.lua` files in the selected pack folder are read in alphabetical order,
with a maximum of 64 files and 1 MiB of relevant Lua text. Individual files
larger than 1 MiB are skipped with `DYNOS_LUA_SKIP`, so a large custom-animation
script does not block a smaller voice table. Files without model/character/voice
markers do not count toward the combined relevant-text limit. Subfolders are
not searched for scripts. Concatenation, computed values, helper functions that construct
voice tables, dynamically loaded scripts and Lua hooks are not evaluated.
Existing Character Select packs therefore work only when their relevant voice
mapping fits these static forms. AIFF, WAV, MP3 and Lua-defined playback behavior
are not supported by this reader.

Static `character_add`/`character_add_costume` records can discover custom BIN
names and retain the model's original ID for voice association. The model must
still pass the player's skeleton and callback checks. A Character Select
base-character override such as `CT_LUIGI` does not supply voice samples:
CoopDX's built-in character sound banks are not present in a model BIN and
are not automatically downloaded by this port.

## Playback and fallback

Native Mario voice events map to their `CHAR_SOUND_*` entries: jumps, punches,
damage, star collection, death, breathing, sleep, and the other original voice
events. The native menu's level-start voice uses `CHAR_SOUND_LETS_A_GO`.
Jump variants use their corresponding array positions (wrapping for shorter
arrays); other lists choose a variant. Missing, unsupported or corrupt entries
keep the original voice, including a missing individual array variant.

Only sounds selected by the native sound-priority system trigger replacements.
The native voice is suppressed for that event, while music, other effects and
Peach's speech retain their original sound path. Voices respect the master/SFX
volume controls, native distance gain, pan and pitch, mute, explicit sound stops
and interruptions by later voice events. One external player voice plays at a
time. Clips are not looped, and do not use the native room reverb; stereo clips
are downmixed to mono and panned using the native source position.

Ogg Vorbis `.ogg` files are supported (not Ogg Opus), with one or two channels
and rates from 8 to 96 kHz. Limits are 20 seconds per clip, 8 MiB per encoded
file, 128 unique clip paths, and 16 MiB of resident decoded PCM, also bounded
by the shared estimated DynOS resident budget. Duplicate references to a clip
share decoded samples. Failed clips fall back to native audio.

Clips are read and decoded when the player model is selected, not when it
jumps or attacks. Changing model or applying MODS settings stops the current
external voice and releases the previous voice set. Switching can briefly pause
while the next voice set is prepared. Restart after installing/changing packs.

Logs include `DYNOS_VOICES_LOADED`, `DYNOS_VOICE_REJECT` and
`DYNOS_VOICE_FILE_REJECT`. These report mapping/format/resource problems without
rejecting an otherwise compatible player model.

## Validation

Native tests generate their own Ogg Vorbis tones and exercise decoding, shared
sample storage, rate conversion, variants, missing/corrupt clips, native
fallbacks, volume, pan, pitch, clipping, mute, sound stops, interrupts and memory
release. Lua tests cover model associations, aliases, arrays, comments, dynamic
expressions, malformed data, ambiguity and unsafe filenames. Filesystem tests
cover script discovery and related files in loose packs/ZIPs and both storage
layouts. Host tests require `libvorbis-dev`; the Dockerfile includes it.

Wii U compilation is checked separately. Actual voice volume, timing and
controller transitions still require console listening tests; host audio tests
do not establish hardware sound quality.

The table conventions follow [Character Select's voice implementation](https://github.com/Squishy6094/character-select-coop/blob/main/voice.lua).
Ogg decoding uses the existing devkitPro port of
[Xiph.Org libvorbisfile](https://xiph.org/vorbis/doc/vorbisfile/).
