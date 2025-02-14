#include "peanut_gb.h"

struct gb_s *peanut_exec_gb = NULL;

#include "cpu_access.h"

static uint8_t __get_f(struct cpu_registers_s *regs)
{
	return (GET_REGF_Z() << 7)
		| (GET_REGF_N() << 6)
		| (GET_REGF_H() << 5)
		| (GET_REGF_C() << 4);
}

static void __set_f(struct cpu_registers_s *regs, uint8_t v)
{
	SET_REGF_Z((v >> 7) & 1);
	SET_REGF_N((v >> 6) & 1);
	SET_REGF_H((v >> 5) & 1);
	SET_REGF_C((v >> 4) & 1);
}

static void __gb_add16(struct cpu_registers_s *regs, uint16_t value)
{
	uint_fast32_t temp = $HL + value;
	SET_REGF_N(0)
	SET_REGF_H((temp ^ $HL ^ value) & 0x1000 ? 1 : 0)
	SET_REGF_C((temp & 0xFFFF0000) ? 1 : 0)
	$HL = (temp & 0x0000FFFF);
}

static void __gb_add8(struct cpu_registers_s *regs, int carry, uint8_t value)
{
	uint8_t c = carry ? GET_REGF_C() : 0;
	uint16_t temp = $A + value + c;
	SET_REGF_Z(((temp & 0xFF) == 0x00))
	SET_REGF_N(0)
	SET_REGF_H(($A ^ value ^ temp) & 0x10 ? 1 : 0)
	SET_REGF_C((temp & 0xFF00) ? 1 : 0)
	$A = (temp & 0xFF);
}

static inline uint8_t __gb_cmp8(struct cpu_registers_s *regs, int carry, uint8_t value)
{
	uint8_t c = carry ? GET_REGF_C() : 0;
	uint16_t temp = $A - value - c;
	SET_REGF_Z(((temp & 0xFF) == 0x00))
	SET_REGF_N(1)
	SET_REGF_H(($A ^ value ^ temp) & 0x10 ? 1 : 0)
	SET_REGF_C((temp & 0xFF00) ? 1 : 0)
	return (temp & 0xFF);
}

static void __gb_sub8(struct cpu_registers_s *regs, int carry, uint8_t value)
{
	$A = __gb_cmp8(regs, carry, value);
}

static uint8_t __gb_cpu_read(uint16_t addr)
{
	return __gb_read(peanut_exec_gb, addr);
}

static void __gb_cpu_write(uint16_t addr, uint8_t value)
{
	__gb_write(peanut_exec_gb, addr, value);
}

static uint8_t __gb_execute_cb(struct cpu_registers_s *regs)
{
	uint8_t inst_cycles;
	uint8_t cbop = __gb_cpu_read($PC++);
	uint8_t r = (cbop & 0x7);
	uint8_t b = (cbop >> 3) & 0x7;
	uint8_t d = (cbop >> 3) & 0x1;
	uint8_t val;
	uint8_t writeback = 1;

	inst_cycles = 8;
	/* Add an additional 8 cycles to these sets of instructions. */
	switch(cbop & 0xC7)
	{
	case 0x06:
	case 0x86:
    	case 0xC6:
		inst_cycles += 8;
    	break;
    	case 0x46:
		inst_cycles += 4;
    	break;
	}

	switch(r)
	{
	case 0:
		val = GET_REG_B();
		break;

	case 1:
		val = GET_REG_C();
		break;

	case 2:
		val = GET_REG_D();
		break;

	case 3:
		val = GET_REG_E();
		break;

	case 4:
		val = GET_REG_H();
		break;

	case 5:
		val = GET_REG_L();
		break;

	case 6:
		val = __gb_cpu_read($HL);
		break;

	/* Only values 0-7 are possible here, so we make the final case
	 * default to satisfy -Wmaybe-uninitialized warning. */
	default:
		val = $A;
		break;
	}

	/* TODO: Find out WTF this is doing. */
	switch(cbop >> 6)
	{
	case 0x0:
		cbop = (cbop >> 4) & 0x3;

		switch(cbop)
		{
		case 0x0: /* RdC R */
		case 0x1: /* Rd R */
			if(d) /* RRC R / RR R */
			{
				uint8_t temp = val;
				val = (val >> 1);
				val |= cbop ? (GET_REGF_C() << 7) : (temp << 7);
				SET_REGF_Z((val == 0x00))
				SET_REGF_N(0)
				SET_REGF_H(0)
				SET_REGF_C((temp & 0x01))
			}
			else /* RLC R / RL R */
			{
				uint8_t temp = val;
				val = (val << 1);
				val |= cbop ? GET_REGF_C() : (temp >> 7);
				SET_REGF_Z((val == 0x00))
				SET_REGF_N(0)
				SET_REGF_H(0)
				SET_REGF_C((temp >> 7))
			}

			break;

		case 0x2:
			if(d) /* SRA R */
			{
				SET_REGF_C(val & 0x01)
				val = (val >> 1) | (val & 0x80);
				SET_REGF_Z((val == 0x00))
				SET_REGF_N(0)
				SET_REGF_H(0)
			}
			else /* SLA R */
			{
				SET_REGF_C((val >> 7))
				val = val << 1;
				SET_REGF_Z((val == 0x00))
				SET_REGF_N(0)
				SET_REGF_H(0)
			}

			break;

		case 0x3:
			if(d) /* SRL R */
			{
				SET_REGF_C(val & 0x01)
				val = val >> 1;
				SET_REGF_Z((val == 0x00))
				SET_REGF_N(0)
				SET_REGF_H(0)
			}
			else /* SWAP R */
			{
				uint8_t temp = (val >> 4) & 0x0F;
				temp |= (val << 4) & 0xF0;
				val = temp;
				SET_REGF_Z((val == 0x00))
				SET_REGF_N(0)
				SET_REGF_H(0)
				SET_REGF_C(0)
			}

			break;
		}

		break;

	case 0x1: /* BIT B, R */
		SET_REGF_Z(!((val >> b) & 0x1))
		SET_REGF_N(0)
		SET_REGF_H(1)
		writeback = 0;
		break;

	case 0x2: /* RES B, R */
		val &= (0xFE << b) | (0xFF >> (8 - b));
		break;

	case 0x3: /* SET B, R */
		val |= (0x1 << b);
		break;
	}

	if(writeback)
	{
		switch(r)
		{
		case 0:
			SET_REG_B(val)
			break;

		case 1:
			SET_REG_C(val)
			break;

		case 2:
			SET_REG_D(val)
			break;

		case 3:
			SET_REG_E(val)
			break;

		case 4:
			SET_REG_H(val)
			break;

		case 5:
			SET_REG_L(val)
			break;

		case 6:
			__gb_cpu_write($HL, val);
			break;

		case 7:
			$A = val;
			break;
		}
	}
	return inst_cycles;
}

/**
 * Internal function used to step the CPU.
 * Returns number of cycles executed
 */
static uint8_t __gb_step_cpu(struct cpu_registers_s *regs)
{
	uint8_t opcode, inst_cycles;
	static const uint8_t op_cycles[0x100] =
	{
		/* *INDENT-OFF* */
		/*0 1 2  3  4  5  6  7  8  9  A  B  C  D  E  F	*/
		4,12, 8, 8, 4, 4, 8, 4,20, 8, 8, 8, 4, 4, 8, 4,	/* 0x00 */
		4,12, 8, 8, 4, 4, 8, 4,12, 8, 8, 8, 4, 4, 8, 4,	/* 0x10 */
		8,12, 8, 8, 4, 4, 8, 4, 8, 8, 8, 8, 4, 4, 8, 4,	/* 0x20 */
		8,12, 8, 8,12,12,12, 4, 8, 8, 8, 8, 4, 4, 8, 4,	/* 0x30 */
		4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,	/* 0x40 */
		4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,	/* 0x50 */
		4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,	/* 0x60 */
		8, 8, 8, 8, 8, 8, 4, 8, 4, 4, 4, 4, 4, 4, 8, 4,	/* 0x70 */
		4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,	/* 0x80 */
		4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,	/* 0x90 */
		4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,	/* 0xA0 */
		4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,	/* 0xB0 */
		8,12,12,16,12,16, 8,16, 8,16,12, 8,12,24, 8,16,	/* 0xC0 */
		8,12,12, 0,12,16, 8,16, 8,16,12, 0,12, 0, 8,16,	/* 0xD0 */
		12,12,8, 0, 0,16, 8,16,16, 4,16, 0, 0, 0, 8,16,	/* 0xE0 */
		12,12,8, 4, 0,16, 8,16,12, 8,16, 4, 0, 0, 8,16	/* 0xF0 */
		/* *INDENT-ON* */
	};

	/* Obtain opcode */
	opcode = (peanut_exec_gb->gb_halt ? 0x00 : __gb_cpu_read($PC++));
	inst_cycles = op_cycles[opcode];

	/* Execute opcode */
	switch(opcode)
	{
	case 0x00: /* NOP */
		break;

	case 0x01: /* LD BC, imm */
		SET_REG_C(__gb_cpu_read($PC++))
		SET_REG_B(__gb_cpu_read($PC++))
		break;

	case 0x02: /* LD (BC), A */
		__gb_cpu_write(regs->bc, $A);
		break;

	case 0x03: /* INC BC */
		regs->bc++;
		break;

	case 0x04: /* INC B */
		SET_REG_B(GET_REG_B()+1);
		SET_REGF_Z((GET_REG_B() == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(((GET_REG_B() & 0x0F) == 0x00))
		break;

	case 0x05: /* DEC B */
		SET_REG_B(GET_REG_B()-1);
		SET_REGF_Z((GET_REG_B() == 0x00))
		SET_REGF_N(1)
		SET_REGF_H(((GET_REG_B() & 0x0F) == 0x0F))
		break;

	case 0x06: /* LD B, imm */
		SET_REG_B(__gb_cpu_read($PC++))
		break;

	case 0x07: /* RLCA */
		$A = ($A << 1) | ($A >> 7);
		SET_REGF_Z(0)
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(($A & 0x01))
		break;

	case 0x08: /* LD (imm), SP */
	{
		uint16_t temp = __gb_cpu_read($PC++);
		temp |= __gb_cpu_read($PC++) << 8;
		__gb_cpu_write(temp++, regs->sp & 0xFF);
		__gb_cpu_write(temp, regs->sp >> 8);
		break;
	}

	case 0x09: /* ADD HL, BC */
	{
		__gb_add16(regs, regs->bc);
		break;
	}

	case 0x0A: /* LD A, (BC) */
		$A = __gb_cpu_read(regs->bc);
		break;

	case 0x0B: /* DEC BC */
		regs->bc--;
		break;

	case 0x0C: /* INC C */
		SET_REG_C(GET_REG_C()+1);
		SET_REGF_Z((GET_REG_C() == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(((GET_REG_C() & 0x0F) == 0x00))
		break;

	case 0x0D: /* DEC C */
		SET_REG_C(GET_REG_C()-1);
		SET_REGF_Z((GET_REG_C() == 0x00))
		SET_REGF_N(1)
		SET_REGF_H(((GET_REG_C() & 0x0F) == 0x0F))
		break;

	case 0x0E: /* LD C, imm */
		SET_REG_C(__gb_cpu_read($PC++))
		break;

	case 0x0F: /* RRCA */
		SET_REGF_C($A & 0x01)
		$A = ($A >> 1) | ($A << 7);
		SET_REGF_Z(0)
		SET_REGF_N(0)
		SET_REGF_H(0)
		break;

	case 0x10: /* STOP */
		//peanut_exec_gb->gb_halt = 1;
		break;

	case 0x11: /* LD DE, imm */
		SET_REG_E(__gb_cpu_read($PC++))
		SET_REG_D(__gb_cpu_read($PC++))
		break;

	case 0x12: /* LD (DE), A */
		__gb_cpu_write(regs->de, $A);
		break;

	case 0x13: /* INC DE */
		regs->de++;
		break;

	case 0x14: /* INC D */
		SET_REG_D(GET_REG_D()+1);
		SET_REGF_Z((GET_REG_D() == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(((GET_REG_D() & 0x0F) == 0x00))
		break;

	case 0x15: /* DEC D */
		SET_REG_D(GET_REG_D()-1);
		SET_REGF_Z((GET_REG_D() == 0x00))
		SET_REGF_N(1)
		SET_REGF_H(((GET_REG_D() & 0x0F) == 0x0F))
		break;

	case 0x16: /* LD D, imm */
		SET_REG_D(__gb_cpu_read($PC++))
		break;

	case 0x17: /* RLA */
	{
		uint8_t temp = $A;
		$A = ($A << 1) | GET_REGF_C();
		SET_REGF_Z(0)
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C((temp >> 7) & 0x01)
		break;
	}

	case 0x18: /* JR imm */
	{
		int8_t temp = (int8_t) __gb_cpu_read($PC++);
		$PC += temp;
		break;
	}

	case 0x19: /* ADD HL, DE */
	{
		__gb_add16(regs, regs->de);
		break;
	}

	case 0x1A: /* LD A, (DE) */
		$A = __gb_cpu_read(regs->de);
		break;

	case 0x1B: /* DEC DE */
		regs->de--;
		break;

	case 0x1C: /* INC E */
		GET_REG_E()++;
		SET_REGF_Z((GET_REG_E() == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(((GET_REG_E() & 0x0F) == 0x00))
		break;

	case 0x1D: /* DEC E */
		SET_REG_E(GET_REG_E()-1);
		SET_REGF_Z((GET_REG_E() == 0x00))
		SET_REGF_N(1)
		SET_REGF_H(((GET_REG_E() & 0x0F) == 0x0F))
		break;

	case 0x1E: /* LD E, imm */
		SET_REG_E(__gb_cpu_read($PC++))
		break;

	case 0x1F: /* RRA */
	{
		uint8_t temp = $A;
		$A = $A >> 1 | (GET_REGF_C() << 7);
		SET_REGF_Z(0)
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(temp & 0x1)
		break;
	}

	case 0x20: /* JP NZ, imm */
		if(!GET_REGF_Z())
		{
			int8_t temp = (int8_t) __gb_cpu_read($PC++);
			$PC += temp;
			inst_cycles += 4;
		}
		else
			$PC++;

		break;

	case 0x21: /* LD HL, imm */
		SET_REG_L(__gb_cpu_read($PC++))
		SET_REG_H(__gb_cpu_read($PC++))
		break;

	case 0x22: /* LDI (HL), A */
		__gb_cpu_write($HL, $A);
		$HL++;
		break;

	case 0x23: /* INC HL */
		$HL++;
		break;

	case 0x24: /* INC H */
		SET_REG_H(GET_REG_H()+1);
		SET_REGF_Z((GET_REG_H() == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(((GET_REG_H() & 0x0F) == 0x00))
		break;

	case 0x25: /* DEC H */
		SET_REG_H(GET_REG_H()-1);
		SET_REGF_Z((GET_REG_H() == 0x00))
		SET_REGF_N(1)
		SET_REGF_H(((GET_REG_H() & 0x0F) == 0x0F))
		break;

	case 0x26: /* LD H, imm */
		SET_REG_H(__gb_cpu_read($PC++))
		break;

	case 0x27: /* DAA */
	{
		uint16_t a = $A;

		if(GET_REGF_N())
		{
			if(GET_REGF_H())
				a = (a - 0x06) & 0xFF;

			if(GET_REGF_C())
				a -= 0x60;
		}
		else
		{
			if(GET_REGF_H() || (a & 0x0F) > 9)
				a += 0x06;

			if(GET_REGF_C() || a > 0x9F)
				a += 0x60;
		}

		if((a & 0x100) == 0x100)
			SET_REGF_C(1)

		$A = a;
		SET_REGF_Z(($A == 0))
		SET_REGF_H(0)

		break;
	}

	case 0x28: /* JP Z, imm */
		if(GET_REGF_Z())
		{
			int8_t temp = (int8_t) __gb_cpu_read($PC++);
			$PC += temp;
			inst_cycles += 4;
		}
		else
			$PC++;

		break;

	case 0x29: /* ADD HL, HL */
	{
		// TODO: optimize?
		__gb_add16(regs, $HL);
		break;
	}

	case 0x2A: /* LD A, (HL+) */
		$A = __gb_cpu_read($HL++);
		break;

	case 0x2B: /* DEC HL */
		$HL--;
		break;

	case 0x2C: /* INC L */
		SET_REG_L(GET_REG_L()+1);
		SET_REGF_Z((GET_REG_L() == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(((GET_REG_L() & 0x0F) == 0x00))
		break;

	case 0x2D: /* DEC L */
		SET_REG_L(GET_REG_L()-1);
		SET_REGF_Z((GET_REG_L() == 0x00))
		SET_REGF_N(1)
		SET_REGF_H(((GET_REG_L() & 0x0F) == 0x0F))
		break;

	case 0x2E: /* LD L, imm */
		SET_REG_L(__gb_cpu_read($PC++))
		break;

	case 0x2F: /* CPL */
		$A = (~$A) & 0xFF;
		SET_REGF_N(1)
		SET_REGF_H(1)
		break;

	case 0x30: /* JP NC, imm */
		if(!GET_REGF_C())
		{
			int8_t temp = (int8_t) __gb_cpu_read($PC++);
			$PC += temp;
			inst_cycles += 4;
		}
		else
			$PC++;

		break;

	case 0x31: /* LD SP, imm */
		regs->sp = __gb_cpu_read($PC++);
		regs->sp |= __gb_cpu_read($PC++) << 8;
		break;

	case 0x32: /* LD (HL), A */
		__gb_cpu_write($HL, $A);
		$HL--;
		break;

	case 0x33: /* INC SP */
		regs->sp++;
		break;

	case 0x34: /* INC (HL) */
	{
		uint8_t temp = __gb_cpu_read($HL) + 1;
		SET_REGF_Z((temp == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(((temp & 0x0F) == 0x00))
		__gb_cpu_write($HL, temp);
		break;
	}

	case 0x35: /* DEC (HL) */
	{
		uint8_t temp = __gb_cpu_read($HL) - 1;
		SET_REGF_Z((temp == 0x00))
		SET_REGF_N(1)
		SET_REGF_H(((temp & 0x0F) == 0x0F))
		__gb_cpu_write($HL, temp);
		break;
	}

	case 0x36: /* LD (HL), imm */
		__gb_cpu_write($HL, __gb_cpu_read($PC++));
		break;

	case 0x37: /* SCF */
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(1)
		break;

	case 0x38: /* JP C, imm */
		if(GET_REGF_C())
		{
			int8_t temp = (int8_t) __gb_cpu_read($PC++);
			$PC += temp;
			inst_cycles += 4;
		}
		else
			$PC++;

		break;

	case 0x39: /* ADD HL, SP */
	{
		__gb_add16(regs, regs->sp);
		break;
	}

	case 0x3A: /* LD A, (HL) */
		$A = __gb_cpu_read($HL--);
		break;

	case 0x3B: /* DEC SP */
		regs->sp--;
		break;

	case 0x3C: /* INC A */
		$A++;
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H((($A & 0x0F) == 0x00))
		break;

	case 0x3D: /* DEC A */
		$A--;
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(1)
		SET_REGF_H((($A & 0x0F) == 0x0F))
		break;

	case 0x3E: /* LD A, imm */
		$A = __gb_cpu_read($PC++);
		break;

	case 0x3F: /* CCF */
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(!GET_REGF_C())
		break;

	case 0x40: /* LD B, B */
		break;

	case 0x41: /* LD B, C */
		SET_REG_B(GET_REG_C())
		break;

	case 0x42: /* LD B, D */
		SET_REG_B(GET_REG_D())
		break;

	case 0x43: /* LD B, E */
		SET_REG_B(GET_REG_E())
		break;

	case 0x44: /* LD B, H */
		SET_REG_B(GET_REG_H())
		break;

	case 0x45: /* LD B, L */
		SET_REG_B(GET_REG_L())
		break;

	case 0x46: /* LD B, (HL) */
		SET_REG_B(__gb_cpu_read($HL))
		break;

	case 0x47: /* LD B, A */
		SET_REG_B($A)
		break;

	case 0x48: /* LD C, B */
		SET_REG_C(GET_REG_B())
		break;

	case 0x49: /* LD C, C */
		break;

	case 0x4A: /* LD C, D */
		SET_REG_C(GET_REG_D())
		break;

	case 0x4B: /* LD C, E */
		SET_REG_C(GET_REG_E())
		break;

	case 0x4C: /* LD C, H */
		SET_REG_C(GET_REG_H())
		break;

	case 0x4D: /* LD C, L */
		SET_REG_C(GET_REG_L())
		break;

	case 0x4E: /* LD C, (HL) */
		SET_REG_C(__gb_cpu_read($HL))
		break;

	case 0x4F: /* LD C, A */
		SET_REG_C($A)
		break;

	case 0x50: /* LD D, B */
		SET_REG_D(GET_REG_B())
		break;

	case 0x51: /* LD D, C */
		SET_REG_D(GET_REG_C())
		break;

	case 0x52: /* LD D, D */
		break;

	case 0x53: /* LD D, E */
		SET_REG_D(GET_REG_E())
		break;

	case 0x54: /* LD D, H */
		SET_REG_D(GET_REG_H())
		break;

	case 0x55: /* LD D, L */
		SET_REG_D(GET_REG_L())
		break;

	case 0x56: /* LD D, (HL) */
		SET_REG_D(__gb_cpu_read($HL))
		break;

	case 0x57: /* LD D, A */
		SET_REG_D($A)
		break;

	case 0x58: /* LD E, B */
		SET_REG_E(GET_REG_B())
		break;

	case 0x59: /* LD E, C */
		SET_REG_E(GET_REG_C())
		break;

	case 0x5A: /* LD E, D */
		SET_REG_E(GET_REG_D())
		break;

	case 0x5B: /* LD E, E */
		break;

	case 0x5C: /* LD E, H */
		SET_REG_E(GET_REG_H())
		break;

	case 0x5D: /* LD E, L */
		SET_REG_E(GET_REG_L())
		break;

	case 0x5E: /* LD E, (HL) */
		SET_REG_E(__gb_cpu_read($HL))
		break;

	case 0x5F: /* LD E, A */
		SET_REG_E($A)
		break;

	case 0x60: /* LD H, B */
		SET_REG_H(GET_REG_B())
		break;

	case 0x61: /* LD H, C */
		SET_REG_H(GET_REG_C())
		break;

	case 0x62: /* LD H, D */
		SET_REG_H(GET_REG_D())
		break;

	case 0x63: /* LD H, E */
		SET_REG_H(GET_REG_E())
		break;

	case 0x64: /* LD H, H */
		break;

	case 0x65: /* LD H, L */
		SET_REG_H(GET_REG_L())
		break;

	case 0x66: /* LD H, (HL) */
		SET_REG_H(__gb_cpu_read($HL))
		break;

	case 0x67: /* LD H, A */
		SET_REG_H($A)
		break;

	case 0x68: /* LD L, B */
		SET_REG_L(GET_REG_B())
		break;

	case 0x69: /* LD L, C */
		SET_REG_L(GET_REG_C())
		break;

	case 0x6A: /* LD L, D */
		SET_REG_L(GET_REG_D())
		break;

	case 0x6B: /* LD L, E */
		SET_REG_L(GET_REG_E())
		break;

	case 0x6C: /* LD L, H */
		SET_REG_L(GET_REG_H())
		break;

	case 0x6D: /* LD L, L */
		break;

	case 0x6E: /* LD L, (HL) */
		SET_REG_L(__gb_cpu_read($HL))
		break;

	case 0x6F: /* LD L, A */
		SET_REG_L($A)
		break;

	case 0x70: /* LD (HL), B */
		__gb_cpu_write($HL, GET_REG_B());
		break;

	case 0x71: /* LD (HL), C */
		__gb_cpu_write($HL, GET_REG_C());
		break;

	case 0x72: /* LD (HL), D */
		__gb_cpu_write($HL, GET_REG_D());
		break;

	case 0x73: /* LD (HL), E */
		__gb_cpu_write($HL, GET_REG_E());
		break;

	case 0x74: /* LD (HL), H */
		__gb_cpu_write($HL, GET_REG_H());
		break;

	case 0x75: /* LD (HL), L */
		__gb_cpu_write($HL, GET_REG_L());
		break;

	case 0x76: /* HALT */
		/* TODO: Emulate HALT bug? */
		peanut_exec_gb->gb_halt = 1;
		break;

	case 0x77: /* LD (HL), A */
		__gb_cpu_write($HL, $A);
		break;

	case 0x78: /* LD A, B */
		$A = GET_REG_B();
		break;

	case 0x79: /* LD A, C */
		$A = GET_REG_C();
		break;

	case 0x7A: /* LD A, D */
		$A = GET_REG_D();
		break;

	case 0x7B: /* LD A, E */
		$A = GET_REG_E();
		break;

	case 0x7C: /* LD A, H */
		$A = GET_REG_H();
		break;

	case 0x7D: /* LD A, L */
		$A = GET_REG_L();
		break;

	case 0x7E: /* LD A, (HL) */
		$A = __gb_cpu_read($HL);
		break;

	case 0x7F: /* LD A, A */
		break;

	case 0x80: /* ADD A, B */
	{
		__gb_add8(regs, 0, GET_REG_B());
		break;
	}

	case 0x81: /* ADD A, C */
	{
		__gb_add8(regs, 0, GET_REG_C());
		break;
	}

	case 0x82: /* ADD A, D */
	{
		__gb_add8(regs, 0, GET_REG_D());
		break;
	}

	case 0x83: /* ADD A, E */
	{
		__gb_add8(regs, 0, GET_REG_E());
		break;
	}

	case 0x84: /* ADD A, H */
	{
		__gb_add8(regs, 0, GET_REG_H());
		break;
	}

	case 0x85: /* ADD A, L */
	{
		__gb_add8(regs, 0, GET_REG_L());
		break;
	}

	case 0x86: /* ADD A, (HL) */
	{
		uint8_t val = __gb_cpu_read($HL);
		__gb_add8(regs, 0, val);
		break;
	}

	case 0x87: /* ADD A, A */
	{
		// TODO: optimize?
		__gb_add8(regs, 0, $A);
		break;
	}

	case 0x88: /* ADC A, B */
	{
		__gb_add8(regs, 1, GET_REG_B());
		break;
	}

	case 0x89: /* ADC A, C */
	{
		__gb_add8(regs, 1, GET_REG_C());
		break;
	}

	case 0x8A: /* ADC A, D */
	{
		__gb_add8(regs, 1, GET_REG_D());
		break;
	}

	case 0x8B: /* ADC A, E */
	{
		__gb_add8(regs, 1, GET_REG_E());
		break;
	}

	case 0x8C: /* ADC A, H */
	{
		__gb_add8(regs, 1, GET_REG_H());
		break;
	}

	case 0x8D: /* ADC A, L */
	{
		__gb_add8(regs, 1, GET_REG_L());
		break;
	}

	case 0x8E: /* ADC A, (HL) */
	{
		uint8_t val = __gb_cpu_read($HL);
		__gb_add8(regs, 1, val);
		break;
	}

	case 0x8F: /* ADC A, A */
	{
		// TODO: optimize?
		__gb_add8(regs, 1, $A);
		break;
	}

	case 0x90: /* SUB B */
	{
		__gb_sub8(regs, 0, GET_REG_B());
		break;
	}

	case 0x91: /* SUB C */
	{
		__gb_sub8(regs, 0, GET_REG_C());
		break;
	}

	case 0x92: /* SUB D */
	{
		__gb_sub8(regs, 0, GET_REG_D());
		break;
	}

	case 0x93: /* SUB E */
	{
		__gb_sub8(regs, 0, GET_REG_E());
		break;
	}

	case 0x94: /* SUB H */
	{
		__gb_sub8(regs, 0, GET_REG_H());
		break;
	}

	case 0x95: /* SUB L */
	{
		__gb_sub8(regs, 0, GET_REG_L());
		break;
	}

	case 0x96: /* SUB (HL) */
	{
		uint8_t val = __gb_cpu_read($HL);
		__gb_sub8(regs, 0, val);
		break;
	}

	case 0x97: /* SUB A */
		$A = 0;
		SET_REGF_Z(1)
		SET_REGF_N(1)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0x98: /* SBC A, B */
	{
		__gb_sub8(regs, 1, GET_REG_B());
		break;
	}

	case 0x99: /* SBC A, C */
	{
		__gb_sub8(regs, 1, GET_REG_C());
		break;
	}

	case 0x9A: /* SBC A, D */
	{
		__gb_sub8(regs, 1, GET_REG_D());
		break;
	}

	case 0x9B: /* SBC A, E */
	{
		__gb_sub8(regs, 1, GET_REG_E());
		break;
	}

	case 0x9C: /* SBC A, H */
	{
		__gb_sub8(regs, 1, GET_REG_H());
		break;
	}

	case 0x9D: /* SBC A, L */
	{
		__gb_sub8(regs, 1, GET_REG_L());
		break;
	}

	case 0x9E: /* SBC A, (HL) */
	{
		uint8_t val = __gb_cpu_read($HL);
		__gb_sub8(regs, 1, val);
		break;
	}

	case 0x9F: /* SBC A, A */
		$A = GET_REGF_C() ? 0xFF : 0x00;
		SET_REGF_Z(GET_REGF_C() ? 0x00 : 0x01)
		SET_REGF_N(1)
		SET_REGF_H(GET_REGF_C())
		break;

	case 0xA0: /* AND B */
		$A = $A & GET_REG_B();
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(1)
		SET_REGF_C(0)
		break;

	case 0xA1: /* AND C */
		$A = $A & GET_REG_C();
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(1)
		SET_REGF_C(0)
		break;

	case 0xA2: /* AND D */
		$A = $A & GET_REG_D();
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(1)
		SET_REGF_C(0)
		break;

	case 0xA3: /* AND E */
		$A = $A & GET_REG_E();
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(1)
		SET_REGF_C(0)
		break;

	case 0xA4: /* AND H */
		$A = $A & GET_REG_H();
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(1)
		SET_REGF_C(0)
		break;

	case 0xA5: /* AND L */
		$A = $A & GET_REG_L();
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(1)
		SET_REGF_C(0)
		break;

	case 0xA6: /* AND B */
		$A = $A & __gb_cpu_read($HL);
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(1)
		SET_REGF_C(0)
		break;

	case 0xA7: /* AND A */
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(1)
		SET_REGF_C(0)
		break;

	case 0xA8: /* XOR B */
		$A = $A ^ GET_REG_B();
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0xA9: /* XOR C */
		$A = $A ^ GET_REG_C();
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0xAA: /* XOR D */
		$A = $A ^ GET_REG_D();
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0xAB: /* XOR E */
		$A = $A ^ GET_REG_E();
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0xAC: /* XOR H */
		$A = $A ^ GET_REG_H();
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0xAD: /* XOR L */
		$A = $A ^ GET_REG_L();
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0xAE: /* XOR (HL) */
		$A = $A ^ __gb_cpu_read($HL);
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0xAF: /* XOR A */
		$A = 0x00;
		SET_REGF_Z(1)
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0xB0: /* OR B */
		$A = $A | GET_REG_B();
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0xB1: /* OR C */
		$A = $A | GET_REG_C();
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0xB2: /* OR D */
		$A = $A | GET_REG_D();
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0xB3: /* OR E */
		$A = $A | GET_REG_E();
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0xB4: /* OR H */
		$A = $A | GET_REG_H();
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0xB5: /* OR L */
		$A = $A | GET_REG_L();
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0xB6: /* OR (HL) */
		$A = $A | __gb_cpu_read($HL);
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0xB7: /* OR A */
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0xB8: /* CP B */
	{
		__gb_cmp8(regs, 0, GET_REG_B());
		break;
	}

	case 0xB9: /* CP C */
	{
		__gb_cmp8(regs, 0, GET_REG_C());
		break;
	}

	case 0xBA: /* CP D */
	{
		__gb_cmp8(regs, 0, GET_REG_D());
		break;
	}

	case 0xBB: /* CP E */
	{
		__gb_cmp8(regs, 0, GET_REG_E());
		break;
	}

	case 0xBC: /* CP H */
	{
		__gb_cmp8(regs, 0, GET_REG_H());
		break;
	}

	case 0xBD: /* CP L */
	{
		__gb_cmp8(regs, 0, GET_REG_L());
		break;
	}

	/* TODO: Optimsation by combining similar opcode routines. */
	case 0xBE: /* CP (HL) */
	{
		uint8_t val = __gb_cpu_read($HL);
		__gb_cmp8(regs, 0, val);
		break;
	}

	case 0xBF: /* CP A */
		SET_REGF_Z(1)
		SET_REGF_N(1)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0xC0: /* RET NZ */
		if(!GET_REGF_Z())
		{
			$PC = __gb_cpu_read(regs->sp++);
			$PC |= __gb_cpu_read(regs->sp++) << 8;
			inst_cycles += 12;
		}

		break;

	case 0xC1: /* POP BC */
		SET_REG_C(__gb_cpu_read(regs->sp++))
		SET_REG_B(__gb_cpu_read(regs->sp++))
		break;

	case 0xC2: /* JP NZ, imm */
		if(!GET_REGF_Z())
		{
			uint16_t temp = __gb_cpu_read($PC++);
			temp |= __gb_cpu_read($PC++) << 8;
			$PC = temp;
			inst_cycles += 4;
		}
		else
			$PC += 2;

		break;

	case 0xC3: /* JP imm */
	{
		uint16_t temp = __gb_cpu_read($PC++);
		temp |= __gb_cpu_read($PC) << 8;
		$PC = temp;
		break;
	}

	case 0xC4: /* CALL NZ imm */
		if(!GET_REGF_Z())
		{
			uint16_t temp = __gb_cpu_read($PC++);
			temp |= __gb_cpu_read($PC++) << 8;
			__gb_cpu_write(--regs->sp, $PC >> 8);
			__gb_cpu_write(--regs->sp, $PC & 0xFF);
			$PC = temp;
			inst_cycles += 12;
		}
		else
			$PC += 2;

		break;

	case 0xC5: /* PUSH BC */
		__gb_cpu_write(--regs->sp, GET_REG_B());
		__gb_cpu_write(--regs->sp, GET_REG_C());
		break;

	case 0xC6: /* ADD A, imm */
	{
		uint8_t value = __gb_cpu_read($PC++);
		__gb_add8(regs, 0, value);
		break;
	}

	case 0xC7: /* RST 0x0000 */
		__gb_cpu_write(--regs->sp, $PC >> 8);
		__gb_cpu_write(--regs->sp, $PC & 0xFF);
		$PC = 0x0000;
		break;

	case 0xC8: /* RET Z */
		if(GET_REGF_Z())
		{
			uint16_t temp = __gb_cpu_read(regs->sp++);
			temp |= __gb_cpu_read(regs->sp++) << 8;
			$PC = temp;
			inst_cycles += 12;
		}

		break;

	case 0xC9: /* RET */
	{
		uint16_t temp = __gb_cpu_read(regs->sp++);
		temp |= __gb_cpu_read(regs->sp++) << 8;
		$PC = temp;
		break;
	}

	case 0xCA: /* JP Z, imm */
		if(GET_REGF_Z())
		{
			uint16_t temp = __gb_cpu_read($PC++);
			temp |= __gb_cpu_read($PC++) << 8;
			$PC = temp;
			inst_cycles += 4;
		}
		else
			$PC += 2;

		break;

	case 0xCB: /* CB INST */
		inst_cycles = __gb_execute_cb(regs);
		break;

	case 0xCC: /* CALL Z, imm */
		if(GET_REGF_Z())
		{
			uint16_t temp = __gb_cpu_read($PC++);
			temp |= __gb_cpu_read($PC++) << 8;
			__gb_cpu_write(--regs->sp, $PC >> 8);
			__gb_cpu_write(--regs->sp, $PC & 0xFF);
			$PC = temp;
			inst_cycles += 12;
		}
		else
			$PC += 2;

		break;

	case 0xCD: /* CALL imm */
	{
		uint16_t addr = __gb_cpu_read($PC++);
		addr |= __gb_cpu_read($PC++) << 8;
		__gb_cpu_write(--regs->sp, $PC >> 8);
		__gb_cpu_write(--regs->sp, $PC & 0xFF);
		$PC = addr;
	}
	break;

	case 0xCE: /* ADC A, imm */
	{
		uint8_t value = __gb_cpu_read($PC++);
		__gb_add8(regs, 1, value);
		break;
	}

	case 0xCF: /* RST 0x0008 */
		__gb_cpu_write(--regs->sp, $PC >> 8);
		__gb_cpu_write(--regs->sp, $PC & 0xFF);
		$PC = 0x0008;
		break;

	case 0xD0: /* RET NC */
		if(!GET_REGF_C())
		{
			uint16_t temp = __gb_cpu_read(regs->sp++);
			temp |= __gb_cpu_read(regs->sp++) << 8;
			$PC = temp;
			inst_cycles += 12;
		}

		break;

	case 0xD1: /* POP DE */
		SET_REG_E(__gb_cpu_read(regs->sp++))
		SET_REG_D(__gb_cpu_read(regs->sp++))
		break;

	case 0xD2: /* JP NC, imm */
		if(!GET_REGF_C())
		{
			uint16_t temp =  __gb_cpu_read($PC++);
			temp |=  __gb_cpu_read($PC++) << 8;
			$PC = temp;
			inst_cycles += 4;
		}
		else
			$PC += 2;

		break;

	case 0xD4: /* CALL NC, imm */
		if(!GET_REGF_C())
		{
			uint16_t temp = __gb_cpu_read($PC++);
			temp |= __gb_cpu_read($PC++) << 8;
			__gb_cpu_write(--regs->sp, $PC >> 8);
			__gb_cpu_write(--regs->sp, $PC & 0xFF);
			$PC = temp;
			inst_cycles += 12;
		}
		else
			$PC += 2;

		break;

	case 0xD5: /* PUSH DE */
		__gb_cpu_write(--regs->sp, GET_REG_D());
		__gb_cpu_write(--regs->sp, GET_REG_E());
		break;

	case 0xD6: /* SUB A, imm */
	{
		uint8_t value = __gb_cpu_read($PC++);
		__gb_sub8(regs, 0, value);
		break;
	}

	case 0xD7: /* RST 0x0010 */
		__gb_cpu_write(--regs->sp, $PC >> 8);
		__gb_cpu_write(--regs->sp, $PC & 0xFF);
		$PC = 0x0010;
		break;

	case 0xD8: /* RET C */
		if(GET_REGF_C())
		{
			uint16_t temp = __gb_cpu_read(regs->sp++);
			temp |= __gb_cpu_read(regs->sp++) << 8;
			$PC = temp;
			inst_cycles += 12;
		}

		break;

	case 0xD9: /* RETI */
	{
		uint16_t temp = __gb_cpu_read(regs->sp++);
		temp |= __gb_cpu_read(regs->sp++) << 8;
		$PC = temp;
		peanut_exec_gb->gb_ime = 1;
	}
	break;

	case 0xDA: /* JP C, imm */
		if(GET_REGF_C())
		{
			uint16_t addr = __gb_cpu_read($PC++);
			addr |= __gb_cpu_read($PC++) << 8;
			$PC = addr;
			inst_cycles += 4;
		}
		else
			$PC += 2;

		break;

	case 0xDC: /* CALL C, imm */
		if(GET_REGF_C())
		{
			uint16_t temp = __gb_cpu_read($PC++);
			temp |= __gb_cpu_read($PC++) << 8;
			__gb_cpu_write(--regs->sp, $PC >> 8);
			__gb_cpu_write(--regs->sp, $PC & 0xFF);
			$PC = temp;
			inst_cycles += 12;
		}
		else
			$PC += 2;

		break;

	case 0xDE: /* SBC A, imm */
	{
		uint8_t value = __gb_cpu_read($PC++);
		__gb_sub8(regs, 1, value);
		break;
	}

	case 0xDF: /* RST 0x0018 */
		__gb_cpu_write(--regs->sp, $PC >> 8);
		__gb_cpu_write(--regs->sp, $PC & 0xFF);
		$PC = 0x0018;
		break;

	case 0xE0: /* LD (0xFF00+imm), A */
		__gb_cpu_write(0xFF00 | __gb_cpu_read($PC++),
			   $A);
		break;

	case 0xE1: /* POP HL */
		SET_REG_L(__gb_cpu_read(regs->sp++))
		SET_REG_H(__gb_cpu_read(regs->sp++))
		break;

	case 0xE2: /* LD (C), A */
		__gb_cpu_write(0xFF00 | GET_REG_C(), $A);
		break;

	case 0xE5: /* PUSH HL */
		__gb_cpu_write(--regs->sp, GET_REG_H());
		__gb_cpu_write(--regs->sp, GET_REG_L());
		break;

	case 0xE6: /* AND imm */
		/* TODO: Optimisation? */
		$A = $A & __gb_cpu_read($PC++);
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(1)
		SET_REGF_C(0)
		break;

	case 0xE7: /* RST 0x0020 */
		__gb_cpu_write(--regs->sp, $PC >> 8);
		__gb_cpu_write(--regs->sp, $PC & 0xFF);
		$PC = 0x0020;
		break;

	case 0xE8: /* ADD SP, imm */
	{
		int8_t offset = (int8_t) __gb_cpu_read($PC++);
		/* TODO: Move flag assignments for optimisation. */
		SET_REGF_Z(0)
		SET_REGF_N(0)
		SET_REGF_H(((regs->sp & 0xF) + (offset & 0xF) > 0xF) ? 1 : 0)
		SET_REGF_C(((regs->sp & 0xFF) + (offset & 0xFF) > 0xFF))
		regs->sp += offset;
		break;
	}

	case 0xE9: /* JP (HL) */
		$PC = $HL;
		break;

	case 0xEA: /* LD (imm), A */
	{
		uint16_t addr = __gb_cpu_read($PC++);
		addr |= __gb_cpu_read($PC++) << 8;
		__gb_cpu_write(addr, $A);
		break;
	}

	case 0xEE: /* XOR imm */
		$A = $A ^ __gb_cpu_read($PC++);
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0xEF: /* RST 0x0028 */
		__gb_cpu_write(--regs->sp, $PC >> 8);
		__gb_cpu_write(--regs->sp, $PC & 0xFF);
		$PC = 0x0028;
		break;

	case 0xF0: /* LD A, (0xFF00+imm) */
		$A =
			__gb_cpu_read(0xFF00 | __gb_cpu_read($PC++));
		break;

	case 0xF1: /* POP AF */
	{
		uint8_t temp_8 = __gb_cpu_read(regs->sp++);
		__set_f(regs, temp_8);
		$A = __gb_cpu_read(regs->sp++);
		break;
	}

	case 0xF2: /* LD A, (C) */
		$A = __gb_cpu_read(0xFF00 | GET_REG_C());
		break;

	case 0xF3: /* DI */
		peanut_exec_gb->gb_ime = 0;
		break;

	case 0xF5: /* PUSH AF */
		__gb_cpu_write(--regs->sp, $A);
		__gb_cpu_write(--regs->sp, __get_f(regs));
		break;

	case 0xF6: /* OR imm */
		$A = $A | __gb_cpu_read($PC++);
		SET_REGF_Z(($A == 0x00))
		SET_REGF_N(0)
		SET_REGF_H(0)
		SET_REGF_C(0)
		break;

	case 0xF7: /* PUSH AF */
		__gb_cpu_write(--regs->sp, $PC >> 8);
		__gb_cpu_write(--regs->sp, $PC & 0xFF);
		$PC = 0x0030;
		break;

	case 0xF8: /* LD HL, SP+/-imm */
	{
		/* Taken from SameBoy, which is released under MIT Licence. */
		int8_t offset = (int8_t) __gb_cpu_read($PC++);
		$HL = regs->sp + offset;
		SET_REGF_Z(0)
		SET_REGF_N(0)
		SET_REGF_H(((regs->sp & 0xF) + (offset & 0xF) > 0xF) ? 1 : 0)
		SET_REGF_C(((regs->sp & 0xFF) + (offset & 0xFF) > 0xFF) ? 1 : 0);
		break;
	}

	case 0xF9: /* LD SP, HL */
		regs->sp = $HL;
		break;

	case 0xFA: /* LD A, (imm) */
	{
		uint16_t addr = __gb_cpu_read($PC++);
		addr |= __gb_cpu_read($PC++) << 8;
		$A = __gb_cpu_read(addr);
		break;
	}

	case 0xFB: /* EI */
		peanut_exec_gb->gb_ime = 1;
		break;

	case 0xFE: /* CP imm */
	{
		uint8_t value = __gb_cpu_read($PC++);
		__gb_cmp8(regs, 0, value);
		break;
	}

	case 0xFF: /* RST 0x0038 */
		__gb_cpu_write(--regs->sp, $PC >> 8);
		__gb_cpu_write(--regs->sp, $PC & 0xFF);
		$PC = 0x0038;
		break;
	
	default:
		(peanut_exec_gb->gb_error)(peanut_exec_gb, GB_INVALID_OPCODE, opcode);
	}
	return inst_cycles;
}

uint8_t __gb_step_chunked(struct cpu_registers_s *regs)
{
	load_regs(regs);
    // take 3 cpu steps at a time
    uint8_t inst_cycles = 0;
    for (size_t i = 0; i < CPU_STEP_CHUNK; ++i)
    {
        inst_cycles += __gb_step_cpu(regs);
    }
	store_regs(regs);
    return inst_cycles;
}