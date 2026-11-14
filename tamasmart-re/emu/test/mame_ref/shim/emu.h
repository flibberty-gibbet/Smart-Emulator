// Minimal stand-in for MAME's emu.h so the unSP core and disassembler sources build standalone.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <climits>
#include <ostream>
#include <string>
#include <utility>
#include <stdexcept>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef uint32_t offs_t;
template <typename T, typename U> constexpr T BIT(T x, U n) { return (x >> n) & T(1); }
extern bool g_core_log;
inline void logerror(const char *fmt, ...) { if (!g_core_log) return; va_list a; va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a); }
[[noreturn]] inline void fatalerror(const char *fmt, ...) { char b[256]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); throw std::runtime_error(b); }
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
