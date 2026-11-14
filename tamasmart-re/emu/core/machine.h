// Tamagotchi Smart bring-up machine: bus, first-pass peripheral stubs, event scheduling.
// Everything here is deliberately simple and logs what it does not understand.
#pragma once
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <vector>
#include <map>
#include <string>
#include <functional>

struct SpiNorFlash {                         // main flash chip as seen through manual SPIFC commands
	std::vector<uint8_t> &mem;
	uint8_t sr = 0, cr = 0, cmd = 0; int phase = 0; uint32_t addr = 0; std::vector<uint8_t> wbuf; bool wel = false, addr4 = false;
	uint64_t n_erase = 0, n_program = 0; std::vector<std::string> log;
	explicit SpiNorFlash(std::vector<uint8_t> &m) : mem(m) { }
	void begin(uint8_t c) { end(); cmd = c; phase = 0; addr = 0; wbuf.clear();
		if (c == 0x06) wel = true; if (c == 0x04) wel = false; if (c == 0xb7) addr4 = true; if (c == 0xe9) addr4 = false; }
	void addr_byte(uint8_t b) { addr = (addr << 8) | b; }
	uint8_t read() {                         // one byte clocked out of the chip
		switch (cmd) {
		case 0x9f: { static const uint8_t id[3] = { 0xc2, 0x20, 0x18 }; return id[phase++ % 3]; }
		case 0x05: return sr | (wel ? 2 : 0);
		case 0x15: case 0x35: return cr;
		case 0x03: case 0x0b: case 0x13: return mem[(addr++) & (mem.size() - 1)];
		default: return 0; }
	}
	void write(uint8_t b) { wbuf.push_back(b); }
	void end() {                             // chip-select released: commit write-type commands
		char t[96];
		switch (cmd) {
		case 0x01: if (wel && !wbuf.empty()) { sr = wbuf[0] & 0xfc; if (wbuf.size() > 1) cr = wbuf[1]; } wel = false; break;
		case 0x31: case 0x11: if (wel && !wbuf.empty()) cr = wbuf[0]; wel = false; break;
		case 0x20: case 0x21: case 0x52: case 0xd8: case 0xdc: if (wel) {
			uint32_t sz = (cmd == 0x20 || cmd == 0x21) ? 0x1000 : (cmd == 0x52 ? 0x8000 : 0x10000), a = addr & ~(sz - 1) & (mem.size() - 1);
			std::fill(mem.begin() + a, mem.begin() + a + sz, 0xff); n_erase++;
			snprintf(t, sizeof t, "erase %06x +%x", a, sz); log.push_back(t); } wel = false; break;
		case 0x02: case 0x12: case 0x32: case 0x38: if (wel && !wbuf.empty()) {
			for (size_t i = 0; i < wbuf.size(); i++) mem[(addr + i) & (mem.size() - 1)] &= wbuf[i];
			n_program++; snprintf(t, sizeof t, "program %06x +%zx", addr, wbuf.size()); log.push_back(t); } wel = false; break;
		default: break; }
		cmd = 0;
	}
};

// A SmaCard: a 2 MB SPI NOR flash that the firmware drives one byte at a time over SPI0, with chip select on a GPIO pin.
struct SerialFlashCard {
	std::vector<uint8_t> mem; bool selected = false, wel = false, dirty = false; uint8_t cmd = 0, sr = 0; int n = 0; uint32_t addr = 0; std::vector<uint8_t> wbuf;
	uint64_t n_cmd = 0, n_read = 0, n_program = 0, n_erase = 0; std::vector<std::string> log;
	void note(const char *what, uint32_t a, size_t len) { if (log.size() < 400) { char t[64]; snprintf(t, sizeof t, "%s %06x +%zx", what, a, len); log.push_back(t); } }
	void select() { selected = true; n = 0; cmd = 0; addr = 0; wbuf.clear(); }
	uint8_t xfer(uint8_t tx) {                 // one byte in, one byte out
		if (!selected || mem.empty()) return 0xff;
		const int i = n++;
		if (i == 0) { cmd = tx; n_cmd++; if (cmdlog.size() < 64) cmdlog.push_back(tx); if (cmd == 0x06) wel = true; if (cmd == 0x04) wel = false; read_len = 0; return 0xff; }
		switch (cmd) {
		case 0x9f: { static const uint8_t id[3] = { 0xc2, 0x20, 0x15 }; return id[(i - 1) % 3]; }   // assumed: a 16 Mbit Macronix part
		case 0x05: return sr | (wel ? 2 : 0);
		case 0x03: case 0x0b:
			if (i <= 3) { addr = (addr << 8) | tx; if (i == 3) read_start = addr; return 0xff; }
			if (cmd == 0x0b && i == 4) return 0xff;
			n_read++; read_len++; return mem[(addr++) % mem.size()];
		case 0x02: if (i <= 3) addr = (addr << 8) | tx; else wbuf.push_back(tx); return 0xff;
		case 0x20: case 0x52: case 0xd8: if (i <= 3) addr = (addr << 8) | tx; return 0xff;
		case 0x01: wbuf.push_back(tx); return 0xff;
		default: return 0xff; }
	}
	void deselect() {                          // chip select released: write-type commands take effect now
		if (!selected) return; selected = false;
		if (mem.empty()) return;
		switch (cmd) {
		case 0x03: case 0x0b: if (read_len) note("read", read_start, read_len); break;
		case 0x02: if (wel && !wbuf.empty()) { for (size_t i = 0; i < wbuf.size(); i++) mem[(addr + i) % mem.size()] &= wbuf[i]; n_program++; dirty = true; note("program", addr, wbuf.size()); } wel = false; break;
		case 0x20: case 0x52: case 0xd8: if (wel && n >= 4) { const uint32_t sz = cmd == 0x20 ? 0x1000 : (cmd == 0x52 ? 0x8000 : 0x10000), a0 = (addr & ~(sz - 1)) % mem.size();
			std::fill(mem.begin() + a0, mem.begin() + std::min<size_t>(a0 + sz, mem.size()), 0xff); n_erase++; dirty = true; note("erase", a0, sz); } wel = false; break;
		case 0xc7: case 0x60: if (wel) { std::fill(mem.begin(), mem.end(), 0xff); n_erase++; dirty = true; note("chip-erase", 0, mem.size()); } wel = false; break;
		case 0x01: if (wel && !wbuf.empty()) sr = wbuf[0] & 0xfc; wel = false; break;
		case 0x9f: note("read-id", 0, 0); break;
		default: break; }
	}
	uint32_t read_start = 0; size_t read_len = 0; std::vector<uint8_t> cmdlog;
};

// CPU is the processor core, BUS the bus interface it expects. The emulator uses MachineT<tsmart::Cpu, tsmart::Bus>;
// the lockstep test also instantiates it with MAME's core to compare the two instruction by instruction.
template <class CPU, class BUS>
class MachineT : public BUS {
public:
	static constexpr uint64_t CPS = 48000000;          // assumed system clock, cycles per second
	double fps = 30.0;                                  // frame interrupt rate; 30.1 measured from a video of a real unit
	// CPU speed relative to the instruction costs in cpu.cpp at 48 MHz. 1/2 means twice as fast. A video of a real
	// unit shows the firmware's one-million-iteration delay before a save taking about 1 s; at 1/1 it takes 1.6 s.
	uint32_t cpu_speed_num = 1, cpu_speed_den = 2; uint64_t cycle_frac = 0;
	// What the LCD shows. The panel has its own memory: it keeps the last frame while the SoC is not streaming
	// (TFT_Ctrl bit 0 clear, as during saves) and powers up white.
	uint16_t lcd[128 * 128];
	bool tft_streaming_prev = false; uint64_t lcd_updates = 0;
	std::vector<uint8_t> flash; uint16_t ram[0x2800] = {}; uint16_t io[0x1000] = {};
	uint16_t spriteram[0x800] = {};                     // two banks of 0x400 words, selected by PPU_RAM_BANK (0x707e)
	uint16_t palette[0x400] = {};                       // four banks of 0x100 entries, selected by Palette_Control (0x703a) bits 2-3
	CPU cpu; SpiNorFlash nor;
	// Card slot. Chip select is port C bit 12 (low = selected), card detect is port C bit 9 (low = card present),
	// card power is port C bit 10 (driven low = on); all three read from the firmware's driver at 0x08f796-0x089fc1.
	SerialFlashCard card; std::vector<uint8_t> spi0_rx;
	void insert_card(std::vector<uint8_t> image) { card = SerialFlashCard(); card.mem = std::move(image); card_present = !card.mem.empty(); }
	void eject_card() { card.deselect(); card_present = false; }
	uint64_t now = 0, insns = 0, frames = 0; bool sleeping = false; bool halted = false;
	// inputs
	uint16_t cts_touch_delta = 0x10;   // extra count per sample on a touched pad (assumed; the firmware needs a summed rise of 0xfc to see a press)
	bool button = false; bool card_present = false; bool pad[4] = {}; uint16_t adc_ch[16] = { 0,0,0,0,0,0,0x6000,0xe000 };   // ch7 = battery: below about 0xd000 the firmware shuts down as "battery empty"
	// logs
	struct IoStat { uint64_t r = 0, w = 0; uint32_t first_pc = 0; uint16_t last_w = 0; };
	std::map<uint16_t, IoStat> iostat; bool trace_io = false; std::vector<std::string> lcd_log; std::string lcd_line;
	std::function<const char *(uint16_t)> regname;

	explicit MachineT(std::vector<uint8_t> img) : flash(std::move(img)), cpu(*this), nor(flash) { reset(); }
	void reset() { for (auto &w : ram) w = 0; for (auto &w : io) w = 0; for (auto &px : lcd) px = 0x7fff; cpu.reset(0xfff0); rtc_base = rtc_counter(); rtc_base_now = 0; now = 0; halted = false; sleeping = false; cycle_frac = 0;
		for (auto &t : tm) t = Timer(); next_frame = frame_period(); }

	// ---- bus ----
	uint16_t read16(uint32_t a) override {
		if (a < 0x2800) return ram[a];
		if (a >= 0x9000 && a < 0x200000) return flash_word(a - 0x9000);
		if (a >= 0x200000) return flash_word(0x1f7000 + (uint32_t)(io[0x810] & 0x3f) * 0x200000 + (a - 0x200000));
		if (a >= 0x7000 && a < 0x8000) return io_r(a);
		note_bad(a, false); return 0;
	}
	void write16(uint32_t a, uint16_t d) override {
		if (a < 0x2800) { ram[a] = d; return; }
		if (a >= 0x7000 && a < 0x8000) { io_w(a, d); return; }
		note_bad(a, true);
	}
	uint16_t read_code(uint32_t a) const { return a < 0x2800 ? ram[a] : (a >= 0x9000 && a < 0x200000 ? flash_word(a - 0x9000) : 0); }
	uint16_t flash_word(uint32_t w) const { size_t o = (size_t(w) * 2) & (flash.size() - 1); return flash[o] | (flash[o + 1] << 8); }

	// ---- execution ----
	void step() {
		if (sleeping) {                            // deep sleep: clocks are stopped, so only a key change or the RTC can wake the SoC
			now = next_frame > now ? next_frame : now + 1; sleep_events();
			const uint16_t pins = port_pins(1), changed = (pins ^ latch[1]) & io[0x86f];          // IOB_KeyEn selects the wake-up pins
			if (changed) { io[0x8ab] |= (uint16_t)(changed << 8); io[0x81f] |= 0x0080; sleeping = false; }   // IOAB_KCIFC (port B in the high byte), SYS_Misc bit 7
			if (rtc_reg[0x40] & rtc_reg[0x50] & 0x0f) sleeping = false;
			if (!sleeping) { for (auto &t : tm) if (t.next < now) t.next = now + t.period; for (int i = 0; i < 3; i++) if (tb_next[i] < now) tb_next[i] = now + tb_period(i); update_lines(); }
			return;
		}
		if (now >= next_due) { run_events(); next_due = next_event(); }          // next_due is cleared by any I/O access, which may reschedule things
		if (halted) { uint64_t n = next_event(); now = n > now ? n : now + 1; run_events(); update_lines(); next_due = 0; }
		int c = cpu.step(); insns++; cycle_frac += (uint64_t)c * cpu_speed_num; now += cycle_frac / cpu_speed_den; cycle_frac %= cpu_speed_den;
		if (cpu.irq_taken_last_step()) halted = false;
	}
	std::map<uint32_t, uint64_t> bad_access;

private:
	enum { LINE_FIQ = 0, LINE_IRQ0, LINE_IRQ1, LINE_IRQ2, LINE_IRQ3, LINE_IRQ4, LINE_IRQ5, LINE_IRQ6, LINE_IRQ7 };
	struct Timer { uint16_t ctrl = 0, preload = 0; uint64_t start = 0, period = 0, next = ~0ull; };
	Timer tm[8];                                        // A..H
	uint64_t tb_next[3] = { ~0ull, ~0ull, ~0ull }; uint64_t next_frame = 0, cts_done = ~0ull, adc_done = 0;
	bool cts_flag = false; uint16_t spifc_latch = 0; std::vector<uint8_t> spifc_fifo; int spifc_tx_left = 0; uint16_t byteswap = 0;
	// RTC: a 48-bit counter of 32768 Hz ticks (seconds = counter >> 15), loaded through registers 0x10-0x15 and read through 0x30-0x35.
	uint64_t rtc_base = 0, rtc_base_now = 0, rtc_latch = 0; uint64_t rtc_half_seen = 0; bool rtc_alarm_armed = false; uint16_t latch[5] = {};
public:
	// The RTC has its own supply on the device, so it survives a reset; front ends carry this across resets and sessions.
	struct RtcState { uint64_t counter = 0; uint16_t reg[0x60] = {}; };
	RtcState rtc_state() const { RtcState r; r.counter = rtc_counter(); for (int i = 0; i < 0x60; i++) r.reg[i] = rtc_reg[i]; return r; }
	void set_rtc_state(const RtcState &r) { for (int i = 0; i < 0x60; i++) rtc_reg[i] = r.reg[i]; rtc_set_counter(r.counter); rtc_half_seen = r.counter >> 14; }
	uint64_t rtc_counter() const { return (rtc_base + (now - rtc_base_now) * 32768 / CPS) & 0xffffffffffffull; }
	void rtc_set_counter(uint64_t c) { rtc_base = c & 0xffffffffffffull; rtc_base_now = now; }
private:
	uint64_t next_due = 0;
	uint16_t rtc_reg[0x60] = {}; uint16_t dma_status = 0; bool dma_latched[2] = {};
	uint64_t frame_period() const { return (uint64_t)(CPS / fps); }
	void note_bad(uint32_t a, bool w) { bad_access[(a << 1) | (w ? 1 : 0)]++; }

	static double src_hz(uint16_t ctrl) {
		static const double fa[16] = { CPS / 2.0, CPS / 256.0, 32768, 8192, 4096, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		static const double fb[8] = { 2048, 1024, 256, 0, 0, 0, -1, 0 };
		double a = fa[ctrl & 0xf], b = fb[(ctrl >> 4) & 7];
		if (a < 0) return b < 0 ? 0 : b; if (b < 0) return a; return a < b ? a : b;
	}
	void timer_arm(Timer &t) {
		double hz = src_hz(t.ctrl); uint32_t ticks = 0x10000 - t.preload;
		if (!(t.ctrl & 0x2000) || hz <= 0) { t.next = ~0ull; t.period = 0; return; }
		t.period = (uint64_t)(ticks * (CPS / hz)); if (t.period == 0) t.period = 1;
		t.start = now; t.next = now + t.period;
	}
	uint16_t timer_count(const Timer &t) const {
		if (!t.period) return t.preload;
		return t.preload + (uint16_t)(((now - t.start) % t.period) * (0x10000 - t.preload) / t.period);
	}
	uint64_t next_event() const {
		uint64_t n = next_frame; for (auto &t : tm) if (t.next < n) n = t.next;
		for (auto x : tb_next) if (x < n) n = x; if (cts_done < n) n = cts_done; return n;
	}
	void run_events() {
		bool any = false;
		for (auto &t : tm) while (t.next <= now) { t.ctrl |= 0x8000; t.next += t.period; any = true; }
		for (int i = 0; i < 3; i++) while (tb_next[i] <= now) { io[0x8b0 + i] |= 0x8000; tb_next[i] += tb_period(i); any = true; }
		while (next_frame <= now) { next_frame += frame_period(); frames++;
			const bool streaming = (io[0x050] & 1) && ((io[0x050] >> 4) & 0xf) == 0xc;
			if (io[0x050] & 1) { io[0x063] |= 0x0001; any = true; }
			// A frame takes a whole frame period to reach the panel, so only latch one if streaming stayed on throughout.
			if (streaming && tft_streaming_prev) { render(lcd); lcd_updates++; }
			tft_streaming_prev = streaming; if (rtc_tick()) any = true; }

		if (cts_done <= now) { cts_done = ~0ull; cts_flag = true; io[0xac0] &= ~1; any = true; }
		if (any) update_lines();
	}
	void sleep_events() { while (next_frame <= now) { next_frame += frame_period(); frames++; rtc_tick(); } tft_streaming_prev = false; }
	// RTC status (register 0x40): bit 0 second, bit 1 alarm, bit 3 half second. Checked once per frame.
	bool rtc_tick() {
		const uint64_t c = rtc_counter(), half = c >> 14; bool any = false;
		if (half != rtc_half_seen) { rtc_reg[0x40] |= 0x08; if ((half >> 1) != (rtc_half_seen >> 1)) rtc_reg[0x40] |= 0x01; rtc_half_seen = half; any = true; }
		uint64_t alarm = 0; for (int i = 5; i >= 0; i--) alarm = (alarm << 8) | (rtc_reg[0x20 + i] & 0xff);
		if (alarm && rtc_alarm_armed && c >= alarm) { rtc_reg[0x40] |= 0x02; rtc_alarm_armed = false; any = true; }
		return any;
	}
	uint64_t tb_period(int i) const { int which = (i << 2) | (io[0x8b0 + i] & 3); return which ? CPS / (1u << (which - 1)) : ~0ull >> 1; }
	void update_lines() {
		bool irq4 = false; for (int i = 0; i < 8; i++) if (i != 5 && (tm[i].ctrl & 0xc000) == 0xc000) irq4 = true;
		cpu.set_line(LINE_FIQ, (tm[5].ctrl & 0xc000) == 0xc000);
		cpu.set_line(LINE_IRQ4, irq4);
		cpu.set_line(LINE_IRQ5, (io[0x062] & io[0x063]) != 0);
		cpu.set_line(LINE_IRQ6, cts_flag || (io[0x8b2] & 0xc000) == 0xc000);
		cpu.set_line(LINE_IRQ7, (io[0x8b0] & 0xc000) == 0xc000 || (io[0x8b1] & 0xc000) == 0xc000 || (rtc_reg[0x40] & rtc_reg[0x50] & 0x0f) != 0);
	}
	int timer_index(uint32_t a, int &field) const {    // field: 0 ctrl, 1 ccpb, 2 preload, 4 upcount
		if (a >= 0x7a00 && a < 0x7a20) { field = a & 7; return (a - 0x7a00) >> 3; }
		if (a >= 0x78e0 && a < 0x78f0) { field = a & 7; return 6 + ((a - 0x78e0) >> 3); }
		switch (a) { case 0x7a20: field = 0; return 4; case 0x7a21: field = 0; return 5; case 0x7a22: field = 1; return 4; case 0x7a23: field = 1; return 5;
			case 0x7a24: field = 2; return 4; case 0x7a25: field = 2; return 5; case 0x7a2a: field = 4; return 4; case 0x7a2b: field = 4; return 5; }
		return -1;
	}

	uint16_t io_r(uint32_t a) { next_due = 0;
		if (a >= 0x7300 && a < 0x7400) return palette[pal_index(a)];
		if (a >= 0x7400 && a < 0x7800) return io[0x07e] <= 1 ? spriteram[(a & 0x3ff) + io[0x07e] * 0x400] : 0;
		uint16_t v = io[a & 0xfff]; int f, ti = timer_index(a, f);
		if (ti >= 0) { v = f == 0 ? tm[ti].ctrl : f == 2 ? tm[ti].preload : f == 4 ? timer_count(tm[ti]) : v; }
		else if (a >= 0x7860 && a < 0x7890 && (a & 7) == 0) v = port_pins((a - 0x7860) >> 3);   // IOx_Data reads the pins
		else if (a >= 0x7860 && a < 0x7890 && (a & 7) == 6) { v = port_pins((a - 0x7860) >> 3); latch[(a - 0x7860) >> 3] = v; }   // IOx_Latch: reading it captures the pins, the reference for key-change wake-up
		else switch (a) {
		case 0x7072: v = 0; break;                                            // SPDMA_Number: sprite DMA is instant, never busy
		case 0x705a: v = 0; break;                                            // TFT_Status: idle
		case 0x780f: v = 2; break;                                            // Power_State
		case 0x7819: v &= ~2; break;                                          // Cache_Ctrl: flush done
		case 0x78a0: v = (io[0x063] & io[0x062]) ? 0x2000 : 0; break;         // INT_Status1: bit 13 = TFT/PPU
		case 0x78a1: v = (cts_flag ? 0x0080 : 0) | ((io[0x8b2] & 0x8000) ? 0x0400 : 0) | ((rtc_reg[0x40] & rtc_reg[0x50] & 0x0f) ? 0x0002 : 0); break;   // INT_Status2: bit 1 = RTC
		case 0x78a2: { v = 0; for (int i = 0; i < 8; i++) if (tm[i].ctrl & 0x8000) v |= 0x100 << i; break; }  // INT_Status3
		case 0x79a1: v = (v & 0x7fcf) | 0x8000; break;                        // MADC_Ctrl: conversion ready
		case 0x79a2: v = adc_ch[io[0x9a1] & 0xf]; break;                      // MADC_Data
		case 0x79f4: v = 1; break;                                            // RTC_Ready
		case 0x79f5: { const int r = io[0x9f1] & 0xff; if (r == 0x30) rtc_latch = rtc_counter();   // RTC_ReadData; reading byte 0 of the counter latches all six
			v = r == 1 ? 1 : (r >= 0x30 && r <= 0x35) ? (uint16_t)((rtc_latch >> ((r - 0x30) * 8)) & 0xff) : rtc_reg[r % 0x60]; break; }
		case 0x7abf: v = dma_status; break;
		case 0x7943: v = (uint16_t)(spi0_rx.size() & 7); break;                 // SPI0_RXStatus: bytes waiting (the FIFO holds 8)
		case 0x7944: v = 0xff; if (!spi0_rx.empty()) { v = spi0_rx.front(); spi0_rx.erase(spi0_rx.begin()); } break;
		case 0x7945: v = (v & ~0x0008) | (spi0_rx.size() >= 8 ? 0x0008 : 0); break;   // SPI0_Misc bit 3: receive FIFO full
		case 0x7ac9: { int ch = (io[0xac1] & 0xf) - 8; v = 0x0100 + ((ch >= 0 && ch < 4 && pad[ch]) ? cts_touch_delta : 0); break; }   // CTS_CNT. 42 readings are summed (less the highest and lowest) and averaged with the previous scan; press threshold 0xfc, release 0x6c
		case 0x7af0: v = byteswap; break;
		case 0x7b40: v = (v & 0x3ffe) | 0x8000 | (spifc_fifo.empty() ? 0x4000 : 0) | 1; break;   // tx_done, rx_empty, idle
		case 0x7b46: v = spifc_latch; break;
		default: break; }
		auto &s = iostat[a]; if (!s.r && !s.w) s.first_pc = cur_pc; s.r++;
		if (trace_io) fprintf(stderr, "IO R %04x -> %04x  pc=%06x %s\n", a, v, cur_pc, regname ? regname(a) : "");
		return v;
	}
	void io_w(uint32_t a, uint16_t d) { next_due = 0;
		if (a >= 0x7300 && a < 0x7400) { palette[pal_index(a)] = d; return; }
		if (a >= 0x7400 && a < 0x7800) { if (io[0x07e] <= 1) spriteram[(a & 0x3ff) + io[0x07e] * 0x400] = d; return; }
		auto &s = iostat[a]; if (!s.r && !s.w) s.first_pc = cur_pc; s.w++; s.last_w = d;
		if (trace_io) fprintf(stderr, "IO W %04x <- %04x  pc=%06x %s\n", a, d, cur_pc, regname ? regname(a) : "");
		int f, ti = timer_index(a, f);
		if (ti >= 0) { Timer &t = tm[ti];
			if (f == 0) { uint16_t flag = (t.ctrl & 0x8000) & ~(d & 0x8000); bool was = t.ctrl & 0x2000;
				uint16_t old = t.ctrl; t.ctrl = (d & 0x7fff) | flag;
				if (((d & 0x2000) != 0) != was || ((old ^ d) & 0x7f)) timer_arm(t); }
			else if (f == 2) { t.preload = d; if (t.ctrl & 0x2000) timer_arm(t); }
			else io[a & 0xfff] = d;
			update_lines(); return; }
		if (a >= 0x7860 && a < 0x7890 && (a & 7) < 2) { uint32_t b = (a & 0xff8) ; io[b] = d; io[b + 1] = d; if (b == 0x870) card_cs_update(); return; }   // Data and Buffer share the output latch
		if (a == 0x7872) { io[0x872] = d; card_cs_update(); return; }
		switch (a) {
		case 0x7050: if (!(d & 1) || ((d >> 4) & 0xf) != 0xc) tft_streaming_prev = false; io[0x050] = d; if (d & 1) { int mode = (d >> 4) & 0xf; if (mode == 8) lcd_cmd(io[0x05b] & 0xff); else if (mode == 0xa) lcd_dat(io[0x05b] & 0xff); } return;
		case 0x7072: {                                                        // SPDMA_Number: copy (count + 1) words into sprite RAM now
			uint32_t src = io[0x070], dst = io[0x071] & 0x3ff, count = d & 0x3ff;
			for (uint32_t i = 0; i <= count; i++) if (dst + i < 0x400 && io[0x07e] <= 1) spriteram[dst + i + io[0x07e] * 0x400] = read16((src + i) & 0xffff);
			sprite_dma_runs++; if (io[0x062] & 4) { io[0x063] |= 4; update_lines(); }
			return; }
		case 0x7063: io[0x063] &= ~d; update_lines(); return;                 // TFT_INT_CLR: write 1 to clear
		case 0x7062: io[0x062] = d; update_lines(); return;
		case 0x780d: io[0x80d] = d; if (d == 0x500a) sleeping = true; return;   // SLEEP: stop the clocks until a key change or the RTC
		case 0x78ab: io[0x8ab] &= ~d; return;                                 // IOAB_KCIFC: key-change flags, write 1 to clear
		case 0x781f: io[0x81f] = (d & ~0x0080) | (io[0x81f] & 0x0080 & ~d); return;   // SYS_Misc: bit 7 = woken by key change, write 1 to clear
		case 0x780c: io[0x80c] = d; halted = true; return;                    // WAIT
		case 0x78a0: case 0x78a1: case 0x78a2: return;                        // status words: flags live in their sources here
		case 0x78b0: case 0x78b1: case 0x78b2: { int i = a - 0x78b0; uint16_t flag = (io[a & 0xfff] & 0x8000) & ~(d & 0x8000);
			io[a & 0xfff] = (d & 0x7fff) | flag; tb_next[i] = (d & 0x2000) ? now + tb_period(i) : ~0ull; update_lines(); return; }
		case 0x79f2: io[0x9f2] = d; return;
		case 0x79f3: io[0x9f3] = d; if (d == 1) { const int r = io[0x9f1] % 0x60; rtc_reg[r] = io[0x9f2];   // RTC_Request: 1 = write (assumed), 2 = read
				if (r == 0x40) rtc_reg[0x40] = 0;                         // the firmware writes 0xf0 here to acknowledge; assumed to clear every flag
				if (r >= 0x20 && r <= 0x25) rtc_alarm_armed = true;
				if (r == 0x15) { uint64_t c = 0; for (int i = 5; i >= 0; i--) c = (c << 8) | (rtc_reg[0x10 + i] & 0xff); rtc_set_counter(c); } } return;   // counter loads when its top byte is written
		case 0x7a80: case 0x7a81: case 0x7a82: case 0x7a83: case 0x7a84: case 0x7a85: case 0x7a86: case 0x7a87:
		case 0x7a88: case 0x7a89: case 0x7a8a: case 0x7a8b: case 0x7a8c: case 0x7a8d: case 0x7a8e: case 0x7a8f: {
			int ch = (a >> 3) & 1, off = a & 7; io[a & 0xfff] = d;
			if (off == 3) { dma_latched[ch] = true; if (io[0xa80 + ch * 8] & 1) dma_run(ch); }
			if (off == 0 && (d & 1) && dma_latched[ch]) dma_run(ch);
			return; }
		case 0x7abf: dma_status &= ~d; return;
		case 0x7ac0: io[0xac0] = d; if ((d & 0x8001) == 0x8001) cts_done = now + CPS / 20000; return;   // start a touch conversion; assumed 50 us, so one sample per Timer C tick (3750 Hz) and a 4-pad scan every 45 ms
		case 0x7ac5: cts_flag = false; update_lines(); return;
		case 0x7940: io[0x940] = d; if (!(d & 0x8000)) spi0_rx.clear(); return;   // SPI0_Ctrl: bit 15 enables
		case 0x7942: { if (!card.selected) spi0_unsel++; uint8_t rx = (card_present && card.selected) ? card.xfer((uint8_t)d) : 0xff; if (spi0_rx.size() < 8) spi0_rx.push_back(rx); spi0_bytes++; return; }
		case 0x7ac9: return;
		case 0x7af0: byteswap = (d >> 8) | (d << 8); return;
		case 0x7b41: io[0xb41] = d; spifc_command(d); return;
		case 0x7b45: nor.write(d & 0xff); if (spifc_tx_left > 0 && --spifc_tx_left == 0) nor.end(); return;
		case 0x7b46: { uint16_t lo = spifc_pop(), hi = spifc_pop(); spifc_latch = lo | (hi << 8); return; }
		default: io[a & 0xfff] = d; return; }
	}
	// GPIO: output pins read back the latch; input pins read an external driver if one exists,
	// otherwise the pull level (Attrib=0: pulled to the Buffer bit) or 0 when floating.
	uint16_t port_pins(int p) {
		uint16_t buf = io[0x861 + p * 8], dir = io[0x862 + p * 8], attr = io[0x863 + p * 8], em = 0, ev = 0;
		if (p == 1) { em |= 0x0004; if (!button) ev |= 0x0004; }             // button: port B bit 2, low when pressed
		if (p == 2) {                                                        // card slot
			const bool powered = (dir & 0x0400) && !(buf & 0x0400);          // bit 10 driven low switches the card's supply on
			em |= 0x1000; if (powered) ev |= 0x1000;                         // bit 12 (chip select), when an input, follows the card supply
			if (card_present) { em |= 0x0200; if (powered) ev |= 0x0200; }   // bit 9 (detect) is tied to the card's supply pin: low while unpowered, high once powered
		}
		return (dir & buf) | (~dir & em & ev) | (~dir & ~em & ~attr & buf);
	}
	void card_cs_update() { const bool sel = (io[0x872] & 0x1000) && !(io[0x871] & 0x1000); if (sel && !card.selected) card.select(); else if (!sel && card.selected) card.deselect(); }
	uint32_t pal_index(uint32_t a) const { return ((a & 0xff) | ((io[0x03a] & 0x000c) << 6)) & 0x3ff; }
	uint8_t spifc_pop() { if (spifc_fifo.empty()) return 0; uint8_t b = spifc_fifo.front(); spifc_fifo.erase(spifc_fifo.begin()); return b; }
	void spifc_command(uint16_t d) {
		uint16_t para = io[0xb42]; uint32_t addr = io[0xb43] | (io[0xb44] << 16);
		nor.begin(d & 0xff); spifc_fifo.clear();
		if (!(para & 0x1000)) { if (nor.addr4) nor.addr_byte(addr >> 24); nor.addr_byte(addr >> 16); nor.addr_byte(addr >> 8); nor.addr_byte(addr); }
		int rx = io[0xb48] & 0x1ff; for (int i = 0; i < rx; i++) spifc_fifo.push_back(nor.read());
		spifc_tx_left = io[0xb47] & 0x1ff;
		if (spifc_tx_left == 0) nor.end();
	}
	void dma_run(int ch) {
		uint16_t *p = &io[0xa80 + ch * 8]; uint16_t mode = p[0];
		uint32_t src = p[1] | (p[4] << 16), dst = p[2] | (p[5] << 16), len = p[3] | (p[6] << 16);
		int sd = (mode & 0x80) ? 0 : ((mode & 0x20) ? -1 : 1), dd = (mode & 0x40) ? 0 : ((mode & 0x10) ? -1 : 1);
		// DMA addresses are wider than the CPU's 22 bits: the firmware passes flash pointers such as 0x807000,
		// which only make sense as a linear continuation of the 0x9000-based flash mapping.
		for (uint32_t i = 0; i < len; i++) {
			uint16_t v = src >= 0x400000 ? flash_word(src - 0x9000) : read16(src);
			if (dst < 0x400000) write16(dst, v); else note_bad(dst, true);
			src += sd; dst += dd; }
		if (src >= 0x400000) dma_linear_reads++;
		dma_status |= 1 << ch; dma_latched[ch] = false; p[3] = 0; p[6] = 0;
		dma_log_count++;
	}
	void lcd_cmd(uint8_t c) { if (!lcd_line.empty()) lcd_log.push_back(lcd_line); char t[16]; snprintf(t, sizeof t, "CMD %02x :", c); lcd_line = t; }
	void lcd_dat(uint8_t c) { char t[8]; snprintf(t, sizeof t, " %02x", c); lcd_line += t; }
public:
	uint32_t cur_pc = 0; uint64_t dma_log_count = 0, dma_linear_reads = 0;
	// ---- picture ----
	// Composes the current frame from sprite RAM and the palette into a 128x128 RGB555 buffer. The firmware only
	// uses sprites (no tile layers), in "direct address" mode: the sprite's tile number times 8 is the address of
	// its pixel data. Sprite format follows MAME's gpl_renderer: 4 words (tile, x, y, attributes) plus one
	// extension word per sprite in the second sprite RAM bank. First pass: no fade, saturation or tile layers.
	int sprite_centre_x = 64, sprite_centre_y = 128;      // origin for centred coordinates (SControl bit 1 clear); found by eye, see docs
	uint32_t gfx_base_words = 0x9000;                     // pixel-data addresses are linear: flash word = address - 0x9000
	uint16_t gfx_word(uint32_t a) const { return a < 0x2800 ? ram[a] : (a >= gfx_base_words ? flash_word(a - gfx_base_words) : 0); }
	static uint16_t mix5(unsigned bottom, unsigned top, unsigned level) { return (uint16_t)((bottom * (32 - level) + top * level) >> 5); }
	int render(uint16_t *out) const {
		const int W = 128, H = 128; int drawn = 0;
		for (int i = 0; i < W * H; i++) out[i] = 0;
		const uint16_t sc = io[0x042];
		if (!(sc & 1)) return 0;
		int limit = sc >> 8; if (!limit) limit = 0x100;
		const uint32_t base = (io[0x07f] & 0x40) ? (((uint32_t)(io[0x02d] & 0x7ff) << 16) | io[0x022]) : (uint32_t)io[0x022] * 0x40;
		for (int pri = 0; pri < 4; pri++) for (int n = 0; n < limit; n++) {
			uint32_t tile = spriteram[4 * n]; int x = (int16_t)spriteram[4 * n + 1], y = (int16_t)spriteram[4 * n + 2];
			const uint16_t attr = spriteram[4 * n + 3], ext = spriteram[0x400 + n];
			if (!tile || ((attr >> 12) & 3) != pri) continue;
			const int h = 8 << ((attr >> 6) & 3), w = 8 << ((attr >> 4) & 3);
			if (!(sc & 2)) { x = sprite_centre_x + x - w / 2; y = sprite_centre_y - y - h / 2; }
			x &= 0x1ff; y &= 0x1ff; if (x >= 0x200 - 64) x -= 0x200; if (y >= 0x200 - 64) y -= 0x200;
			const unsigned nc = ((attr & 3) + 1) * 2, row_words = nc * w / 16;
			uint32_t tile_words = row_words * h;
			if (sc & 0x10) { tile |= (uint32_t)(ext & 0xff) << 16; tile_words = 8; }
			const bool flipx = attr & 4, flipy = attr & 8, blend = attr & 0x4000; const unsigned level = ((ext >> 8) & 0x3f) >> 1;
			unsigned pal = (attr & 0x0f00) >> 4; if (io[0x03a] & 1) pal |= 0x100; if (attr & 0x8000) pal |= 0x200;
			pal = (pal >> nc) << nc; drawn++;
			for (int row = 0; row < h; row++) {
				const int sy = y + row; if (sy < 0 || sy >= H) continue;
				uint32_t m = base + tile_words * tile + row_words * (flipy ? h - 1 - row : row), bits = 0; unsigned nbits = 0;
				for (int i = 0; i < w; i++) {
					bits <<= nc;
					if (nbits < nc) { uint16_t b = gfx_word(m++); b = (uint16_t)((b << 8) | (b >> 8)); bits |= (uint32_t)b << (nc - nbits); nbits += 16; }
					nbits -= nc; const unsigned pix = bits >> 16; bits &= 0xffff;
					const int sx = x + (flipx ? w - 1 - i : i); if (sx < 0 || sx >= W) continue;
					const uint16_t rgb = palette[(pal + pix) & 0x3ff]; if (rgb & 0x8000) continue;
					uint16_t &dst = out[sy * W + sx];
					dst = blend ? (uint16_t)((mix5((dst >> 10) & 0x1f, (rgb >> 10) & 0x1f, level) << 10) | (mix5((dst >> 5) & 0x1f, (rgb >> 5) & 0x1f, level) << 5) | mix5(dst & 0x1f, rgb & 0x1f, level)) : rgb;
				}
			}
		}
		return drawn;
	} uint64_t sprite_dma_runs = 0; uint64_t spi0_bytes = 0, spi0_unsel = 0;
	void lcd_flush() { if (!lcd_line.empty()) { lcd_log.push_back(lcd_line); lcd_line.clear(); } }
};
