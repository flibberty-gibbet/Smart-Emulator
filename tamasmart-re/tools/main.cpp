// unsp-dasm: recursive-descent / linear disassembler for GPL951xx SPI flash images.
// usage: unsp-dasm image.bin linear START END            (CPU word addresses, hex)
//        unsp-dasm image.bin trace ENTRY [ENTRY...]      (follow control flow, print listing)
#include "emu.h"
#include "unspdasm.h"
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <cstring>
static std::vector<u8> img;
static const u32 BASE = 0x9000;
struct buf : util::disasm_interface::data_buffer {
	u16 r16(offs_t pc) const override { if (pc < BASE) return 0; size_t o = size_t(pc - BASE) * 2; if (o + 1 >= img.size()) return 0xffff; return img[o] | (img[o+1] << 8); }
};
struct insn { u32 len; std::string text; std::vector<u32> targets; bool stop; bool call; };
static insn decode(u32 pc) {
	static unsp_20_disassembler d; buf b; std::ostringstream ss;
	u32 r = d.disassemble(ss, pc, b, b); insn i; i.len = r & 0xffff; if (!i.len) i.len = 1; i.text = ss.str(); i.stop = false; i.call = false;
	u16 op = b.r16(pc), imm = b.r16(pc + 1);
	u16 op0 = op >> 12, opA = (op >> 9) & 7, op1 = (op >> 6) & 7;
	if (op0 < 15 && opA == 7 && op1 < 2) { u32 t = op1 == 0 ? pc + 1 + (op & 0x3f) : pc + 1 - (op & 0x3f); i.targets.push_back(t); if (op0 == 14) i.stop = true; }
	else if ((op & 0xffc0) == 0xf040) { i.targets.push_back(((op & 0x3f) << 16) | imm); i.call = true; }
	else if ((op & 0xffc0) == 0xfe80) { i.targets.push_back(((op & 0x3f) << 16) | imm); i.stop = true; }
	else if (op == 0x9a90 || op == 0x9a98) i.stop = true;
	else if (op0 < 14 && opA == 7) i.stop = true; // pc = ... (computed jump / pop pc)
	else if ((op & 0xf1f8) == 0x9088 && ((op & 7) + ((op >> 3) & 7)) >= 7 && (op & 0x0e00) != 0x0a00) {} // pop range reaching pc handled by text check below
	if (i.text.find("pop") != std::string::npos && i.text.find("pc") != std::string::npos) i.stop = true;
	if (i.text.find("retf") != std::string::npos || i.text.find("reti") != std::string::npos) i.stop = true;
	return i;
}
static void line(u32 pc, const insn &i) {
	buf b; printf("%06x: ", pc); for (u32 k = 0; k < 3; k++) if (k < i.len) printf("%04x ", b.r16(pc + k)); else printf("     "); printf(" %s\n", i.text.c_str());
}
int main(int argc, char **argv) {
	if (argc < 4) { fprintf(stderr, "usage\n"); return 1; }
	std::ifstream f(argv[1], std::ios::binary); img.assign(std::istreambuf_iterator<char>(f), {});
	if (!strcmp(argv[2], "linear")) {
		u32 pc = strtoul(argv[3], 0, 16), end = strtoul(argv[4], 0, 16);
		while (pc < end) { insn i = decode(pc); line(pc, i); pc += i.len; }
	} else {
		std::map<u32, insn> code; std::deque<u32> q; std::set<u32> funcs; std::multimap<u32,u32> xref;
		u32 lim = 0x400000;
		for (int a = 3; a < argc; a++) { u32 e = strtoul(argv[a], 0, 16); q.push_back(e); funcs.insert(e); }
		while (!q.empty()) {
			u32 pc = q.front(); q.pop_front();
			while (pc >= BASE && pc < lim && !code.count(pc)) {
				insn i = decode(pc); if (i.text.find("--") == 0 || i.text.find("<") != std::string::npos) { i.stop = true; }
				code[pc] = i;
				for (u32 t : i.targets) { xref.insert({t, pc}); if (i.call) funcs.insert(t); if (!code.count(t)) q.push_back(t); }
				if (i.stop) break; pc += i.len;
			}
		}
		u32 prev = 0;
		for (auto &kv : code) {
			if (kv.first != prev) printf("\n");
			if (funcs.count(kv.first)) { printf("; ---- sub_%06x  (callers:", kv.first); int n = 0; auto r = xref.equal_range(kv.first); for (auto it = r.first; it != r.second && n < 8; ++it, ++n) printf(" %06x", it->second); printf(")\n"); }
			else if (xref.count(kv.first)) printf("loc_%06x:\n", kv.first);
			line(kv.first, kv.second); prev = kv.first + kv.second.len;
		}
		fprintf(stderr, "insns=%zu funcs=%zu\n", code.size(), funcs.size());
	}
}
