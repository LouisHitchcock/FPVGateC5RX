#!/usr/bin/env bash
# Build and run the unit tests on a PC with g++. No hardware needed.
set -uo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
FLAGS="-std=c++14 -Wall -Wextra -Wno-unused-parameter -O1 -I firmware/core"
CORE="$(ls firmware/core/*.cpp)"

mkdir -p build
fail=0
for t in test/test_*.cpp; do
  name="$(basename "$t" .cpp)"
  if ! $CXX $FLAGS $CORE "$t" -o "build/$name.exe" 2> "build/$name.build.log"; then
    echo "BUILD FAIL: $name"; cat "build/$name.build.log"; fail=1; continue
  fi
  if ! "./build/$name.exe"; then fail=1; fi
done
[ $fail -eq 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit $fail
