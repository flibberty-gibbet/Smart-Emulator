# Emulator architecture (proposal)

Language: C++17. One portable core library with no UI, audio or file-system dependencies, and thin front ends around it.

```
tamasmart/
  core/            static library, no external dependencies
    cpu/           µ'nSP 2.0 interpreter
    soc/           one file per peripheral block
    devices/       things outside the SoC: flash, LCD, accelerometer, card
    machine.h/.cpp wiring, scheduler, public API
  frontends/
    sdl/           desktop app (window, input, audio, file dialogs)
    headless/      command-line runner for tests and traces
    web/           later: Emscripten build + JavaScript UI
  tests/
  tools/           the disassembler and scripts from this package
```

## Current state versus this design

`emu/` is the bring-up version of this design. The CPU core is in its final form (`core/cpu.h`, `core/cpu.cpp`). Everything else lives in one header, `core/machine.h`, as a template `MachineT<CPU, BUS>` so the tests can build the same machine around MAME's core. The peripherals in it are first-pass models, each a few lines long. Split them into the files below as their behaviour settles, and keep the template (or an equivalent seam) so the lockstep test keeps working.

Inputs and outputs are plain members for now: `button`, `pad[4]`, `adc_ch[16]`, `flash`, `render(buffer)`. The API below is the target.

## Public API of the core

```cpp
class Machine {
public:
    explicit Machine(std::vector<uint8_t> flashImage);   // 16 MB RAW image
    void reset();
    void runFrame();                       // run until the next frame interrupt
    const uint16_t* framebuffer() const;   // 128 x 128, RGB565
    size_t readAudio(int16_t* out, size_t maxFrames);

    void setButton(bool pressed);
    void setTouchPad(int pad, bool touched);          // pad 0..3
    void setAcceleration(float x, float y, float z);  // in g
    void setBatteryLevel(float volts);
    void setHostTime(std::time_t t);                  // seeds the RTC
    void insertCard(std::vector<uint8_t> image);      // later

    const std::vector<uint8_t>& flash() const;        // persist this as the save file
    bool flashDirty() const;

    void setTraceSink(std::function<void(const TraceEvent&)>);  // debugging
};
```

Front ends own everything else: windowing, key mapping, the save file next to the dump, fast-forward (call `runFrame` more often and drop audio).

## CPU

- State: `sp, r1, r2, r3, r4, bp, sr, pc`, `r8–r15`, `fr`, interrupt-pending bits, and a cycle counter.
- 22-bit program counter: `((sr & 0x3f) << 16) | pc`. Data access uses `sr` bits 10–15 as the segment when an instruction says `ds:`.
- Decode the same groups MAME's disassembler uses (`unspdasm*.cpp` is a compact reference for the encoding): short jumps, the `0xExxx` bit and shift group, the `0xFxxx` group (call, goto, multiply, divide steps, flag operations, `fr` access), the `0xFF80` extended prefix for `r8–r15`, and the general ALU form with its addressing modes.
- Interrupt entry: push `pc`, push `sr`, and with nesting enabled push `fr`; load the vector; clear `sr`. Priority register in `fr` limits which lines may pre-empt.
- Writing `WAIT` (`0x780c`) puts the CPU in a halted state that ends at the next interrupt; the scheduler should then skip straight to the next event.
- Start with a plain switch-based interpreter. At roughly 24 million instructions per second it needs no further optimisation on a desktop.

## Bus

```cpp
uint16_t Bus::read(uint32_t addr);
void     Bus::write(uint32_t addr, uint16_t value);
```

- `addr < 0x2800`: RAM array.
- `0x7000–0x7fff`: a 4,096-entry table of register handlers. Unhandled entries store the value, return the last value written, and log the first access with the current PC. This keeps early bring-up moving and produces the to-do list automatically.
- `0x9000–0x1fffff`: direct flash read. `0x200000–0x3fffff`: banked flash read.
- Everything else: log and return 0.

## Scheduler

A single cycle counter and a small priority queue of timed events (timer overflow, time base tick, frame start, ADC done, touch conversion done, flash busy end). `runFrame` executes instructions until the frame event has fired and its interrupt has been taken. Derive event periods from the registers where known, and make the unknown ones (frame rate, CPU clock) configuration constants so they are easy to tune against real hardware.

## Peripheral blocks and their first-pass behaviour

| Block | Registers | First pass | Later |
|---|---|---|---|
| System/clock/power | `0x7800–0x781f` | Store values; `Power_State` returns 2; `WAIT` halts | Sleep and wake |
| Interrupt controller | `0x78a0–0x78a6` | Three write-1-to-clear status words mapped to CPU lines as in MAME's `update_interrupts` plus the handlers in `boot-and-runtime.md` | Priorities |
| GPIO A–F | `0x7860–0x788f` | Store; `IOB_Data` bit 2 reflects the button (1 = released) | Key-change interrupts, card detect |
| Timers and time base | `0x7a00–0x7a2c`, `0x78e0–0x78ec`, `0x78b0–0x78b8` | Periodic overflow flags and interrupts; up-counters derived from the cycle counter | Accurate clock sources, PWM |
| ADC | `0x79a0–0x79a7` | Immediate completion; fixed values per channel | Battery curve |
| RTC | `0x79f0–0x79f5` | Ready immediately; register 1 returns 1; seconds counter from host time | Alarm, wake |
| Flash controller + flash | `0x7b40–0x7b4b`, `0x7810`, `0x7af0` | JEDEC ID, status registers, read; `Byte_Swap` | Erase and program with correct NOR semantics (program can only clear bits) |
| TFT + LCD | `0x7050–0x706f` | Accept commands, `TFT_Status` returns 0; record the command stream for the M4 check | Sleep, brightness |
| Picture processor | `0x7000–0x77ff` | Register storage; blank frame; frame interrupt | Sprites, layers, palette, blending |
| DMA | `0x7a80–0x7abf`, `0x7070–0x7072` | Perform the copy or fill instantly and report done | |
| Touch | `0x7ac0–0x7ac9` | Conversion completes after a short delay; `CTS_CNT` = base count + 0x200 if that pad is touched | Noise, calibration test |
| I2C + accelerometer | `0x78c0–0x78c6` | Acknowledge everything; registers `0x28–0x2d` return the set acceleration | Step simulation |
| Microphone | `0x79b0–0x79bf` | Silence | Host microphone |
| Sound unit and DACs | `0x7b80–0x7fff`, `0x78f0–0x78fa` | Register storage, no output | Port from MAME's `sunplus_gcm394_audio` and `gpl_chx` |
| SPI0 + card | `0x7940–0x7945` | No card: reads return `0xff` | Second flash model |
| UART | `0x7900–0x7906` | Print transmitted bytes to the log | |

## Tracing and debugging support to build in from day one

- Instruction trace (PC and registers) to a file, switchable at run time.
- Register-access log with symbolic names from `analysis/regnames.json`.
- Symbol lookup from `analysis/symbols.csv` so logs read `lcd_write_cmd` instead of `0x077a8d`.
- Breakpoints on PC and on register access in the headless runner.
- Save states (serialise CPU, RAM, peripheral state and flash).

## Test strategy

1. **Decoder cross-check.** For every word in the code region, compare the interpreter's instruction length against the disassembler's. This catches decode errors before anything runs.
2. **Lockstep against MAME's core.** `emu/test/lockstep` builds two identical machines, one on each CPU core, steps them together and compares every register, the flags register, elapsed cycles and finally RAM. It needs no internal ROM because only MAME's CPU files are used. Re-run it whenever new firmware code paths become reachable, since it only proves the instructions that actually executed.
3. **Milestone checks.** `docs/milestones.md` defines an observable condition for each stage; the headless runner should exit 0 or 1 on each so they can run as regression tests.
4. **Screenshot comparison** once the picture processor draws: fixed frame numbers hashed and compared.

## Things deliberately left out of the first version

Card slot, ComAir link between two instances, microphone input, sleep and wake timing, and battery behaviour. Each is isolated behind the API above and can be added without touching the CPU or bus.
