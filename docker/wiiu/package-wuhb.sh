#!/usr/bin/env bash
set -euo pipefail

version="${VERSION:-us}"
external_data="${EXTERNAL_DATA:-1}"
build_name="${BUILD_NAME:-sm64ex-alo}"
title="${BUILD_TITLE:-Super Mario 64 EX}"
basename="${WUHB_BASENAME:-${build_name}}"
author="${WUHB_AUTHOR:-DobleD}"
artifact="${1:-build/${version}_wiiu/sm64.${version}.f3dex2e.rpx}"
artifact_dir="$(dirname "${artifact}")"
basepack="${artifact_dir}/sm64ex_res/base.zip"
content_dir="${artifact_dir}/wuhb-content"
output="${artifact_dir}/${basename}.wuhb"
asset_dir="assets/wuhb"

case "${version}" in
    us|jp|eu|sh|cn) ;;
    *)
        echo "Unsupported VERSION '${version}'." >&2
        exit 2
        ;;
esac

case "${external_data}" in
    0|1) ;;
    *)
        echo "Unsupported EXTERNAL_DATA '${external_data}'. Use 0 or 1." >&2
        exit 2
        ;;
esac

if [[ ! "${build_name}" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]]; then
    echo "BUILD_NAME must start with a letter or digit and may only contain letters, digits, dots, underscores, and hyphens." >&2
    exit 3
fi

if [[ ! "${basename}" =~ ^[A-Za-z0-9._-]+$ ]]; then
    echo "WUHB_BASENAME may only contain letters, digits, dots, underscores, and hyphens." >&2
    exit 4
fi

if [[ ! -s "${artifact}" ]]; then
    echo "Missing RPX: ${artifact}" >&2
    exit 5
fi

if [[ "${external_data}" == "1" && ! -s "${basepack}" ]]; then
    echo "Missing external-data base pack: ${basepack}" >&2
    exit 6
fi

rm -rf "${content_dir}"

args=(
    --name="${title}"
    --short-name="${title}"
    --author="${author}"
)

if [[ "${external_data}" == "1" ]]; then
    mkdir -p "${content_dir}/sm64ex_res"
    cp "${basepack}" "${content_dir}/sm64ex_res/base.zip"
    args+=(--content="${content_dir}")
fi

if [[ -f "${asset_dir}/icon.png" ]]; then
    args+=(--icon="${asset_dir}/icon.png")
fi
if [[ -f "${asset_dir}/boot-tv.png" ]]; then
    args+=(--tv-image="${asset_dir}/boot-tv.png")
fi
if [[ -f "${asset_dir}/boot-gamepad.png" ]]; then
    args+=(--drc-image="${asset_dir}/boot-gamepad.png")
fi

wuhbtool "${artifact}" "${output}" "${args[@]}"

sha256sum "${output}"
echo "Aroma WUHB ready: ${output}"
