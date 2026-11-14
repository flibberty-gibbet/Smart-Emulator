# tamasmart-re

Reverse-engineering notes, tools and a first-stage emulator for the Bandai Tamagotchi Smart (GeneralPlus GPL951xx, µ'nSP 2.0). Read `HANDOFF.md` first.

No firmware is included. You need your own RAW 16 MB dump (the one that starts with `GP-SPIF-HEADER`, without the 0x400-byte card update header). Addresses in the docs and the runner's milestone checks are for the English RAW image.

Requirements: `g++` (C++17; C++20 for the tests), `python3`, and `git` for anything that uses MAME's sources. Run everything from the package root.

## Desktop app (Windows, Linux, macOS)

`emu/sdl/main.cpp` is a small SDL2 app. It has been built and smoke-tested on Linux only; the Windows steps below are the standard ones but have not been run.

**Windows 11 with MSYS2** (simplest): install MSYS2 from msys2.org, open the "MSYS2 UCRT64" terminal, then:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2
cd /c/path/to/tamasmart-re/emu
cmake -S . -B build -G Ninja
cmake --build build
./build/tsmart.exe "C:\Users\Katu\Documents\TamagotchiStuff\ROMS\Tamagotchi Smart\Smart Firmware [Eng+JP]\TMGC_Smart_FW_RAW_[ENG].bin"
```

To run it outside that terminal, copy `SDL2.dll` (from `C:\msys64\ucrt64\bin`) next to `tsmart.exe`.

**Windows 11 with Visual Studio 2022 and vcpkg:**

```bat
vcpkg install sdl2:x64-windows
cmake -S emu -B build -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
build\Release\tsmart.exe C:\path\to\TMGC_Smart_FW_RAW.bin
```

The code has only been compiled with GCC so far, so MSVC may need small fixes.

**Using it**

```sh
tsmart TMGC_Smart_FW_RAW.bin [--card CARD.bin]... [--scale N] [--fresh] [--realtime]
```

| Input | Action |
|---|---|
| Left mouse button on the screen | Touch. The screen is four pads (quadrants) |
| Space or Enter | The device's button (opens the menu, ends name entry, wakes the device) |
| Arrow keys | Swipe up, down, left, right |
| Q, W, A, S | The four pads from the keyboard (top-left, top-right, bottom-left, bottom-right) |
| C / X | Insert the next card from the library / eject the card |
| Drop a file on the window | Add it to the card library and insert it |
| Tab (hold) | Fast-forward, 8× |
| F5 | Reset the device (keeps saves, clock and card) |
| F12 | Screenshot, written next to the image |
| Esc | Quit |

How the device reads touches (this is the firmware's behaviour, not the emulator's): a **tap** only counts after the pad has been held for about **0.4 s**, so click and hold briefly. A **swipe** is a short touch on one half of the screen followed quickly by the other half; with the mouse, press on one half and drag across within about a third of a second, or use the arrow keys. The menu's second page is reached with swipe up.

Files, all next to the inputs and never the inputs themselves:

| File | Contents |
|---|---|
| `IMAGE.bin.flash` | The device's flash: saves and any downloaded card content. `--fresh` ignores it (and the clock file) |
| `IMAGE.bin.rtc` | The clock. It stops while the app is closed; `--realtime` makes it catch up instead |
| `CARD.bin.card` | Working copy of a card. A download writes a use record to the card (three allowed), so delete this file to get an unused card back |

`--scale N` sets the window size (default 4). The window title shows the card in the slot, the current scene id, and whether the device is asleep.

To use a card: start with `--card`, or drop the file on the window; on the device press the button, swipe up, tap the bottom-right icon, tap TAMASMA CARD, tap DOWNLOAD, tap YES. The download takes about 20 s.

## Headless runner

```sh
sh emu/build.sh                                   # builds emu/tsmart-run, no dependencies
./emu/tsmart-run IMAGE.bin --frames 400 --shot 300:time.ppm
```

Useful options:

| Option | Effect |
|---|---|
| `--frames N` | Stop after N frames (default 120) |
| `--shot FRAME:file.ppm` | Write a 128×128 screenshot at that frame (repeatable) |
| `--touch PAD:FROM:TO` | Hold touch pad 0–3 between two frame numbers (repeatable). Taps need 14 or more frames; a swipe is 6 frames on one pad then 6 on another |
| `--press FROM:TO` | Hold the button between two frame numbers (repeatable) |
| `--card FILE` | Insert a card image; `--card-at FRAME` and `--eject-at FRAME` time it, `--save-card OUT.bin` writes the card back on exit. The report lists what the firmware did to the card |
| `--watch RAMADDR` | Print every change of a RAM word, for example `--watch 494` for the scene id (repeatable) |
| `--adc CH:HEX` | Set an ADC channel, for example `--adc 7:c000` for a flat battery |
| `--stop PC` | Stop at an address and print a call-stack guess and recent calls |
| `--io-report` | List every I/O register touched, unmapped accesses and flash erase/program operations |
| `--trace-io` | Print every I/O access |
| `--trace FILE` | Full instruction trace (very large) |
| `--save-flash OUT.bin` | Write the flash image, including saves, on exit |
| `--fps N`, `--cpu-speed N/D`, `--centre X,Y`, `--gfx-base HEX` | Tuning knobs for unknowns (`--cpu-speed 1/1` is MAME's cost table at 48 MHz; the default is `1/2`, twice as fast) |

The runner reports milestones M1–M5, and stops with a listing of the loop it is stuck in if the firmware stops making progress.

For disassembled traces and hang listings, build with MAME's disassembler:

```sh
sh tools/build.sh                # fetches MAME's unSP sources into tools/mame, builds tools/unsp-dasm
sh emu/test/mame_ref/fetch.sh    # copies the files the tests need
sh emu/build.sh --dasm
```

### Scripted walkthrough

Input is scripted by frame number (30 per second). `--touch PAD:FROM:TO` holds a pad (2 top-left, 1 top-right, 3 bottom-left, 0 bottom-right), `--press FROM:TO` holds the button; both can be repeated. This goes from a blank device to a hatched pet and saves the flash (about 2 minutes of device time):

```sh
T=""; f=200; for i in 1 2 3 4 5 6 7 8 9 10; do T="$T --touch 3:$f:$((f+14))"; f=$((f+60)); done      # SET through time and birthday
./emu/tsmart-run IMAGE.bin --frames 4000 --watch 494 $T --press 800:815 --press 900:915 --touch 3:1000:1014 \
    --save-flash hatched.bin --shot 3900:room.ppm
```

Then, from that image: CONTINUE, dismiss the time check, open the menu, swipe up, connection, TAMASMA CARD, DOWNLOAD, YES:

```sh
./emu/tsmart-run hatched.bin --frames 2200 --watch 494 --card CARD.bin --save-card card_after.bin --save-flash after.bin \
    --touch 2:300:315 --touch 3:420:435 --press 520:535 --touch 0:620:626 --touch 1:626:632 \
    --touch 0:680:696 --touch 3:800:816 --touch 2:900:916 --touch 3:1080:1096 --shot 2100:after.ppm
```

Frame numbers are tied to the current timing model; if timing changes, re-derive them from the `--watch 494` scene log. Run from the package root (the runner loads `analysis/regnames.json`).

## Tests

```sh
sh emu/test/build_lockstep.sh
./emu/test/lockstep IMAGE.bin decode analysis/tamasmart_eng_code.asm   # instruction lengths vs the listing
./emu/test/lockstep IMAGE.bin run 400000000                            # our CPU core vs MAME's, step by step
```

## Analysis tools

```sh
sh tools/regen.sh IMAGE.bin      # writes out/trace.asm, out/annotated.asm, out/census.txt, out/main_tree.txt
```

| Tool | Use |
|---|---|
| `tools/unsp-dasm IMG linear START END` | Linear disassembly between two CPU addresses (hex) |
| `tools/unsp-dasm IMG trace ENTRY…` | Follows control flow from the given entry points; marks functions and callers |
| `tools/scan.py IMG OUT` | Finds call targets across the image to use as extra entry points |
| `tools/census.py LISTING` | Counts references to each I/O register |
| `tools/funcs.py LISTING tree ADDR DEPTH` | Call tree labelled with the hardware each function touches |
| `tools/funcs.py LISTING users REG` | Functions that reference a register (for example `7868`) |
| `tools/funcs.py LISTING callers ADDR` | Functions that call an address |
| `tools/show.py ADDR[:MAXLINES]…` | Prints a function with compiler noise removed and registers named |
| `tools/iosum.py ADDR…` | Summarises a function as register reads and writes |
| `tools/lcdseq.py ADDR` | Extracts an LCD command/data sequence from a function |
| `tools/annotate.py LISTING OUT` | Adds register, function and scene-handler names to a listing |
| `tools/regnames.py SOC_CPP` | Rebuilds `analysis/regnames.json` from MAME's `generalplus_gpl951xx_soc.cpp` |

`funcs.py` must be run once on a listing before `show.py`, `iosum.py` or `lcdseq.py`; it writes the index `out/funcs.pkl` that they read. `analysis/tamasmart_eng_code.asm` works as the listing.

## Address conventions

- CPU addresses are word addresses. Flash byte offset = (CPU address − `0x9000`) × 2. The same rule extends to "linear" addresses above `0x3fffff` used by DMA, the flash driver and sprite data.
- Listings print `ds:[r3]` for accesses through the data segment and `[7809]` for direct addressing.
- The compiler's usual I/O idiom is `r3 = 7809 / ds = 0 / r4 = ds:[r3]`, so register references mostly appear as immediates, which is why the census has a "pointer loads" column.
