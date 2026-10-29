#!/bin/bash

cd "$(dirname "$0")/.." || exit 1

export JOBS=${JOBS:-1}
nproc() { echo "$JOBS"; }
export -f nproc

# initialize submodules recursively (monero-cpp and its monero-project)
./bin/update_submodules.sh || exit 1

# build monero-project and libmonero-cpp
( cd external/monero-cpp && ./bin/build_libmonero_cpp.sh ) || exit 1

# build libmonero_c in $BUILD_DIR, build by default; extra args go to cmake, e.g. -DBUILD_TESTS=ON
BUILD_DIR=${BUILD_DIR:-build}
cmake -S . -B "$BUILD_DIR" "$@" &&
cmake --build "$BUILD_DIR" -j"$JOBS"
