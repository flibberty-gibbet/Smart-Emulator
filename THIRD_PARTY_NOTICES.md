# Third-party notices

The project's own code is under the MIT License (`LICENSE`). Some files come from or are based on
[MAME](https://github.com/mamedev/mame) and keep their original licenses, listed below.

MAME is copyright (c) 1997-2026 MAMEdev and contributors. MAME is a registered trademark of Gregory Ember.
This project is not affiliated with or endorsed by the MAME team.

## Files in this repository

| File(s) | Origin | License | Copyright holders |
|---|---|---|---|
| `tamasmart-re/emu/test/mame_ref/cpu_glue.cpp` | Adapted from MAME `src/devices/cpu/unsp/unsp.cpp` | GPL-2.0-or-later (`LICENSES/GPL-2.0.txt`) | Segher Boessenkool, Ryan Holtz, David Haywood |
| `tamasmart-re/emu/test/mame_ref/shim/unsp.h` | Adapted from MAME `src/devices/cpu/unsp/unsp.h` | GPL-2.0-or-later (`LICENSES/GPL-2.0.txt`) | Segher Boessenkool, Ryan Holtz, David Haywood |
| `tamasmart-re/analysis/regnames.json` | Register names extracted by `tools/regnames.py` from MAME `src/devices/machine/generalplus_gpl951xx_soc.cpp` | BSD-3-Clause (`LICENSES/BSD-3-Clause.txt`) | David Haywood |

The two adapted files were changed for this project in 2026: the MAME device framework and recompiler were
removed so the interpreter builds on its own.

## MAME sources downloaded at build time (not in this repository)

`tamasmart-re/tools/build.sh` and `tamasmart-re/emu/test/mame_ref/fetch.sh` download these from MAME:

- `src/devices/cpu/unsp/unsp_*.cpp`, `unspdasm*.cpp`, `unspdasm.h`: GPL-2.0-or-later (Segher Boessenkool, Ryan Holtz, David Haywood)
- `src/devices/cpu/unsp/unspdefs.h`: BSD-3-Clause (Ryan Holtz)
- GeneralPlus GPL951xx SoC and driver files, used only as reference for reading

Programs built with these sources are covered by the GPL as a whole: `tools/unsp-dasm`, `emu/test/lockstep`, and
the headless emulator when built with `--dasm`. If you share those binaries, you must also offer their complete
source under GPL-2.0-or-later. The default emulator build (`emu/core`, `emu/headless`, `emu/sdl`) does not include
any MAME code.

## Reference use

The CPU core in `tamasmart-re/emu/core` was written for this project and tested against MAME's unSP core
(see `emu/test/lockstep.cpp`). Its instruction cycle costs follow MAME's estimates. Hardware details in
`tamasmart-re/docs/` and `HANDOFF.md`, such as register names, interrupt mapping and sprite format, were learned
in part from MAME's GeneralPlus sources.
