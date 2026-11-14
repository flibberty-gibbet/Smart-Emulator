// µ'nSP 2.0 interpreter. See cpu.h for provenance and the two intentional FIQ differences from MAME.
#include "cpu.h"
#include <cstdio>
#include <stdexcept>
#include <string>

namespace tsmart {

// ---------------------------------------------------------------------------------------------
// Reset, FR register, helpers
// ---------------------------------------------------------------------------------------------

void Cpu::reset(uint16_t vector_base)
{
	vector_base_ = vector_base;
	for (auto &x : r_) x = 0;
	for (auto &x : alt_) x = 0;
	aq_ = bank_ = fraction_ = fiq_en_ = irq_en_ = nest_ = false;
	fir_move_ = true; sb_ = 0; pri_ = 8;
	in_fiq_ = in_irq_ = took_interrupt_ = false; lines_ = 0;
	divq_left_ = 0; divq_dividend_ = divq_divisor_ = divq_acc_ = 0;
	r_[PC] = rd(uint32_t(vector_base_) + 7);
}

uint16_t Cpu::get_fr() const
{
	return uint16_t((aq_ << 14) | (bank_ << 13) | (fraction_ << 12) | (fir_move_ << 11) | ((sb_ & 0xf) << 7)
	              | (fiq_en_ << 6) | (irq_en_ << 5) | (nest_ << 4) | (pri_ & 0xf));
}

void Cpu::swap_bank()
{
	for (int i = 0; i < 4; i++) { uint16_t t = r_[R1 + i]; r_[R1 + i] = alt_[i]; alt_[i] = t; }
}

void Cpu::set_fr(uint16_t fr)
{
	aq_ = (fr >> 14) & 1;
	const bool new_bank = (fr >> 13) & 1;
	if (new_bank != bank_) swap_bank();
	bank_ = new_bank;
	fraction_ = (fr >> 12) & 1;
	fir_move_ = (fr >> 11) & 1;
	sb_ = (fr >> 7) & 0xf;
	fiq_en_ = (fr >> 6) & 1;
	irq_en_ = (fr >> 5) & 1;
	nest_ = (fr >> 4) & 1;
	pri_ = fr & 0xf;
}

uint16_t Cpu::fetch()
{
	const uint32_t at = lpc();
	const uint16_t w = rd(at);
	set_lpc(at + 1);
	return w;
}

void Cpu::undefined(uint16_t op, uint32_t at)
{
	char msg[96];
	std::snprintf(msg, sizeof msg, "undefined opcode %04x before %06x", op, at);
	throw std::runtime_error(msg);
}

void Cpu::flags_nz(uint16_t v)
{
	r_[SR] &= ~(FLAG_N | FLAG_Z);
	if (v & 0x8000) r_[SR] |= FLAG_N;
	if (v == 0) r_[SR] |= FLAG_Z;
}

// a + b + carry_in. Subtraction is done by the caller passing ~b. S is the "true sign" of the result
// (N corrected for overflow), which is what the signed conditional jumps test.
uint16_t Cpu::add_with_flags(uint16_t a, uint16_t b, unsigned carry_in, bool flags)
{
	const uint32_t wide = uint32_t(a) + uint32_t(b) + carry_in;
	if (flags) {
		r_[SR] &= ~(FLAG_N | FLAG_Z | FLAG_S | FLAG_C);
		const unsigned carry = (wide >> 16) & 1;
		if (carry != (((a ^ b) >> 15) & 1u)) r_[SR] |= FLAG_S;
		if (wide & 0x8000) r_[SR] |= FLAG_N;
		if ((wide & 0xffff) == 0) r_[SR] |= FLAG_Z;
		if (carry) r_[SR] |= FLAG_C;
	}
	return uint16_t(wide);
}

// The sixteen ALU functions selected by the top nibble. Returns true when `out` must be written back.
bool Cpu::alu(unsigned fn, uint16_t a, uint16_t b, uint32_t addr, bool flags, uint16_t &out)
{
	const unsigned c = (r_[SR] & FLAG_C) ? 1 : 0;
	switch (fn) {
	case 0x0: out = add_with_flags(a, b, 0, flags); return true;                 // add
	case 0x1: out = add_with_flags(a, b, c, flags); return true;                 // adc
	case 0x2: out = add_with_flags(a, uint16_t(~b), 1, flags); return true;      // sub
	case 0x3: out = add_with_flags(a, uint16_t(~b), c, flags); return true;      // sbc
	case 0x4: add_with_flags(a, uint16_t(~b), 1, flags); return false;           // cmp
	case 0x6: out = uint16_t(0u - b); if (flags) flags_nz(out); return true;     // neg
	case 0x8: out = a ^ b; if (flags) flags_nz(out); return true;                // xor
	case 0x9: out = b; if (flags) flags_nz(out); return true;                    // load
	case 0xa: out = a | b; if (flags) flags_nz(out); return true;                // or
	case 0xb: out = a & b; if (flags) flags_nz(out); return true;                // and
	case 0xc: if (flags) flags_nz(a & b); return false;                          // test
	case 0xd: wr(addr, a); return false;                                         // store
	default:  return false;                                                      // 5, 7, e, f: no ALU function
	}
}

// ---------------------------------------------------------------------------------------------
// Main ALU group: fn ra, <operand>
// ---------------------------------------------------------------------------------------------

void Cpu::exec_general(uint16_t op)
{
	const unsigned fn = op >> 12, ra = (op >> 9) & 7, mode = (op >> 6) & 7, n = (op >> 3) & 7, rb = op & 7;

	if (mode == 2) {
		if (fn == 0xd) {                                   // push n registers ending at ra, downwards
			cycles_ += 4 + 2 * n;
			unsigned reg = ra;
			for (unsigned i = 0; i < n; i++) { push(r_[reg & 7], rb); reg--; }
			return;
		}
		if (fn == 0x9) {
			if (op == 0x9a98) {                            // reti
				cycles_ += 8;
				// FIQ: the fast interrupt does not stack FR (the firmware's handler saves it by hand),
				// so it must not be popped on the way out. MAME pops it whenever nesting is enabled.
				if (nest_ && !(in_fiq_ && !mame_fiq_compat)) set_fr(pop(SP));
				r_[SR] = pop(SP);
				r_[PC] = pop(SP);
				if (in_fiq_) in_fiq_ = false; else if (in_irq_) in_irq_ = false;
				return;
			}
			cycles_ += 4 + 2 * n;                          // pop n registers starting above ra
			unsigned reg = ra;
			for (unsigned i = 0; i < n; i++) { reg++; r_[reg & 7] = pop(rb); }
			return;
		}
		undefined(op, lpc());
	}

	uint16_t a = r_[ra], b = 0;
	uint32_t addr = 0;
	const bool store = (fn == 0xd);
	const bool to_pc = (ra == 7);

	switch (mode) {
	case 0:                                                // [bp + imm6]
		cycles_ += 6;
		addr = uint16_t(r_[BP] + (op & 0x3f));
		if (!store) b = rd(addr);
		break;
	case 1:                                                // imm6
		cycles_ += 2;
		b = op & 0x3f;
		break;
	case 3: {                                              // [rb], [rb--], [rb++], [++rb], optionally through DS
		cycles_ += to_pc ? 7 : 6;
		const bool far = n & 4;
		const unsigned form = n & 3;
		if (form == 3) {                                   // pre-increment
			r_[rb]++;
			if (far && r_[rb] == 0x0000) r_[SR] += 0x0400;
		}
		addr = far ? ds_addr(rb) : r_[rb];
		if (!store) b = rd(addr);
		if (form == 1) {                                   // post-decrement
			r_[rb]--;
			if (far && r_[rb] == 0xffff) r_[SR] -= 0x0400;
		} else if (form == 2) {                            // post-increment
			r_[rb]++;
			if (far && r_[rb] == 0x0000) r_[SR] += 0x0400;
		}
		break;
	}
	case 4:
		switch (n) {
		case 0:                                            // ra op= rb
			cycles_ += to_pc ? 5 : 3;
			b = r_[rb];
			break;
		case 1:                                            // ra = rb op imm16
			cycles_ += to_pc ? 5 : 4;
			a = r_[rb];
			b = fetch();
			break;
		case 2:                                            // ra = rb op [imm16]
			cycles_ += to_pc ? 8 : 7;
			a = r_[rb];
			addr = fetch();
			if (!store) b = rd(addr);
			break;
		case 3:                                            // [imm16] = rb op ra
			cycles_ += to_pc ? 8 : 7;
			b = a;
			a = r_[rb];
			addr = fetch();
			break;
		default: {                                         // ra op= rb asr (n - 3)
			cycles_ += to_pc ? 5 : 3;
			uint32_t v = (uint32_t(r_[rb]) << 4) | sb_;
			if (v & 0x80000) v |= 0xf00000;
			v >>= (n - 3);
			sb_ = v & 0xf;
			b = uint16_t(v >> 4);
			break;
		}
		}
		break;
	case 5:                                                // lsl 1..4 / lsr 1..4, shifted-out bits kept in SB
		cycles_ += to_pc ? 5 : 3;
		if (n & 4) {
			const uint32_t v = ((uint32_t(r_[rb]) << 4) | sb_) >> (n - 3);
			sb_ = v & 0xf;
			b = uint16_t(v >> 4);
		} else {
			const uint32_t v = ((uint32_t(sb_) << 16) | r_[rb]) << (n + 1);
			sb_ = (v >> 16) & 0xf;
			b = uint16_t(v);
		}
		break;
	case 6: {                                              // rol / ror through SB
		cycles_ += to_pc ? 5 : 3;
		uint32_t v = ((((uint32_t(sb_) << 16) | r_[rb]) << 4) | sb_);
		if (n & 4) { v >>= (n - 3); sb_ = v & 0xf; }
		else       { v <<= (n + 1); sb_ = (v >> 20) & 0xf; }
		b = uint16_t(v >> 4);
		break;
	}
	case 7:                                                // [imm6]
		cycles_ += to_pc ? 6 : 5;
		addr = op & 0x3f;
		b = rd(addr);
		break;
	}

	uint16_t out = 0;
	if (alu(fn, a, b, addr, !to_pc, out)) {
		if (mode == 4 && n == 3) wr(addr, out);
		else r_[ra] = out;
	}
}

// ---------------------------------------------------------------------------------------------
// Conditional and unconditional short jumps
// ---------------------------------------------------------------------------------------------

void Cpu::exec_jump(uint16_t op)
{
	const uint16_t f = r_[SR];
	const bool c = f & FLAG_C, s = f & FLAG_S, z = f & FLAG_Z, nflag = f & FLAG_N;
	bool take;
	switch (op >> 12) {
	case 0x0: take = !c; break;              // jb  (unsigned <)
	case 0x1: take = c; break;               // jae
	case 0x2: take = !s; break;              // jge
	case 0x3: take = s; break;               // jl
	case 0x4: take = !z; break;              // jne
	case 0x5: take = z; break;               // je
	case 0x6: take = !nflag; break;          // jpl
	case 0x7: take = nflag; break;           // jmi
	case 0x8: take = !(c && !z); break;      // jbe
	case 0x9: take = c && !z; break;         // ja
	case 0xa: take = z || s; break;          // jle
	case 0xb: take = !(z || s); break;       // jg
	case 0xc: take = (nflag == s); break;    // jvc
	case 0xd: take = (nflag != s); break;    // jvs
	default:  take = true; break;            // jmp
	}
	if (!take) { cycles_ += 2; return; }
	cycles_ += 4;
	const uint32_t dist = op & 0x3f;
	set_lpc(((op >> 6) & 7) == 0 ? lpc() + dist : lpc() - dist);
}

// ---------------------------------------------------------------------------------------------
// 0xExxx: bit operations, unsigned multiply, shifts by a register amount
// ---------------------------------------------------------------------------------------------

void Cpu::bit_op(unsigned which, unsigned bit, uint16_t &value)
{
	r_[SR] &= ~FLAG_Z;
	if (!((value >> bit) & 1)) r_[SR] |= FLAG_Z;
	switch (which) {
	case 1: value |= uint16_t(1u << bit); break;       // setb
	case 2: value &= uint16_t(~(1u << bit)); break;    // clrb
	case 3: value ^= uint16_t(1u << bit); break;       // invb
	default: break;                                    // tstb
	}
}

void Cpu::mem_bit_op(unsigned which, unsigned bit, uint32_t addr)
{
	uint16_t v = rd(addr);
	bit_op(which, bit, v);
	if (which != 0) wr(addr, v);
}

void Cpu::exec_e(uint16_t op)
{
	const unsigned ra = (op >> 9) & 7, rb = op & 7, which = (op >> 4) & 3;

	if ((op & 0xf1c8) == 0xe000) { bit_op(which, r_[rb] & 0xf, r_[ra]); return; }        // bit of ra, index in rb
	if ((op & 0xf1c0) == 0xe040) { bit_op(which, op & 0xf, r_[ra]); return; }            // bit of ra, constant index
	if ((op & 0xf1c0) == 0xe180) { mem_bit_op(which, op & 0xf, r_[ra]); return; }        // bit of [ra]
	if ((op & 0xf1c0) == 0xe1c0) { mem_bit_op(which, op & 0xf, ds_addr(ra)); return; }   // bit of ds:[ra]
	if ((op & 0xf1c8) == 0xe100) { mem_bit_op(which, r_[rb] & 0xf, r_[ra]); return; }
	if ((op & 0xf1c8) == 0xe140) { mem_bit_op(which, r_[rb] & 0xf, ds_addr(ra)); return; }

	if ((op & 0xf1f8) == 0xe008) {                                                         // mr = ra * rb, unsigned
		cycles_ += 12;
		const uint32_t p = uint32_t(r_[ra]) * uint32_t(r_[rb]);
		r_[R4] = uint16_t(p >> 16); r_[R3] = uint16_t(p);
		return;
	}
	if ((op & 0xf188) == 0xe108) {                                                         // ra shifted by rb
		const unsigned amount = r_[rb] & 0x1f;
		const uint16_t v = r_[ra];
		switch ((op >> 4) & 7) {
		case 0: r_[ra] = uint16_t(int32_t(int16_t(v)) >> amount); return;                  // asr
		case 1: { const uint32_t x = uint32_t(int32_t(uint32_t(v) << 16) >> amount);       // asror into mr
			r_[R3] |= uint16_t(x); r_[R4] = uint16_t(x >> 16); return; }
		case 2: r_[ra] = uint16_t(uint32_t(v) << amount); return;                          // lsl
		case 3: { const uint32_t x = uint32_t(v) << amount;                                // lslor into mr
			r_[R3] = uint16_t(x); r_[R4] |= uint16_t(x >> 16); return; }
		case 4: r_[ra] = uint16_t(uint32_t(v) >> amount); return;                          // lsr
		case 5: { const uint32_t x = (uint32_t(v) << 16) >> amount;                        // lsror into mr
			r_[R3] |= uint16_t(x); r_[R4] = uint16_t(x >> 16); return; }
		default: break;                                                                    // rol / ror: not seen yet
		}
	}
	undefined(op, lpc());
}

// ---------------------------------------------------------------------------------------------
// 0xFxxx: far call/jump, multiply, segment and flag register access, interrupt control, extended prefix
// ---------------------------------------------------------------------------------------------

void Cpu::sum_of_products(int rd_reg, int rs_reg, int count)
{
	const uint16_t pa = r_[rd_reg], pb = r_[rs_reg];
	uint16_t taps[16];
	int64_t total = 0;
	for (int i = 0; i < count; i++) {
		taps[i] = rd(uint32_t(pa) + i);
		const uint16_t other = rd(uint32_t(pb) + i);
		total += int64_t(int32_t(int16_t(taps[i])) * int32_t(int16_t(other)));
	}
	sb_ = 0;
	if (fir_move_)
		for (int i = count - 1; i > 0; i--) wr(uint32_t(pa) + i, taps[i - 1]);
	r_[rd_reg] = uint16_t(r_[rd_reg] + count);
	r_[rs_reg] = uint16_t(r_[rs_reg] + count);
	r_[R4] = uint16_t(uint64_t(total) >> 16);
	r_[R3] = uint16_t(uint64_t(total));
}

// One step of a non-restoring 32/16 division: sixteen consecutive DIVQ instructions leave the quotient in R3.
void Cpu::divq_step()
{
	if (divq_left_ == 0) {
		divq_left_ = 16;
		divq_dividend_ = (uint32_t(r_[R4]) << 16) | r_[R3];
		divq_divisor_ = r_[R2];
		divq_acc_ = 0;
	}
	aq_ = (divq_acc_ >> 31) & 1;
	const uint32_t in_bit = (divq_dividend_ >> 15) & 1;
	if (aq_) divq_acc_ += divq_acc_ + in_bit + divq_divisor_;
	else     divq_acc_ += divq_acc_ + in_bit - divq_divisor_;
	divq_dividend_ <<= 1;
	divq_dividend_++;
	divq_dividend_ ^= (divq_acc_ >> 31) & 1;
	r_[R3] = uint16_t(divq_dividend_);
	divq_left_--;
}

void Cpu::exec_misc(uint16_t op)
{
	cycles_ += 1;
	const unsigned k = op & 0x3f;
	if (k & 0x20) {
		switch (k & 7) {
		case 1: {                                          // call mr
			const uint32_t target = r_[R3] | (uint32_t(r_[R4] & 0x3f) << 16);
			push(r_[PC], SP); push(r_[SR], SP);
			set_lpc(target);
			return;
		}
		case 3: divq_step(); return;
		case 4: {                                          // exp: count of redundant sign bits of r4
			const uint16_t v = r_[R4];
			const uint16_t x = (v & 0x8000) ? uint16_t(~v) : v;
			int lead = 0;
			for (int bit = 15; bit >= 0 && !((x >> bit) & 1); bit--) lead++;
			r_[R2] = uint16_t(lead - 1);
			return;
		}
		case 5: cycles_ += 2; return;                      // nop
		default: undefined(op, lpc());                     // break, divs and the unassigned slots
		}
	}
	switch (k) {
	case 0x00: cycles_ += 2; irq_en_ = false; fiq_en_ = false; return;   // int off
	case 0x01: cycles_ += 2; irq_en_ = true;  fiq_en_ = false; return;   // int irq
	case 0x02: cycles_ += 2; irq_en_ = false; fiq_en_ = true;  return;   // int fiq
	case 0x03: cycles_ += 2; irq_en_ = true;  fiq_en_ = true;  return;   // int fiq,irq
	case 0x04: fir_move_ = true; return;
	case 0x05: fir_move_ = false; return;
	case 0x06: fraction_ = false; return;
	case 0x07: fraction_ = true; return;
	case 0x08: cycles_ += 2; irq_en_ = false; return;
	case 0x09: cycles_ += 2; irq_en_ = true; return;
	case 0x0a: if (bank_) swap_bank(); bank_ = false; return;            // secbank off
	case 0x0b: if (!bank_) swap_bank(); bank_ = true; return;            // secbank on
	case 0x0c: cycles_ += 2; fiq_en_ = false; return;
	case 0x0d: nest_ = false; return;                                    // irqnest off
	case 0x0e: cycles_ += 2; fiq_en_ = true; return;
	case 0x0f: nest_ = true; return;                                     // irqnest on
	default: undefined(op, lpc());
	}
}

void Cpu::exec_f(uint16_t op)
{
	const unsigned ra = (op >> 9) & 7, rb = op & 7, n = (op >> 3) & 7;
	switch ((op >> 6) & 7) {
	case 0:
		if ((op & 0xffc0) == 0xfe00) { r_[SR] = (r_[SR] & 0x03ff) | uint16_t((op & 0x3f) << 10); return; }   // ds = imm6
		switch (op & 0xf1f8) {
		case 0xf020: r_[rb] = get_ds(); return;
		case 0xf028: r_[SR] = (r_[SR] & 0x03ff) | uint16_t((r_[rb] & 0x3f) << 10); return;
		case 0xf030: r_[rb] = get_fr(); return;
		case 0xf038: set_fr(r_[rb]); return;
		default: break;
		}
		{                                                  // mr = ra * rb, rb signed, ra unsigned
			cycles_ += 12;
			uint32_t p = uint32_t(r_[ra]) * uint32_t(r_[rb]);
			if (r_[rb] & 0x8000) p -= uint32_t(r_[ra]) << 16;
			r_[R4] = uint16_t(p >> 16); r_[R3] = uint16_t(p);
		}
		return;
	case 1:
		if ((op & 0xf3c0) == 0xf040) {                     // call a22
			cycles_ += 9;
			const uint16_t low = fetch();
			push(r_[PC], SP); push(r_[SR], SP);
			r_[PC] = low;
			r_[SR] = (r_[SR] & 0xffc0) | (op & 0x3f);
			return;
		}
		if ((op & 0xf3c0) == 0xf240) {                     // bit operation on [imm16] or ds:[imm16]
			const uint16_t low = fetch();
			const uint32_t addr = (op & 0x0400) ? ((uint32_t(get_ds()) << 16) | low) : low;
			mem_bit_op((op >> 4) & 3, op & 0xf, addr);
			return;
		}
		break;
	case 2:
		if ((op & 0xffc0) == 0xfe80) {                     // goto a22
			cycles_ += 5;
			const uint16_t low = rd(lpc());
			r_[PC] = low;
			r_[SR] = (r_[SR] & 0xffc0) | (op & 0x3f);
			return;
		}
		break;
	case 3:
		if ((op & 0xffc0) == 0xfec0) {                     // goto mr
			cycles_ += 5;
			r_[PC] = r_[R3];
			r_[SR] = (r_[SR] & 0xffc0) | (r_[R4] & 0x3f);
			return;
		}
		break;
	case 4:
		if ((op & 0xf1f8) == 0xf108) {                     // mr = ra * rb, both signed
			cycles_ += 12;
			const int32_t p = int32_t(int16_t(r_[ra])) * int32_t(int16_t(r_[rb]));
			r_[R4] = uint16_t(uint32_t(p) >> 16); r_[R3] = uint16_t(uint32_t(p));
			return;
		}
		break;
	case 5:
		exec_misc(op);
		return;
	case 6:
		if (op == 0xff80) { exec_extended(); return; }
		sum_of_products(ra, rb, n ? n : 16);
		return;
	case 7:
		sum_of_products(ra, rb, n + 8);
		return;
	}
	undefined(op, lpc());
}

// ---------------------------------------------------------------------------------------------
// 0xFF80 prefix: the 2.0 instruction forms that can address r8..r15
// ---------------------------------------------------------------------------------------------

void Cpu::exec_extended()
{
	const uint16_t x = fetch();
	const unsigned fn = x >> 12;
	const unsigned form = (x >> 4) & 0x1f;
	const unsigned wide_a = ((x >> 9) & 7) | ((x >> 5) & 8);   // 4-bit register number: bit 8 selects r8..r15
	const unsigned wide_b = x & 0xf;
	const unsigned hi_a = ((x >> 9) & 7) + 8;                  // forms that only reach r8..r15
	uint16_t out = 0;

	switch (form) {
	case 0x00: case 0x10:                                      // ra op= rb
		if (alu(fn, r_[wide_a], r_[wide_b], r_[wide_a], wide_a != 7, out)) r_[wide_a] = out;
		return;
	case 0x02: {                                               // push / pop of r8..r15
		unsigned count = (x >> 12) & 7; if (count == 0) count = 8;
		unsigned reg = (x >> 9) & 7;
		if (x & 0x8000) { for (unsigned i = 0; i < count; i++) { push(r_[(reg & 7) + 8], wide_b); reg--; } }
		else            { for (unsigned i = 0; i < count; i++) { reg++; r_[(reg & 7) + 8] = pop(wide_b); } }
		return;
	}
	case 0x04: case 0x14: {                                    // ra = rb op imm16
		const uint16_t imm = fetch();
		if (fn == 0xd) undefined(x, lpc());
		if (alu(fn, r_[wide_b], imm, 0, wide_a != 7, out)) r_[wide_a] = out;
		return;
	}
	case 0x06: case 0x16: {                                    // ra = rb op [imm16]
		const uint16_t addr = fetch();
		const uint16_t m = rd(addr);
		if (alu(fn, r_[wide_b], m, addr, wide_a != 7, out)) r_[wide_a] = out;
		return;
	}
	case 0x07: case 0x17: {                                    // [imm16] = ra op rb
		const uint16_t addr = fetch();
		if (alu(fn, r_[wide_a], r_[wide_b], addr, true, out)) wr(addr, out);
		return;
	}
	case 0x08: case 0x09: case 0x0a: case 0x0b: {              // rx op= [ry], with the usual four pointer forms
		const unsigned ry = (x & 7) + 8;
		const bool far = (x >> 5) & 1;
		const unsigned ptr_form = (x >> 3) & 3;
		const uint16_t a = r_[hi_a];
		uint16_t b = 0;
		if (ptr_form == 3) { r_[ry]++; if (far && r_[ry] == 0x0000) r_[SR] += 0x0400; }
		const uint32_t addr = far ? ds_addr(ry) : r_[ry];
		if (fn != 0xd) b = rd(addr);
		if (ptr_form == 1) { r_[ry]--; if (far && r_[ry] == 0xffff) r_[SR] -= 0x0400; }
		else if (ptr_form == 2) { r_[ry]++; if (far && r_[ry] == 0x0000) r_[SR] += 0x0400; }
		if (alu(fn, a, b, addr, true, out)) r_[hi_a] = out;
		return;
	}
	case 0x0c: case 0x0d: case 0x0e: case 0x0f: {              // rx op= [bp + imm6]
		const uint32_t addr = uint16_t(r_[BP] + (x & 0x3f));
		const uint16_t m = rd(addr);
		if (alu(fn, r_[hi_a], m, addr, true, out)) r_[hi_a] = out;
		return;
	}
	case 0x18: case 0x19: case 0x1a: case 0x1b:                // rx op= imm6
		if (fn == 0xd) undefined(x, lpc());
		if (alu(fn, r_[hi_a], x & 0x3f, 0, true, out)) r_[hi_a] = out;
		return;
	case 0x1c: case 0x1d: case 0x1e: case 0x1f: {              // rx op= [imm6]
		const uint32_t addr = x & 0x3f;
		const uint16_t m = rd(addr);
		if (alu(fn, r_[hi_a], m, addr, true, out)) r_[hi_a] = out;
		return;
	}
	default:
		undefined(x, lpc());
	}
}

// ---------------------------------------------------------------------------------------------
// Interrupts and the step function
// ---------------------------------------------------------------------------------------------

void Cpu::set_line(int line, int asserted)
{
	if (line < LINE_FIQ || line > LINE_IRQ7) return;
	if (asserted) lines_ |= uint16_t(1u << line);
	else          lines_ &= uint16_t(~(1u << line));
}

// Only the highest-priority asserted line is considered; if it cannot be taken, nothing is.
void Cpu::enter_interrupts()
{
	if (!lines_) return;
	int line = 0;
	while (!((lines_ >> line) & 1)) line++;

	if (line == LINE_FIQ) {
		if (!fiq_en_ || in_fiq_) return;
		in_fiq_ = true; took_interrupt_ = true;
		push(r_[PC], SP); push(r_[SR], SP);
		r_[PC] = rd(uint32_t(vector_base_) + 6);
		r_[SR] = 0;
		return;
	}
	const unsigned n = unsigned(line - LINE_IRQ0);
	// FIQ: nothing pre-empts the fast interrupt. MAME lets an ordinary interrupt in here.
	if (in_fiq_ && !mame_fiq_compat) return;
	if ((!nest_ && in_irq_) || pri_ <= n || !irq_en_) return;
	in_irq_ = true; took_interrupt_ = true;
	push(r_[PC], SP); push(r_[SR], SP);
	if (nest_) { push(get_fr(), SP); pri_ = uint8_t(n); }
	r_[PC] = rd(uint32_t(vector_base_) + 8 + n);
	r_[SR] = 0;
}

int Cpu::step()
{
	took_interrupt_ = false;
	cycles_ = 0;
	const uint16_t op = fetch();
	const unsigned fn = op >> 12, ra = (op >> 9) & 7, mode = (op >> 6) & 7;

	if (fn == 0xf) exec_f(op);
	else if (ra == 7 && mode < 2) exec_jump(op);
	else if (fn == 0xe) exec_e(op);
	else exec_general(op);

	if (op != 0x9a98) enter_interrupts();      // an interrupt is never taken straight after reti
	return cycles_ > 0 ? cycles_ : 1;
}

int Cpu::insn_length(uint16_t op, uint16_t next)
{
	const unsigned fn = op >> 12, ra = (op >> 9) & 7, mode = (op >> 6) & 7, n = (op >> 3) & 7;
	if (fn == 0xf) {
		if (mode == 1 && ((op & 0xf3c0) == 0xf040 || (op & 0xf3c0) == 0xf240)) return 2;
		if (mode == 2 && (op & 0xffc0) == 0xfe80) return 2;
		if (op == 0xff80) {
			const unsigned form = (next >> 4) & 0x1f;
			return (form == 0x04 || form == 0x14 || form == 0x06 || form == 0x16 || form == 0x07 || form == 0x17) ? 3 : 2;
		}
		return 1;
	}
	if (ra == 7 && mode < 2) return 1;
	if (fn == 0xe) return 1;
	return (mode == 4 && n >= 1 && n <= 3) ? 2 : 1;
}

}  // namespace tsmart
