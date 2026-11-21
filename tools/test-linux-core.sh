#!/bin/sh
set -eu

source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/isoduo-core.XXXXXXXX")
export CC="${CC:-clang}" CXX="${CXX:-clang++}"

set -x
cmake -S "$source_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DISODUO_BUILD_DESKTOP=OFF \
    -DISODUO_BUILD_TESTS=ON -DISODUO_INSTALL=OFF
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
ctest --test-dir "$build_dir" --show-only=json-v1 > "$build_dir/ctest-tests.json"
ctest --test-dir "$build_dir" --output-on-failure --no-tests=error \
    --output-junit "$build_dir/ctest-results.xml"
