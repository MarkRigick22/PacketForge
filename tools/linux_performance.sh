#!/usr/bin/env bash
set -euo pipefail
[[ $(uname -s) == Linux ]] || { echo 'NOT EXECUTED: Linux host required' >&2; exit 1; }
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DPACKETFORGE_WARNINGS_AS_ERRORS=ON
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
cmake -S . -B build-profile -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPACKETFORGE_PROFILE=ON
cmake --build build-profile --parallel
python3 tools/benchmark_matrix.py --binary build-release/packetforge "$@"
bash tools/profile_linux.sh build-profile
