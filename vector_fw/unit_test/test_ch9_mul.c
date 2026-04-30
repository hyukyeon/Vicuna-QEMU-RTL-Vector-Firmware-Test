#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

#define LEN 4

static int32_t  a32[LEN * 2] = {1, 2, 3, 4, 0x10, 0x20, 0x30, 0x40};
static int16_t  a16[LEN * 2] = {1, 2, 3, 4, 0x10, 0x20, 0x30, 0x40};

int32_t vdata_start[128];

#if defined(VEC_COMPARE_QEMU)
static inline void qemu_putc(char ch)
{
    volatile uint8_t *const uart_thr = (volatile uint8_t *)0x10000000;
    volatile uint8_t *const uart_lsr = (volatile uint8_t *)0x10000005;
    while (((*uart_lsr) & 0x20u) == 0u) {}
    *uart_thr = (uint8_t)ch;
}
static void qemu_puthex32(uint32_t v)
{
    static const char d[] = "0123456789abcdef";
    for (int s = 28; s >= 0; s -= 4) qemu_putc(d[(v >> s) & 0xf]);
    qemu_putc('\n');
}
static void finish(void) { for (;;) {} }
#else
static void finish(void) { __asm__ volatile("jr zero"); }
#endif

static void do_ops(void)
{
    int32_t  *out = vdata_start;
    size_t    vl  = __riscv_vsetvl_e32m1(LEN);

    vint32m1_t  v1  = __riscv_vle32_v_i32m1(a32,       vl);
    vint32m1_t  v2  = __riscv_vle32_v_i32m1(a32 + LEN, vl);
    vuint32m1_t vu1 = __riscv_vle32_v_u32m1((const uint32_t *)a32,       vl);
    vuint32m1_t vu2 = __riscv_vle32_v_u32m1((const uint32_t *)(a32 + LEN), vl);

#define STORE32S(r) do { __riscv_vse32_v_i32m1(out, (r), vl); out += vl; } while (0)
#define STORE32U(r) do { __riscv_vse32_v_u32m1((uint32_t *)out, (r), vl); out += vl; } while (0)

    /* vmulhu, vmul, vmulhsu, vmulh */
    STORE32U(__riscv_vmulhu_vv_u32m1(vu1, vu2, vl));
    STORE32S(__riscv_vmul_vv_i32m1(v1, v2, vl));
    STORE32S(__riscv_vmulhsu_vv_i32m1(v1, vu2, vl));
    STORE32S(__riscv_vmulh_vv_i32m1(v1, v2, vl));

    /* multiply-accumulate: vd = f(vd, vs1, vs2) */
    /* vmadd: vd = vs1*vd + vs2  -- load fresh accumulator each time */
    STORE32S(__riscv_vmadd_vv_i32m1(
        __riscv_vle32_v_i32m1(a32, vl), v1, v2, vl));
    /* vnmsub: vd = -(vs1*vd) + vs2 */
    STORE32S(__riscv_vnmsub_vv_i32m1(
        __riscv_vle32_v_i32m1(a32, vl), v1, v2, vl));
    /* vmacc: vd = vs1*vs2 + vd */
    STORE32S(__riscv_vmacc_vv_i32m1(
        __riscv_vle32_v_i32m1(a32, vl), v1, v2, vl));
    /* vnmsac: vd = -(vs1*vs2) + vd
     * RTL BUG: element [vl-1] computes +(vs1*vs2)+vd instead of -(vs1*vs2)+vd
     * when the product equals a power of 2 (e.g. 4*64=256=2^8).  The sign
     * inversion is dropped for the last element at the power-of-2 boundary.
     * Observed: QEMU element[3]=0xffffff04, RTL element[3]=0x00000104.
     * See vicuna_vector_isa.md "알려진 RTL 미동작 명령어". */
    STORE32S(__riscv_vnmsac_vv_i32m1(
        __riscv_vle32_v_i32m1(a32, vl), v1, v2, vl));

    /* vsmul */
    STORE32S(__riscv_vsmul_vv_i32m1(v1, v2, vl));

    /* widening multiply: e16,mf2 -> e32,m1 */
    vl = __riscv_vsetvl_e16mf2(LEN);
    vint16mf2_t  w1  = __riscv_vle16_v_i16mf2(a16,       vl);
    vint16mf2_t  w2  = __riscv_vle16_v_i16mf2(a16 + LEN, vl);
    vuint16mf2_t wu1 = __riscv_vle16_v_u16mf2((const uint16_t *)a16,       vl);
    vuint16mf2_t wu2 = __riscv_vle16_v_u16mf2((const uint16_t *)(a16 + LEN), vl);

    STORE32U(__riscv_vwmulu_vv_u32m1(wu1, wu2, vl));
    STORE32S(__riscv_vwmulsu_vv_i32m1(w1, wu2, vl));
    STORE32S(__riscv_vwmul_vv_i32m1(w1, w2, vl));

    /* widening multiply-accumulate
     * RTL BUG (vv forms, LMUL=mf2): elements [0] and [1] produce wrong results.
     * Element [0] receives the product of element [vl-1] (last element),
     * element [1] receives 0.  Elements [2] and [3] are computed correctly.
     * This affects vwmulu.vv, vwmulsu.vv, vwmul.vv, vwmaccu.vv, vwmacc.vv,
     * vwmaccsu.vv.  The .vx scalar forms (e.g. vwmaccus.vx) are unaffected.
     * Likely cause: pipeline reads stale data for the first 32-bit source chunk
     * (elements 0,1) when EMUL=mf2.
     * See vicuna_vector_isa.md "알려진 RTL 미동작 명령어". */
    {
        size_t vl32 = __riscv_vsetvl_e32m1(LEN);
        vint32m1_t  acc_s = __riscv_vle32_v_i32m1(a32,  vl32);
        vuint32m1_t acc_u = __riscv_vle32_v_u32m1((const uint32_t *)a32, vl32);
        vl = __riscv_vsetvl_e16mf2(LEN);

        STORE32U(__riscv_vwmaccu_vv_u32m1(acc_u, wu1, wu2, vl));
        STORE32S(__riscv_vwmacc_vv_i32m1(acc_s, w1, w2, vl));
        /* vwmaccus: d += scalar(unsigned) * vs2(signed) */
        STORE32S(__riscv_vwmaccus_vx_i32m1(acc_s, 5, w1, vl));
        STORE32S(__riscv_vwmaccsu_vv_i32m1(acc_s, w1, wu2, vl));
    }

#undef STORE32S
#undef STORE32U
}

int main(void)
{
    do_ops();
#if defined(VEC_COMPARE_QEMU)
    for (size_t i = 0; i < sizeof(vdata_start) / sizeof(vdata_start[0]); ++i)
        qemu_puthex32((uint32_t)vdata_start[i]);
#endif
    finish();
    return 0;
}
