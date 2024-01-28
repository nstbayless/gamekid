#pragma once

#ifndef TARGET_QEMU
#include "pd_api.h"
#endif

#include <stdint.h>
#include <stdbool.h>

// note: assumes little-endian

#define JIT_ZNH_BIT_N 0
#define JIT_ZNH_BIT_H 4
typedef struct jit_regfile_t
{
    // we store as 32-bit instead of 16 for better alignment properties.
    uint32_t a;
    
    union
    {
        uint32_t bc;
        struct {
            uint8_t c;
            uint8_t b;
        };
    };
    
    union
    {
        uint32_t de;
        struct {
            uint8_t e;
            uint8_t d;
        };
    };
    
    union
    {
        uint32_t hl;
        struct {
            uint8_t l;
            uint8_t h;
        };
    };
    
    // bit 0 is 1 iff carry is set
    // bits 1-7 always 0
    uint32_t carry;
    
    // 0 if set
    // any nonzero value if unset
    uint32_t z;
    
    // nh is stored lazily in an unusual format:
    // bits 0-4, 6-7: garbage (could be 0 or 1)
    // bit 5: n
    // byte 1: operand c (carry in; in addition to 0 and 1, can also be 0x10; if c is 0x10 and a and b are 0, then h is set.)
    // byte 2: operand b
    // byte 3: operand a
    // h is implicit: it is 1 if (a&0x0f + b&0x0f + c)&0x10 and n==0
    //                it is 1 if (a&0x0f - b&0x0f - c)&0x10 and n==1
    uint32_t nh;
    uint32_t sp;

    uint32_t pc;
    uint32_t ime;
} jit_regfile_t;

static inline bool jit_regfile_getn(uint32_t nh)
{
    return (nh >> JIT_ZNH_BIT_N) & 1;
}

static inline bool jit_regfile_geth(uint32_t nh)
{
    return (nh >> JIT_ZNH_BIT_H) & 1;
}

// returns corrected value in lower 8 bits (r0)
// returns new value of carry in bit 32 (r1)
// all other bits are clear.
// remember to set Z=* and H=0 after calling this.
static inline uint64_t jit_regfile_daa(const uint32_t nh, uint8_t v, bool c)
{
    // adapted with reference to peanut_gb.h
    const uint8_t n = jit_regfile_getn(nh);
    const uint8_t h = jit_regfile_geth(nh);
    if (n)
    {
        if (h)
        {
            v = (v - 0x06) & 0xFF;
        }

        if (c)
        {
            v -= 0x60;
        }
    }
    else
    {
        if (h || (v & 0x0F) > 9)
        {
            v += 0x06;
        }

        if (c || v > 0x9F)
        {
            v += 0x60;
        }
    }

    if (v & 0x100)
    {
        c = 1;
    }
    
    return (v) | ((uint64_t)c << 32);
}

// can assign the result to the .nh field.
static inline uint32_t jit_regfile_setnh(bool n, bool h)
{
    return ((!!n) << JIT_ZNH_BIT_N) | ((!!h) << JIT_ZNH_BIT_H);
}

static inline uint8_t jit_regfile_get_f(uint32_t carry, uint32_t z, uint32_t nh)
{
    return ((!z) << 7) | (carry << 4) | jit_regfile_geth(nh) << 5 | (jit_regfile_getn(nh) << 6);
}

static inline uint8_t jit_regfile_p_get_f(jit_regfile_t* regfile)
{
    return jit_regfile_get_f(regfile->carry, regfile->z, regfile->nh);
}

static inline void jit_regfile_set_f(jit_regfile_t* regfile, uint8_t f)
{
    regfile->z = !(f >> 7);
    regfile->carry = (f >> 4) & 1;
    regfile->nh = jit_regfile_setnh(
        (f >> 6) & 1,
        (f >> 5) & 1
    );
}

static inline uint16_t jit_regfile_get_af(jit_regfile_t* regfile)
{
    uint8_t a = regfile->a;
    uint8_t f = jit_regfile_p_get_f(regfile);
    return (a << 8) | f;
}

static inline void jit_regfile_set_af(jit_regfile_t* regfile, uint16_t af)
{
    uint8_t a = af >> 8;
    uint8_t f = af & 0xff;
    jit_regfile_set_f(regfile, f);
    regfile->a = a;
}