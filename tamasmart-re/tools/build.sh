#!/bin/sh
# Builds tools/unsp-dasm. Fetches only MAME's unSP disassembler sources (GPL-2.0+) and the
# GPL951xx SoC/driver files (BSD-3-Clause) used as reference; nothing else from MAME is needed.
set -e
cd "$(dirname "$0")"
if [ ! -d mame ]; then
  git clone --depth 1 --filter=blob:none --sparse https://github.com/mamedev/mame.git
  (cd mame && git sparse-checkout set --no-cone '/src/devices/cpu/unsp/' '/src/devices/machine/generalplus_gpl*' \
     '/src/devices/machine/gpl*' '/src/mame/handheld/generalplus_gpl951xx*' '/src/devices/video/st77*')
fi
R=mame/src/devices/cpu/unsp
g++ -O1 -std=c++17 -w -I. -I$R -o unsp-dasm main.cpp $R/unspdasm.cpp $R/unspdasm_exxx.cpp \
    $R/unspdasm_extended.cpp $R/unspdasm_jumps.cpp $R/unspdasm_fxxx.cpp $R/unspdasm_other.cpp
echo "built tools/unsp-dasm"
