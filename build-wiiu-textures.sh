#!/usr/bin/env bash
set -euo pipefail

mode=""
input_pack=""
profile=""
version=""
fps=""
jobs=""
build_name=""
build_title=""
external_textures=""
legacy_paths=""
base_zip_input=""
assume_yes=0
temporary_build_log=""
temporary_rom=""

cleanup() {
    if [[ -n "$temporary_build_log" && -f "$temporary_build_log" ]]; then
        mkdir -p build/dist/logs
        mv -f -- "$temporary_build_log" build/dist/logs/build.log
    fi
    if [[ -n "$temporary_rom" ]]; then rm -f -- "$temporary_rom"; fi
}
trap cleanup EXIT

usage() {
    cat <<'EOF'
Usage: ./build-wiiu-textures.sh [options]

Without options, the interactive wizard is shown. Automation options:
  --game-only              compile the Wii U game only
  --textures-only          create a converted texture pack only
  --game-and-textures      compile the game and create the texture pack
  --build-game             alias for --game-and-textures
  --input PATH             texture pack ZIP inside the repository
  --profile PROFILE        full-720, full-720-hd-hud, half-source,
                           quarter-source, max-6x, or clean-ex-alo
  --base-zip PATH          prepared game data for --textures-only
  --version REGION         optional ROM region filter: us, eu, jp, sh, or cn
  --fps FPS                30 or 60
  --external-textures BOOL enabled or disabled (game-only mode)
  --legacy-paths BOOL      use SD-root settings, saves, and texture paths
  --jobs NUMBER            parallel build jobs (1-64; detected automatically)
  --name NAME              application directory and WUHB file name
  --title TITLE            title displayed by Aroma
  --yes                    skip final confirmation
  --help                   show this help
EOF
}

set_mode() {
    local requested="$1"
    if [[ -n "$mode" && "$mode" != "$requested" ]]; then
        echo 'Only one output mode can be selected.' >&2
        exit 2
    fi
    mode="$requested"
}

normalize_toggle() {
    case "${1,,}" in
        1|yes|y|on|true|enabled) printf '1' ;;
        0|no|n|off|false|disabled) printf '0' ;;
        *) return 1 ;;
    esac
}

while (($#)); do
    case "$1" in
        --game-only) set_mode game; shift ;;
        --textures-only) set_mode textures; shift ;;
        --game-and-textures|--build-game) set_mode both; shift ;;
        --input) input_pack="${2:?Missing value for --input}"; shift 2 ;;
        --profile) profile="${2:?Missing value for --profile}"; shift 2 ;;
        --base-zip) base_zip_input="${2:?Missing value for --base-zip}"; shift 2 ;;
        --version) version="${2:?Missing value for --version}"; shift 2 ;;
        --fps) fps="${2:?Missing value for --fps}"; shift 2 ;;
        --external-textures)
            external_textures="$(normalize_toggle "${2:?Missing value for --external-textures}")" || {
                echo 'External textures must be enabled or disabled.' >&2
                exit 2
            }
            shift 2
            ;;
        --legacy-paths)
            legacy_paths="$(normalize_toggle "${2:?Missing value for --legacy-paths}")" || {
                echo 'Legacy paths must be enabled or disabled.' >&2
                exit 2
            }
            shift 2
            ;;
        --jobs) jobs="${2:?Missing value for --jobs}"; shift 2 ;;
        --name) build_name="${2:?Missing value for --name}"; shift 2 ;;
        --title) build_title="${2:?Missing value for --title}"; shift 2 ;;
        --yes) assume_yes=1; shift ;;
        --help|-h) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

choose() {
    local prompt="$1"
    local default_index="$2"
    shift 2
    local options=("$@")
    local answer

    printf '\n' >&2
    echo "$prompt" >&2
    local index
    for ((index = 0; index < ${#options[@]}; index += 2)); do
        printf '  %d. %s\n' "$((index / 2 + 1))" "${options[index]}" >&2
    done
    while true; do
        read -r -p "Selection [${default_index}]: " answer
        answer="${answer:-$default_index}"
        if [[ "$answer" =~ ^[0-9]+$ ]] && ((answer >= 1 && answer <= ${#options[@]} / 2)); then
            printf '%s' "${options[(answer - 1) * 2 + 1]}"
            return
        fi
        echo 'Invalid selection.' >&2
    done
}

read_default() {
    local prompt="$1"
    local default_value="$2"
    local answer
    read -r -p "$prompt [$default_value]: " answer
    printf '%s' "${answer:-$default_value}"
}

safe_name() {
    local value="$1"
    value="$(printf '%s' "$value" | sed -E 's/[^A-Za-z0-9._-]+/-/g; s/^[-.]+//; s/[-.]+$//')"
    printf '%s' "${value:-sm64ex-alo}"
}

run_quiet() {
    local label="$1"
    local log_file="$2"
    shift 2
    local frames=('|' '/' '-' '\\')
    local frame=0
    local status

    mkdir -p "$(dirname "$log_file")"
    "$@" >"$log_file" 2>&1 &
    local command_pid=$!

    if [[ -t 1 ]]; then
        while kill -0 "$command_pid" 2>/dev/null; do
            printf '\r  [%s] %s' "${frames[frame % ${#frames[@]}]}" "$label"
            frame=$((frame + 1))
            sleep 0.2
        done
        printf '\r'
    else
        echo "  $label..."
    fi

    if wait "$command_pid"; then status=0; else status=$?; fi
    if ((status == 0)); then
        printf '  [OK] %s\n' "$label"
        return 0
    fi

    printf '  [FAILED] %s\n' "$label" >&2
    echo "  Last log lines ($log_file):" >&2
    tail -n 40 "$log_file" >&2
    return "$status"
}

echo '=== SM64 EX-ALO Wii U Builder ==='

if [[ -z "$mode" ]]; then
    mode="$(choose 'What do you want to create?' 3 \
        'Wii U game only' 'game' \
        'Texture pack only' 'textures' \
        'Wii U game + texture pack (recommended for first use)' 'both')"
fi

base_zip=""
if [[ "$mode" == textures ]]; then
    if [[ -n "$base_zip_input" ]]; then
        base_zip="$base_zip_input"
    else
        mapfile -t cached_bases < <(find user-assets/base -maxdepth 1 -type f -iname '*.zip' -print 2>/dev/null | sort)
        if ((${#cached_bases[@]} == 0)); then
            mapfile -t cached_bases < <(find build -path '*/sm64ex_res/base.zip' -type f -print 2>/dev/null | sort)
        fi

        if ((${#cached_bases[@]} == 0)); then
            echo
            echo 'Texture conversion requires data prepared during a game build.'
            if [[ -t 0 && "$assume_yes" == 0 ]]; then
                missing_data_action="$(choose 'How do you want to continue?' 1 \
                    'Build the game and texture pack now' 'build' \
                    'Select existing game data' 'select' \
                    'Cancel' 'cancel')"
                case "$missing_data_action" in
                    build) mode=both ;;
                    select) read -r -p 'Game data ZIP path: ' base_zip ;;
                    cancel) echo 'Cancelled.'; exit 0 ;;
                esac
            else
                echo 'Build the game once or provide prepared data with --base-zip.' >&2
                exit 7
            fi
        elif ((${#cached_bases[@]} == 1)); then
            base_zip="${cached_bases[0]}"
        else
            base_options=()
            for cached_base in "${cached_bases[@]}"; do
                base_options+=("$(basename "$cached_base")" "$cached_base")
            done
            base_options+=('Select another game data ZIP' '__custom__')
            base_zip="$(choose 'Prepared game data:' 1 "${base_options[@]}")"
            if [[ "$base_zip" == '__custom__' ]]; then
                read -r -p 'Game data ZIP path: ' base_zip
            fi
        fi
    fi

    if [[ ! -s "$base_zip" ]]; then
        echo "Prepared game data was not found: $base_zip" >&2
        exit 7
    fi
fi

if [[ "$mode" == textures || "$mode" == both ]]; then
    if [[ -z "$input_pack" ]]; then
        mapfile -t packs < <(find user-assets/texture-packs -maxdepth 1 -type f -iname '*.zip' -print 2>/dev/null | sort)
        if ((${#packs[@]} == 0)); then
            echo 'No texture packs were found.' >&2
            echo 'Place ZIP files in user-assets/texture-packs and run the wizard again.' >&2
            exit 3
        fi
        pack_options=()
        for pack in "${packs[@]}"; do
            pack_options+=("$(basename "$pack")" "$pack")
        done
        echo
        echo 'Texture packs folder: user-assets/texture-packs' >&2
        input_pack="$(choose 'Select a texture pack:' 1 "${pack_options[@]}")"
    fi

    if [[ ! -f "$input_pack" || "${input_pack,,}" != *.zip ]]; then
        echo "Texture pack ZIP not found: $input_pack" >&2
        exit 4
    fi

    if [[ -z "$profile" ]]; then
        profile="$(choose 'Texture quality (lightest to heaviest):' 3 \
            'Lowest memory: limit textures to 3x the original game size' 'full-720' \
            'Low memory + sharper HUD: 3x limit, keeping the HUD at pack size' 'full-720-hd-hud' \
            'For HD packs (recommended): reduce width and height to 50% (never below 3x)' 'half-source' \
            'For 4K packs: reduce width and height to 25% (never below 3x)' 'quarter-source' \
            'Higher quality: up to 6x, with Castle Grounds optimized' 'max-6x' \
            'Original quality: keep texture sizes and only remove unused files' 'clean-ex-alo')"
    fi
    case "$profile" in
        full-720|full-720-hd-hud|half-source|quarter-source|max-6x|clean-ex-alo) ;;
        *) echo "Invalid profile: $profile" >&2; exit 5 ;;
    esac

    case "$profile" in
        full-720) profile_description='3x limit (lowest memory)' ;;
        full-720-hd-hud) profile_description='3x limit with original-size HUD' ;;
        quarter-source) profile_description='25% width and height, minimum 3x (for 4K packs)' ;;
        half-source) profile_description='50% width and height, minimum 3x (for HD packs)' ;;
        max-6x) profile_description='maximum 6x, with Castle Grounds at half source size' ;;
        clean-ex-alo) profile_description='original sizes, cleanup only' ;;
    esac

    pack_stem="$(safe_name "$(basename "${input_pack%.zip}")")"
    pack_name="${pack_stem}-ex-alo-${profile}.zip"
    report_dir='build/dist/reports'
    report_output="${report_dir}/${pack_stem}-ex-alo-${profile}-report.json"
fi

log_dir='build/dist/logs'
if [[ "$mode" == game || "$mode" == both ]]; then
    if [[ -n "$version" ]]; then
        case "$version" in us|eu|jp|sh|cn) ;; *) echo "Invalid region filter: $version" >&2; exit 8 ;; esac
    fi

    mapfile -t rom_candidates < <(
        {
            find user-assets/rom -maxdepth 1 -type f ! -name '.gitignore' -print 2>/dev/null
            find . -maxdepth 1 -type f -size 8M -print
        } | sort -u
    )
    valid_roms=()
    valid_versions=()
    for candidate in "${rom_candidates[@]}"; do
        candidate_sha1="$(sha1sum "$candidate" | awk '{print $1}')"
        for candidate_version in us eu jp sh cn; do
            expected_sha1="$(awk 'NR == 1 {print $1}' "sm64.${candidate_version}.sha1")"
            if [[ "$candidate_sha1" == "$expected_sha1" && (-z "$version" || "$version" == "$candidate_version") ]]; then
                valid_roms+=("$candidate")
                valid_versions+=("$candidate_version")
            fi
        done
    done

    if ((${#valid_roms[@]} == 0)); then
        echo 'No supported ROM was recognized by content.' >&2
        echo 'Place a valid ROM in user-assets/rom; its file name does not matter.' >&2
        exit 8
    elif ((${#valid_roms[@]} == 1)); then
        selected_rom="${valid_roms[0]}"
        version="${valid_versions[0]}"
    else
        rom_options=()
        for ((rom_index = 0; rom_index < ${#valid_roms[@]}; rom_index++)); do
            rom_options+=("$(basename "${valid_roms[rom_index]}") (${valid_versions[rom_index]^^})" "${valid_roms[rom_index]}")
        done
        selected_rom="$(choose 'Multiple valid ROMs found. Select one:' 1 "${rom_options[@]}")"
        for ((rom_index = 0; rom_index < ${#valid_roms[@]}; rom_index++)); do
            if [[ "${valid_roms[rom_index]}" == "$selected_rom" ]]; then
                version="${valid_versions[rom_index]}"
                break
            fi
        done
    fi

    if [[ -z "$fps" ]]; then fps="$(choose 'Frame rate:' 1 '60 FPS' '60' '30 FPS' '30')"; fi
    case "$fps" in 30|60) ;; *) echo "Invalid FPS value: $fps" >&2; exit 9 ;; esac

    if [[ "$mode" == both ]]; then
        external_textures=1
    elif [[ -z "$external_textures" ]]; then
        external_textures="$(choose 'Texture pack support:' 1 \
            'Enabled - allows external textures, but increases loading times' '1' \
            'Disabled - uses built-in textures and loads faster' '0')"
    fi

    if [[ -z "$legacy_paths" ]]; then
        legacy_paths="$(choose 'Storage layout:' 1 \
            'Application folder (recommended) - keeps settings, saves, and packs beside the WUHB' '0' \
            'SD card root (legacy) - uses the original shared locations on the SD card' '1')"
    fi

    if [[ -z "$jobs" ]]; then
        jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '4')"
        # Wii U translation units are memory-heavy. Four parallel jobs are a
        # safe default even when Docker exposes many host CPU cores.
        if ((jobs > 4)); then jobs=4; fi
    fi
    if [[ ! "$jobs" =~ ^[0-9]+$ ]] || ((jobs < 1 || jobs > 64)); then
        echo 'Build jobs must be between 1 and 64.' >&2
        exit 10
    fi

    if [[ -z "$build_name" ]]; then build_name="$(read_default 'Application directory and WUHB name' 'sm64ex-alo')"; fi
    build_name="$(safe_name "$build_name")"
    if [[ -z "$build_title" ]]; then build_title="$(read_default 'Title displayed by Aroma' 'Super Mario 64')"; fi

    rom="baserom.${version}.z64"
    high_fps=0
    [[ "$fps" == 60 ]] && high_fps=1
    dist_root='build/dist/wiiu'
    app_dir="${dist_root}/apps/${build_name}"

    echo
    echo 'Optional WUHB artwork (user-assets/wuhb):'
    echo '  icon.png          PNG, 128x128, RGBA'
    echo '  boot-tv.png       PNG, 1280x720, RGB'
    echo '  boot-gamepad.png  PNG, 854x480, RGB'
fi

if [[ "$mode" == both ]]; then
    if [[ "$legacy_paths" == 1 ]]; then
        pack_output="build/dist/sm64ex_res/${pack_name}"
    else
        pack_output="${app_dir}/mods/${pack_name}"
    fi
elif [[ "$mode" == textures ]]; then
    pack_output="build/dist/texture-packs/${pack_name}"
fi

echo
echo 'Summary:'
case "$mode" in
    game) echo '  Output:       Wii U game only' ;;
    textures) echo '  Output:       Texture pack only' ;;
    both) echo '  Output:       Wii U game + texture pack' ;;
esac
if [[ "$mode" == game || "$mode" == both ]]; then
    echo "  ROM:          $selected_rom (${version^^})"
    echo "  Frame rate:   $fps FPS"
    if [[ "$external_textures" == 1 ]]; then
        if [[ "$mode" == both ]]; then
            echo '  Texture packs: Enabled (required)'
        else
            echo '  Texture packs: Enabled'
        fi
    else
        echo '  Texture packs: Disabled'
    fi
    if [[ "$legacy_paths" == 1 ]]; then
        echo '  Storage:      SD card root (legacy)'
    else
        echo '  Storage:      Application folder'
    fi
    echo "  Title:        $build_title"
    echo "  Game output:  $app_dir"
fi
if [[ "$mode" == textures || "$mode" == both ]]; then
    echo "  Pack:         $input_pack"
    echo "  Quality:      $profile_description"
    [[ "$mode" == textures ]] && echo "  Game data:    $base_zip"
    echo "  Pack output:  $pack_output"
fi

if ((assume_yes == 0)); then
    read -r -p 'Continue? [Y/n]: ' confirmation
    if [[ "${confirmation:-y}" =~ ^[Nn]([Oo])?$ ]]; then
        echo 'Cancelled.'
        exit 0
    fi
fi

if [[ "$mode" == game || "$mode" == both ]]; then
    if [[ "$selected_rom" != "$rom" ]]; then
        if [[ -e "$rom" ]]; then
            selected_sha1="$(sha1sum "$selected_rom" | awk '{print $1}')"
            canonical_sha1="$(sha1sum "$rom" | awk '{print $1}')"
            if [[ "$selected_sha1" != "$canonical_sha1" ]]; then
                echo "Cannot create $rom because a different file already exists at that path." >&2
                exit 11
            fi
        else
            cp -- "$selected_rom" "$rom"
            temporary_rom="$rom"
        fi
    fi

    mkdir -p dist
    temporary_build_log="$(mktemp 'dist/wiiu-build.XXXXXX.log')"
    echo
    run_quiet 'Building game and WUHB' "$temporary_build_log" \
        env VERSION="$version" JOBS="$jobs" EXTERNAL_DATA="$external_textures" \
        HIGH_FPS_PC="$high_fps" WIIU_LEGACY_PATHS="$legacy_paths" \
        BUILD_NAME="$build_name" WUHB_BASENAME="$build_name" BUILD_TITLE="$build_title" \
        WUHB_ASSET_DIR='user-assets/wuhb' DIST_ROOT="$dist_root" \
        bash docker/wiiu/build.sh

    mkdir -p "$log_dir"
    mv -f -- "$temporary_build_log" "${log_dir}/build.log"
    temporary_build_log=""

    if [[ "$external_textures" == 1 ]]; then
        base_zip="build/${version}_wiiu/sm64ex_res/base.zip"
        if [[ ! -s "$base_zip" ]]; then
            echo 'The game build did not prepare the data required for external textures.' >&2
            exit 12
        fi
        mkdir -p user-assets/base
        cp -- "$base_zip" "user-assets/base/base-${version}.zip"
    fi

    if [[ ! -s "${app_dir}/${build_name}.wuhb" ]]; then
        echo 'The final WUHB is missing.' >&2
        exit 13
    fi
fi

if [[ "$mode" == textures || "$mode" == both ]]; then
    mkdir -p "$(dirname "$pack_output")" "$report_dir" "$log_dir"
    if [[ "$mode" == both ]]; then
        # Avoid mounting two generated versions of the same pack when an
        # application name is reused.
        find "$(dirname "$pack_output")" -maxdepth 1 -type f -name '*-ex-alo-*.zip' -delete
    fi

    run_quiet "Converting textures ($profile)" "${log_dir}/texture-conversion.log" \
        python3 tools/wiiu_texture_pack/build_pack.py \
            --input "$input_pack" \
            --base-zip "$base_zip" \
            --profile "$profile" \
            --output "$pack_output" \
            --report "$report_output"

    if [[ ! -s "$pack_output" ]]; then
        echo 'The final texture pack is incomplete.' >&2
        exit 13
    fi
fi

if [[ "$mode" == game || "$mode" == both ]]; then
    {
        echo 'SM64 EX-ALO Wii U'
        echo "Region: ${version}"
        echo "FPS: ${fps}"
        [[ "$external_textures" == 1 ]] && echo 'Texture pack support: enabled' || echo 'Texture pack support: disabled'
        [[ "$legacy_paths" == 1 ]] && echo 'Storage layout: SD card root (legacy)' || echo 'Storage layout: application folder'
        if [[ "$mode" == both ]]; then
            echo "Texture profile: ${profile}"
            echo "Pack: mods/${pack_name}"
            echo "Report: ../../../reports/$(basename "$report_output")"
        fi
        echo
        echo 'Copy the build/dist/wiiu folder to the root of the SD card.'
        if [[ "$legacy_paths" == 1 && "$mode" == both ]]; then
            echo 'Also copy build/dist/sm64ex_res to the root of the SD card.'
        fi
        echo "The game must end up in SD:/wiiu/apps/${build_name}/"
        echo 'sm64config.txt is not included, so existing settings and save data are preserved.'
        if [[ "$mode" == both ]]; then
            echo 'Reduced texture packs are intended for selective loading (precache false).'
        fi
    } > "${app_dir}/BUILD-INFO.txt"

    echo
    [[ "$mode" == both ]] && echo 'Game and texture pack complete.' || echo 'Game build complete.'
    if [[ "$legacy_paths" == 1 && "$mode" == both ]]; then
        echo '  Copy these folders to the SD card root:'
        echo "    $dist_root"
        echo '    build/dist/sm64ex_res'
    else
        echo "  Copy this folder to the SD card root: $dist_root"
    fi
    echo "  Final SD path: SD:/wiiu/apps/${build_name}/"
    if [[ "$legacy_paths" == 1 && "$external_textures" == 1 ]]; then
        echo '  External texture path: SD:/sm64ex_res/'
    fi
    echo "  Game: ${app_dir}/${build_name}.wuhb"
fi

if [[ "$mode" == textures ]]; then
    echo
    echo 'Texture pack complete.'
fi
if [[ "$mode" == textures || "$mode" == both ]]; then
    echo "  Pack: $pack_output"
    echo "  Report: $report_output"
fi
echo "  Logs: $log_dir"
