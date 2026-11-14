# Tamagotchi Smart emulator: handoff

Start here. This package holds the firmware analysis and a working first-stage emulator (state as of 13 Nov 2026), with everything needed to continue without the original conversation.

## Goal

A standalone C++ emulator for the Bandai Tamagotchi Smart (2021), in the spirit of tamaemu: the user supplies their own 16 MB flash dump and the emulator runs it unmodified. Planned shape: a UI-free C++ core, an SDL desktop front end, and later a WebAssembly build with a JavaScript front end.

## Where things stand

The English firmware is playable in the emulator, silently: set-up, hatching, the room, menus, sleep, saving and resuming, and a full SmaCard download all work.

| Milestone | State |
|---|---|
| M0 tooling | Done |
| M1–M5 reset code, RAM boot routine, `main`, hardware init, main loop | Pass |
| M6 first picture | Pass. Every screen reached so far renders correctly; the time screen matches a video of a real unit |
| M7 input | Pass. Taps, swipes and the button all work, and the firmware's own gesture rules are documented (`docs/hardware.md`, "Input events") |
| M8 persistence | Pass. A saved flash image restarts to CONTINUE / RESET ALL, then "CHECK TIME IS CORRECT", then the room with the same pet. RESET ALL also works |
| M10 card slot | Pass for SPI NOR SmaCards: the Marine Change card is verified, downloaded into main flash, and its character appears (`docs/smacards.md`) |
| M9 sound; accelerometer, microphone, link | Not started |

Scenes seen running (RAM `0x0494`): `0x8400` logo, `0x8500` continue/reset, `0x8310` time and birthday, `0x8330` name entry, `0x1000` room with egg, `0x3740` hatching, `0x1060` room, `0x1400` menu, `0x3200` bath menu, `0x3700`, `0x3300` connection menu, `0xffe2` TamaSma Card, `0x9000` sleep.

What exists in `emu/`:

- **`core/cpu.h`, `core/cpu.cpp`**: a µ'nSP 2.0 interpreter written for this project. Latest lockstep run against MAME's core: 300 million instructions (756 frames) with identical registers, flags, timing and RAM at every step; instruction lengths match the disassembler for all 405,864 listed instructions. The fast interrupt never fired during that run, so that path is untested.
- **`core/machine.h`**: memory map, peripherals, event timing, sprite renderer, RTC, deep sleep, the card slot and the card flash model.
- **`sdl/main.cpp`**: the desktop app (SDL2). Mouse on four screen quadrants, arrow keys for swipes, Space for the button, card library with drag-and-drop, saves in `IMAGE.bin.flash`, clock in `IMAGE.bin.rtc`. Built and smoke-tested on Linux with SDL's dummy video driver, then tested on Windows 11 by the user.
- **`headless/main.cpp`**: command-line runner with milestone checks, hang detection, RAM watches, screenshots, scripted button/touch input, card insertion and I/O reports.
- **`test/`**: the lockstep and decode checks. These link MAME's GPL core; the emulator itself does not.

## How to drive the device (learned 13 Nov 2026)

- **Tap** = hold a pad for at least 12 frames (0.4 s). **Swipe** = touch one half of the screen briefly, then the other half within 10 frames. The firmware decides this, not the emulator.
- First set-up: tap SET (bottom-left) through year, month, day, hour, minute, birthday; on the name screen the **button** ends entry; then a tap confirms. About 60 s later the egg hatches. The outer two bumps on the device's physical shell are decorative (confirmed by the owner); there is one button.
- In the room the button opens the menu (two pages of four icons; **swipe up/down** changes page). Page 2 bottom-right is the connection menu, which holds TAMAGOTCHI SMART (link) and TAMASMA CARD.
- A scripted run that does all of this is in `README.md` ("Scripted walkthrough").

## Fixes made to get here (each was a real blocker)

| Symptom | Cause | Fix in `machine.h` |
|---|---|---|
| Egg never hatched, button did nothing in the room | RTC counter did not run | 48-bit 32,768 Hz counter, registers `0x10–0x15` load and `0x30–0x35` read |
| Swipes always read as taps | Touch scan modelled at 134 ms; the firmware samples once per Timer C tick | Conversion time 50 µs, so a scan is 45 ms; smaller touched count |
| Device hung on entering sleep | `IOB_Latch` read as 0, so "button released" never came | Latch registers read the pins |
| Device woke straight after sleeping | Key-change flags never cleared; sleep register ignored | `0x780d` = `0x500a` stops the CPU; `0x78ab` and `0x781f` bit 7 are write-1-to-clear; wake on enabled pin change or RTC |
| "NO CARD FOUND" with a card inserted | Detect pin polarity depends on slot power | Port C bit 9 follows the card supply (bit 10 low = on) |

## What a video of a real unit settled (13 Nov 2026)

The user filmed a cold boot, menu navigation and time setting on a real Smart. Compared with the emulator:

- **Frame rate: 30.1 frames per second.** The blinking field on the time screen toggles every 15 game frames in the emulator and every 0.498 s on the device.
- **Touch layout: four quadrants.** Pad 2 top-left, pad 1 top-right, pad 3 bottom-left, pad 0 bottom-right. The firmware draws its touch ripple and button highlights in those positions, and the video shows the same actions from the same places.
- **The time screen renders identically** (layout, fonts, colours allowing for the camera).
- **CPU speed:** before each save the firmware stops the display and runs a one-million-iteration delay loop. That takes about 1 s on the device and 1.6 s with MAME's instruction costs at 48 MHz, so the emulator now runs the CPU at twice that speed (`cpu_speed_num/den` in `machine.h`). Treat this as a first calibration.
- **Panel memory:** the LCD keeps its last frame while the SoC is not streaming. The emulator now models this (`lcd[]`), so the picture freezes during saves instead of going blank.
- **Still different from the device:**
  - On the device, screens fade in from white and fade out to black. In the emulator both go through black. The fade state is the word at RAM `0x0b27` (bits 2–3 type, bit 1 direction, bits 10–15 level), set by `0x0607e5`.
  - The boot logo stays up for about 1.8 s on the device and about 0.5 s in the emulator. The logo scene starts something with `0x00ee8c(0x3c, 0x80, …)` and waits for `0x08a246(handle)` to report completion, which is most likely the start-up jingle. It should fix itself once sound is modelled.
  - The device is white for the first second; the emulator shows about 0.8 s of black before the logo.
  - The filmed unit had a save, so after the logo it showed CONTINUE / RESET ALL, then "CHECK TIME IS CORRECT", then the room. The emulator now does the same from a saved flash image.
- **Shell:** three bumps under the screen, but only the middle one is a button; the outer two are decorative (owner confirmed).

## What is in this package

| Path | What it is |
|---|---|
| `HANDOFF.md` | This file |
| `README.md` | How to build and run the tools and the emulator |
| `docs/hardware.md` | The SoC, memory map and every peripheral the firmware uses |
| `docs/boot-and-runtime.md` | Boot sequence, interrupts, main loop, scene system, saves |
| `docs/smacards.md` | SmaCard format, slot wiring, what a download does, and card support in the emulator |
| `docs/emulator-architecture.md` | Target C++ design and test strategy |
| `docs/milestones.md` | Milestones with pass/fail checks and current status |
| `analysis/tamasmart_eng_code.asm` | Disassembly of the English RAW image with register and function names |
| `analysis/symbols.csv` | Functions identified so far (112) |
| `analysis/scene_table.csv` | The firmware's 200 scenes: id, init, update, exit and extra handlers (all now disassembled and labelled in the listing) |
| `analysis/register_census.csv` | Every I/O register the firmware references |
| `analysis/lcd_init_sequence.txt` | The LCD command sequence the firmware sends |
| `analysis/ramcode_boot_RAM0100.asm` | The boot routine that runs from RAM |
| `emu/` | The emulator and its tests |
| `tools/` | Disassembler wrapper and analysis scripts |

The firmware images are not included. All addresses are from `TMGC_Smart_FW_RAW_[ENG].bin` (SHA-1 `1d2fc1a8…`). The Japanese Mint image has the same boot block but different function addresses (`main` is at `0x07ca60`). The files can be found at `C:\Users\Katu\Documents\TamagotchiStuff\ROMS\Tamagotchi Smart`

## Facts that matter most

1. **Chip:** GeneralPlus GPL951xx-family SoC, µ'nSP 2.0 CPU (16-bit words, word addressing, 22-bit program counter). RAM is `0x0000–0x27ff`.
2. **Flash is executed in place.** CPU address = flash byte offset / 2 + `0x9000`. `0x200000–0x3fffff` is a banked window (register `0x7810`). DMA, the flash driver and the picture hardware also use *linear* addresses beyond 22 bits with the same rule (for example `0x807000` is flash byte `0xffc000`).
3. **Vectors** at CPU `0xfff5–0xffff`; reset goes to `0x918e` and ends at `main` (`0x07c3c8`). The chip's internal boot ROM is not needed: the firmware never calls it.
4. **Header checksum** = sum of bytes `0x80–0xdfff`; verified by the firmware at boot.
5. **Picture:** the firmware uses only sprites. Up to 96 sprites, 4 words each, in "direct address" mode where tile number × 8 is the linear address of the pixel data. The ST7735S LCD just receives an init sequence and then the finished 128×128 frame.
6. **Touch is four capacitive pads**, reported as a 4-bit mask, not coordinates. Taps need a 0.4 s hold; swipes are half-to-half sequences.
7. **One button:** port B bit 2, active low. It also wakes the device from sleep.
8. **Saves:** 594 bytes, written to two slots at flash `0xffc000` and `0xffd000` on every scene change.
9. **Battery:** ADC channel 7. Too low a reading forces a "battery empty" scene that saves and powers down, which looks like a hang if not modelled.
10. **Scenes:** the game is a table of 200 scenes, each with init/update/exit handlers. RAM `0x0494` holds the current scene id, which makes progress easy to observe.
11. **Clock:** a 48-bit RTC counter at 32,768 Hz. The game's time comes from it, so it must run.
12. **Sleep:** after about 34 s idle the firmware stops the SoC's clocks (`0x780d` = `0x500a`) until the button changes or the RTC fires.
13. **Card slot:** SPI0 with chip select, power and detect on port C. Content cards are 2 MB SPI NOR; a download copies the card's data package to main flash `0x32f000–0x4f8fff` and writes a use record back to the card.
14. **Other hardware, not yet modelled:** I2C accelerometer (7-bit address `0x18`), microphone ADC, sound unit and two DACs, and a "ComAir" sound-based link.

## Things that turned out differently from the first plan

- MAME already lists this device (`tamasmrt`, not working) and documents the chip's registers, but needs the internal ROM. The standalone route proved quicker.
- The display needs the SoC's sprite hardware modelled, not just the LCD.
- Touch is four zones, not a resistive X/Y panel.
- The Niziu image has a different layout and fails the checksum. Do not develop against it.
- The `SmacardVer` firmware file is the RAW image behind a 0x400-byte update header. Use RAW.

## Recommended next steps, in order

1. **Run the desktop app on Windows** (`README.md` has the build steps) and play it by hand. Everything so far was driven by scripts; a person will find rough edges in mouse taps and swipes first. Tune from there. EDIT: this has been done
2. **Sound**: port the sound unit and DAC channels (M9). It is the largest missing piece and should also fix the short boot logo.
3. **Clock policy**: the app freezes the clock while closed (or catches up with `--realtime`). Decide what users want, and check what the firmware does after a long gap. Also confirm the RTC second/alarm wake-ups during sleep; the device slept through a 50 s test without one.
4. **Play further**: feeding, bath, games, growth stages, outings, the "current card" content. Use `--watch 494` and screenshots; each new scene may need a hardware feature (blending, more sprite modes).
5. **Fade from white**, battery scale, CPU clock: the remaining differences from the video.
6. **Accelerometer (steps), microphone, device-to-device link.**
7. **Speed**: the interpreter runs about 200 frames per second on a desktop core. Enough for real time, thin for 8× fast-forward and for WebAssembly. Profile shows no single hot spot; a decoded-instruction cache is the usual next step.
8. Split `machine.h` into the per-peripheral files described in `docs/emulator-architecture.md`, then the WebAssembly build.

## Open questions

| Question | How to answer it |
|---|---|
| Why fades come from white on the device but from black in the emulator | Read the palette code at `0x060049` and the fade type bits in RAM `0x0b27`; check whether the palette powers up white |
| Exact CPU clock and per-instruction costs | Time more firmware delay loops against video, or capture a GPIO toggle with a logic analyser |
| Real touch counts and conversion time | Assumed values work with the firmware's thresholds; measure on hardware if gestures feel wrong |
| Battery ADC scaling | Read the maths at `0x08b5af`, or measure channel 7 on hardware at known voltages |
| Does the fast interrupt stack the flags register? | The firmware's handler suggests not (see `docs/hardware.md`); confirm when Timer F is running |
| Why the boot logo sits on grey and the next screen is black | Compare with a real unit; likely unmodelled blending or palette effects |
| What register `0x78b2` really is | The card code writes `0xe000` to it when enabling the slot; the emulator treats it as time base C |
| The card's 16 bytes at `0x40`, the device identity, main-flash sector `0x52d000` | See "Open points" in `docs/smacards.md` |
| SD-protocol (firmware-update) cards and the 0x400-byte update header | Model SD SPI mode on SPI0 and run the updater near the "Erasing Card…" strings (`0x0955ea`) |
| How to enter the factory test and scene-select menus | Trace references to `DUCK_TM_TESTMODE` (`0x0943e1`) and scene ids `e000`–`ffe8` |
| Does a wall item (a wreath) flicker in the room, or is that a rendering fault | It is present in some frames and absent in others; compare with a device |

## Licence note

The emulator in `emu/core` and `emu/headless` contains no MAME code. Its CPU core was written from the instruction set's behaviour, using MAME's core as the reference to test against. `emu/test/mame_ref` is adapted from MAME and is GPL-2.0+; it is used only by the tests and the optional trace disassembler. `tools/` compiles MAME's disassembler, fetched by `tools/build.sh`. Register names and several peripheral behaviours were learned from MAME's BSD-3-Clause GPL951xx sources. I am not a lawyer; check before publishing.
