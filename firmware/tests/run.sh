#!/bin/sh
# Run the PC-side unit tests and the ARM cross-compile check in the build container.
#   firmware/tests/run.sh
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SUBDIR=firmware/tests "$ROOT/tools/buildenv/run.sh" sh -c 'make -s clean && make -s test && make -s cross'
