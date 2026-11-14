#!/bin/sh
# Builds emu/test/lockstep. Needs tools/build.sh and emu/test/mame_ref/fetch.sh to have been run once.
set -e
cd "$(dirname "$0")"
[ -f mame_ref/mame_unsp/unsp_other.cpp ] || { echo "run tools/build.sh and emu/test/mame_ref/fetch.sh first"; exit 1; }
g++ -O2 -std=c++20 -w -Imame_ref/shim -Imame_ref/mame_unsp -I../core -o lockstep \
    lockstep.cpp ../core/cpu.cpp mame_ref/cpu_glue.cpp mame_ref/mame_unsp/*.cpp
echo "built emu/test/lockstep"
