FROM ghcr.io/wiiu-env/librpxloader:20260329 AS librpxloader

FROM devkitpro/devkitppc:20260503

LABEL org.opencontainers.image.title="SM64EX-ALO Wii U development environment"
LABEL org.opencontainers.image.source="https://github.com/AloUltraExt/sm64ex-alo"

ENV DEVKITPRO=/opt/devkitpro \
    DEVKITPPC=/opt/devkitpro/devkitPPC \
    WUT_ROOT=/opt/devkitpro/wut \
    PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:${PATH}

WORKDIR /workspace

COPY --from=librpxloader /artifacts ${DEVKITPRO}

RUN apt-get update \
    && apt-get install -y --no-install-recommends bsdextrautils python3-numpy python3-pil \
    && rm -rf /var/lib/apt/lists/*

RUN powerpc-eabi-gcc --version \
    && test -f "${WUT_ROOT}/share/wut.specs" \
    && test -d "${DEVKITPRO}/portlibs/wiiu/include/SDL2" \
    && test -f "${DEVKITPRO}/wums/include/rpxloader/rpxloader.h" \
    && test -f "${DEVKITPRO}/wums/lib/librpxloader.a"

CMD ["bash"]
