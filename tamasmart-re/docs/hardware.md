# Hardware as seen by the firmware

Sources: static analysis of the English RAW image, MAME's GPL951xx SoC source (register names and map), and Caralynx's 2021 teardown (flash chip, board name "Duck"). The firmware's own factory test menu confirms the hardware list: `SHIPPING, LCD, BackLight, ROM, SAVE, Card, Input, ACC., Battery, MIC, Sound, ComAir, Current`.

Confidence: **high** means read directly from code or data; **medium** means inferred from context; **low** means a guess to verify.

## Memory map (CPU word addresses)

| Range | Contents | Confidence |
|---|---|---|
| `0x000000–0x0027ff` | RAM, 10,240 words. Stack starts at `0x27ff` and grows down | high |
| `0x007000–0x0077ff` | Picture processor registers, line RAM, palette, sprite RAM | high (MAME) |
| `0x007800–0x007bff` | System, GPIO, interrupts, timers, serial buses, ADC, RTC, DMA, touch, flash controller | high (MAME) |
| `0x007b80–0x007fff` | Sound unit | high (MAME) |
| `0x008000–0x008fff` | Internal boot ROM. Never referenced by the firmware | high |
| `0x009000–0x1fffff` | SPI flash, executed in place. Flash byte offset = (address − `0x9000`) × 2 | high |
| `0x200000–0x3fffff` | Banked flash window. Flash word = `0x1f7000` + bank × `0x200000` + (address − `0x200000`); bank is register `0x7810` bits 0–5 | high (MAME) |

Words are little-endian in the flash file.

### RAM regions the firmware sets up

| RAM | Use |
|---|---|
| `0x0000–0x000a` | Delay loop copied from flash `0x926a` at reset; called with the count in `r1` |
| `0x0100–0x06c2` | Boot and flash-control routines copied from flash `0xa000` (0x5c3 words). Must stay intact; flash writes probably run from here |
| `0x0f1f–0x0f30` | Initialised data (0x12 words copied from `0x095dfe`) |
| everything else | Filled with `0xbcdf` at reset before use |

## CPU

µ'nSP 2.0. Registers: `sp, r1–r4, bp, sr, pc`, plus `r8–r15` (2.0 extension) and a flags register `fr`.

- `sr` bits 0–5 are the code segment (high bits of the 22-bit PC), bit 6 carry, bit 7 sign, bit 8 zero, bit 9 negative, bits 10–15 the data segment.
- Vectors: `0xfff5` break, `0xfff6` fast interrupt, `0xfff7` reset, `0xfff8–0xffff` interrupts 0–7.
- The firmware enables nested interrupts (`irqnest on`, `fr |= 8`). With nesting on, an interrupt pushes `pc`, `sr` and `fr`, and only higher-priority lines can pre-empt.
- The fast interrupt appears not to stack `fr`: the firmware's handler at `0x00904a` saves and restores it by hand, while ordinary handlers rely on `reti` to restore it. The emulator's core follows that reading; MAME pops `fr` on every `reti` when nesting is on. Untested, because Timer F has not fired yet.
- MAME runs this SoC at 48 MHz (`96000000/2`) and notes about 2 cycles per instruction on average.

## Peripherals in use

Register names are GeneralPlus's, as recorded in MAME. `analysis/register_census.csv` has the full list with counts.

### SPI flash controller (`0x7b40–0x7b4b`) and the flash chip. Confidence: high

- The controller both maps the flash for execution and sends manual commands.
- Boot code reads the JEDEC ID (command `0x9f`) and looks up `(manufacturer << 8) | capacity` in a table. Accepted values: `0xc218`, `0xc818`, `0xa118`, `0xa117`. The real chip is a Macronix KH25L12833F, so return `C2 20 18`.
- Commands seen: `0x03` read, `0xeb`/`0xec` quad read (for execute-in-place), `0x05`, `0x15`, `0x35` status reads, `0x01`, `0x31`, `0x11` status writes, `0x06` write enable, `0x66`/`0x99` reset, `0xb7` enter 4-byte mode.
- The firmware sets the quad-enable bit and reads it back, so the status registers must retain written values.
- Register `0x7af0` (Byte_Swap) must return the last written value with its bytes swapped; the checksum loop depends on it.

### Display. Confidence: high

- TFT controller registers `0x7050–0x705b`, `0x706c–0x706f`, plus `Free_Width`/`Free_Height` (`0x70dc`/`0x70db`) = 128.
- LCD commands go out through `TFT_MemMode_WCmd` (`0x705b`): write the byte, wait for `TFT_Status` (`0x705a`) bits 4–7 to read zero, then write `TFT_Ctrl` (`0x7050`) = `0xa089` for a command or `0xa0a9` for data.
- Init sequence (full list in `analysis/lcd_init_sequence.txt`): software reset, sleep out, frame-rate and power settings, gamma, then pixel format `0x3a = 05` (RGB565), tearing effect on, `0x36 = 00`, column window 2–129, row window 1–128, display on, memory write.
- After init, `TFT_Ctrl = 0x20cd`: I80 continuous mode, enabled. The SoC then streams the picture processor's output to the panel every frame.
- Two GPIO helpers toggle pins around the reset: `gpio_set_pin(6, x)` drives port B bit 6, and `main` later calls `gpio_set_pin(3, 0)` on port A bit 3. One is probably LCD reset and the other backlight or panel power (low).

### Picture processor (`0x7000–0x77ff`). Confidence: high for what the first screens use

Same family as the GeneralPlus GPL162xx video block that MAME emulates (`generalplus_gpl162xx_soc_video.cpp`, `gpl_renderer.cpp`). Observed by running the firmware through the boot logo and time screen:

- **Sprites only.** The tile layers are never enabled; `Tx1` control is written with 0 every frame.
- `SControl` (`0x7042`) = `0x60f5`: sprites on, 96 sprites, direct-address mode. `PPU_Enable` (`0x707f`) = `0x006b`. `Palette_Control` (`0x703a`) = 5. `Segment_sp` = 0. `Fade_Control` stays 0.
- **Sprite RAM** `0x7400–0x77ff` has two banks selected by `PPU_RAM_BANK` (`0x707e`). Bank 0 holds 4 words per sprite: tile, x, y, attributes. Bank 1 holds one extension word per sprite: bits 0–7 are tile bits 16–23, bits 8–13 a blend level.
- **Attributes:** bits 0–1 colour depth (2, 4, 6 or 8 bits per pixel), bit 2 flip x, bit 3 flip y, bits 4–5 width (8 << n), bits 6–7 height (8 << n), bits 8–11 palette, bits 12–13 priority, bit 14 blend, bit 15 palette +0x200.
- **Pixel data** is at linear address tile × 8 (flash word = address − `0x9000`), one row after another, each word byte-swapped and read most significant bit first.
- **Palette** `0x7300–0x73ff`: four banks selected by `Palette_Control` bits 2–3; RGB555 with bit 15 meaning transparent.
- **Sprite DMA** (`0x7070` source, `0x7071` target, `0x7072` count): each frame the firmware copies `0x180` words from RAM `0x0b35` and then waits for `0x7072` to read 0.
- **The panel has its own memory.** Before a save the firmware writes `TFT_Ctrl = 0`, and afterwards re-sends LCD command `0x2c` and `TFT_Ctrl = 0x20cd`; the picture stays frozen in between. The panel powers up white.
- **Frame rate:** 30.1 per second, measured from video of a real unit.
- **Coordinates** are centred. The emulator uses an origin of (64, 128), found by eye from the two screens seen so far; MAME's renderer uses (160, 128) for a 320×256 field. The x value needs checking against more screens.
- Not yet explained: the boot logo renders on a grey panel, and the second start-up scene (`0x8500`) draws four sprites that come out black. Blending or palette effects are the likely cause.
- Frame interrupt: `TFT_INT_EN` (`0x7062`) bit 0 enables, `TFT_INT_CLR` (`0x7063`) bit 0 is the frame flag, bit 14 a second source that increments `HB_Ctrl` (`0x7073`).

### Touch: capacitive sense block (`0x7ac0–0x7ac9`). Confidence: high

- Four channels on port F bits 8–11. `CTS_CH` values per channel: `0x88, 0x99, 0xaa, 0xbb`.
- Setup: `CTS_DIV = 1`, `CTS_Ctrl3 = 1`, `CTS_Ctrl2 = 2`, `CTS_Ctrl1 = 0x048e`, `CTS_CYCLE = 0x34`. A conversion starts with `CTS_Ctrl1 |= 0x8001` and ends with an interrupt (`INT_Status2` bit 7, interrupt line 6). The count is read from `CTS_CNT` (`0x7ac9`) after writing 0 to it.
- Each channel is converted 42 times. The sum, minus the smallest and largest reading, is averaged with the previous value into RAM `0x0d57 + channel`. Baselines live at `0x0d5b + channel`.
- The firmware works on sample − baseline, and a positive difference means touch. The per-channel tables hold `0xfc` and `0x6c`, which appear to be the press and release levels.
- Result: bitmask at RAM `0x0f1c`, with channels 0–3 mapped to bits `0x0100, 0x0200, 0x0400, 0x0800`.
- Tables are in flash at `0x091167–0x0911be`.
- Sampling is paced by Timer C (3,750 interrupts per second): one conversion per tick, so a scan of all four pads takes about 45 ms. Debounce is one scan (table `0x09116b`). Because each scan is averaged with the previous one, a release takes about three scans to fall below the release level.
- The emulator returns `0x100` per reading and `0x110` when touched (`cts_touch_delta`), and finishes a conversion in 50 µs. Both are assumptions chosen so the firmware's own thresholds and timing work; real counts and conversion times are unknown. Measured in the emulator: a press shows up after about 3 frames and a release after about 6.
- **Pad geometry: quadrants (confirmed).** Pad 2 is top-left, pad 1 top-right, pad 3 bottom-left, pad 0 bottom-right. `touch_frame_update` (`0x02005c`) turns the mask into RAM `0x0609` using the table at `0x095885`: bit 0 top-left, bit 1 bottom-left, bit 2 bottom-right, bit 3 top-right; bits 4–7 previous frame, 8–11 newly pressed, 12–15 newly released.

### Input events (taps and swipes). Confidence: high

Scenes do not read pads directly. They keep a list of up to 27 two-word event entries and call `input_events_update` (`0x0201d2`) each frame; each entry's type (bits 0–5) selects a handler from the table at `0x095889`, and bit 6 is set when the event fires (`input_event_fired` `0x02023a`).

| Type | Meaning |
|---|---|
| 6, 7, 8, 9 | **Tap** on one pad: the pad must stay held for **12 frames** after the press. Releasing earlier cancels |
| `0x0a` | Swipe left: press on the right half (while the left is not held), release, then the left half touched within 10 frames |
| `0x0b` | Swipe right (left half, then right half) |
| `0x0c` | Swipe up (bottom half, then top half) |
| `0x0d` | Swipe down (top half, then bottom half) |
| others | Not yet read (0–5 and `0x0e`–`0x1a`; several test combinations of halves and hold times) |

So a tap is a press of about 0.4 s, and a swipe is a short touch on one half followed quickly by the other half. The main menu (scene `0x1400`) has two pages of four icons and turns pages on swipe up and swipe down. For scripted input: a swipe is 6 frames on the first pad then 6 on the second; a tap is 14 or more frames.

### Button. Confidence: high

- `button_poll` (`0x08e812`) reads `IOB_Data` (`0x7868`); the button is bit 2, active low, reported as key bit `0x02`.
- RAM `0x05f2` held, `0x05f3` newly pressed, `0x05f7` newly released, `0x05f6` idle counter.
- `PwrKey_State` (`0x7802`) is referenced once, in the sleep code at `0x06f0dd`. The same button may also be the SoC's power key.

### Accelerometer over I2C (`0x78c0–0x78c6`). Confidence: high

- Device address byte `0x30` (7-bit `0x18`). The driver reads registers `0x28–0x2d` one at a time, which is the X/Y/Z output layout of ST's LIS2DH family and its clones.
- I2C setup: `I2C_Ctrl = 0x90`, `I2C_Clk = 0x80`, `I2C_Debounce = 0x10`, `I2C_MISC = 1`.
- Driver functions: `0x078680–0x078a87`.
- Used for the step counter and the "Turn Over" test.

### Battery and general ADC (`0x79a0–0x79a7`). Confidence: high for behaviour, low for scaling

- Start a conversion by OR-ing `0x40 | channel` into `MADC_Ctrl` (`0x79a1`). Ready when bit 15 is set; bits 4–5 must then be zero. Result in `MADC_Data` (`0x79a2`).
- The reset code reads channels 6 and 7 and refuses to boot unless `2.625 × (ch6 >> 4) ≤ (ch7 >> 2)`.
- At run time `battery_adc_start` (`0x091dec`) sets `ADC_Setup = 0x4400`, and channel 7 is read every frame at `0x091dd3`.
- `battery_update` (`0x08b595`) turns that into a status in RAM `0x05e7` using double-precision maths and a 90-sample average. Status 2 requests scene `0x9030` (warning); status 3 requests scene `0x9031`, which saves and shuts down.
- In the emulator, channel 7 values of `0xd000` and above keep the device running, while `0x8000–0xc000` trigger the shutdown a few seconds after the time screen appears. A quick sweep was not monotonic (`0x6000` also kept running), so treat the scale as unknown.
- The test menu shows three charger-detect voltages and "INSERT USB", so more channels are probably in use elsewhere.

### Microphone (`0x79b0–0x79bf`). Confidence: medium

- Auto-sampling microphone ADC with gain control. Interrupt line 1 copies `0x79b4` into RAM `0x0d74`.

### Real-time clock (`0x79f0–0x79f5`). Confidence: high

- Indirect access: write `RTC_Addr`, write `RTC_Request` (1 = write `RTC_WriteData`, 2 = read), poll `RTC_Ready` bit 0, read `RTC_ReadData`. Helpers: `rtc_read_reg` `0x07d6e9`, `rtc_write_reg` `0x07d662`.
- The reset code loops until RTC register 1 bit 0 reads 1. Register 2 holds `0x5a` once the firmware has set the clock up ("reliable" marker). Losing it is the likely reason for the "CHECK TIME IS CORRECT" screen after CONTINUE (unconfirmed).
- **The clock is a 48-bit counter of 32,768 Hz ticks.** Loaded through registers `0x10–0x15` (`rtc_set_counter` `0x07d816`), read through `0x30–0x35` (`rtc_read_counter` `0x07d70b`). Seconds = counter >> 15; the firmware takes that modulo 86,400 for the time of day. **The game's time only advances if this counter runs**: with a frozen counter the egg never hatches.
- Alarm value in `0x20–0x25`. Status in `0x40` (bit 0 second, bit 1 alarm, bit 3 half second; the firmware acknowledges by writing `0xf0`). Interrupt enable in `0x50` (the firmware writes 5 or 1 around sleep). A pending RTC interrupt shows as `INT_Status2` bit 1; `rtc_irq` (`0x07daf7`) sets RAM `0x0d56`.
- The emulator ticks the counter with emulated time and raises the second, half-second and alarm flags once per frame. The emulator's alarm and interrupt behaviour is a first pass and has not been seen to wake the device.

### Sleep. Confidence: high for the mechanism

- After about 34 s without input in the room the firmware enters scene `0x9000` (`sleep_scene_update` `0x03f698`): it waits for the button to be released, stops the display, sound and ADCs, enables key-change wake-up on port B bit 2 (`IOB_KeyEn` `0x786f`, `IOAB_KCIEN` `0x78a7`), reads `IOB_Latch` (`0x786e`) to capture the reference pin state, and calls `soc_sleep` (`0x009299`).
- `soc_sleep` runs a routine from RAM that writes `0x500a` to `0x780d`: clocks stop until a wake-up event.
- On waking the firmware checks `SYS_Misc` (`0x781f`) bit 7 and `IOAB_KCIFC` (`0x78ab`) bit 10 for a key change (both write-1-to-clear), and RAM `0x0d56` for the RTC. A key wake leads back to the room after the LCD is re-initialised; otherwise it goes back to sleep.
- The emulator models this: `sleeping` is set by the write to `0x780d`, frames keep counting, and a change on an enabled port B pin or a pending RTC interrupt ends it. The picture is black while asleep.

### Second SPI bus, SPI0 (`0x7940–0x7945`): the card slot. Confidence: high

- Register use, pins (port C bits 9, 10, 12) and the two card protocols (SPI NOR SmaCards and SD-protocol cards) are described in `docs/smacards.md`.
- Driver entry points: `spi0_init` `0x08f796`, `spi0_xfer_byte` `0x08f7d3`, `spi0_write_block` `0x08f7f5`, `card_nor_init` `0x009da9`, `card_read_jedec` `0x009dd5`, `card_page_program` `0x009f33`, `card_slot_enable` `0x0913ed`, `card_present` `0x091481`, `sd_card_init` `0x09018a`.
- Test menu strings: "INSERT Card", "Ext.Card Test", "ExtFlashWrite", "Erasing Card… Write Card… Verifying…".

### Timers, time base, interrupts. Confidence: high

- Timers A–D at `0x7a00–0x7a1c`, E/F at `0x7a20–0x7a2c`, G/H at `0x78e0–0x78ec`. Control bit 13 enables, bit 14 enables the interrupt, bit 15 is the flag (write 1 to clear).
- `clock_init` (`0x07c507`) compares `TimerA_UpCount` against 20,000 over a Time Base C period, and interrupt line 6 measures `TimerH_UpCount` between Time Base C ticks. Up-counters must count at a believable rate. MAME currently returns random values here.
- `INT_Status1–3` at `0x78a0–0x78a2` are write-1-to-clear flag registers.

### Sound. Confidence: medium

- Sound unit at `0x7b80–0x7fff` (MAME: `sunplus_gcm394_audio`), serviced from interrupt line 4 via `0x03b7b1`.
- Two DAC channels (`CHA` `0x78f0`, `CHB` `0x78f8`) clocked by timers G and H.
- The sound driver at `0x03b9a8–0x03cxxx` is the only user of the flash bank register, so sample data lives in the banked window.

### Others

- **UART** (`0x7900`, `0x7904`): two small functions at `0x090395`. Probably debug output. Low.
- **ComAir**: test strings mention clock error and timer preload, consistent with a sound-carrier data link through the speaker and microphone. Not analysed. Low.
- **Watchdog**: `Watchdog_Ctrl = 0x8000` at start; cleared every frame.
- **`WAIT`** (`0x780c`): writing `0x5005` halts the CPU until the next interrupt.
- **`Power_State`** (`0x780f`): low 3 bits must reach 2 after the PLL is configured.

## Flash image layout (byte offsets)

| Offset | Contents |
|---|---|
| `0x000000` | 32-byte magic `GP-SPIF-HEADER--0123456789abcdef` |
| `0x000020`, `0x000024` | Checksum, stored twice |
| `0x000030` | End of checksummed range as a CPU address (`0x0000ffff`) |
| `0x00007c` | Entry pointer (`0x0000918e`) |
| `0x000080` | Initialised-data table used by the reset code, then interrupt handlers and reset code |
| `0x002000–0x002b85` | Boot routine that runs from RAM |
| `0x00dfea–0x00dfff` | Vector table |
| up to about `0x114000` | Program code (CPU `0x9000–0x93000`), then constant data and strings to about `0x130000` |
| `0x130000–0xd88000` | Data and assets: fonts, graphics, sound. Format not analysed |
| `0xd88000–0xff0000` | Empty (zero-filled in these dumps) |
| `0xff7ffc` | 4-byte value that differs per image |
| `0xffc000`, `0xffd000` | **Save slots 0 and 1**: 594 bytes each (checksum word, inverted checksum word, then 0x127 words copied from RAM `0x049d`). Each slot is a 4 KB sector that is erased and rewritten on every scene change |
| `0xffbff0` | 16-byte marker, the same one that ends a SmaCard image (with the bytes of each word swapped); see `smacards.md` |

A card update file is a 0x400-byte header followed by this image. Content cards are described in `smacards.md`.
