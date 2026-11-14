// Framework-free replacements for the parts of MAME's unsp.cpp that the instruction files rely on:
// reset, flags, stack, FR/DS access, interrupt entry and the single-step loop.
// Adapted from MAME's src/devices/cpu/unsp/unsp.cpp (GPL-2.0+, Segher Boessenkool, Ryan Holtz, David Haywood).
#include "emu.h"
#include "unsp.h"
bool g_core_log = false;
void unsp_device::unimplemented_opcode(uint16_t op) { fatalerror("UNSP: unknown opcode %04x at %06x", op, UNSP_LPC); }
void unsp_device::unimplemented_opcode(uint16_t op, uint16_t ximm) { fatalerror("UNSP: unknown opcode %04x %04x at %06x", op, ximm, UNSP_LPC); }
void unsp_device::unimplemented_opcode(uint16_t op, uint16_t ximm, uint16_t x2) { fatalerror("UNSP: unknown opcode %04x %04x %04x at %06x", op, ximm, x2, UNSP_LPC); }
void unsp_device::reset(uint16_t vectorbase)
{
	m_vectorbase = vectorbase;
	for (int i = 0; i < 16; i++) m_core->m_r[i] = (i < m_numregs) ? 0 : 0xdeadbeef;
	for (auto &s : m_core->m_secbank) s = 0;
	m_core->m_r[REG_PC] = read16(m_vectorbase + 0x7);
	m_core->m_enable_irq = 0; m_core->m_enable_fiq = 0; m_core->m_fir_move = 1;
	m_core->m_sb = 0; m_core->m_aq = 0; m_core->m_fra = 0; m_core->m_bnk = 0; m_core->m_ine = 0; m_core->m_pri = 8;
	m_core->m_fiq = 0; m_core->m_irq = 0; m_core->m_sirq = 0; m_core->m_divq_bit = UINT_MAX;
	m_core->m_divq_dividend = m_core->m_divq_divisor = m_core->m_divq_a = 0;
}
void unsp_device::update_nzsc(uint32_t value, uint16_t r0, uint16_t r1)
{
	m_core->m_r[REG_SR] &= ~(UNSP_N | UNSP_Z | UNSP_S | UNSP_C);
	if (BIT(value, 16) != BIT((uint32_t)(r0 ^ r1), 15)) m_core->m_r[REG_SR] |= UNSP_S;
	if (BIT(value, 15)) m_core->m_r[REG_SR] |= UNSP_N;
	if ((uint16_t)value == 0) m_core->m_r[REG_SR] |= UNSP_Z;
	if (BIT(value, 16)) m_core->m_r[REG_SR] |= UNSP_C;
}
void unsp_device::update_nz(uint32_t value)
{
	m_core->m_r[REG_SR] &= ~(UNSP_N | UNSP_Z);
	if (value & 0x8000) m_core->m_r[REG_SR] |= UNSP_N;
	if ((uint16_t)value == 0) m_core->m_r[REG_SR] |= UNSP_Z;
}
void unsp_device::push(uint32_t value, uint32_t *reg) { write16(*reg, (uint16_t)value); *reg = (uint16_t)(*reg - 1); }
uint16_t unsp_device::pop(uint32_t *reg) { *reg = (uint16_t)(*reg + 1); return (uint16_t)read16(*reg); }
void unsp_device::trigger_fiq()
{
	if (!m_core->m_enable_fiq || m_core->m_fiq) return;
	m_core->m_fiq = 1; m_irq_taken = true;
	push(m_core->m_r[REG_PC], &m_core->m_r[REG_SP]);
	push(m_core->m_r[REG_SR], &m_core->m_r[REG_SP]);
	m_core->m_r[REG_PC] = read16(m_vectorbase + 0x06);
	m_core->m_r[REG_SR] = 0;
}
void unsp_device::trigger_irq(int line)
{
	if ((m_core->m_ine == 0 && m_core->m_irq == 1) || m_core->m_pri <= (uint32_t)line || !m_core->m_enable_irq) return;
	m_core->m_irq = 1; m_irq_taken = true;
	push(m_core->m_r[REG_PC], &m_core->m_r[REG_SP]);
	push(m_core->m_r[REG_SR], &m_core->m_r[REG_SP]);
	if (m_core->m_ine) { push(get_fr(), &m_core->m_r[REG_SP]); m_core->m_pri = line; }
	m_core->m_r[REG_PC] = read16(m_vectorbase + 0x08 + line);
	m_core->m_r[REG_SR] = 0;
}
void unsp_device::check_irqs()
{
	if (!m_core->m_sirq) return;
	int highest = -1;
	for (int i = 0; i <= 8; i++) if (BIT(m_core->m_sirq, i)) { highest = i; break; }
	if (highest == UNSP_FIQ_LINE) trigger_fiq(); else trigger_irq(highest - 1);
}
void unsp_device::execute_one(const uint16_t op)
{
	const uint16_t op0 = (op >> 12) & 15, opa = (op >> 9) & 7, op1 = (op >> 6) & 7;
	if (op0 == 0xf) return execute_fxxx_group(op);
	if (op0 < 0xf && opa == 0x7 && op1 < 2) return execute_jumps(op);
	if (op0 == 0xe) return execute_exxx_group(op);
	execute_remaining(op);
}
int unsp_device::step()
{
	m_irq_taken = false;
	m_core->m_icount = 0;
	const uint16_t op = read16(UNSP_LPC);
	add_lpc(1);
	execute_one(op);
	if (op != 0x9a98) check_irqs();
	int used = -m_core->m_icount;
	return used > 0 ? used : 1;
}
void unsp_device::set_line(int line, int state)
{
	m_core->m_sirq &= ~(1 << line);
	if (state && line <= UNSP_IRQ7_LINE) m_core->m_sirq |= (1 << line);
}
uint8_t unsp_device::get_csb() { return 1 << ((UNSP_LPC >> 20) & 3); }
uint16_t unsp_device::get_ds() { return (m_core->m_r[REG_SR] >> 10) & 0x3f; }
void unsp_device::set_ds(uint16_t ds) { m_core->m_r[REG_SR] &= 0x03ff; m_core->m_r[REG_SR] |= (ds & 0x3f) << 10; }
void unsp_device::set_fr(uint16_t fr)
{
	m_core->m_aq = BIT(fr, 14);
	const uint32_t old_bank = m_core->m_bnk;
	m_core->m_bnk = BIT(fr, 13);
	if (m_core->m_bnk != old_bank)
		for (int i = 0; i < 4; i++) std::swap(m_core->m_r[REG_R1 + i], m_core->m_secbank[i]);
	m_core->m_fra = BIT(fr, 12); m_core->m_fir_move = BIT(fr, 11); m_core->m_sb = (fr >> 7) & 0xf;
	m_core->m_enable_fiq = BIT(fr, 6); m_core->m_enable_irq = BIT(fr, 5); m_core->m_ine = BIT(fr, 4); m_core->m_pri = fr & 0xf;
}
uint16_t unsp_device::get_fr()
{
	return (m_core->m_aq << 14) | (m_core->m_bnk << 13) | (m_core->m_fra << 12) | (m_core->m_fir_move << 11) | (m_core->m_sb << 7)
	     | (m_core->m_enable_fiq << 6) | (m_core->m_enable_irq << 5) | (m_core->m_ine << 4) | m_core->m_pri;
}
