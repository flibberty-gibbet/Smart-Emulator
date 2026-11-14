// tsmart-run: headless bring-up runner (emu/headless) for the Tamagotchi Smart firmware.
// usage: tsmart-run IMAGE [--watch RAMADDR]... [--shot FRAME:file.ppm]... [--adc CH:HEX]... [--card FILE [--card-at FRAME] [--eject-at FRAME] [--save-card OUT.bin]] [--save-flash OUT.bin] [--frames N] [--insns N] [--trace-io] [--trace FILE] [--io-report] [--stop PC] [--press F0:F1]... [--touch PAD:F0:F1]...
#include "tsmart.h"
#ifdef TSMART_WITH_MAME_DASM   // optional: link MAME's unSP disassembler (GPL-2.0+) for readable traces
#include "emu.h"
#include "unspdasm.h"
#endif
#include <map>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <set>

static std::map<uint16_t, std::string> g_regnames;
static void load_regnames(const char *path) {
	std::ifstream f(path); std::string s((std::istreambuf_iterator<char>(f)), {});
	size_t p = 0;
	while ((p = s.find('"', p)) != std::string::npos) {
		size_t e = s.find('"', p + 1); std::string k = s.substr(p + 1, e - p - 1);
		size_t v0 = s.find('"', e + 1), v1 = s.find('"', v0 + 1);
		if (v0 == std::string::npos) break;
		g_regnames[(uint16_t)strtoul(k.c_str(), 0, 16)] = s.substr(v0 + 1, v1 - v0 - 1); p = v1 + 1;
	}
}
static const char *regname(uint16_t a) { auto it = g_regnames.find(a); return it == g_regnames.end() ? "" : it->second.c_str(); }

using tsmart::Machine;
#ifdef TSMART_WITH_MAME_DASM
struct DasmBuf : util::disasm_interface::data_buffer { Machine &m; explicit DasmBuf(Machine &mm) : m(mm) { }
	u16 r16(offs_t pc) const override { return m.read_code(pc); } };
static std::string dasm(Machine &m, uint32_t pc) { static unsp_20_disassembler d; DasmBuf b(m); std::ostringstream s; d.disassemble(s, pc, b, b); return s.str(); }
#else
static std::string dasm(Machine &m, uint32_t pc) {      // raw words; look the address up in analysis/tamasmart_eng_code.asm
	char t[40]; uint16_t op = m.read_code(pc), nx = m.read_code(pc + 1); int n = tsmart::Cpu::insn_length(op, nx);
	if (n == 1) snprintf(t, sizeof t, "%04x", op); else if (n == 2) snprintf(t, sizeof t, "%04x %04x", op, nx); else snprintf(t, sizeof t, "%04x %04x %04x", op, nx, m.read_code(pc + 2));
	return t; }
#endif

int main(int argc, char **argv) {
	if (argc < 2) { fprintf(stderr, "usage: tsmart-run IMAGE [options]\n"); return 2; }
	std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> img((std::istreambuf_iterator<char>(f)), {});
	if (img.size() != 0x1000000 || memcmp(img.data(), "GP-SPIF-HEADER", 14)) { fprintf(stderr, "expected a 16 MB RAW image starting with GP-SPIF-HEADER\n"); return 2; }
	uint64_t max_frames = 120, max_insns = 2000000000ull; bool io_report = false; uint32_t stop_pc = 0; FILE *trace = nullptr;
	struct Touch { int pad; unsigned long long f0, f1; }; std::vector<Touch> touches; std::vector<std::pair<unsigned long long, unsigned long long>> presses;
	std::string save_flash, card_path, save_card; uint64_t card_at = 0, eject_at = ~0ull; std::vector<uint8_t> card_img; Machine m(img); std::vector<uint16_t> watch_ram, watch_last; std::vector<std::pair<uint64_t, std::string>> shots; uint64_t shot_frame = ~0ull;
	for (int i = 2; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--frames") max_frames = strtoull(argv[++i], 0, 0);
		else if (a == "--insns") max_insns = strtoull(argv[++i], 0, 0);
		else if (a == "--trace-io") m.trace_io = true;
		else if (a == "--io-report") io_report = true;
		else if (a == "--stop") stop_pc = strtoul(argv[++i], 0, 16);
		else if (a == "--trace") trace = fopen(argv[++i], "w");
		else if (a == "--fps") m.fps = atof(argv[++i]);
		else if (a == "--card") card_path = argv[++i];
		else if (a == "--card-at") card_at = strtoull(argv[++i], 0, 0);
		else if (a == "--save-card") save_card = argv[++i];
		else if (a == "--eject-at") eject_at = strtoull(argv[++i], 0, 0);
		else if (a == "--cpu-speed") { unsigned n1 = 1, d1 = 2; if (sscanf(argv[++i], "%u/%u", &n1, &d1) == 2 && n1 && d1) { m.cpu_speed_num = n1; m.cpu_speed_den = d1; } }
		else if (a == "--save-flash") save_flash = argv[++i];
		else if (a == "--adc") { unsigned ch, v; if (sscanf(argv[++i], "%u:%x", &ch, &v) == 2 && ch < 16) m.adc_ch[ch] = (uint16_t)v; }
		else if (a == "--shot") { unsigned long long fr; char path[256]; if (sscanf(argv[++i], "%llu:%255s", &fr, path) == 2) shots.push_back({ fr, path }); }
		else if (a == "--centre") { sscanf(argv[++i], "%d,%d", &m.sprite_centre_x, &m.sprite_centre_y); }
		else if (a == "--gfx-base") m.gfx_base_words = strtoul(argv[++i], 0, 16);
		else if (a == "--watch") watch_ram.push_back((uint16_t)strtoul(argv[++i], 0, 16));
		else if (a == "--press") { unsigned long long f0 = 0, f1 = 0; sscanf(argv[++i], "%llu:%llu", &f0, &f1); presses.push_back({f0, f1}); }
		else if (a == "--touch") { Touch t{0, 0, 0}; sscanf(argv[++i], "%d:%llu:%llu", &t.pad, &t.f0, &t.f1); touches.push_back(t); }
	}
	load_regnames("analysis/regnames.json"); m.regname = regname;
	watch_last.assign(watch_ram.size(), 0xffff);
	bool was_sleeping = false;
	if (!card_path.empty()) { std::ifstream cf(card_path, std::ios::binary); card_img.assign(std::istreambuf_iterator<char>(cf), {});
		if (card_img.empty()) { fprintf(stderr, "cannot read card %s\n", card_path.c_str()); return 2; } }

	// milestone watch points (English RAW image)
	struct Watch { uint32_t pc; const char *name; uint64_t hits; uint64_t first_insn; };
	std::vector<Watch> watch = {
		{ 0x009226, "M1  reset code done, RAM filled", 0, 0 }, { 0x00922c, "M2  RAM boot routine returned", 0, 0 },
		{ 0x07c3c8, "M3  main() entered", 0, 0 },             { 0x07c4b3, "M4  hardware init done (LCD initialised)", 0, 0 },
		{ 0x07c4fe, "    main loop entered", 0, 0 },           { 0x07a637, "M5  game_frame_update calls", 0, 0 } };
	uint64_t last_fu = 0, last_fu_frame = 0;
	std::vector<uint8_t> seen(0x400000, 0); uint64_t since_new = 0; const uint64_t HANG = 30000000;
	std::vector<std::pair<uint32_t,uint32_t>> calls(64); size_t cpos = 0;
	std::vector<uint32_t> recent(8192, 0); size_t rpos = 0; int rc = 0; std::string why = "frame limit reached";
	try {
		while (m.frames < max_frames && m.insns < max_insns) {
			uint32_t pc = m.cpu.lpc(); m.cur_pc = pc;
			m.button = false; for (auto &pr : presses) if (m.frames >= pr.first && m.frames < pr.second) m.button = true;
			if (!touches.empty()) { for (int k = 0; k < 4; k++) m.pad[k] = false; for (auto &t : touches) if (m.frames >= t.f0 && m.frames < t.f1) m.pad[t.pad & 3] = true; }
			for (auto &w : watch) if (w.pc == pc) { if (!w.hits++) { w.first_insn = m.insns; fprintf(stderr, "[%10llu insns, frame %llu] %s\n", (unsigned long long)m.insns, (unsigned long long)m.frames, w.name); } }
			for (size_t w = 0; w < watch_ram.size(); w++) if (m.ram[watch_ram[w]] != watch_last[w]) { watch_last[w] = m.ram[watch_ram[w]];
				fprintf(stderr, "[frame %llu] ram[%04x] = %04x  (pc %06x)\n", (unsigned long long)m.frames, watch_ram[w], watch_last[w], pc); }
			if (m.frames != shot_frame) { shot_frame = m.frames;
				if (!card_img.empty() && m.frames == card_at) { m.insert_card(card_img); fprintf(stderr, "[frame %llu] card inserted\n", (unsigned long long)m.frames); }
				if (m.frames == eject_at && m.card_present) { m.eject_card(); fprintf(stderr, "[frame %llu] card ejected\n", (unsigned long long)m.frames); }
				for (auto &sh : shots) if (sh.first == m.frames) { std::vector<uint16_t> fb(m.lcd, m.lcd + 128 * 128), scratch(128 * 128); int n = m.render(scratch.data());
					FILE *o = fopen(sh.second.c_str(), "wb"); fprintf(o, "P6\n128 128\n255\n");
					for (uint16_t px : fb) { uint8_t c[3] = { (uint8_t)(((px >> 10) & 31) * 255 / 31), (uint8_t)(((px >> 5) & 31) * 255 / 31), (uint8_t)((px & 31) * 255 / 31) }; fwrite(c, 1, 3, o); }
					fclose(o); fprintf(stderr, "[frame %llu] wrote %s (%d sprites drawn, scene %04x, palette[000,100,200,300]=%04x %04x %04x %04x, sprite0=%04x %04x %04x %04x)\n", (unsigned long long)m.frames, sh.second.c_str(), n, m.ram[0x494], m.palette[0], m.palette[0x100], m.palette[0x200], m.palette[0x300], m.spriteram[0], m.spriteram[1], m.spriteram[2], m.spriteram[3]); } }
			if (stop_pc && pc == stop_pc) { why = "stop address reached"; break; }
			if (trace) fprintf(trace, "%06x sp=%04x r1=%04x r2=%04x r3=%04x r4=%04x bp=%04x sr=%04x  %s\n", pc, m.cpu.reg(0), m.cpu.reg(1), m.cpu.reg(2), m.cpu.reg(3), m.cpu.reg(4), m.cpu.reg(5), m.cpu.reg(6), dasm(m, pc).c_str());
			recent[rpos++ & 8191] = pc;
			{ uint16_t op = m.read_code(pc); if ((op & 0xffc0) == 0xf040) calls[cpos++ & 63] = { pc, ((uint32_t)(op & 0x3f) << 16) | m.read_code(pc + 1) }; }
			if (!seen[pc]) { seen[pc] = 1; since_new = 0; } else if (!m.halted && ++since_new > HANG && !watch[5].hits) { why = "no new code reached for 30M instructions (probable polling loop)"; rc = 1; break; }
			if (watch[5].hits != last_fu) { last_fu = watch[5].hits; last_fu_frame = m.frames; }
			else if (m.sleeping) { if (!was_sleeping) fprintf(stderr, "[frame %llu] deep sleep\n", (unsigned long long)m.frames); was_sleeping = true; last_fu_frame = m.frames; }
			else if (was_sleeping) { fprintf(stderr, "[frame %llu] woke\n", (unsigned long long)m.frames); was_sleeping = false; last_fu_frame = m.frames; }
			else if (watch[5].hits && m.frames > last_fu_frame + 150) { why = "game_frame_update has not been called for 150 frames (stuck inside a frame)"; rc = 1; break; }
			m.step();
		}
		if (m.insns >= max_insns) why = "instruction limit reached";
	} catch (std::exception &e) { why = std::string("CPU core error: ") + e.what(); rc = 1; }
	m.lcd_flush(); if (trace) fclose(trace);
	if (!save_flash.empty()) { std::ofstream o(save_flash, std::ios::binary); o.write((const char *)m.flash.data(), (std::streamsize)m.flash.size()); fprintf(stderr, "wrote flash image with saves to %s\n", save_flash.c_str()); }

	printf("stopped: %s\n  pc=%06x  insns=%llu  frames=%llu  halted=%d\n", why.c_str(), m.cpu.lpc(), (unsigned long long)m.insns, (unsigned long long)m.frames, m.halted);
	printf("  sp=%04x r1=%04x r2=%04x r3=%04x r4=%04x bp=%04x sr=%04x fr=%04x\n", m.cpu.reg(0), m.cpu.reg(1), m.cpu.reg(2), m.cpu.reg(3), m.cpu.reg(4), m.cpu.reg(5), m.cpu.reg(6), m.cpu.get_fr());
	printf("milestones:\n"); for (auto &w : watch) printf("  %-45s %s (hits %llu)\n", w.name, w.hits ? "reached" : "NOT reached", (unsigned long long)w.hits);
	printf("ram: frame_counter[061e]=%04x button[05f2]=%04x pressed_edges_seen touch_mask[0f1c]=%04x touch[0d57..]=%04x %04x %04x %04x base=%04x %04x %04x %04x\n",
		m.ram[0x61e], m.ram[0x5f2], m.ram[0xf1c], m.ram[0xd57], m.ram[0xd58], m.ram[0xd59], m.ram[0xd5a], m.ram[0xd5b], m.ram[0xd5c], m.ram[0xd5d], m.ram[0xd5e]);
	printf("flash: %llu erases, %llu programs; dma runs %llu; lcd bytes lines %zu\n", (unsigned long long)m.nor.n_erase, (unsigned long long)m.nor.n_program, (unsigned long long)m.dma_log_count, m.lcd_log.size());
	if (!save_card.empty() && !m.card.mem.empty()) { std::ofstream cf(save_card, std::ios::binary); cf.write((const char *)m.card.mem.data(), m.card.mem.size()); }
	if (!card_img.empty()) { printf("card: %llu SPI bytes, %llu commands, %llu bytes read, %llu programs, %llu erases%s\n", (unsigned long long)m.spi0_bytes, (unsigned long long)m.card.n_cmd, (unsigned long long)m.card.n_read, (unsigned long long)m.card.n_program, (unsigned long long)m.card.n_erase, m.card.dirty ? " (card contents changed)" : "");
		printf("  unselected bytes %llu; first commands:", (unsigned long long)m.spi0_unsel); for (auto c : m.card.cmdlog) printf(" %02x", c); printf("\n");
		size_t shown = 0; for (auto &l : m.card.log) { if (shown++ < 60) printf("  card %s\n", l.c_str()); } if (m.card.log.size() > 60) printf("  ... %zu more\n", m.card.log.size() - 60); }
	if (rc || stop_pc) {   // likely return addresses on the stack: a word pair (sr, pc) whose target follows a call instruction
		printf("call stack guess (innermost first):");
		for (uint32_t sp = m.cpu.reg(0) + 1; sp < 0x27ff; sp++) { uint32_t ret = ((uint32_t)(m.ram[sp] & 0x3f) << 16) | m.ram[sp + 1];
			if (ret >= 0x9002 && ret < 0x200000) { uint16_t c2 = m.read_code(ret - 2), c1 = m.read_code(ret - 1);
				if ((c2 & 0xffc0) == 0xf040) printf(" %06x(->%06x)", ret - 2, ((uint32_t)(c2 & 0x3f) << 16) | c1); else if ((c1 & 0xf1e7) == 0xf161) printf(" %06x(->mr)", ret - 1); } }
		printf("\n"); }
	if (rc || stop_pc) { printf("last calls (oldest first):\n"); for (size_t i = 0; i < 64; i++) { auto &c = calls[(cpos + i) & 63]; if (c.first) printf("  %06x -> %06x\n", c.first, c.second); } }
	if (rc) {   // where is it stuck?
		std::map<uint32_t, int> hot; for (auto p : recent) if (p >= 0x9200 || p < 0x2800) hot[p]++;   // skip interrupt handlers
		uint32_t lo = ~0u, hi = 0; for (auto &h : hot) { lo = std::min(lo, h.first); hi = std::max(hi, h.first); }
		printf("loop spans %06x..%06x (%zu distinct PCs). Listing:\n", lo, hi, hot.size());
		int n = 0; for (auto &h : hot) { if (n++ > 48) { printf("  ...\n"); break; } printf("  %06x: %s\n", h.first, dasm(m, h.first).c_str()); }
	}
	{ std::ofstream o("out/lcd_seen.txt"); for (auto &l : m.lcd_log) o << l << "\n"; }
	if (io_report) {
		printf("I/O registers touched (addr name reads writes last_written first_pc):\n");
		for (auto &kv : m.iostat) printf("  %04x %-22s r=%-8llu w=%-8llu last=%04x pc=%06x\n", kv.first, regname(kv.first), (unsigned long long)kv.second.r, (unsigned long long)kv.second.w, kv.second.last_w, kv.second.first_pc);
		for (auto &kv : m.bad_access) printf("  unmapped %s %06x x%llu\n", (kv.first & 1) ? "write" : "read", kv.first >> 1, (unsigned long long)kv.second);
		for (auto &l : m.nor.log) printf("  flash %s\n", l.c_str());
	}
	return rc;
}
