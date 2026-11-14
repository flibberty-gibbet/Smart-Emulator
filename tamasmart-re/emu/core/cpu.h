// µ'nSP 2.0 interpreter for the Tamagotchi Smart emulator.
//
// Written for this project. Instruction behaviour was checked against MAME's unSP core by running both in
// lockstep (see emu/test/lockstep.cpp), and cycle costs follow MAME's estimates so the two stay comparable.
// Two deliberate differences from MAME, both about the fast interrupt, are marked "FIQ" in cpu.cpp.
#pragma once
#include <cstdint>

namespace tsmart {

struct Bus {
	virtual ~Bus() = default;
	virtual uint16_t read16(uint32_t addr) = 0;              // addr is a 22-bit word address
	virtual void write16(uint32_t addr, uint16_t value) = 0;
};

class Cpu {
public:
	enum Reg { SP, R1, R2, R3, R4, BP, SR, PC, R8, R9, R10, R11, R12, R13, R14, R15 };
	enum Line { LINE_FIQ = 0, LINE_IRQ0, LINE_IRQ1, LINE_IRQ2, LINE_IRQ3, LINE_IRQ4, LINE_IRQ5, LINE_IRQ6, LINE_IRQ7 };
	enum : uint16_t { FLAG_C = 0x0040, FLAG_S = 0x0080, FLAG_Z = 0x0100, FLAG_N = 0x0200 };

	explicit Cpu(Bus &bus) : bus_(bus) { }

	void reset(uint16_t vector_base = 0xfff0);   // loads PC from vector_base + 7
	int  step();                                 // one instruction, then at most one interrupt entry; returns cycles
	void set_line(int line, int asserted);       // level-sensitive request lines, see Line
	bool irq_taken_last_step() const { return took_interrupt_; }

	uint32_t lpc() const { return (uint32_t(r_[SR] & 0x3f) << 16) | r_[PC]; }
	uint32_t reg(int i) const { return r_[i & 15]; }
	void     set_reg(int i, uint32_t v) { r_[i & 15] = uint16_t(v); }
	uint16_t get_fr() const;
	void     set_fr(uint16_t fr);
	uint16_t get_ds() const { return (r_[SR] >> 10) & 0x3f; }
	bool     in_fiq() const { return in_fiq_; }
	bool     mame_fiq_compat = false;            // test-only: reproduce MAME's FIQ behaviour for lockstep runs

	// Number of words (1..3) the instruction starting with `op` occupies; `next` is the following word.
	static int insn_length(uint16_t op, uint16_t next);

private:
	Bus &bus_;
	uint16_t r_[16] = {};
	uint16_t alt_[4] = {};                       // second register bank for R1..R4 (SECBANK)
	// fields of the FR register
	bool aq_ = false, bank_ = false, fraction_ = false, fir_move_ = true, fiq_en_ = false, irq_en_ = false, nest_ = false;
	uint8_t sb_ = 0, pri_ = 8;
	// interrupt state
	bool in_fiq_ = false, in_irq_ = false, took_interrupt_ = false;
	uint16_t lines_ = 0;
	uint16_t vector_base_ = 0xfff0;
	// DIVQ runs as 16 consecutive single-bit steps
	int divq_left_ = 0; uint32_t divq_dividend_ = 0, divq_divisor_ = 0, divq_acc_ = 0;
	int cycles_ = 0;

	uint16_t rd(uint32_t a) { return bus_.read16(a & 0x3fffff); }
	void     wr(uint32_t a, uint16_t v) { bus_.write16(a & 0x3fffff, v); }
	uint16_t fetch();
	void     set_lpc(uint32_t a) { r_[PC] = uint16_t(a); r_[SR] = (r_[SR] & 0xffc0) | ((a >> 16) & 0x3f); }
	void     push(uint16_t v, int base) { wr(r_[base], v); r_[base]--; }
	uint16_t pop(int base) { r_[base]++; return rd(r_[base]); }
	uint32_t ds_addr(int reg) const { return (uint32_t(get_ds()) << 16) | r_[reg]; }
	void     flags_nz(uint16_t v);
	uint16_t add_with_flags(uint16_t a, uint16_t b, unsigned carry_in, bool flags);
	bool     alu(unsigned fn, uint16_t a, uint16_t b, uint32_t addr, bool flags, uint16_t &out);
	void     swap_bank();
	void     exec_general(uint16_t op);
	void     exec_jump(uint16_t op);
	void     exec_e(uint16_t op);
	void     exec_f(uint16_t op);
	void     exec_extended();
	void     exec_misc(uint16_t op);
	void     bit_op(unsigned which, unsigned bit, uint16_t &value);
	void     mem_bit_op(unsigned which, unsigned bit, uint32_t addr);
	void     sum_of_products(int rd_reg, int rs_reg, int count);
	void     divq_step();
	void     enter_interrupts();
	[[noreturn]] void undefined(uint16_t op, uint32_t at);
};

}  // namespace tsmart
