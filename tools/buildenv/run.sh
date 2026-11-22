#!/bin/sh
# Run a command inside the build container with the project folder mounted at /work.
# Usage: tools/buildenv/run.sh <cmd...>     (run from anywhere)
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
IMG=uls03gw-build:2
docker image inspect $IMG >/dev/null 2>&1 || docker build -t $IMG "$ROOT/tools/buildenv"
exec docker run --rm -u "$(id -u):$(id -g)" -v "$ROOT:/work" -w "/work${SUBDIR:+/$SUBDIR}" $IMG "$@"
