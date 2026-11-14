# SmaCards

Based on one card image, `TamaSmaCard_MarineChange_EnglishV2.bin`, the firmware's card driver, and a complete download of that card run in the emulator on 13 Nov 2026 (the Marine Change character arrives in the room afterwards). Items marked "unverified" have not been observed.

## What a card is

- A 2 MB (0x200000-byte) SPI NOR flash chip on the SoC's second SPI bus (SPI0, registers `0x7940–0x7945`).
- It holds **data only**. The image contains no µ'nSP call or return instructions anywhere; the device's own firmware interprets everything.
- The slot also accepts a second kind of card. When a card is present at power-on the firmware first tries an **SD/MMC SPI-mode initialisation** (`sd_card_init` `0x09018a`: CMD0 `0x40 …`, eight attempts). If that succeeds the card type (RAM `0x0491`) is 1; if it fails the card is treated as an SPI NOR SmaCard (type 2). A 16 MB firmware cannot fit a 2 MB NOR chip, so firmware-update cards are almost certainly the SD-protocol kind. Unverified: no update card has been run.
- `TMGC_Smart_FW_SmacardVer_[ENG].bin` is a firmware-update image: a 0x400-byte scrambled header followed by the RAW firmware.

## Slot wiring, as the firmware drives it (port C)

| Pin | Role | Evidence |
|---|---|---|
| bit 9 | Card detect. Tied to the card's supply pin: with the slot unpowered and a pull-up it reads **low** when a card is in (`card_detect_boot` `0x089fa4`); with the slot powered and a pull-down it reads **high** when a card is in (`card_present` `0x091481`) | Both readers, and the emulator only finds the card when modelled this way |
| bit 10 | Card power, active low (`card_power_on` `0x089f74`, `card_slot_enable` `0x0913ed`) | Driver |
| bit 12 | Chip select, active low (`card_cs_low` `0x08f7bf`, `card_cs_high` `0x08f7b8`). When switched to an input it reads the card supply: the firmware waits for it to go high after power-on and low after power-off | Driver |
| bits 11, 13, 14 | SPI0 clock and data (driven high at power-on, low at power-off) | Driver |

SPI0 as used: write a byte to `SPI0_TXData` (`0x7942`), `SPI0_RXStatus` (`0x7943`) bits 0–2 count received bytes, `SPI0_RXData` (`0x7944`) pops one, `SPI0_Misc` (`0x7945`) bit 3 means eight bytes are waiting. `SPI0_Ctrl` (`0x7940`) bit 15 enables.

NOR commands the firmware sends: `0x9f` read ID (no strict check; the capacity byte only selects 3- or 4-byte addressing), `0x05` status, `0x03` read, `0x06` write enable, `0x20` 4 KB sector erase, `0x02` page program. The emulator answers the ID with `c2 20 15` (assumed; the real chip's ID is unknown and does not appear to matter).

## Layout of a content card (byte offsets)

| Offset | Contents | Confidence |
|---|---|---|
| `0x000000` | `ff 01`: unknown (type or format version) | low |
| `0x000002` | 16-bit checksum: the low 16 bits of the sum of all little-endian words of the data. On an unused card the sum from `0x50` and the sum from `0x1000` are equal (`0x53f9`) because `0x50–0xfff` is zero; after a download that area is `0xff` and only the sum from `0x1000` still matches, so the checked range is most likely `0x1000` to the end | medium |
| `0x000004–0x00000f` | **Use records.** Zero on an unused card. A download writes a 4-byte value identifying the device at `0x04`; there is room for three, matching the "MAX 3 USES" screen | high for the first record, medium for the other two |
| `0x000010` | Magic text `BANDAINTPD_0_0_0TAMASUMA_TIM0000` (32 bytes) | high |
| `0x000030` | Eight words: `0, 0x000b, 0, 2022, 4, 22, 1, 0`. Probably card number 11, date 22 April 2022, version 1 | medium |
| `0x000040` | 16 bytes that change when a use record is written, so a hash or signature over the header | medium |
| `0x000050–0x000fff` | Zero on an unused card; `0xff` after the first download (the firmware erases sector 0 and re-programs only the first 0x50 bytes) | high |
| `0x001000` | Data package header, 32-bit values: `0x00043232, 0, 0x44, 0x13fdc, 0x13fda, 0, 0x14020, 0x1b5a80, 0x1b5a80`. Two (offset, size) pairs: section A at `0x44` with size `0x13fdc`, section B at `0x14020` with size `0x1b5a80` | high for the pairs |
| `0x001044–0x01501f` | Section A: offset tables and small records | medium |
| `0x015020–0x1caa9f` | Section B: bulk data, most likely sprite pixel data and sound | medium |
| `0x1caaa0–0x1fffef` | Zero | high |
| `0x1ffff0` | 16-byte end marker `32 84 ab 56 22 11 dc fe 8e 6b 55 ff b5 10 7f 33` | high |

Offsets inside the package header are relative to `0x1000`.

## What a download does (observed)

Path on the device: button → menu → swipe up to page 2 → bottom-right icon (connection) → TAMASMA CARD → DOWNLOAD. Scene ids: menu `0x1400`, connection `0x3300`, card scene `0xffe2`.

1. `card_slot_enable(1)` powers the slot; `card_present` must then read the detect pin high, otherwise "NO CARD FOUND". Nothing is sent to the card before this check.
2. The firmware reads the header, then the **whole card** in 4 KB reads (status command, then read command) to verify it. This takes a few seconds.
3. It shows "DOWNLOAD — MAX 3 USES — USED n" with YES / NO.
4. On YES it reads the card again and copies bytes `0x1000–0x1cafff` (0x1ca000 bytes, the data package) to **main flash `0x32f000–0x4f8fff`**, and rewrites the 4 KB sector at main flash `0x52d000`, which is all zero afterwards (purpose not determined; possibly an index or a copy of the card's zero-filled header area). About 526 sector erases and 8,200 page programs in total, saves included.
5. If this device is not already recorded on the card, it erases card sector 0 and programs a new 0x50-byte header with the use record and the 16 bytes at `0x40`.
6. "DOWNLOAD COMPLETE", then the card's character walks into the room and announces the new content.

Afterwards the card is not needed: the content lives in the main flash, so the emulator's `.flash` working copy carries it. Trying DOWNLOAD again with the same card on the same device shows "DOWNLOADED CARD", and CURRENT CARD is enabled. A device with the same identity as an existing use record is shown the existing count and downloads without writing to the card.

The firmware only looks for a card at power-on (to choose SD or NOR handling) and inside the card scene, so a NOR card can be inserted or swapped at any time while the device runs.

## Cards in the emulator

Implemented in `emu/core/machine.h`:

- `SerialFlashCard`: a byte-serial NOR model with the commands listed above; write-type commands take effect when chip select is released; `dirty` is set when the contents change.
- SPI0 with an 8-byte receive FIFO, chip select on port C bit 12, and the detect and supply behaviour in `port_pins`.
- `insert_card(bytes)` and `eject_card()`.
- Runner: `--card FILE [--card-at FRAME] [--eject-at FRAME] [--save-card OUT.bin]`; the report lists the card's commands, reads, programs and erases.

Desktop app:

- `--card FILE` (repeatable) builds a **card library**; dropping a file on the window adds and inserts it. `C` inserts the next card (after the last one: empty slot), `X` ejects. The window title shows the card in the slot.
- The user's card file is never modified. Because a download writes a use record, the app keeps a working copy in `CARD.bin.card` and loads that if present. Delete it to get the unused card back.

## Several cards

The device has one slot and the firmware holds one downloaded card at a time (one fixed region of main flash), so two cards cannot be active together without changing the firmware. What works or is feasible:

- **Library with hot-swap: done.** Any number of card files, switched in the app like swapping physical cards; then DOWNLOAD on the device to make one current.
- **Instant switching (not built).** The downloaded content sits in main flash `0x32f000–0x4f8fff` plus the sector at `0x52d000`. An emulator-only shortcut could snapshot those regions per card and swap them directly. Before doing that, find what the save data (594 bytes at RAM `0x049d`) records about the current card, or the firmware may show stale or mismatched content.
- **Truly concurrent content** would mean patching the firmware's content tables. Out of scope for an emulator that runs unmodified firmware.

## Open points

- The 16 bytes at `0x40`: which function computes them (look at the card scene code `0x00c19a–0x00e0f5` and its helpers around `0x06cc3f–0x06d227`).
- Where the 4-byte device identity comes from (probably generated at first set-up and kept in the save).
- What the main-flash sector at `0x52d000` holds and who reads it.
- SD-protocol cards: the driver is at `0x089c88–0x09018a` (512-byte blocks, CMD17 read, CMD24 write). Needed only for firmware-update cards.
- Other content cards have not been tried.

## English-patched cards

The patched image passes its word checksum, so whoever patched it recalculated bytes 2–3. If you edit a card image, recompute that checksum. The firmware accepted this patched card, so the 16 bytes at `0x40` did not block it.
