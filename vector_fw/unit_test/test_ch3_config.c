/* test_ch3_config.c
 *
 * Validates vsetvl / vsetvli / vsetivli behaviour and CSR read/write.
 *
 * Expected output (VLEN=128, all implementations should match QEMU):
 *
 *  [0]  vsetvl_e8m1(32)    → 16    (VLMAX = VLEN/8  = 16)
 *  [1]  vsetvl_e16m1(32)   →  8    (VLMAX = VLEN/16 =  8)
 *  [2]  vsetvl_e32m1(32)   →  4    (VLMAX = VLEN/32 =  4)
 *  [3]  vsetvl_e32m4(32)   → 16    (VLMAX = VLEN*4/32 = 16)
 *  [4]  vsetvl_e32m8(32)   → 32    (VLMAX = VLEN*8/32 = 32; AVL=32=VLMAX)
 *  [5]  vsetvl_e8mf2(32)   →  8    (VLMAX = VLEN/(8*2) = 8)
 *  [6]  vsetvl_e16mf2(32)  →  4    (VLMAX = VLEN/(16*2) = 4)
 *  [7]  vsetvl_e32mf2(32)  →  2    (VLMAX = VLEN/(32*2) = 2)
 *  [8]  vsetivli 4, e32m1  →  4    (immediate AVL=4)
 *  [9]  vlenb CSR           → 16    (VLEN/8)
 *  [10] vl CSR              →  4    (vl after [8])
 *  [11] vxsat before sat    →  0
 *  [12] vsaddu sat result[0]→ 0xffffffff  (0xffffffff + 1 saturates)
 *  [13] vxsat after sat     →  1    (saturation flag set)
 *  [14] vxrm after csrw 1  →  1
 *  [15] vcsr (vxrm:vxsat)  →  3    (vxrm=01b → bits[2:1], vxsat=1 → bit[0])
 */

#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

int32_t vdata_start[32];

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

    /* ---- vsetvl / vsetvli: request AVL=32 (> VLMAX for most configs) ---- */
    *out++ = (int32_t)__riscv_vsetvl_e8m1(32);    /* [0]  16 */
    *out++ = (int32_t)__riscv_vsetvl_e16m1(32);   /* [1]   8 */
    *out++ = (int32_t)__riscv_vsetvl_e32m1(32);   /* [2]   4 */
    *out++ = (int32_t)__riscv_vsetvl_e32m4(32);   /* [3]  16 */
    *out++ = (int32_t)__riscv_vsetvl_e32m8(32);   /* [4]  32 */
    *out++ = (int32_t)__riscv_vsetvl_e8mf2(32);   /* [5]   8 */
    *out++ = (int32_t)__riscv_vsetvl_e16mf2(32);  /* [6]   4 */
    /* RTL BUG: vsetvl with e32,mf2 returns 0 in Vicuna RTL.
     * VLMAX = VLEN/(SEW*2) = 128/(32*2) = 2 is valid per spec, but the RTL
     * returns vl=0 (treats it as illegal).  e16,mf2 and e8,mf2 work correctly.
     * See vicuna_vector_isa.md §3 "알려진 RTL 미동작". */
    *out++ = (int32_t)__riscv_vsetvl_e32mf2(32);  /* [7]   QEMU=2, RTL=0 */

    /* ---- vsetivli: immediate AVL ---- */
    {
        size_t vl;
        __asm__ volatile("vsetivli %0, 4, e32, m1, ta, ma" : "=r"(vl));
        *out++ = (int32_t)vl;   /* [8]   4 */
    }

    /* ---- CSR reads: vlenb and vl ---- */
    {
        uint32_t vlenb_val;
        __asm__ volatile("csrr %0, vlenb" : "=r"(vlenb_val));
        *out++ = (int32_t)vlenb_val;   /* [9]  16 */
    }
    {
        /* vl was last set by vsetivli above (vl=4); read it back */
        uint32_t vl_csr;
        __asm__ volatile("csrr %0, vl" : "=r"(vl_csr));
        *out++ = (int32_t)vl_csr;     /* [10]  4 */
    }

    /* ---- vxsat: clear → read → saturate → read ---- */
    __riscv_vsetvl_e32m1(4);
    {
        /* clear vxsat */
        __asm__ volatile("csrw vxsat, zero" ::: "memory");
        uint32_t vxsat_before;
        __asm__ volatile("csrr %0, vxsat" : "=r"(vxsat_before));
        *out++ = (int32_t)vxsat_before;  /* [11]  0 */

        /* saturating unsigned add: 0xffffffff + 1 → saturates, sets vxsat */
        vuint32m1_t vmax  = __riscv_vmv_v_x_u32m1(0xFFFFFFFFu, 4);
        vuint32m1_t vone  = __riscv_vmv_v_x_u32m1(1u, 4);
        vuint32m1_t vsat  = __riscv_vsaddu_vv_u32m1(vmax, vone, 4);
        /* store element [0] of saturated result to confirm saturation occurred */
        __riscv_vse32_v_u32m1((uint32_t *)out, vsat, 4);
        out++;          /* [12]  0xffffffff */
        out += 4 - 1;   /* pad: leave slots [13]..[15] written by store, skip them */

        /* RTL BUG: vxsat flag is NOT set by vsaddu (or any saturating op) in
         * Vicuna RTL.  The RTL executes the vsaddu correctly (result saturates
         * to 0xffffffff) but the vxsat CSR is never updated.
         * Also: csrw vxrm / csrr vxrm returns 0 regardless of what was written.
         * vcsr likewise always reads as 0.
         * See vicuna_vector_isa.md §3 "알려진 RTL 미동작". */
        uint32_t vxsat_after;
        __asm__ volatile("csrr %0, vxsat" : "=r"(vxsat_after));
        *out++ = (int32_t)vxsat_after;  /* [16]  1 */
    }

    /* ---- vxrm: write 1 (round nearest-up), read back ---- */
    {
        __asm__ volatile("csrw vxrm, %0" : : "r"(1u) : "memory");
        uint32_t vxrm_val;
        __asm__ volatile("csrr %0, vxrm" : "=r"(vxrm_val));
        *out++ = (int32_t)vxrm_val;     /* [17]  1 */
    }

    /* ---- vcsr: combined view (vxrm[1:0]:vxsat[0]) ---- */
    /* After vxrm=1, vxsat=1: vcsr = (1<<1)|1 = 3 */
    {
        uint32_t vcsr_val;
        __asm__ volatile("csrr %0, vcsr" : "=r"(vcsr_val));
        *out++ = (int32_t)vcsr_val;     /* [18]  3 */
    }
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
