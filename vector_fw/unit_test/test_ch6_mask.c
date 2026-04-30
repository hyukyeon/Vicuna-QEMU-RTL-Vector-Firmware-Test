#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

/* NOTE: Known Vicuna RTL bug (mask-store path)
 * In Verilator/RTL runs some mask/store requests produce incorrect
 * write-be (e.g., 0x3 for byte stores) and sign-extended wdata
 * (e.g., 0xfffffff5). This causes mismatches with QEMU in
 * `test_ch6_mask.c` (observed ~17 differing lines). Documented in
 * vicuna_vector_isa.md. Do not modify RTL here; the test keeps
 * expected behavior for QEMU and is annotated for RTL bugs.
 */
#define LEN 4

static int32_t  src_a[LEN] = {1, 2, 3, 4};
static int32_t  src_b[LEN] = {0x10, 2, 0x30, 1};
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
    int32_t  *out = vdata_start;
    uint8_t  *mout;
    size_t    vl  = __riscv_vsetvl_e32m1(LEN);

    vint32m1_t  v1   = __riscv_vle32_v_i32m1(src_a, vl);
    vint32m1_t  v2   = __riscv_vle32_v_i32m1(src_b, vl);
    vuint32m1_t vu1  = __riscv_vle32_v_u32m1((const uint32_t *)src_a, vl);
    vbool32_t   carry = __riscv_vlm_v_b32((const uint8_t *)&mask_word, vl);

    /* vadc / vsbc */
    { vint32m1_t r = __riscv_vadc_vvm_i32m1(v1, v2, carry, vl); __riscv_vse32_v_i32m1(out, r, vl); out += vl; }
    { vint32m1_t r = __riscv_vsbc_vvm_i32m1(v1, v2, carry, vl); __riscv_vse32_v_i32m1(out, r, vl); out += vl; }

    /* vmadc / vmsbc -> mask results (1 byte each, 4-byte aligned slots) */
#define STORE_MASK(m) do { mout = (uint8_t *)out; __riscv_vsm_v_b32(mout, (m), vl); out += 1; } while (0)
    STORE_MASK(__riscv_vmadc_vvm_i32m1_b32(v1, v2, carry, vl));
    STORE_MASK(__riscv_vmsbc_vvm_i32m1_b32(v1, v2, carry, vl));

    /* comparison masks */
    STORE_MASK(__riscv_vmseq_vv_i32m1_b32(v1, v2, vl));
    STORE_MASK(__riscv_vmsne_vv_i32m1_b32(v1, v2, vl));
    STORE_MASK(__riscv_vmsltu_vv_u32m1_b32(vu1, __riscv_vle32_v_u32m1((const uint32_t *)src_b, vl), vl));
    STORE_MASK(__riscv_vmslt_vv_i32m1_b32(v1, v2, vl));
    STORE_MASK(__riscv_vmsleu_vv_u32m1_b32(vu1, __riscv_vle32_v_u32m1((const uint32_t *)src_b, vl), vl));
    STORE_MASK(__riscv_vmsle_vv_i32m1_b32(v1, v2, vl));
    STORE_MASK(__riscv_vmsgtu_vx_u32m1_b32(vu1, 0, vl));
    STORE_MASK(__riscv_vmsgt_vx_i32m1_b32(v1, 0, vl));

    /* mask logical ops: use carry as m0, vmseq result as m1 */
    vbool32_t m0 = carry;
    vbool32_t m1 = __riscv_vmseq_vv_i32m1_b32(v1, v2, vl);
    STORE_MASK(__riscv_vmandn_mm_b32(m0, m1, vl));
    STORE_MASK(__riscv_vmand_mm_b32(m0, m1, vl));
    STORE_MASK(__riscv_vmor_mm_b32(m0, m1, vl));
    STORE_MASK(__riscv_vmxor_mm_b32(m0, m1, vl));
    STORE_MASK(__riscv_vmorn_mm_b32(m0, m1, vl));
    STORE_MASK(__riscv_vmnand_mm_b32(m0, m1, vl));
    STORE_MASK(__riscv_vmnor_mm_b32(m0, m1, vl));
    STORE_MASK(__riscv_vmxnor_mm_b32(m0, m1, vl));
#undef STORE_MASK
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
