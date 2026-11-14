# Smart-Emulator

A project to emulate the Tamagotchi Smart on PC and hopefully eventually mobile. Currently tested on Linux (by Claude) and Windows (by a human). Built with MAME's unSP CPU core and GeneralPlus documentation as a reference (see Credits).

As of project initialization, all code is LLM written by Claude Fable 5.2 and has not yet been verified by a human. Please treat it appropriately. I am working on improving it for general use, but *please* treat everything in this repo as highly experimental and potentially harmful to your PC.

This repo does not contain any ROMs.

Current tested features:
- Loads Tamagotchi Smart Firmware.
- Allows users to load multiple SmaCards and activate 1 at a time.
- Saves and loads data automatically.

Current limitations:
- Sound NYI
- Not a standalone program

Installation instructions (LLM written) are here: https://github.com/flibberty-gibbet/Smart-Emulator/blob/main/tamasmart-re/README.md

Controls:

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

To load a SmaCard, drop the file onto the window.

## Credits and licenses

This project would not exist without [MAME](https://github.com/mamedev/mame). The emulator's CPU core was tested instruction by instruction against MAME's unSP core by Segher Boessenkool, Ryan Holtz and David Haywood, and much of the hardware knowledge comes from David Haywood's GeneralPlus GPL951xx work in MAME.

The project's own code is MIT licensed (`LICENSE`). A few test and tool files are adapted from MAME and stay under GPL-2.0-or-later or BSD-3-Clause; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This project is not affiliated with or endorsed by the MAME team.
