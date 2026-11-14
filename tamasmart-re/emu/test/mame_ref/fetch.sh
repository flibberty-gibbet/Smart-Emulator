#!/bin/sh
# Copies MAME's unSP instruction and disassembler sources (GPL-2.0+) next to the shim headers so they compile
# unchanged. Used only by the lockstep test and the optional trace disassembler. Run tools/build.sh first.
set -e
cd "$(dirname "$0")"
R=../../../tools/mame/src/devices/cpu/unsp
[ -d "$R" ] || { echo "run tools/build.sh first"; exit 1; }
mkdir -p mame_unsp
cp $R/unsp_other.cpp $R/unsp_jumps.cpp $R/unsp_fxxx.cpp $R/unsp_exxx.cpp $R/unsp_extended.cpp $R/unspdefs.h $R/unspdasm.h $R/unspdasm*.cpp mame_unsp/
echo "copied MAME unSP sources into emu/test/mame_ref/mame_unsp"
