#include "peanut_gb.h"

static uint8_t __gb_execute_cb(struct gb_s *gb)
{
	uint8_t inst_cycles;
	uint8_t cbop = __gb_read(gb, gb->cpu_reg.pc++);
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
		val = gb->cpu_reg.b;
		break;

	case 1:
		val = gb->cpu_reg.c;
		break;

	case 2:
		val = gb->cpu_reg.d;
		break;

	case 3:
		val = gb->cpu_reg.e;
		break;

	case 4:
		val = gb->cpu_reg.h;
		break;

	case 5:
		val = gb->cpu_reg.l;
		break;

	case 6:
		val = __gb_read(gb, gb->cpu_reg.hl);
		break;

	/* Only values 0-7 are possible here, so we make the final case
	 * default to satisfy -Wmaybe-uninitialized warning. */
	default:
		val = gb->cpu_reg.a;
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
				val |= cbop ? (gb->cpu_reg.carry << 7) : (temp << 7);
				gb->cpu_reg.z = val;
				gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
				gb->cpu_reg.carry = (temp & 0x01);
			}
			else /* RLC R / RL R */
			{
				uint8_t temp = val;
				val = (val << 1);
				val |= cbop ? gb->cpu_reg.carry : (temp >> 7);
				gb->cpu_reg.z = val;
				gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
				gb->cpu_reg.carry = (temp >> 7);
			}

			break;

		case 0x2:
			if(d) /* SRA R */
			{
				gb->cpu_reg.carry = val & 0x01;
				val = (val >> 1) | (val & 0x80);
				gb->cpu_reg.z = val;
				gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
			}
			else /* SLA R */
			{
				gb->cpu_reg.carry = (val >> 7);
				val = val << 1;
				gb->cpu_reg.z = val;
				gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
			}

			break;

		case 0x3:
			if(d) /* SRL R */
			{
				gb->cpu_reg.carry = val & 0x01;
				val = val >> 1;
				gb->cpu_reg.z = val;
				gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
			}
			else /* SWAP R */
			{
				uint8_t temp = (val >> 4) & 0x0F;
				temp |= (val << 4) & 0xF0;
				val = temp;
				gb->cpu_reg.z = val;
				gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
				gb->cpu_reg.carry = 0;
			}

			break;
		}

		break;

	case 0x1: /* BIT B, R */
		gb->cpu_reg.z = ((val >> b) & 0x1);
		gb->cpu_reg.nh = jit_regfile_setnh(0, 1);
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
			gb->cpu_reg.b = val;
			break;

		case 1:
			gb->cpu_reg.c = val;
			break;

		case 2:
			gb->cpu_reg.d = val;
			break;

		case 3:
			gb->cpu_reg.e = val;
			break;

		case 4:
			gb->cpu_reg.h = val;
			break;

		case 5:
			gb->cpu_reg.l = val;
			break;

		case 6:
			__gb_write(gb, gb->cpu_reg.hl, val);
			break;

		case 7:
			gb->cpu_reg.a = val;
			break;
		}
	}
	return inst_cycles;
}

static inline void __cpu_add8(struct gb_s *gb, int carry, uint8_t val)
{
	uint8_t c = carry ? gb->cpu_reg.carry : 0;
	uint16_t temp = gb->cpu_reg.a + val + c;
	gb->cpu_reg.z = (temp & 0xFF);
	gb->cpu_reg.nh = jit_regfile_setnh_op(
		0, // n
		gb->cpu_reg.a, // a
		val, // b
		c // c
	);
	gb->cpu_reg.carry = temp >> 8;
	gb->cpu_reg.a = (temp & 0xFF);
}

static inline uint8_t __cpu_cmp8(struct gb_s *gb, int carry, uint8_t val)
{
	uint8_t c = carry ? gb->cpu_reg.carry : 0;
	uint16_t temp = gb->cpu_reg.a - val - c;
	gb->cpu_reg.z = (temp & 0xFF);
	gb->cpu_reg.nh = jit_regfile_setnh_op(
		1, // n
		gb->cpu_reg.a, // a
		val, // b
		c // c
	);
	gb->cpu_reg.carry = temp >> 8;
	return temp;
}

static inline void __cpu_sub8(struct gb_s *gb, int carry, uint8_t val)
{
	gb->cpu_reg.a = __cpu_cmp8(gb, carry, val);
}

/**
 * Internal function used to step the CPU.
 * Returns number of cycles executed
 */
static uint8_t __gb_step_cpu(struct gb_s *gb)
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

	/* Handle interrupts */
	if((gb->cpu_reg.ime || gb->gb_halt) &&
			(gb->gb_reg.IF & gb->gb_reg.IE & ANY_INTR))
	{
		gb->gb_halt = 0;

		if(gb->cpu_reg.ime)
		{
			/* Disable interrupts */
			gb->cpu_reg.ime = 0;

			/* Push Program Counter */
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);

			/* Call interrupt handler if required. */
			if(gb->gb_reg.IF & gb->gb_reg.IE & VBLANK_INTR)
			{
				gb->cpu_reg.pc = VBLANK_INTR_ADDR;
				gb->gb_reg.IF ^= VBLANK_INTR;
			}
			else if(gb->gb_reg.IF & gb->gb_reg.IE & LCDC_INTR)
			{
				gb->cpu_reg.pc = LCDC_INTR_ADDR;
				gb->gb_reg.IF ^= LCDC_INTR;
			}
			else if(gb->gb_reg.IF & gb->gb_reg.IE & TIMER_INTR)
			{
				gb->cpu_reg.pc = TIMER_INTR_ADDR;
				gb->gb_reg.IF ^= TIMER_INTR;
			}
			else if(gb->gb_reg.IF & gb->gb_reg.IE & SERIAL_INTR)
			{
				gb->cpu_reg.pc = SERIAL_INTR_ADDR;
				gb->gb_reg.IF ^= SERIAL_INTR;
			}
			else if(gb->gb_reg.IF & gb->gb_reg.IE & CONTROL_INTR)
			{
				gb->cpu_reg.pc = CONTROL_INTR_ADDR;
				gb->gb_reg.IF ^= CONTROL_INTR;
			}
		}
	}

	/* Obtain opcode */
	opcode = (gb->gb_halt ? 0x00 : __gb_read(gb, gb->cpu_reg.pc++));
	inst_cycles = op_cycles[opcode];

	/* Execute opcode */
	switch(opcode)
	{
	case 0x00: /* NOP */
		break;

	case 0x01: /* LD BC, imm */
		gb->cpu_reg.c = __gb_read(gb, gb->cpu_reg.pc++);
		gb->cpu_reg.b = __gb_read(gb, gb->cpu_reg.pc++);
		break;

	case 0x02: /* LD (BC), A */
		__gb_write(gb, gb->cpu_reg.bc, gb->cpu_reg.a);
		break;

	case 0x03: /* INC BC */
		gb->cpu_reg.bc++;
		break;

	case 0x04: /* INC B */
		gb->cpu_reg.nh = jit_regfile_setnh_op(
			0, // n
			gb->cpu_reg.b, // a
			0, // b
			1 // c
		);
		gb->cpu_reg.b++;
		gb->cpu_reg.z = gb->cpu_reg.b;
		break;

	case 0x05: /* DEC B */
		gb->cpu_reg.nh = jit_regfile_setnh_op(
			1, // n
			gb->cpu_reg.b, // a
			0, // b
			0x01 // c
		);
		gb->cpu_reg.b--;
		gb->cpu_reg.z = gb->cpu_reg.b;
		break;

	case 0x06: /* LD B, imm */
		gb->cpu_reg.b = __gb_read(gb, gb->cpu_reg.pc++);
		break;

	case 0x07: /* RLCA */
		gb->cpu_reg.a = (gb->cpu_reg.a << 1) | (gb->cpu_reg.a >> 7);
		gb->cpu_reg.z = 1;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = (gb->cpu_reg.a & 0x01);
		break;

	case 0x08: /* LD (imm), SP */
	{
		uint16_t temp = __gb_read(gb, gb->cpu_reg.pc++);
		temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
		__gb_write(gb, temp++, gb->cpu_reg.sp & 0xFF);
		__gb_write(gb, temp, gb->cpu_reg.sp >> 8);
		break;
	}

	case 0x09: /* ADD HL, BC */
	{
		uint_fast32_t temp = gb->cpu_reg.hl + gb->cpu_reg.bc;
		// TODO: optimize?
		gb->cpu_reg.nh = jit_regfile_setnh(
			0,
			(temp ^ gb->cpu_reg.hl ^ gb->cpu_reg.bc) & 0x1000 ? 1 : 0
		);
		gb->cpu_reg.carry = temp >> 16;
		gb->cpu_reg.hl = (temp & 0x0000FFFF);
		break;
	}

	case 0x0A: /* LD A, (BC) */
		gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.bc);
		break;

	case 0x0B: /* DEC BC */
		gb->cpu_reg.bc--;
		break;

	case 0x0C: /* INC C */
		gb->cpu_reg.nh = jit_regfile_setnh_op(
			0, // n
			gb->cpu_reg.a, // a
			0, // b
			1 // c
		);
		gb->cpu_reg.c++;
		gb->cpu_reg.z = gb->cpu_reg.c;
		break;

	case 0x0D: /* DEC C */
		gb->cpu_reg.nh = jit_regfile_setnh_op(
			1, // n
			gb->cpu_reg.c, // a
			0, // b
			0x01 // c
		);
		gb->cpu_reg.c--;
		gb->cpu_reg.z = gb->cpu_reg.c;
		break;

	case 0x0E: /* LD C, imm */
		gb->cpu_reg.c = __gb_read(gb, gb->cpu_reg.pc++);
		break;

	case 0x0F: /* RRCA */
		gb->cpu_reg.carry = gb->cpu_reg.a & 0x01;
		gb->cpu_reg.a = (gb->cpu_reg.a >> 1) | (gb->cpu_reg.a << 7);
		gb->cpu_reg.z = 1;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		break;

	case 0x10: /* STOP */
		//gb->gb_halt = 1;
		break;

	case 0x11: /* LD DE, imm */
		gb->cpu_reg.e = __gb_read(gb, gb->cpu_reg.pc++);
		gb->cpu_reg.d = __gb_read(gb, gb->cpu_reg.pc++);
		break;

	case 0x12: /* LD (DE), A */
		__gb_write(gb, gb->cpu_reg.de, gb->cpu_reg.a);
		break;

	case 0x13: /* INC DE */
		gb->cpu_reg.de++;
		break;

	case 0x14: /* INC D */
		gb->cpu_reg.nh = jit_regfile_setnh_op(
			0, // n
			gb->cpu_reg.d, // a
			0, // b
			1 // c
		);
		gb->cpu_reg.d++;
		gb->cpu_reg.z = gb->cpu_reg.d;
		break;

	case 0x15: /* DEC D */
		gb->cpu_reg.d--;
		gb->cpu_reg.z = gb->cpu_reg.d;
		gb->cpu_reg.nh = jit_regfile_setnh_op(
			1, // n
			gb->cpu_reg.d, // a
			0, // b
			0x01 // c
		);
		break;

	case 0x16: /* LD D, imm */
		gb->cpu_reg.d = __gb_read(gb, gb->cpu_reg.pc++);
		break;

	case 0x17: /* RLA */
	{
		uint8_t temp = gb->cpu_reg.a;
		gb->cpu_reg.a = (gb->cpu_reg.a << 1) | gb->cpu_reg.carry;
		gb->cpu_reg.z = 1;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = (temp >> 7) & 0x01;
		break;
	}

	case 0x18: /* JR imm */
	{
		int8_t temp = (int8_t) __gb_read(gb, gb->cpu_reg.pc++);
		gb->cpu_reg.pc += temp;
		break;
	}

	case 0x19: /* ADD HL, DE */
	{
		uint_fast32_t temp = gb->cpu_reg.hl + gb->cpu_reg.de;
		gb->cpu_reg.nh = jit_regfile_setnh(
			0,
			(temp ^ gb->cpu_reg.hl ^ gb->cpu_reg.de) & 0x1000 ? 1 : 0
		);
		gb->cpu_reg.carry = temp >> 16;
		gb->cpu_reg.hl = (temp & 0x0000FFFF);
		break;
	}

	case 0x1A: /* LD A, (DE) */
		gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.de);
		break;

	case 0x1B: /* DEC DE */
		gb->cpu_reg.de--;
		break;

	case 0x1C: /* INC E */
		gb->cpu_reg.nh = jit_regfile_setnh_op(
			0, // n
			gb->cpu_reg.e, // a
			0, // b
			1 // c
		);
		gb->cpu_reg.z = ++gb->cpu_reg.e;
		break;

	case 0x1D: /* DEC E */
		gb->cpu_reg.nh = jit_regfile_setnh_op(
			1, // n
			gb->cpu_reg.e, // a
			0, // b
			0x01 // c
		);
		gb->cpu_reg.z = --gb->cpu_reg.e;
		break;

	case 0x1E: /* LD E, imm */
		gb->cpu_reg.e = __gb_read(gb, gb->cpu_reg.pc++);
		break;

	case 0x1F: /* RRA */
	{
		uint8_t temp = gb->cpu_reg.a;
		gb->cpu_reg.a = gb->cpu_reg.a >> 1 | (gb->cpu_reg.carry << 7);
		gb->cpu_reg.z = 1;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = temp & 0x1;
		break;
	}

	case 0x20: /* JP NZ, imm */
		if(gb->cpu_reg.z)
		{
			int8_t temp = (int8_t) __gb_read(gb, gb->cpu_reg.pc++);
			gb->cpu_reg.pc += temp;
			inst_cycles += 4;
		}
		else
			gb->cpu_reg.pc++;

		break;

	case 0x21: /* LD HL, imm */
		gb->cpu_reg.l = __gb_read(gb, gb->cpu_reg.pc++);
		gb->cpu_reg.h = __gb_read(gb, gb->cpu_reg.pc++);
		break;

	case 0x22: /* LDI (HL), A */
		__gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.a);
		gb->cpu_reg.hl++;
		break;

	case 0x23: /* INC HL */
		gb->cpu_reg.hl++;
		break;

	case 0x24: /* INC H */
		gb->cpu_reg.nh = jit_regfile_setnh_op(
			0, // n
			gb->cpu_reg.h, // a
			0, // b
			1 // c
		);
		gb->cpu_reg.h++;
		gb->cpu_reg.z = gb->cpu_reg.h;
		break;

	case 0x25: /* DEC H */
		gb->cpu_reg.nh = jit_regfile_setnh_op(
			1, // n
			gb->cpu_reg.h, // a
			0, // b
			0x01 // c
		);
		gb->cpu_reg.h--;
		gb->cpu_reg.z = gb->cpu_reg.h;
		break;

	case 0x26: /* LD H, imm */
		gb->cpu_reg.h = __gb_read(gb, gb->cpu_reg.pc++);
		break;

	case 0x27: /* DAA */
	{
		uint8_t n = jit_regfile_getn(gb->cpu_reg.nh);
		uint64_t value = jit_regfile_daa(gb->cpu_reg.nh, gb->cpu_reg.a, gb->cpu_reg.carry);
		gb->cpu_reg.carry = value >> 32;
		gb->cpu_reg.a = value & 0xFF;
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(n, 0);
		break;
	}

	case 0x28: /* JP Z, imm */
		if(!gb->cpu_reg.z)
		{
			int8_t temp = (int8_t) __gb_read(gb, gb->cpu_reg.pc++);
			gb->cpu_reg.pc += temp;
			inst_cycles += 4;
		}
		else
			gb->cpu_reg.pc++;

		break;

	case 0x29: /* ADD HL, HL */
	{
		uint_fast32_t temp = gb->cpu_reg.hl + gb->cpu_reg.hl;
		gb->cpu_reg.nh = jit_regfile_setnh(
			0,
			temp & 0x1000 ? 1 : 0
		);
		gb->cpu_reg.carry = temp >> 16;
		gb->cpu_reg.hl = (temp & 0x0000FFFF);
		break;
	}

	case 0x2A: /* LD A, (HL+) */
		gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.hl++);
		break;

	case 0x2B: /* DEC HL */
		gb->cpu_reg.hl--;
		break;

	case 0x2C: /* INC L */
		gb->cpu_reg.nh = jit_regfile_setnh_op(
			0, // n
			gb->cpu_reg.l, // a
			0, // b
			1 // c
		);
		gb->cpu_reg.l++;
		gb->cpu_reg.z = gb->cpu_reg.l;
		break;

	case 0x2D: /* DEC L */
		gb->cpu_reg.nh = jit_regfile_setnh_op(
			1, // n
			gb->cpu_reg.l, // a
			0, // b
			0x01 // c
		);
		gb->cpu_reg.l--;
		gb->cpu_reg.z = gb->cpu_reg.l;
		break;

	case 0x2E: /* LD L, imm */
		gb->cpu_reg.l = __gb_read(gb, gb->cpu_reg.pc++);
		break;

	case 0x2F: /* CPL */
		gb->cpu_reg.a = ~gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(1, 1);
		break;

	case 0x30: /* JP NC, imm */
		if(!gb->cpu_reg.carry)
		{
			int8_t temp = (int8_t) __gb_read(gb, gb->cpu_reg.pc++);
			gb->cpu_reg.pc += temp;
			inst_cycles += 4;
		}
		else
			gb->cpu_reg.pc++;

		break;

	case 0x31: /* LD SP, imm */
		gb->cpu_reg.sp = __gb_read(gb, gb->cpu_reg.pc++);
		gb->cpu_reg.sp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
		break;

	case 0x32: /* LD (HL), A */
		__gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.a);
		gb->cpu_reg.hl--;
		break;

	case 0x33: /* INC SP */
		gb->cpu_reg.sp++;
		break;

	case 0x34: /* INC (HL) */
	{
		uint8_t temp = __gb_read(gb, gb->cpu_reg.hl);
		gb->cpu_reg.nh = jit_regfile_setnh_op(
			0, // n
			temp, // a
			0, // b
			0x01 // c
		);
		gb->cpu_reg.z = (uint8_t)(temp + 1);
		__gb_write(gb, gb->cpu_reg.hl, temp + 1);
		break;
	}

	case 0x35: /* DEC (HL) */
	{
		uint8_t temp = __gb_read(gb, gb->cpu_reg.hl);
		gb->cpu_reg.nh = jit_regfile_setnh_op(
			1, // n
			temp, // a
			0, // b
			0x01 // c
		);
		--temp;
		gb->cpu_reg.z = temp;
		__gb_write(gb, gb->cpu_reg.hl, temp);
		break;
	}

	case 0x36: /* LD (HL), imm */
		__gb_write(gb, gb->cpu_reg.hl, __gb_read(gb, gb->cpu_reg.pc++));
		break;

	case 0x37: /* SCF */
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = 1;
		break;

	case 0x38: /* JP C, imm */
		if(gb->cpu_reg.carry)
		{
			int8_t temp = (int8_t) __gb_read(gb, gb->cpu_reg.pc++);
			gb->cpu_reg.pc += temp;
			inst_cycles += 4;
		}
		else
			gb->cpu_reg.pc++;

		break;

	case 0x39: /* ADD HL, SP */
	{
		uint_fast32_t temp = gb->cpu_reg.hl + gb->cpu_reg.sp;
		gb->cpu_reg.nh = jit_regfile_setnh(
			0,
			((gb->cpu_reg.hl & 0xFFF) + (gb->cpu_reg.sp & 0xFFF)) & 0x1000 ? 1 : 0
		);
		gb->cpu_reg.carry = temp & 0x10000 ? 1 : 0;
		gb->cpu_reg.hl = (uint16_t)temp;
		break;
	}

	case 0x3A: /* LD A, (HL) */
		gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.hl--);
		break;

	case 0x3B: /* DEC SP */
		gb->cpu_reg.sp--;
		break;

	case 0x3C: /* INC A */
		gb->cpu_reg.nh = jit_regfile_setnh_op(
			0, // n
			gb->cpu_reg.a, // a
			0, // b
			1 // c
		);
		gb->cpu_reg.a++;
		gb->cpu_reg.z = gb->cpu_reg.a;
		break;

	case 0x3D: /* DEC A */
		gb->cpu_reg.nh = jit_regfile_setnh_op(
			1, // n
			gb->cpu_reg.a, // a
			0, // b
			0x01 // c
		);
		gb->cpu_reg.a--;
		gb->cpu_reg.z = gb->cpu_reg.a;
		break;

	case 0x3E: /* LD A, imm */
		gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.pc++);
		break;

	case 0x3F: /* CCF */
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = ~gb->cpu_reg.carry;
		break;

	case 0x40: /* LD B, B */
		break;

	case 0x41: /* LD B, C */
		gb->cpu_reg.b = gb->cpu_reg.c;
		break;

	case 0x42: /* LD B, D */
		gb->cpu_reg.b = gb->cpu_reg.d;
		break;

	case 0x43: /* LD B, E */
		gb->cpu_reg.b = gb->cpu_reg.e;
		break;

	case 0x44: /* LD B, H */
		gb->cpu_reg.b = gb->cpu_reg.h;
		break;

	case 0x45: /* LD B, L */
		gb->cpu_reg.b = gb->cpu_reg.l;
		break;

	case 0x46: /* LD B, (HL) */
		gb->cpu_reg.b = __gb_read(gb, gb->cpu_reg.hl);
		break;

	case 0x47: /* LD B, A */
		gb->cpu_reg.b = gb->cpu_reg.a;
		break;

	case 0x48: /* LD C, B */
		gb->cpu_reg.c = gb->cpu_reg.b;
		break;

	case 0x49: /* LD C, C */
		break;

	case 0x4A: /* LD C, D */
		gb->cpu_reg.c = gb->cpu_reg.d;
		break;

	case 0x4B: /* LD C, E */
		gb->cpu_reg.c = gb->cpu_reg.e;
		break;

	case 0x4C: /* LD C, H */
		gb->cpu_reg.c = gb->cpu_reg.h;
		break;

	case 0x4D: /* LD C, L */
		gb->cpu_reg.c = gb->cpu_reg.l;
		break;

	case 0x4E: /* LD C, (HL) */
		gb->cpu_reg.c = __gb_read(gb, gb->cpu_reg.hl);
		break;

	case 0x4F: /* LD C, A */
		gb->cpu_reg.c = gb->cpu_reg.a;
		break;

	case 0x50: /* LD D, B */
		gb->cpu_reg.d = gb->cpu_reg.b;
		break;

	case 0x51: /* LD D, C */
		gb->cpu_reg.d = gb->cpu_reg.c;
		break;

	case 0x52: /* LD D, D */
		break;

	case 0x53: /* LD D, E */
		gb->cpu_reg.d = gb->cpu_reg.e;
		break;

	case 0x54: /* LD D, H */
		gb->cpu_reg.d = gb->cpu_reg.h;
		break;

	case 0x55: /* LD D, L */
		gb->cpu_reg.d = gb->cpu_reg.l;
		break;

	case 0x56: /* LD D, (HL) */
		gb->cpu_reg.d = __gb_read(gb, gb->cpu_reg.hl);
		break;

	case 0x57: /* LD D, A */
		gb->cpu_reg.d = gb->cpu_reg.a;
		break;

	case 0x58: /* LD E, B */
		gb->cpu_reg.e = gb->cpu_reg.b;
		break;

	case 0x59: /* LD E, C */
		gb->cpu_reg.e = gb->cpu_reg.c;
		break;

	case 0x5A: /* LD E, D */
		gb->cpu_reg.e = gb->cpu_reg.d;
		break;

	case 0x5B: /* LD E, E */
		break;

	case 0x5C: /* LD E, H */
		gb->cpu_reg.e = gb->cpu_reg.h;
		break;

	case 0x5D: /* LD E, L */
		gb->cpu_reg.e = gb->cpu_reg.l;
		break;

	case 0x5E: /* LD E, (HL) */
		gb->cpu_reg.e = __gb_read(gb, gb->cpu_reg.hl);
		break;

	case 0x5F: /* LD E, A */
		gb->cpu_reg.e = gb->cpu_reg.a;
		break;

	case 0x60: /* LD H, B */
		gb->cpu_reg.h = gb->cpu_reg.b;
		break;

	case 0x61: /* LD H, C */
		gb->cpu_reg.h = gb->cpu_reg.c;
		break;

	case 0x62: /* LD H, D */
		gb->cpu_reg.h = gb->cpu_reg.d;
		break;

	case 0x63: /* LD H, E */
		gb->cpu_reg.h = gb->cpu_reg.e;
		break;

	case 0x64: /* LD H, H */
		break;

	case 0x65: /* LD H, L */
		gb->cpu_reg.h = gb->cpu_reg.l;
		break;

	case 0x66: /* LD H, (HL) */
		gb->cpu_reg.h = __gb_read(gb, gb->cpu_reg.hl);
		break;

	case 0x67: /* LD H, A */
		gb->cpu_reg.h = gb->cpu_reg.a;
		break;

	case 0x68: /* LD L, B */
		gb->cpu_reg.l = gb->cpu_reg.b;
		break;

	case 0x69: /* LD L, C */
		gb->cpu_reg.l = gb->cpu_reg.c;
		break;

	case 0x6A: /* LD L, D */
		gb->cpu_reg.l = gb->cpu_reg.d;
		break;

	case 0x6B: /* LD L, E */
		gb->cpu_reg.l = gb->cpu_reg.e;
		break;

	case 0x6C: /* LD L, H */
		gb->cpu_reg.l = gb->cpu_reg.h;
		break;

	case 0x6D: /* LD L, L */
		break;

	case 0x6E: /* LD L, (HL) */
		gb->cpu_reg.l = __gb_read(gb, gb->cpu_reg.hl);
		break;

	case 0x6F: /* LD L, A */
		gb->cpu_reg.l = gb->cpu_reg.a;
		break;

	case 0x70: /* LD (HL), B */
		__gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.b);
		break;

	case 0x71: /* LD (HL), C */
		__gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.c);
		break;

	case 0x72: /* LD (HL), D */
		__gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.d);
		break;

	case 0x73: /* LD (HL), E */
		__gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.e);
		break;

	case 0x74: /* LD (HL), H */
		__gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.h);
		break;

	case 0x75: /* LD (HL), L */
		__gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.l);
		break;

	case 0x76: /* HALT */
		/* TODO: Emulate HALT bug? */
		gb->gb_halt = 1;
		break;

	case 0x77: /* LD (HL), A */
		__gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.a);
		break;

	case 0x78: /* LD A, B */
		gb->cpu_reg.a = gb->cpu_reg.b;
		break;

	case 0x79: /* LD A, C */
		gb->cpu_reg.a = gb->cpu_reg.c;
		break;

	case 0x7A: /* LD A, D */
		gb->cpu_reg.a = gb->cpu_reg.d;
		break;

	case 0x7B: /* LD A, E */
		gb->cpu_reg.a = gb->cpu_reg.e;
		break;

	case 0x7C: /* LD A, H */
		gb->cpu_reg.a = gb->cpu_reg.h;
		break;

	case 0x7D: /* LD A, L */
		gb->cpu_reg.a = gb->cpu_reg.l;
		break;

	case 0x7E: /* LD A, (HL) */
		gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.hl);
		break;

	case 0x7F: /* LD A, A */
		break;

	case 0x80: /* ADD A, B */
	{
		__cpu_add8(gb, 0, gb->cpu_reg.b);
		break;
	}

	case 0x81: /* ADD A, C */
	{
		__cpu_add8(gb, 0, gb->cpu_reg.c);
		break;
	}

	case 0x82: /* ADD A, D */
	{
		__cpu_add8(gb, 0, gb->cpu_reg.d);
		break;
	}

	case 0x83: /* ADD A, E */
	{
		__cpu_add8(gb, 0, gb->cpu_reg.e);
		break;
	}

	case 0x84: /* ADD A, H */
	{
		__cpu_add8(gb, 0, gb->cpu_reg.h);
		break;
	}

	case 0x85: /* ADD A, L */
	{
		__cpu_add8(gb, 0, gb->cpu_reg.l);
		break;
	}

	case 0x86: /* ADD A, (HL) */
	{
		uint8_t val = __gb_read(gb, gb->cpu_reg.hl);
		__cpu_add8(gb, 0, val);
		break;
	}

	case 0x87: /* ADD A, A */
	{
		// TODO: optimize?
		__cpu_add8(gb, 0, gb->cpu_reg.a);
		break;
	}

	case 0x88: /* ADC A, B */
	{
		__cpu_add8(gb, 1, gb->cpu_reg.b);
		break;
	}

	case 0x89: /* ADC A, C */
	{
		__cpu_add8(gb, 1, gb->cpu_reg.c);
		break;
	}

	case 0x8A: /* ADC A, D */
	{
		__cpu_add8(gb, 1, gb->cpu_reg.d);
		break;
	}

	case 0x8B: /* ADC A, E */
	{
		__cpu_add8(gb, 1, gb->cpu_reg.e);
		break;
	}

	case 0x8C: /* ADC A, H */
	{
		__cpu_add8(gb, 1, gb->cpu_reg.h);
		break;
	}

	case 0x8D: /* ADC A, L */
	{
		__cpu_add8(gb, 1, gb->cpu_reg.l);
		break;
	}

	case 0x8E: /* ADC A, (HL) */
	{
		uint8_t val = __gb_read(gb, gb->cpu_reg.hl);
		__cpu_add8(gb, 1, val);
		break;
	}

	case 0x8F: /* ADC A, A */
	{
		// TODO: optimize?
		__cpu_add8(gb, 1, gb->cpu_reg.a);
		break;
	}

	case 0x90: /* SUB B */
	{
		__cpu_sub8(gb, 0, gb->cpu_reg.b);
		break;
	}

	case 0x91: /* SUB C */
	{
		__cpu_sub8(gb, 0, gb->cpu_reg.c);
		break;
	}

	case 0x92: /* SUB D */
	{
		__cpu_sub8(gb, 0, gb->cpu_reg.d);
		break;
	}

	case 0x93: /* SUB E */
	{
		__cpu_sub8(gb, 0, gb->cpu_reg.e);
		break;
	}

	case 0x94: /* SUB H */
	{
		__cpu_sub8(gb, 0, gb->cpu_reg.h);
		break;
	}

	case 0x95: /* SUB L */
	{
		__cpu_sub8(gb, 0, gb->cpu_reg.l);
		break;
	}

	case 0x96: /* SUB (HL) */
	{
		uint8_t val = __gb_read(gb, gb->cpu_reg.hl);
		__cpu_sub8(gb, 0, val);
		break;
	}

	case 0x97: /* SUB A */
		gb->cpu_reg.a = 0;
		gb->cpu_reg.z = 0;
		gb->cpu_reg.nh = jit_regfile_setnh(1, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0x98: /* SBC A, B */
	{
		__cpu_sub8(gb, 1, gb->cpu_reg.a);
		break;
	}

	case 0x99: /* SBC A, C */
	{
		__cpu_sub8(gb, 1, gb->cpu_reg.c);
		break;
	}

	case 0x9A: /* SBC A, D */
	{
		__cpu_sub8(gb, 1, gb->cpu_reg.d);
		break;
	}

	case 0x9B: /* SBC A, E */
	{
		__cpu_sub8(gb, 1, gb->cpu_reg.e);
		break;
	}

	case 0x9C: /* SBC A, H */
	{
		__cpu_sub8(gb, 1, gb->cpu_reg.h);
		break;
	}

	case 0x9D: /* SBC A, L */
	{
		__cpu_sub8(gb, 1, gb->cpu_reg.l);
		break;
	}

	case 0x9E: /* SBC A, (HL) */
	{
		uint8_t val = __gb_read(gb, gb->cpu_reg.hl);
		__cpu_sub8(gb, 1, val);
		break;
	}

	case 0x9F: /* SBC A, A */
		gb->cpu_reg.a = gb->cpu_reg.carry ? 0xFF : 0x00;
		gb->cpu_reg.z = gb->cpu_reg.carry;
		gb->cpu_reg.nh = jit_regfile_setnh(1, gb->cpu_reg.carry);
		break;

	case 0xA0: /* AND B */
		gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.b;
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 1);
		gb->cpu_reg.carry = 0;
		break;

	case 0xA1: /* AND C */
		gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.c;
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 1);
		gb->cpu_reg.carry = 0;
		break;

	case 0xA2: /* AND D */
		gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.d;
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 1);
		gb->cpu_reg.carry = 0;
		break;

	case 0xA3: /* AND E */
		gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.e;
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 1);
		gb->cpu_reg.carry = 0;
		break;

	case 0xA4: /* AND H */
		gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.h;
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 1);
		gb->cpu_reg.carry = 0;
		break;

	case 0xA5: /* AND L */
		gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.l;
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 1);
		gb->cpu_reg.carry = 0;
		break;

	case 0xA6: /* AND B */
		gb->cpu_reg.a = gb->cpu_reg.a & __gb_read(gb, gb->cpu_reg.hl);
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 1);
		gb->cpu_reg.carry = 0;
		break;

	case 0xA7: /* AND A */
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 1);
		gb->cpu_reg.carry = 0;
		break;

	case 0xA8: /* XOR B */
		gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.b;
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0xA9: /* XOR C */
		gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.c;
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0xAA: /* XOR D */
		gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.d;
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0xAB: /* XOR E */
		gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.e;
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0xAC: /* XOR H */
		gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.h;
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0xAD: /* XOR L */
		gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.l;
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0xAE: /* XOR (HL) */
		gb->cpu_reg.a = gb->cpu_reg.a ^ __gb_read(gb, gb->cpu_reg.hl);
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0xAF: /* XOR A */
		gb->cpu_reg.a = 0x00;
		gb->cpu_reg.z = 0;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0xB0: /* OR B */
		gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.b;
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0xB1: /* OR C */
		gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.c;
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0xB2: /* OR D */
		gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.d;
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0xB3: /* OR E */
		gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.e;
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0xB4: /* OR H */
		gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.h;
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0xB5: /* OR L */
		gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.l;
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0xB6: /* OR (HL) */
		gb->cpu_reg.a = gb->cpu_reg.a | __gb_read(gb, gb->cpu_reg.hl);
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0xB7: /* OR A */
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0xB8: /* CP B */
	{
		__cpu_cmp8(gb, 0, gb->cpu_reg.b);
		break;
	}

	case 0xB9: /* CP C */
	{
		__cpu_cmp8(gb, 0, gb->cpu_reg.c);
		break;
	}

	case 0xBA: /* CP D */
	{
		__cpu_cmp8(gb, 0, gb->cpu_reg.d);
		break;
	}

	case 0xBB: /* CP E */
	{
		__cpu_cmp8(gb, 0, gb->cpu_reg.e);
		break;
	}

	case 0xBC: /* CP H */
	{
		__cpu_cmp8(gb, 0, gb->cpu_reg.h);
		break;
	}

	case 0xBD: /* CP L */
	{
		__cpu_cmp8(gb, 0, gb->cpu_reg.l);
		break;
	}

	case 0xBE: /* CP (HL) */
	{
		uint8_t val = __gb_read(gb, gb->cpu_reg.hl);
		__cpu_cmp8(gb, 0, val);
		break;
	}

	case 0xBF: /* CP A */
		gb->cpu_reg.z = 0;
		gb->cpu_reg.nh = jit_regfile_setnh(1, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0xC0: /* RET NZ */
		if(gb->cpu_reg.z)
		{
			gb->cpu_reg.pc = __gb_read(gb, gb->cpu_reg.sp++);
			gb->cpu_reg.pc |= __gb_read(gb, gb->cpu_reg.sp++) << 8;
			inst_cycles += 12;
		}

		break;

	case 0xC1: /* POP BC */
		gb->cpu_reg.c = __gb_read(gb, gb->cpu_reg.sp++);
		gb->cpu_reg.b = __gb_read(gb, gb->cpu_reg.sp++);
		break;

	case 0xC2: /* JP NZ, imm */
		if(gb->cpu_reg.z)
		{
			uint16_t temp = __gb_read(gb, gb->cpu_reg.pc++);
			temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
			gb->cpu_reg.pc = temp;
			inst_cycles += 4;
		}
		else
			gb->cpu_reg.pc += 2;

		break;

	case 0xC3: /* JP imm */
	{
		uint16_t temp = __gb_read(gb, gb->cpu_reg.pc++);
		temp |= __gb_read(gb, gb->cpu_reg.pc) << 8;
		gb->cpu_reg.pc = temp;
		break;
	}

	case 0xC4: /* CALL NZ imm */
		if(gb->cpu_reg.z)
		{
			uint16_t temp = __gb_read(gb, gb->cpu_reg.pc++);
			temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
			gb->cpu_reg.pc = temp;
			inst_cycles += 12;
		}
		else
			gb->cpu_reg.pc += 2;

		break;

	case 0xC5: /* PUSH BC */
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.b);
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.c);
		break;

	case 0xC6: /* ADD A, imm */
	{
		uint8_t imm = __gb_read(gb, gb->cpu_reg.pc++);
		__cpu_add8(gb, 0, imm);
		break;
	}

	case 0xC7: /* RST 0x0000 */
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
		gb->cpu_reg.pc = 0x0000;
		break;

	case 0xC8: /* RET Z */
		if(!gb->cpu_reg.z)
		{
			uint16_t temp = __gb_read(gb, gb->cpu_reg.sp++);
			temp |= __gb_read(gb, gb->cpu_reg.sp++) << 8;
			gb->cpu_reg.pc = temp;
			inst_cycles += 12;
		}

		break;

	case 0xC9: /* RET */
	{
		uint16_t temp = __gb_read(gb, gb->cpu_reg.sp++);
		temp |= __gb_read(gb, gb->cpu_reg.sp++) << 8;
		gb->cpu_reg.pc = temp;
		break;
	}

	case 0xCA: /* JP Z, imm */
		if(!gb->cpu_reg.z)
		{
			uint16_t temp = __gb_read(gb, gb->cpu_reg.pc++);
			temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
			gb->cpu_reg.pc = temp;
			inst_cycles += 4;
		}
		else
			gb->cpu_reg.pc += 2;

		break;

	case 0xCB: /* CB INST */
		inst_cycles = __gb_execute_cb(gb);
		break;

	case 0xCC: /* CALL Z, imm */
		if(!gb->cpu_reg.z)
		{
			uint16_t temp = __gb_read(gb, gb->cpu_reg.pc++);
			temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
			gb->cpu_reg.pc = temp;
			inst_cycles += 12;
		}
		else
			gb->cpu_reg.pc += 2;

		break;

	case 0xCD: /* CALL imm */
	{
		uint16_t addr = __gb_read(gb, gb->cpu_reg.pc++);
		addr |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
		gb->cpu_reg.pc = addr;
	}
	break;

	case 0xCE: /* ADC A, imm */
	{
		uint8_t imm = __gb_read(gb, gb->cpu_reg.pc++);
		__cpu_add8(gb, 1, imm);
		break;
	}

	case 0xCF: /* RST 0x0008 */
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
		gb->cpu_reg.pc = 0x0008;
		break;

	case 0xD0: /* RET NC */
		if(!gb->cpu_reg.carry)
		{
			uint16_t temp = __gb_read(gb, gb->cpu_reg.sp++);
			temp |= __gb_read(gb, gb->cpu_reg.sp++) << 8;
			gb->cpu_reg.pc = temp;
			inst_cycles += 12;
		}

		break;

	case 0xD1: /* POP DE */
		gb->cpu_reg.e = __gb_read(gb, gb->cpu_reg.sp++);
		gb->cpu_reg.d = __gb_read(gb, gb->cpu_reg.sp++);
		break;

	case 0xD2: /* JP NC, imm */
		if(!gb->cpu_reg.carry)
		{
			uint16_t temp =  __gb_read(gb, gb->cpu_reg.pc++);
			temp |=  __gb_read(gb, gb->cpu_reg.pc++) << 8;
			gb->cpu_reg.pc = temp;
			inst_cycles += 4;
		}
		else
			gb->cpu_reg.pc += 2;

		break;

	case 0xD4: /* CALL NC, imm */
		if(!gb->cpu_reg.carry)
		{
			uint16_t temp = __gb_read(gb, gb->cpu_reg.pc++);
			temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
			gb->cpu_reg.pc = temp;
			inst_cycles += 12;
		}
		else
			gb->cpu_reg.pc += 2;

		break;

	case 0xD5: /* PUSH DE */
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.d);
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.e);
		break;

	case 0xD6: /* SUB A, imm */
	{
		uint8_t imm = __gb_read(gb, gb->cpu_reg.pc++);
		__cpu_sub8(gb, 0, imm);
		break;
	}

	case 0xD7: /* RST 0x0010 */
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
		gb->cpu_reg.pc = 0x0010;
		break;

	case 0xD8: /* RET C */
		if(gb->cpu_reg.carry)
		{
			uint16_t temp = __gb_read(gb, gb->cpu_reg.sp++);
			temp |= __gb_read(gb, gb->cpu_reg.sp++) << 8;
			gb->cpu_reg.pc = temp;
			inst_cycles += 12;
		}

		break;

	case 0xD9: /* RETI */
	{
		uint16_t temp = __gb_read(gb, gb->cpu_reg.sp++);
		temp |= __gb_read(gb, gb->cpu_reg.sp++) << 8;
		gb->cpu_reg.pc = temp;
		gb->cpu_reg.ime = 1;
	}
	break;

	case 0xDA: /* JP C, imm */
		if(gb->cpu_reg.carry)
		{
			uint16_t addr = __gb_read(gb, gb->cpu_reg.pc++);
			addr |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
			gb->cpu_reg.pc = addr;
			inst_cycles += 4;
		}
		else
			gb->cpu_reg.pc += 2;

		break;

	case 0xDC: /* CALL C, imm */
		if(gb->cpu_reg.carry)
		{
			uint16_t temp = __gb_read(gb, gb->cpu_reg.pc++);
			temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
			gb->cpu_reg.pc = temp;
			inst_cycles += 12;
		}
		else
			gb->cpu_reg.pc += 2;

		break;

	case 0xDE: /* SBC A, imm */
	{
		uint8_t imm = __gb_read(gb, gb->cpu_reg.pc++);
		__cpu_sub8(gb, 1, imm);
		break;
	}

	case 0xDF: /* RST 0x0018 */
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
		gb->cpu_reg.pc = 0x0018;
		break;

	case 0xE0: /* LD (0xFF00+imm), A */
		__gb_write(gb, 0xFF00 | __gb_read(gb, gb->cpu_reg.pc++),
			   gb->cpu_reg.a);
		break;

	case 0xE1: /* POP HL */
		gb->cpu_reg.l = __gb_read(gb, gb->cpu_reg.sp++);
		gb->cpu_reg.h = __gb_read(gb, gb->cpu_reg.sp++);
		break;

	case 0xE2: /* LD (C), A */
		__gb_write(gb, 0xFF00 | gb->cpu_reg.c, gb->cpu_reg.a);
		break;

	case 0xE5: /* PUSH HL */
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.h);
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.l);
		break;

	case 0xE6: /* AND imm */
		/* TODO: Optimisation? */
		gb->cpu_reg.a = gb->cpu_reg.a & __gb_read(gb, gb->cpu_reg.pc++);
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 1);
		gb->cpu_reg.carry = 0;
		break;

	case 0xE7: /* RST 0x0020 */
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
		gb->cpu_reg.pc = 0x0020;
		break;

	case 0xE8: /* ADD SP, imm */
	{
		int8_t offset = (int8_t) __gb_read(gb, gb->cpu_reg.pc++);
		/* TODO: Move flag assignments for optimisation. */
		gb->cpu_reg.z = 1;
		gb->cpu_reg.nh = jit_regfile_setnh(
			0,
			((gb->cpu_reg.sp & 0xF) + (offset & 0xF) > 0xF) ? 1 : 0
		);
		gb->cpu_reg.carry = ((gb->cpu_reg.sp & 0xFF) + (offset & 0xFF) > 0xFF);
		gb->cpu_reg.sp += offset;
		break;
	}

	case 0xE9: /* JP (HL) */
		gb->cpu_reg.pc = gb->cpu_reg.hl;
		break;

	case 0xEA: /* LD (imm), A */
	{
		uint16_t addr = __gb_read(gb, gb->cpu_reg.pc++);
		addr |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
		__gb_write(gb, addr, gb->cpu_reg.a);
		break;
	}

	case 0xEE: /* XOR imm */
		gb->cpu_reg.a = gb->cpu_reg.a ^ __gb_read(gb, gb->cpu_reg.pc++);
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0xEF: /* RST 0x0028 */
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
		gb->cpu_reg.pc = 0x0028;
		break;

	case 0xF0: /* LD A, (0xFF00+imm) */
		gb->cpu_reg.a =
			__gb_read(gb, 0xFF00 | __gb_read(gb, gb->cpu_reg.pc++));
		break;

	case 0xF1: /* POP AF */
	{
		uint8_t temp_8 = __gb_read(gb, gb->cpu_reg.sp++);
		jit_regfile_set_f(&gb->cpu_reg, temp_8);
		gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.sp++);
		break;
	}

	case 0xF2: /* LD A, (C) */
		gb->cpu_reg.a = __gb_read(gb, 0xFF00 | gb->cpu_reg.c);
		break;

	case 0xF3: /* DI */
		gb->cpu_reg.ime = 0;
		break;

	case 0xF5: /* PUSH AF */
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.a);
		__gb_write(gb, --gb->cpu_reg.sp,
			   jit_regfile_p_get_f(&gb->cpu_reg));
		break;

	case 0xF6: /* OR imm */
		gb->cpu_reg.a = gb->cpu_reg.a | __gb_read(gb, gb->cpu_reg.pc++);
		gb->cpu_reg.z = gb->cpu_reg.a;
		gb->cpu_reg.nh = jit_regfile_setnh(0, 0);
		gb->cpu_reg.carry = 0;
		break;

	case 0xF7: /* PUSH AF */
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
		gb->cpu_reg.pc = 0x0030;
		break;

	case 0xF8: /* LD HL, SP+/-imm */
	{
		/* Taken from SameBoy, which is released under MIT Licence. */
		int8_t offset = (int8_t) __gb_read(gb, gb->cpu_reg.pc++);
		gb->cpu_reg.hl = gb->cpu_reg.sp + offset;
		gb->cpu_reg.z = 1;
		gb->cpu_reg.nh = jit_regfile_setnh(
			0,
			((gb->cpu_reg.sp & 0xF) + (offset & 0xF) > 0xF) ? 1 : 0
		);
		gb->cpu_reg.carry = ((gb->cpu_reg.sp & 0xFF) + (offset & 0xFF) > 0xFF) ? 1 :
				       0;
		break;
	}

	case 0xF9: /* LD SP, HL */
		gb->cpu_reg.sp = gb->cpu_reg.hl;
		break;

	case 0xFA: /* LD A, (imm) */
	{
		uint16_t addr = __gb_read(gb, gb->cpu_reg.pc++);
		addr |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
		gb->cpu_reg.a = __gb_read(gb, addr);
		break;
	}

	case 0xFB: /* EI */
		gb->cpu_reg.ime = 1;
		break;

	case 0xFE: /* CP imm */
	{
		uint8_t temp_8 = __gb_read(gb, gb->cpu_reg.pc++);
		__cpu_cmp8(gb, 0, temp_8);
		break;
	}

	case 0xFF: /* RST 0x0038 */
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
		__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
		gb->cpu_reg.pc = 0x0038;
		break;

	default:
		(gb->gb_error)(gb, GB_INVALID_OPCODE, opcode);
	}
	return inst_cycles;
}


uint8_t __gb_step_chunked(struct gb_s *gb)
{
    // take 3 cpu steps at a time
    uint8_t inst_cycles = 0;
    for (size_t i = 0; i < CPU_STEP_CHUNK; ++i)
    {
        inst_cycles += __gb_step_cpu(gb);
    }
    return inst_cycles;
}