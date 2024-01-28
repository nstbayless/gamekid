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