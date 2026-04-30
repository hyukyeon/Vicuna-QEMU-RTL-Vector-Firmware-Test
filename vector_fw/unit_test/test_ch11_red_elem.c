#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

#define LEN 4

static int32_t  src32[LEN] = {1, 2, 3, 4};
static int16_t  src16[LEN] = {1, 2, 3, 4};
static uint32_t mask_word = 0x05; /* 0101b -> elements 0,2 active */

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
    int32_t *out = vdata_start;
    size_t   vl  = __riscv_vsetvl_e32m1(LEN);

    vint32m1_t  v1    = __riscv_vle32_v_i32m1(src32, vl);
    vuint32m1_t vu1   = __riscv_vle32_v_u32m1((const uint32_t *)src32, vl);
    vint32m1_t  init  = __riscv_vmv_v_x_i32m1(0, vl);
    vuint32m1_t uinit = __riscv_vmv_v_x_u32m1(0, vl);

    /* reduction ops: result lands in element 0 of return vector */
#define STORE_RED_S(r) do { __riscv_vse32_v_i32m1(out, (r), vl); out += vl; } while (0)
#define STORE_RED_U(r) do { __riscv_vse32_v_u32m1((uint32_t *)out, (r), vl); out += vl; } while (0)

    STORE_RED_S(__riscv_vredsum_vs_i32m1_i32m1(v1,  init,  vl));
    STORE_RED_S(__riscv_vredand_vs_i32m1_i32m1(v1,  init,  vl));
    STORE_RED_S(__riscv_vredor_vs_i32m1_i32m1(v1,   init,  vl));
    STORE_RED_S(__riscv_vredxor_vs_i32m1_i32m1(v1,  init,  vl));
    STORE_RED_U(__riscv_vredminu_vs_u32m1_u32m1(vu1, uinit, vl));
    STORE_RED_S(__riscv_vredmin_vs_i32m1_i32m1(v1,  init,  vl));
    STORE_RED_U(__riscv_vredmaxu_vs_u32m1_u32m1(vu1, uinit, vl));
    STORE_RED_S(__riscv_vredmax_vs_i32m1_i32m1(v1,  init,  vl));

    /* widening reductions: e16,m1 -> e32,m1 scalar result */
    {
        size_t vl16 = __riscv_vsetvl_e16m1(LEN);
        vint16m1_t   v16  = __riscv_vle16_v_i16m1(src16, vl16);
        vuint16m1_t  vu16 = __riscv_vle16_v_u16m1((const uint16_t *)src16, vl16);
        vl = __riscv_vsetvl_e32m1(LEN);
        vint32m1_t  init32  = __riscv_vmv_v_x_i32m1(0,  vl);
        vuint32m1_t uinit32 = __riscv_vmv_v_x_u32m1(0u, vl);
        vl16 = __riscv_vsetvl_e16m1(LEN);

        STORE_RED_S(__riscv_vwredsum_vs_i16m1_i32m1(v16,  init32,  vl16));
        STORE_RED_U(__riscv_vwredsumu_vs_u16m1_u32m1(vu16, uinit32, vl16));
    }

    /* vcpop / vfirst
     * RTL BUG (vfirst.m): when the first active mask element is at index 0,
     * RTL returns 1 instead of 0 (off-by-one).  vcpop.m is correct.
     * Observed: mask=0x05 (elements 0,2 active), QEMU vfirst=0, RTL vfirst=1.
     * Likely cause: internal priority encoder uses 1-based indexing or has an
     * off-by-one in its bit-0 handling.
     * See vicuna_vector_isa.md "알려진 RTL 미동작 명령어". */
    vl = __riscv_vsetvl_e32m1(LEN);
    {
        vbool32_t m = __riscv_vlm_v_b32((const uint8_t *)&mask_word, vl);
        *out++ = (int32_t)__riscv_vcpop_m_b32(m, vl);
        *out++ = (int32_t)__riscv_vfirst_m_b32(m, vl);
        out += vl - 2; /* pad to next full 4-slot boundary */
    }

    /* vid.v */
    {
        vuint32m1_t vid = __riscv_vid_v_u32m1(vl);
        __riscv_vse32_v_u32m1((uint32_t *)out, vid, vl);
        out += vl;
    }

    /* viota.m */
    {
        vbool32_t m = __riscv_vlm_v_b32((const uint8_t *)&mask_word, vl);
        vuint32m1_t viota = __riscv_viota_m_u32m1(m, vl);
        __riscv_vse32_v_u32m1((uint32_t *)out, viota, vl);
        out += vl;
    }

#undef STORE_RED_S
#undef STORE_RED_U
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
