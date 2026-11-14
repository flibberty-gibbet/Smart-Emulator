// license:GPL-2.0+
// copyright-holders:Segher Boessenkool, Ryan Holtz, David Haywood
// Modified for Smart-Emulator, 2026. Full license: LICENSES/GPL-2.0.txt
// Stand-in for MAME's cpu/unsp/unsp.h: same class shape and member names, without the MAME
// device framework or the recompiler, so unsp_{other,jumps,fxxx,exxx,extended}.cpp compile unchanged.
// Adapted from MAME (GPL-2.0+, Segher Boessenkool, Ryan Holtz, David Haywood).
#pragma once
#include "emu.h"
#include "unspdefs.h"
enum { UNSP_FIQ_LINE = 0, UNSP_IRQ0_LINE, UNSP_IRQ1_LINE, UNSP_IRQ2_LINE, UNSP_IRQ3_LINE, UNSP_IRQ4_LINE,
       UNSP_IRQ5_LINE, UNSP_IRQ6_LINE, UNSP_IRQ7_LINE, UNSP_BRK_LINE, UNSP_NUM_LINES };
struct unsp_bus {
	virtual ~unsp_bus() = default;
	virtual uint16_t read16(uint32_t addr) = 0;
	virtual void write16(uint32_t addr, uint16_t data) = 0;
};
class unsp_device {
public:
	unsp_device(unsp_bus &bus, int iso, int numregs) : m_core(&m_local_core), m_iso(iso), m_bus(bus), m_numregs(numregs) { }
	virtual ~unsp_device() = default;
	void reset(uint16_t vectorbase = 0xfff0);
	int  step();                                   // executes one instruction, returns cycles used
	void set_line(int line, int state);
	bool irq_taken_last_step() const { return m_irq_taken; }
	uint8_t get_csb();
	void set_ds(uint16_t ds);
	uint16_t get_ds();
	void set_fr(uint16_t fr);
	uint16_t get_fr();
	uint32_t lpc() const { return ((m_local_core.m_r[6] & 0x3f) << 16) | m_local_core.m_r[7]; }
	uint32_t reg(int i) const { return m_local_core.m_r[i]; }
	void set_reg(int i, uint32_t v) { m_local_core.m_r[i] = v; }
protected:
	enum : uint32_t { REG_SP = 0, REG_R1, REG_R2, REG_R3, REG_R4, REG_BP, REG_SR, REG_PC, REG_SR1 = 0, REG_SR2, REG_SR3, REG_SR4 };
	struct internal_unsp_state {
		uint32_t m_r[16]; uint32_t m_secbank[4];
		uint32_t m_enable_irq, m_enable_fiq, m_fir_move, m_fiq, m_irq, m_sirq, m_sb, m_aq, m_fra, m_bnk, m_ine, m_pri;
		uint32_t m_divq_bit, m_divq_dividend, m_divq_divisor, m_divq_a;
		uint32_t m_arg0, m_arg1, m_jmpdest;
		int m_icount;
	};
	uint16_t read16(uint32_t address) { return m_bus.read16(address & 0x3fffff); }
	void write16(uint32_t address, uint16_t data) { m_bus.write16(address & 0x3fffff, data); }
	void add_lpc(const int32_t offset) {
		const uint32_t new_lpc = UNSP_LPC + offset;
		m_core->m_r[REG_PC] = (uint16_t)new_lpc;
		m_core->m_r[REG_SR] &= 0xffc0;
		m_core->m_r[REG_SR] |= (new_lpc >> 16) & 0x3f;
	}
	void execute_fxxx_000_group(uint16_t op);
	void execute_fxxx_001_group(uint16_t op);
	void execute_fxxx_010_group(uint16_t op);
	void execute_fxxx_011_group(uint16_t op);
	virtual void execute_fxxx_101_group(uint16_t op);
	void execute_fxxx_110_group(uint16_t op);
	void execute_fxxx_111_group(uint16_t op);
	void execute_fxxx_group(uint16_t op);
	void execute_fxxx_100_group(uint16_t op);
	virtual void execute_extended_group(uint16_t op);
	virtual void execute_exxx_group(uint16_t op);
	void execute_muls_ss(const uint16_t rd, const uint16_t rs, const uint16_t size);
	void unimplemented_opcode(uint16_t op);
	void unimplemented_opcode(uint16_t op, uint16_t ximm);
	void unimplemented_opcode(uint16_t op, uint16_t ximm, uint16_t ximm_2);
	virtual bool op_is_divq(const uint16_t op) { return false; }
	internal_unsp_state *m_core;
	int m_iso;
	void push(uint32_t value, uint32_t *reg);
	uint16_t pop(uint32_t *reg);
	void update_nz(uint32_t value);
	void update_nzsc(uint32_t value, uint16_t r0, uint16_t r1);
	bool do_basic_alu_ops(const uint16_t &op0, uint32_t &lres, uint16_t &r0, uint16_t &r1, uint32_t &r2, bool update_flags);
private:
	void execute_jumps(const uint16_t op);
	void execute_remaining(const uint16_t op);
	void execute_one(const uint16_t op);
	void trigger_fiq();
	void trigger_irq(int line);
	void check_irqs();
	internal_unsp_state m_local_core;
	unsp_bus &m_bus;
	uint16_t m_vectorbase = 0xfff0;
	bool m_irq_taken = false;
protected:
	int m_numregs;
};
class unsp_12_device : public unsp_device {
public:
	unsp_12_device(unsp_bus &bus, int iso = 12, int numregs = 8) : unsp_device(bus, iso, numregs) { }
protected:
	virtual void execute_fxxx_101_group(uint16_t op) override;
	virtual void execute_exxx_group(uint16_t op) override;
	void execute_divq(uint16_t op);
	bool op_is_divq(const uint16_t op) override;
};
class unsp_20_device : public unsp_12_device {
public:
	unsp_20_device(unsp_bus &bus) : unsp_12_device(bus, 20, 16) { }
protected:
	virtual void execute_extended_group(uint16_t op) override;
};
