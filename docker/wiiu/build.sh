#!/usr/bin/env bash
set -euo pipefail

version="${VERSION:-us}"
jobs="${JOBS:-4}"
rom="baserom.${version}.z64"

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

# Build flags are not represented in the original dependency graph. Start from
# a clean target directory so configuration changes always reach the RPX.
make TARGET_WII_U=1 VERSION="${version}" clean

# Host-built extraction tools are not reusable inside this Linux container.
make -C tools clean

make -j"${jobs}" \
    TARGET_WII_U=1 \
    VERSION="${version}" \
    EXTERNAL_DATA="${EXTERNAL_DATA:-1}" \
    HIGH_FPS_PC="${HIGH_FPS_PC:-1}"

artifact="build/${version}_wiiu/sm64.${version}.f3dex2e.rpx"
if [[ ! -s "${artifact}" ]]; then
    echo "Build completed without the expected RPX: ${artifact}" >&2
    exit 4
fi

sha256sum "${artifact}"
echo "Wii U build ready: ${artifact}"

bash docker/wiiu/package-wuhb.sh "${artifact}"
