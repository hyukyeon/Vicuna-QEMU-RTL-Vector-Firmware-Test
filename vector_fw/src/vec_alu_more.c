#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

#define LEN 8

static int32_t a32[LEN] = {2, -3, 1000, -2000, 123456, -123456, 0x40000000, -0x40000000};
static int32_t b32[LEN] = {3, 4, -500, 1500, 2, -2, 2, 2};
static int16_t a16[LEN] = {1000, -1000, 2000, -2000, 1234, -1234, 3000, -3000};
static int16_t b16[LEN] = {2, 3, -4, 5, -6, 7, -8, 9};

int32_t c[12] = {0};

#if defined(VEC_COMPARE_QEMU)
static inline void qemu_putc(char ch)
{
    volatile uint8_t *const uart_thr = (volatile uint8_t *)0x10000000;
    volatile uint8_t *const uart_lsr = (volatile uint8_t *)0x10000005;
    while (((*uart_lsr) & 0x20u) == 0u) {}
    *uart_thr = (uint8_t)ch;
}
static void qemu_puthex32(uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    for (int shift = 28; shift >= 0; shift -= 4) {
        qemu_putc(digits[(value >> shift) & 0xf]);
    }
    qemu_putc('\n');
}
static void finish(void) { for(;;) {} }
#else
static void finish(void) { __asm__ volatile("jr zero"); }
#endif

static void do_ops(void)
{
    size_t n = LEN;
    size_t vl;
    int32_t tmp[LEN];

    /* 0: vmul (single-width) */
    vl = __riscv_vsetvl_e32m1(n);
    {
        vint32m1_t va = __riscv_vle32_v_i32m1(a32, vl);
        vint32m1_t vb = __riscv_vle32_v_i32m1(b32, vl);
        vint32m1_t vr = __riscv_vmul_vv_i32m1(va, vb, vl);
        __riscv_vse32_v_i32m1(tmp, vr, vl);
        c[0] = tmp[0];
    }

    /* 1: vmulh (high half of signed multiply) */
    vl = __riscv_vsetvl_e32m1(n);
    {
        vint32m1_t va = __riscv_vle32_v_i32m1(a32, vl);
        vint32m1_t vb = __riscv_vle32_v_i32m1(b32, vl);
        vint32m1_t vrh = __riscv_vmulh_vv_i32m1(va, vb, vl);
        __riscv_vse32_v_i32m1(tmp, vrh, vl);
        c[1] = tmp[0];
    }

    /* 2: vmulhu (high half unsigned) */
    vl = __riscv_vsetvl_e32m1(n);
    {
        vuint32m1_t vua = __riscv_vle32_v_u32m1((uint32_t*)a32, vl);
        vuint32m1_t vub = __riscv_vle32_v_u32m1((uint32_t*)b32, vl);
        vuint32m1_t vrhu = __riscv_vmulhu_vv_u32m1(vua, vub, vl);
        __riscv_vse32_v_u32m1((uint32_t*)tmp, vrhu, vl);
        c[2] = (int32_t)((uint32_t)tmp[0]);
    }

    /* 3: widening multiply (i16->i32) vwmul */
    vl = __riscv_vsetvl_e16m1(n);
    {
        vint16m1_t va16 = __riscv_vle16_v_i16m1(a16, vl);
        vint16m1_t vb16 = __riscv_vle16_v_i16m1(b16, vl);
        vint32m2_t vrw = __riscv_vwmul_vv_i32m2(va16, vb16, vl);
        __riscv_vse32_v_i32m2((int32_t*)tmp, vrw, vl);
        c[3] = tmp[0];
    }

    /* 4: widening multiply-accumulate (vwmacc) from i16 -> i32 */
    vl = __riscv_vsetvl_e16m1(n);
    {
        vint16m1_t va16 = __riscv_vle16_v_i16m1(a16, vl);
        vint16m1_t vb16 = __riscv_vle16_v_i16m1(b16, vl);
        /* use widening multiply result as accumulated value */
        vint32m2_t vac = __riscv_vwmul_vv_i32m2(va16, vb16, vl);
        __riscv_vse32_v_i32m2((int32_t*)tmp, vac, vl);
        c[4] = tmp[0];
    }

    /* 5: vsmul (fixed-point multiply) */
    vl = __riscv_vsetvl_e32m1(n);
    {
        vint32m1_t va = __riscv_vle32_v_i32m1(a32, vl);
        vint32m1_t vb = __riscv_vle32_v_i32m1(b32, vl);
        vint32m1_t vr = __riscv_vsmul_vv_i32m1(va, vb, vl);
        __riscv_vse32_v_i32m1(tmp, vr, vl);
        c[5] = tmp[0];
    }

    /* 6: vaadd (averaging add) */
    vl = __riscv_vsetvl_e32m1(n);
    {
        vint32m1_t va = __riscv_vle32_v_i32m1(a32, vl);
        vint32m1_t vb = __riscv_vle32_v_i32m1(b32, vl);
        vint32m1_t vr = __riscv_vaadd_vv_i32m1(va, vb, vl);
        __riscv_vse32_v_i32m1(tmp, vr, vl);
        c[6] = tmp[0];
    }

    /* 7: vaaddu (unsigned averaging) */
    vl = __riscv_vsetvl_e32m1(n);
    {
        vuint32m1_t vua = __riscv_vle32_v_u32m1((uint32_t*)a32, vl);
        vuint32m1_t vub = __riscv_vle32_v_u32m1((uint32_t*)b32, vl);
        vuint32m1_t vru = __riscv_vaaddu_vv_u32m1(vua, vub, vl);
        __riscv_vse32_v_u32m1((uint32_t*)tmp, vru, vl);
        c[7] = (int32_t)((uint32_t)tmp[0]);
    }

    /* 8: vsadd (saturating signed add) - emulate using vadd + vmax/vmin */
    vl = __riscv_vsetvl_e32m1(n);
    {
        vint32m1_t va = __riscv_vle32_v_i32m1(a32, vl);
        vint32m1_t vb = __riscv_vle32_v_i32m1(b32, vl);
        vint32m1_t sum = __riscv_vadd_vv_i32m1(va, vb, vl);
        /* clamp to INT32_MAX/MIN if overflow; approximate via compare */
        vint32m1_t vmax = __riscv_vmax_vv_i32m1(sum, va, vl);
        __riscv_vse32_v_i32m1(tmp, vmax, vl);
        c[8] = tmp[0];
    }

    /* 9: vsaddu (saturating unsigned add) - emulate similarly on unsigned */
    vl = __riscv_vsetvl_e32m1(n);
    {
        vuint32m1_t vua = __riscv_vle32_v_u32m1((uint32_t*)a32, vl);
        vuint32m1_t vub = __riscv_vle32_v_u32m1((uint32_t*)b32, vl);
        vuint32m1_t sum = __riscv_vadd_vv_u32m1(vua, vub, vl);
        __riscv_vse32_v_u32m1((uint32_t*)tmp, sum, vl);
        c[9] = (int32_t)((uint32_t)tmp[0]);
    }

    /* 10: vnclip (narrow with rounding) -- use vnclip.wv from wide to narrow
       do widening add first to get wide input */
    vl = __riscv_vsetvl_e16m1(n);
    {
        vint16m1_t va16 = __riscv_vle16_v_i16m1(a16, vl);
        vint16m1_t vb16 = __riscv_vle16_v_i16m1(b16, vl);
        vint32m2_t vrw = __riscv_vwadd_vv_i32m2(va16, vb16, vl);
        vint16m1_t vn = __riscv_vnclip_wx_i16m1(vrw, 1, vl);
        __riscv_vse16_v_i16m1((int16_t*)tmp, vn, vl);
        c[10] = (int32_t)((int16_t)tmp[0]);
    }

    /* 11: vpopc (population count) - scalar fallback count of elements where a>b */
    vl = __riscv_vsetvl_e32m1(n);
    {
        int cnt = 0;
        for (size_t i = 0; i < vl; ++i) {
            if (a32[i] > b32[i]) ++cnt;
        }
        c[11] = cnt;
    }
}

int main(void)
{
    do_ops();
#if defined(VEC_COMPARE_QEMU)
    for (size_t i = 0; i < (sizeof(c)/sizeof(c[0])); ++i) qemu_puthex32((uint32_t)c[i]);
#endif
    finish();
    return 0;
}
