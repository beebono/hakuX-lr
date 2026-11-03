#!/bin/sh
# Copyright (C) 2026-present PortareOS
# The x86-64 Linux build and its checks (#56), in the hakux-linux image
# built for amd64, as CI runs them (.github/workflows/linux.yml): the core
# with the KVM backend (HAKUX_CPU=kvm) and the DSP JIT (HAKUX_DSP_JIT, the
# image's Rust) and its tests (linux-build.sh), and the pure HLE tests (the
# audio's at each vector level the runner has), and the release package and
# its verification.
#   sh tools/native/linux-ci-x86_64.sh [<version>]
set -eu

repo=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
version=${1:-dev}
export HAKUX_DEPS="${HAKUX_DEPS:-$repo/build/deps}"
export HAKUX_BUILD="${HAKUX_BUILD:-$repo/build/linux-x86_64}"
export HAKUX_VERSION="$version"
jobs=${JOBS:-4}

python3 tools/native/linux-deps.py --dest "$HAKUX_DEPS"
sh tools/native/linux-build.sh
if ! grep -q '^HAKUX_DSP_JIT:BOOL=ON$' "$HAKUX_BUILD/CMakeCache.txt"; then
    echo "linux-ci-x86_64.sh: the build has no DSP JIT" >&2
    exit 1
fi
# the KVM backend's and the DSP JIT's checks once more, each line in the log
"$HAKUX_BUILD/kvm-smoke"
"$HAKUX_BUILD/linux-smoke" | grep -E '^(PASS|FAIL)'
"$HAKUX_BUILD/apu-jit-smoke" | grep -E '^(PASS|FAIL)'

tests=$repo/build/hle-tests-x86_64
cmake -S tests/hle -B "$tests" -G Ninja -DCMAKE_C_COMPILER="${CC:-clang}" \
    -DNC_DEPS="$HAKUX_DEPS" > /dev/null
cmake --build "$tests" -j "$jobs"
ctest --test-dir "$tests" --output-on-failure
sh tools/native/linux-package.sh "$version"
python3 tools/native/linux-verify.py "$version"
python3 tools/native/statelock.py
echo "PASS: linux-ci-x86_64"
