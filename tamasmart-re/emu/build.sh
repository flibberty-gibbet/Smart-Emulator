#!/bin/sh
# Builds emu/tsmart-run, the headless runner. No external dependencies.
# With MAME's disassembler present (run tools/build.sh, then emu/test/mame_ref/fetch.sh) pass --dasm for readable traces.
set -e
cd "$(dirname "$0")"
EXTRA=""
if [ "$1" = "--dasm" ]; then
  [ -f test/mame_ref/mame_unsp/unspdasm.cpp ] || { echo "MAME disassembler sources missing: run tools/build.sh then emu/test/mame_ref/fetch.sh"; exit 1; }
  EXTRA="-DTSMART_WITH_MAME_DASM -Itest/mame_ref/shim -Itest/mame_ref/mame_unsp test/mame_ref/mame_unsp/unspdasm*.cpp"
fi
g++ -O2 -std=c++17 -Wall -Wno-unused -Wno-misleading-indentation -Icore -o tsmart-run headless/main.cpp core/cpu.cpp $EXTRA
echo "built emu/tsmart-run"
