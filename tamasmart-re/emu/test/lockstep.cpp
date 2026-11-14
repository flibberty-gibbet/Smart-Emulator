// Test tool, not part of the emulator. Links MAME's unSP core (GPL-2.0+) as a reference and checks the
// project's own core against it.
//   lockstep IMAGE decode LISTING      compare instruction lengths for every instruction in a listing
//   lockstep IMAGE run [MAX_INSNS]     run both cores on identical machines, compare state after every step
#include "emu.h"
#include "unsp.h"
#include "unspdasm.h"
#include "cpu.h"
#include "machine.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
using RefMachine = MachineT<unsp_20_device, unsp_bus>;
using NewMachine = MachineT<tsmart::Cpu, tsmart::Bus>;
struct DasmBuf : util::disasm_interface::data_buffer { RefMachine &m; explicit DasmBuf(RefMachine &mm) : m(mm) { }
	u16 r16(offs_t pc) const override { return m.read_code(pc); } };
static std::string dasm(RefMachine &m, uint32_t pc) { static unsp_20_disassembler d; DasmBuf b(m); std::ostringstream s; d.disassemble(s, pc, b, b); return s.str(); }
template <class M> static void dump(const char *who, M &m) {
	printf("  %-5s pc=%06x sp=%04x r1=%04x r2=%04x r3=%04x r4=%04x bp=%04x sr=%04x fr=%04x r8..15=", who, m.cpu.lpc(), m.cpu.reg(0), m.cpu.reg(1), m.cpu.reg(2), m.cpu.reg(3), m.cpu.reg(4), m.cpu.reg(5), m.cpu.reg(6), m.cpu.get_fr());
	for (int i = 8; i < 16; i++) printf("%04x ", m.cpu.reg(i) & 0xffff);
	printf(" now=%llu halted=%d\n", (unsigned long long)m.now, m.halted);
}
int main(int argc, char **argv) {
	if (argc < 3) { fprintf(stderr, "usage: lockstep IMAGE decode LISTING | run [MAX_INSNS]\n"); return 2; }
	std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> img((std::istreambuf_iterator<char>(f)), {});
	if (img.size() != 0x1000000) { fprintf(stderr, "expected a 16 MB image\n"); return 2; }
	if (!strcmp(argv[2], "decode")) {
		std::ifstream l(argv[3]); std::string line; unsigned long n = 0, bad = 0;
		auto word = [&](uint32_t a) { size_t o = size_t(a - 0x9000) * 2; return uint16_t(img[o] | (img[o + 1] << 8)); };
		while (std::getline(l, line)) {
			if (line.size() < 12 || line[6] != ':') continue;
			uint32_t a = strtoul(line.substr(0, 6).c_str(), 0, 16); if (a < 0x9000) continue;
			int words = 0; size_t p = 8; while (p + 4 <= line.size() && isxdigit((unsigned char)line[p]) && isxdigit((unsigned char)line[p + 3]) && (p + 4 == line.size() || line[p + 4] == ' ')) { words++; p += 5; }
			if (line.find("<BAD>") != std::string::npos || line.find("--") != std::string::npos) continue;
			int mine = tsmart::Cpu::insn_length(word(a), word(a + 1)); n++;
			if (mine != words) { if (bad++ < 20) printf("length mismatch at %06x: listing %d, core %d: %s\n", a, words, mine, line.c_str()); }
		}
		printf("decode check: %lu instructions, %lu length mismatches\n", n, bad); return bad ? 1 : 0;
	}
	uint64_t max = argc > 3 ? strtoull(argv[3], 0, 0) : 60000000ull;
	RefMachine ref(img); NewMachine mine(img); mine.cpu.mame_fiq_compat = true;
	uint32_t prev_pc = 0; uint64_t fiqs = 0;
	for (uint64_t i = 0; i < max; i++) {
		bool same = ref.now == mine.now && ref.halted == mine.halted && ref.cpu.get_fr() == mine.cpu.get_fr();
		for (int r = 0; r < 16 && same; r++) same = (ref.cpu.reg(r) & 0xffff) == (mine.cpu.reg(r) & 0xffff);
		if (!same) {
			printf("DIVERGED after %llu instructions; last instruction at %06x: %s\n", (unsigned long long)i, prev_pc, dasm(ref, prev_pc).c_str());
			dump("mame", ref); dump("ours", mine); return 1;
		}
		prev_pc = ref.cpu.lpc(); ref.cur_pc = mine.cur_pc = prev_pc;
		if (prev_pc == 0x00904a) fiqs++;
		try { ref.step(); } catch (std::exception &e) { printf("reference core stopped after %llu instructions at %06x: %s\n", (unsigned long long)i, prev_pc, e.what()); return 1; }
		try { mine.step(); } catch (std::exception &e) { printf("our core stopped after %llu instructions at %06x: %s\n", (unsigned long long)i, prev_pc, e.what()); return 1; }
	}
	bool ram_same = !memcmp(ref.ram, mine.ram, sizeof ref.ram);
	printf("lockstep OK: %llu instructions, %llu frames, identical registers and timing at every step; RAM %s; fast-interrupt entries seen: %llu\n",
		(unsigned long long)max, (unsigned long long)mine.frames, ram_same ? "identical" : "DIFFERS", (unsigned long long)fiqs);
	return ram_same ? 0 : 1;
}
