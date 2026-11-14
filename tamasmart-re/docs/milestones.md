# Milestones

Status on 13 Nov 2026 (evening): M0–M8 pass in `emu/tsmart-run`. The firmware runs from a blank device through time, birthday and name entry, the egg hatching, the room, the menus, idle sleep and wake, and a restart that offers CONTINUE and resumes the pet. From M10, card images pass: a SmaCard download completes and its character appears. M9 (sound) and the rest of M10 are not started. The runner prints the M1–M5 checks on every run.

Each milestone has a check that a headless run can evaluate. Addresses are for the English RAW image; look them up again in `analysis/symbols.csv` terms if working from another image.

| # | Milestone | Pass when | Typical failures |
|---|---|---|---|
| M0 | Tooling | Done. `tools/regen.sh` reproduces `analysis/tamasmart_eng_code.asm` | |
| M1 | CPU core executes reset code | PC reaches `0x009226` with RAM `0x0000–0x27ff` equal to `0xbcdf`; before that, RAM `0x0000–0x000a` held the delay loop | ADC ready bit never set (hang at `0x91ce`); battery comparison fails (loop at `0x91ee`) |
| M2 | RAM boot routine passes | The call at `0x00922b` returns (PC reaches `0x00922c`); RAM `0x047f/0x0480` hold the checksum `0x700a/0x0057` | `Power_State` (hang at `0x0102`/`0x010f`); JEDEC ID not in table (hang at `0x028e`); status register read-back mismatch; `Byte_Swap` missing |
| M3 | Reaches `main` | PC = `0x07c3c8`; RAM `0x0f1f–0x0f30` matches flash words at `0x095dfe` | RTC ready/reliable bit (loop `0x9232–0x9246`); segment handling in the table copy |
| M4 | Hardware init completes | PC reaches `0x07c4b3` and the LCD model has received exactly the bytes in `analysis/lcd_init_sequence.txt`; then `TFT_Ctrl` is written with `0x20cd` | `clock_init` timer loop; DMA status; `TFT_Status`; I2C status waits |
| M5 | Main loop turns over | `game_frame_update` (`0x07a637`) is entered on 60 consecutive frames; RAM `0x061e` increases by one per frame | Frame interrupt not delivered; `WAIT` not waking; interrupt nesting bugs (stack corruption shows up here first) |
| M6 | First picture | A non-blank 128×128 frame that matches the device's start-up screen | Palette format, sprite RAM banking, sprite DMA, layer enable bits |
| M7 | Input | Pressing the button changes RAM `0x05f3`; touching a pad sets its bit in RAM `0x0f1c`; the egg/start flow can be advanced | Timer C not firing (touch is sampled from its interrupt); touch counts below threshold; baseline never settles |
| M8 | Persistence | The firmware's flash erase/program commands are applied to the image; after a restart the device resumes instead of starting a new egg; the clock keeps time | NOR semantics (erase to `0xff`, program only clears bits); write routines running from RAM while flash is busy |
| M9 | Sound | Start-up jingle is recognisable; no drift against video over 10 minutes | Timer G/H rates; sample banking through `0x7810` |
| M10 | Extras | Step counting from simulated acceleration; microphone; card images; two instances linked | |

## Suggested order of work inside M1

1. Instruction decoder with length calculation; run the decoder cross-check from `emulator-architecture.md`.
2. ALU operations and flags for the six addressing modes.
3. Jumps, call, return, push/pop ranges.
4. `fr` access, interrupt enable/disable instructions, `call mr`.
5. Extended (`0xff80` prefix) instructions: only the forms that appear in `analysis/tamasmart_eng_code.asm` need to work at first. Search the listing for `Extended group` to enumerate them.
6. Multiply and the `divq` step used at `0x009177`.

## Evidence to collect from real hardware when available

| Measurement | Why |
|---|---|
| Photo or video of the boot screen and first minutes | Reference for M6 and for frame rate. In particular: is the boot logo on a grey panel, and what is the screen between the logo and the time setting? |
| Logic-analyser capture of the LCD write strobe | Real frame rate and whether frames are sent continuously |
| Which pad bit each screen region sets (use the factory "Input" test if it can be entered) | Touch geometry for M7 |
| Flash dump before and after a save | Confirms the save area for M8 |
| Chip marking on the SoC, if not a blob | Exact variant |
