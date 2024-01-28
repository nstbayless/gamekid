#if 0
static uint8_t $A;
static uint16_t $PC;
#elif defined(__arm__)
register uint8_t $A  asm("r5");
register uint16_t $PC asm("r6");
//register uint16_t HL asm("r5");
//register uint16_t BC asm("r6");
//register uint16_t DE asm("r7");
//register uint16_t F asm("r8");
//register uint16_t Z  asm("r9");
//register uint16_t NH asm("r10");
//register uint16_t SP asm("r11");
#else
register uint8_t $A  asm("bl");
register uint16_t $PC asm("r15");
//register uint16_t HL asm("r8d");
//register uint16_t BC asm("r9d");
//register uint16_t DE asm("r10d");
//register uint16_t F asm("r11d");
//register uint16_t Z  asm("r12d");
//register uint16_t NH asm("r13d");
//register uint16_t SP asm("r14d");
#endif

#define SET_REG_B(x) regs->b = (x);
#define GET_REG_B(x) (regs->b)
#define SET_REG_C(x) regs->c = (x);
#define GET_REG_C(x) (regs->c)
#define SET_REG_D(x) regs->d = (x);
#define GET_REG_D(x) (regs->d)
#define SET_REG_E(x) regs->e = (x);
#define GET_REG_E(x) (regs->e)
#define SET_REG_H(x) regs->h = (x);
#define GET_REG_H(x) (regs->h)
#define SET_REG_L(x) regs->l = (x);
#define GET_REG_L(x) (regs->l)
#define SET_REGF_Z(x) regs->f_bits.z = (x);
#define GET_REGF_Z() (regs->f_bits.z)
#define SET_REGF_N(x) regs->f_bits.n = (x);
#define GET_REGF_N() (regs->f_bits.n)
#define SET_REGF_H(x) regs->f_bits.h = (x);
#define GET_REGF_H() (regs->f_bits.h)
#define SET_REGF_C(x) regs->f_bits.c = (x);
#define GET_REGF_C() (regs->f_bits.c)

static inline void load_regs(struct cpu_registers_s *regs)
{
	$A = regs->a;
	$PC = regs->pc;
	//HL = regs->hl;
	//BC = regs->bc;
	//DE = regs->de;
	//F = regs->f;
	//SP = regs->sp;
}

static inline void store_regs(struct cpu_registers_s *regs)
{
	regs->a = $A;
	regs->pc = $PC;
	//regs->hl = HL;
	//regs->bc = BC;
	//regs->de = DE;
	//regs->f = F;
	//regs->sp = SP;
}