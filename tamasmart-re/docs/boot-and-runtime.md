# Boot sequence and runtime structure

Addresses are CPU word addresses in the English RAW image. "Needs" lists what an emulator must provide for the firmware to get past each step. Every loop marked **polls** will hang an emulator that returns the wrong value.

## 1. Reset code (`0x00918e–0x009263`)

| Step | What the firmware does | Needs |
|---|---|---|
| 1 | Interrupts off, `sp = 0x27ff` | Start with `pc = [0xfff7]`, `sr = 0` |
| 2 | Copies an 11-word delay loop from `0x926a` to RAM `0x0000` | RAM executable |
| 3 | Port E setup, `SYS_CTRL |= 0x0c`, ADC setup, 60 delay calls | Plain register storage |
| 4 | **Polls** `MADC_Ctrl` (`0x79a1`) for channels 6 and 7: writes `ctrl | 0x46`, waits for bit 15, retries if bits 4–5 are set, reads `MADC_Data` (`0x79a2`). Then requires `2.625 × (ch6 >> 4) ≤ (ch7 >> 2)`; otherwise it powers down in a loop at `0x91ee–0x9219` | ADC that sets bit 15 and returns values satisfying the comparison (zeros work) |
| 5 | Fills RAM `0x0000–0x27ff` with `0xbcdf`, `Clock_Ctrl = 0x8008` | |
| 6 | `copy_ramcode` (`0x9275`): 0x5c3 words from `0x00a000` to RAM `0x0100`, then `call mr` to `0x000100` | The 2.0 extended instructions (`r8–r15`), `ds:` addressing, `call mr` |
| 7 | RAM routine (section 2) | See below |
| 8 | **Polls** the RTC: `RTC_Ctrl = 1`, `RTC_Addr = 1`, `RTC_Request = 2`, waits for `RTC_Ready` bit 0, reads `RTC_ReadData`; repeats until bit 0 is 1 | RTC that reports ready and returns 1 for register 1 |
| 9 | Walks the table at `0x9040`: one entry copying 0x12 words from `0x095dfe` to RAM `0x0f1f`. Uses `sp` as the table pointer | `sr` data-segment bits honoured by `ds:[r3++]` |
| 10 | `sp = 0x27ff`, `fr |= 8`, `irqnest on`, `goto 0x07c3c8` | Flags register, nested-interrupt mode |

## 2. Boot routine in RAM (`0x0100–0x06c2`, source `0x00a000`)

Listing: `analysis/ramcode_boot_RAM0100.asm`.

1. **Polls** `Power_State` (`0x780f`) until its low 3 bits are non-zero, sets `PLL_Sel = 0x0e18` and `Clock_Ctrl = 0x8008`, then **polls** until the low 3 bits equal 2.
2. Configures the flash controller for a plain read (`0x03`), then calls `0x04a9` and `0x063e`.
3. `0x023b`: compares 16 words at `0x9000` with a reference copy of the header magic held at RAM `0x01e0`, and checks that the two checksum copies at `0x9010` and `0x9012` match. On failure it steps `SPIFC_TIMING` (`0x7b49`) down and retries; when exhausted it tries extended-address mode.
4. `0x05ee`: reads the JEDEC ID with command `0x9f` and searches the 7-word-per-entry table at RAM `0x069d`. **No match leads to an infinite loop at `0x028e`.** Table rows:

   ```
   c218 0002 bf40 3f80 ffff 060f ffff
   c818 0001 0000 fd02 0000 0420 ffff
   a118 0002 0000 fd02 0000 0420 ffff
   a117 0002 0000 fd02 0000 0420 ffff
   ```

5. `0x0350`: reads status registers (`0x05`, then `0x15` for Macronix or `0x35` otherwise), applies the mask/value pairs from the table row, writes them back (`0x01`, `0x31`, `0x11`), then re-reads and compares. A mismatch loops.
6. `0x02d6`: switches the controller to quad read (`0x20eb`, or `0x20ec` with extended addressing).
7. `0x0294`: re-verifies the header through the fast path, stepping `PLL_Sel` if needed.
8. `0x0136`: checksum. Reads the end address from `0x9018` (`0xffff`), then for each word from `0x9040` to `0xffff` adds the low byte and, through `Byte_Swap` (`0x7af0`), the high byte. The 32-bit total must equal the dword at `0x9010`. On failure it retries with slower timing and finally powers down at `0x0183`.

Needs: a flash-controller model with a receive FIFO, JEDEC ID `C2 20 18`, writable status registers, `Byte_Swap`, and direct mapped reads that do not depend on timing settings.

A shortcut exists for early bring-up: return from `0x0100` immediately. The caller ignores `r1`. Treat this as temporary, because the routine leaves state in RAM `0x0481–0x048e` (for example the matched flash-table row) that later flash writes probably rely on, and the block itself must still be copied.

## 3. `main` (`0x07c3c8`)

Compiler-generated, unoptimised C. Arguments are passed on the stack and I/O is done through far pointers (`r3 = 0x7809; ds = 0; r4 = ds:[r3]`).

| Order | Call | Purpose |
|---|---|---|
| 1 | `mem_fill_dma` | Clears memory using system DMA channel 0 (`0x7a80–0x7a86`, status `0x7abf`) |
| 2 | inline | `PM_Ctrl`, `PLLWaitCLK`, `Watchdog_Ctrl = 0x8000` |
| 3 | `clock_init` `0x07c507` | Time Base C and Timer A. **Polls** `TimerA_UpCount` against 20,000 |
| 4 | inline | `P_SPI_Improve |= 0x20`, `SYS_CTRL |= 0x20`, `PLL_Sel |= 0x100` |
| 5 | `rtc_init` `0x07d580` | |
| 6 | `0x06ac2f`, `0x08f474` | Not identified |
| 7 | `gpio_init` `0x07c57b` | Pin directions; key-change interrupts cleared; calls into the SPI0 and card-detect helpers |
| 8 | `audio_dac_init` `0x07c83d` | |
| 9 | `touch_init` `0x08e343`, `0x08e39f(0x30)`, `touch_start_scan` `0x08e37a` | Starts the first touch conversion |
| 10 | `0x08f480(…)`, `0x08f4c3(…)` | Not identified |
| 11 | `mic_init` `0x090e2c`, `0x090edb` | |
| 12 | `0x08f5bb(0x0f74, 0x148c)` | Not identified (buffer setup?) |
| 13 | `lcd_init_wrapper` `0x077fbd(0)` | TFT timing plus the ST7735S command sequence. **Polls** `TFT_Status & 0xf0 == 0` before every byte |
| 14 | `gpio_set_pin(3, 0)`, then `TFT_Ctrl = 0x20cd` | Continuous output on |
| 15 | `ppu_init` `0x08aefa`, `ppu_config` `0x08adb7(0,0,1,0,1,1,0,0,0,0)` | |
| 16 | `game_init` `0x07a588` | Palette, sprites, sound |
| 17 | `frame_irq_clear`, `frame_irq_enable`, `ppu_enable`, `fiq on` | |
| 18 | `set_tft_h_start(2)` | |

Main loop at `0x07c4fe`:

```
loop:
    wait_frame()          ; 0x08aeac: write WAIT (0x780c) = 0x5005 until RAM 0x061e changes
    watchdog_clear()      ; 0x092626
    game_frame_update()   ; 0x07a637
    goto loop
```

`game_frame_update` calls, among others, sprite DMA (`0x044f1d`), layer setup (`0x08ddc6`), palette/fade (`0x060049`), `button_poll` (`0x08e812`), the accelerometer driver (`0x078ae4`) and two large functions (`0x07a6ca`, `0x060236`) that look like the scene state machine.

## 3a. Scenes, saves and what the first seconds look like

`scene_step` (`0x07a6ca`, called from `game_frame_update`) runs one scene at a time from a 200-entry table at `0x093230` (`analysis/scene_table.csv`). Each entry is 9 words: id, then far pointers to init, update, exit and one more handler.

| RAM | Meaning |
|---|---|
| `0x0494` | Current scene id |
| `0x0495` | Requested next scene (`0xffff` = none) |
| `0x0496` | Previous scene |
| `0x0498` | Bits 0–2: phase (0 init, 1 running, 2 exiting); higher bits: flags |

On every scene exit the firmware calls `save_now` (`0x038cfb`), which rewrites both save slots. After each frame it also checks `battery_status()` and can force scenes `0x9030` or `0x9031`.

Before writing, `save_prepare` (`0x091a1f`) stops LCD streaming, runs a delay loop of 1,000,000 iterations, stops sound and the picture processor and disables interrupts; `save_finish` (`0x091a4e`) undoes that. Every scene change therefore freezes the picture for about a second on real hardware.

Observed start-up in the emulator (30 frames per second, CPU at twice MAME's 48 MHz costs):

| Frame | Event |
|---|---|
| 0–20 | Reset code delays and battery check |
| 20–21 | RAM boot routine: flash ID, quad enable, checksum |
| 21–49 | `main` hardware init; most of it is LCD reset delays |
| 50 | Main loop starts |
| 51–75 | First save (scene 0 exit): display frozen during the delay loop |
| 75–103 | Scene `0x8400`, boot logo: fade in, hold, fade out. Its states are 0 → `0x64` → `0x3e8` (waits for `0x08a246`) → `0x7fe0` → `0x7fe1` → `0x7ff0` |
| 103–126 | Scene `0x8500`, then another save |
| 126 | Scene `0x8310`: "CURRENT TIME 2021 01/01 pm 12:00" with SET and GO BACK; visible from about frame 150 after its save and fade-in. It then waits for input |

Flash writes go through a driver that the firmware copies into RAM `0x0100` before use (`flash_driver_load`, `0x0906fb`): write enable, status polls, sector erase `0x20`, page program `0x02` in 256-byte pieces, then back to quad read. Addresses given to it are linear (`0x807000 + slot × 0x800`).

## 4. Interrupts

| Vector | Handler | Source flag checked | Action |
|---|---|---|---|
| Fast interrupt | `0x00904a` | `INT_Status3` bit 13 | Calls `0x08c9ef`; `TimerF_Ctrl |= 0x8000` |
| 0 | `0x00905f` | | `reti` |
| 1 | `0x009060` | `INT_Status2` bit 0 | Microphone sample `0x79b4` to RAM `0x0d74`; rewrites `0x79b3` |
| 2 | `0x00906f` | `INT_Status1` bit 0 | Sets RAM `0x0d95 = 1`; acknowledges |
| 3 | `0x00907c` | | `reti` |
| 4 | `0x009095` | `INT_Status3` high byte; then Timer A, B, C control bit 15 | Calls `0x073f61`, `0x08fb32`, and (`0x08e40c` touch processing, `0x08e3af` touch sampling, `0x00ffa6`, RAM `0x0f23`++) respectively; clears Timer D. Then sound unit `0x7b85` bit 14 calls `0x03b7b1` |
| 5 | `0x0090da` | `INT_Status1` bit 13; `0x7063` bits 14 and 0 | Bit 14: `HB_Ctrl`++. Bit 0: frame counter RAM `0x061e`++ |
| 6 | `0x009110` | `INT_Status2` bit 7 (touch), Time Base C bit 15, `INT_Status2` bit 10 | Touch flag; clock calibration from `TimerH_UpCount` into RAM `0x0607/0x0608`; tick RAM `0x0606`++ |
| 7 | `0x009151` | `INT_Status2` bit 1 | Calls `0x07daf7` (RTC) |

Consequence: touch sampling and processing are driven by **Timer C**, so Timer C must fire for touch to work at all, and the frame counter only advances when the frame interrupt fires.

## 5. Useful RAM variables

| Address | Meaning |
|---|---|
| `0x061e` | Frame counter (incremented in interrupt 5) |
| `0x061d` | Last frame seen by `wait_frame` |
| `0x0492` | Re-entrancy guard for the frame interrupt |
| `0x05f2`, `0x05f3`, `0x05f7` | Button held, pressed, released |
| `0x0d57–0x0d5a` | Filtered touch readings, channels 0–3 |
| `0x0d5b–0x0d5e` | Touch baselines |
| `0x0f1c` | Touch bitmask (`0x100` per channel, shifted) |
| `0x0f17` | Touch state flags (`0x8000` running, `0x4000` scanning, `0x1000` sample ready) |
| `0x0606` | Tick counter from interrupt 6 |
| `0x0607`, `0x0608`, `0x061a` | Clock calibration state |
| `0x0481–0x048e` | Flash controller work area used by the RAM routine |

## 6. Debug and test features worth knowing about

- Factory test menu strings at `0x094379` onward and a scene-name list at `0x093a0b` (`TAIKI00`, `GOHAN`, `TUUSIN`, `EVOLUTION`, `TEST MODE`, `SHUTDOWN` …). A scene selector exists; how to enter it has not been traced. The identifiers `DUCK_TM_COLDBOOT` and `DUCK_TM_TESTMODE` at `0x0943e1` are a starting point.
- Build date string: `Apr 20 2022` at `0x095df2`.
- An emulator option to force the test menu would exercise every peripheral model one at a time.

## First set-up and play flow (observed in the emulator, 13 Nov 2026)

| Scene id | What it is | Leaves by |
|---|---|---|
| `0x8400` | Boot logo | Timer |
| `0x8500` | CONTINUE / RESET ALL (only when a save exists); RESET ALL asks REALLY? | Tap |
| `0x8310` | Set time, then birthday | SET through each field |
| `0x8330` | "YOUR NAME?" | The button ends entry, then a tap confirms |
| `0x1000` | Room with the egg | About 60 s of RTC time |
| `0x3740` | Hatching | Runs by itself |
| `0x1060` | Room with the pet | Button opens the menu; about 34 s idle leads to sleep |
| `0x1400` | Menu, two pages of four icons | Tap an icon; swipe up/down changes page; button closes |
| `0x3200`, `0x3700` | Bath/toilet menu (bottom-right icon); the scene behind the top-right icon, not examined | |
| `0x3300` | Connection menu: TAMAGOTCHI SMART, TAMASMA CARD | Tap; times out to the room |
| `0xffe2` | TamaSma Card: DOWNLOAD / CURRENT CARD | See `docs/smacards.md` |
| `0x9000` | Sleep | Button |

After a restart with a save: logo, CONTINUE, "CHECK TIME IS CORRECT", room. The time check is probably shown because the RTC's marker register (register 2 = `0x5a`) was lost, as after a battery change; not yet confirmed with a preserved RTC.
