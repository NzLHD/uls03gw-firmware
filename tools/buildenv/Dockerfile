# Minimal build environment for the ULS-03GW clean firmware.
# Provides make/python3 for the Beken SDK (which uses its own bundled ARM toolchain)
# and a host gcc for the PC-side unit tests.
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        make gcc libc6-dev python3 python-is-python3 xz-utils file ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
