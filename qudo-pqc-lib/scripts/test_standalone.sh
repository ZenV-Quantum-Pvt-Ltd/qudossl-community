#!/bin/bash

# Build and run each subproject's standalone test suite (its own CMakeLists,
# independent of the top-level qudo-pqc build). The top-level build does not
# compile these suites, so they must be exercised separately. CI runs this
# coverage via .github/workflows/ci-standalone.yml; locally it is opt-in:
#
#   ./scripts/test_standalone.sh
#
# Environment variables:
#   BUILD_TYPE   CMake build type (default: Release)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

SUBPROJECTS="qudo-mlkem qudo-mldsa qudo-slhdsa"
BUILD_TYPE="${BUILD_TYPE:-Release}"

if command -v nproc &> /dev/null; then
    JOBS="$(nproc)"
elif command -v sysctl &> /dev/null; then
    JOBS="$(sysctl -n hw.ncpu)"
else
    JOBS=4
fi

for sub in $SUBPROJECTS; do
    echo "======================================"
    echo "  $sub (standalone)"
    echo "======================================"
    cmake -S "$ROOT_DIR/$sub" -B "$ROOT_DIR/$sub/build-standalone" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    cmake --build "$ROOT_DIR/$sub/build-standalone" -j"$JOBS"
    ctest --test-dir "$ROOT_DIR/$sub/build-standalone" --output-on-failure
    echo ""
done

echo "All standalone subproject suites passed."
