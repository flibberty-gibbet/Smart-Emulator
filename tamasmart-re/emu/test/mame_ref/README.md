# MAME reference core (test only)

`shim/unsp.h` and `cpu_glue.cpp` are adapted from MAME's `src/devices/cpu/unsp/` and are licensed GPL-2.0+
(Segher Boessenkool, Ryan Holtz, David Haywood); see `THIRD_PARTY_NOTICES.md` and `LICENSES/GPL-2.0.txt` at the repo root. `fetch.sh` copies the remaining MAME instruction and
disassembler sources into `mame_unsp/` so they compile unchanged.

Nothing in this folder is linked into the emulator. It exists so `emu/test/lockstep` can run MAME's CPU core
beside the project's own core and compare them instruction by instruction, and so `emu/build.sh --dasm` can
print disassembled traces.
