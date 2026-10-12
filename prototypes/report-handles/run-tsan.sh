#!/usr/bin/env bash
# THROWAWAY: build and run the report-handle scenarios under ThreadSanitizer on Linux (WSL).
# clang on Windows has no TSan runtime, so this runs on the Linux side.
# Usage (from Windows): wsl.exe -e bash prototypes/report-handles/run-tsan.sh
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/build-tsan"
mkdir -p "$out"
CXX="${CXX:-clang++}"
"$CXX" -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=thread -pthread \
  -Wall -Wextra "$here/main.cpp" -o "$out/report_handles"
# halt_on_error: the first race fails the run; second_deadlock_stack helps lock-order reports.
TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1 ${TSAN_OPTIONS:-}" "$out/report_handles"
