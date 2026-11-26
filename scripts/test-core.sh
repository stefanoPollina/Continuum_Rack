#!/usr/bin/env bash
# Builds and runs the JUCE-independent DSP tests only (no JUCE download needed).
set -euo pipefail
rack_root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
cmake -S "$rack_root" -B "$rack_root/build-core" \
  -DRACK_BUILD_PLUGIN=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "$rack_root/build-core" --parallel 2
ctest --test-dir "$rack_root/build-core" --output-on-failure
