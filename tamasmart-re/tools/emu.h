// Minimal stand-in for MAME's emu.h so unspdasm*.cpp can be built standalone.
#pragma once
#include <cstdint>
#include <cstdio>
#include <ostream>
#include <string>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint32_t offs_t;
namespace util {
class disasm_interface {
public:
	static constexpr u32 SUPPORTED = 0x80000000, STEP_OUT = 0x40000000, STEP_OVER = 0x20000000, STEP_COND = 0x10000000, LENGTHMASK = 0x0000ffff;
	class data_buffer { public: virtual ~data_buffer() = default; virtual u16 r16(offs_t pc) const = 0; };
	virtual ~disasm_interface() = default;
	virtual u32 opcode_alignment() const = 0;
	virtual offs_t disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params) = 0;
};
#pragma GCC diagnostic ignored "-Wformat-security"
template <typename... A> void stream_format(std::ostream &s, const char *fmt, A... a) { char b[256]; snprintf(b, sizeof b, fmt, a...); s << b; }
}
