#!/usr/bin/env bash
set -euo pipefail

version="${VERSION:-us}"
jobs="${JOBS:-4}"
rom="baserom.${version}.z64"
legacy_paths="${WIIU_LEGACY_PATHS:-0}"

legacy_path_args=()
case "${legacy_paths}" in
    0) ;;
    1) legacy_path_args+=(WIIU_LEGACY_PATHS=1) ;;
    *)
        echo "Unsupported WIIU_LEGACY_PATHS '${legacy_paths}'. Use 0 or 1." >&2
        exit 2
        ;;
esac

case "${version}" in
    us|jp|eu|sh|cn) ;;
    *)
        echo "Unsupported VERSION '${version}'. Use us, jp, eu, sh, or cn." >&2
        exit 2
        ;;
esac

if [[ ! -f "${rom}" ]]; then
    echo "Missing ${rom}. Place your legally obtained SM64 ROM in /workspace." >&2
    exit 3
fi

# Build flags are not represented in the original dependency graph. Remove
# only this region's intermediate files so configuration changes reach the RPX
# without deleting the finished products stored under build/dist.
target_build_dir="build/${version}_wiiu"
rm -rf -- "${target_build_dir}"

# Host-built extraction tools are not reusable inside this Linux container.
make -C tools clean

make -j"${jobs}" \
    TARGET_WII_U=1 \
    VERSION="${version}" \
    EXTERNAL_DATA="${EXTERNAL_DATA:-1}" \
    HIGH_FPS_PC="${HIGH_FPS_PC:-1}" \
    "${legacy_path_args[@]}"

artifact="build/${version}_wiiu/sm64.${version}.f3dex2e.rpx"
if [[ ! -s "${artifact}" ]]; then
    echo "Build completed without the expected RPX: ${artifact}" >&2
    exit 4
fi

sha256sum "${artifact}"
echo "Wii U build ready: ${artifact}"

bash docker/wiiu/package-wuhb.sh "${artifact}"
