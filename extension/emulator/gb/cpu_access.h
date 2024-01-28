#if 0
static uint8_t $A;
static uint32_t $Z;
static uint32_t $NH;
static uint32_t $CR;
static uint16_t $PC;
#elif defined(__arm__)
#define R_A "r5"
#define R_NH "r6"
#define R_Z "r7"
#define R_CR "r8"
#define R_PC "r9"
register uint8_t  $A  asm("r5");
register uint32_t $NH asm("r6");
register uint32_t $Z  asm("r7");
register uint32_t $CR asm("r8");
register uint16_t $PC asm("r9");
//register uint16_t BC asm("r6");
//register uint16_t DE asm("r7");
//register uint16_t F asm("r8");
//register uint16_t Z  asm("r9");
//register uint16_t NH asm("r10");
//register uint16_t SP asm("r11");
#else
register uint8_t  $A  asm("bl");
register uint32_t $NH asm("r12");
register uint32_t $Z  asm("r13");
register uint32_t $CR asm("r14");
register uint16_t $PC asm("r15");
//register uint16_t BC asm("r9d");
//register uint16_t DE asm("r10d");
//register uint16_t F asm("r11d");
//register uint16_t Z  asm("r12d");
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
#define SET_REG_H(x) $HL = ((x) << 8) | ($HL & 0x00FF);
#define GET_REG_H(x) (($HL) >> 8)
#define SET_REG_L(x) $HL = ((x) & 0xFF) | ($HL & 0xFF00);
#define GET_REG_L(x) (($HL) & 0xFF)
#define SET_REGF_Z(x) $Z = (x);
#define GET_REGF_Z() ($Z)
#define SET_REGF_N(x) $NH = ($NH & ~1) | (x);
#define GET_REGF_N() ($NH & 1)
#define SET_REGF_H(x) $NH = ($NH & ~0x10) | ((x) << 4);
#define GET_REGF_H() (($NH >> 4) & 1)
#define SET_REGF_C(x) $CR = (x);
#define GET_REGF_C() ($CR)

static inline void load_regs(struct cpu_registers_s *regs)
{
    #if ARMASM
    __asm__(
        "ldm %[regs], {" R_A ", " R_NH ", " R_Z ", " R_CR ", " R_PC "}"
        :
        : [regs] "r" (regs)
    );
    #else
	$A = regs->a;
    $NH = regs->nh;
    $Z = regs->z;
    $CR = regs->cr;
	$PC = regs->pc;
    #endif
    //$HL = regs->hl;
	//$BC = regs->bc;
	//$DE = regs->de;
	//$SP = regs->sp
}

static inline void store_regs(struct cpu_registers_s *regs)
{
    #if ARMASM
    __asm__(
        "stm %[regs], {" R_A ", " R_NH ", " R_Z ", " R_CR ", " R_PC "}"
        :
        : [regs] "r" (regs)
    );
    #else
	regs->a = $A;
    regs->nh = $NH;
    regs->z = $Z;
    regs->cr = $CR;
	regs->pc = $PC;
    #endif
    //regs->hl = $HL;
	//regs->bc = $BC;
	//regs->de = $DE;
	//regs->sp = $SP;
}

#define $HL regs->hl