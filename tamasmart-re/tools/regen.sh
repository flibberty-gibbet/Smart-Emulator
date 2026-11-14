#!/bin/sh
# Regenerates the disassembly and register census from a RAW (no update header) 16 MB image.
# usage: tools/regen.sh path/to/TMGC_Smart_FW_RAW.bin     (run from the package root)
set -e
IMG="$1"; mkdir -p out
python3 tools/scan.py "$IMG" out/calltargets.txt
tools/unsp-dasm "$IMG" trace $(cat analysis/entries.txt) $(tr '\n' ' ' < out/calltargets.txt) > out/trace.asm
python3 tools/census.py out/trace.asm > out/census.txt
python3 tools/funcs.py out/trace.asm tree 07c3c8 2 > out/main_tree.txt
python3 tools/cov.py out/trace.asm
python3 tools/annotate.py out/trace.asm out/annotated.asm
echo "wrote out/trace.asm, out/annotated.asm, out/census.txt, out/main_tree.txt (funcs index: out/funcs.pkl)"
