#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

/* NOTE: Observed differences between QEMU and Vicuna RTL in fixed-point ops
 * (also documented in vicuna_vector_isa.md).
 * Summary:
 *  - indices 25..32: ALU rounding/clip off-by-one (RTL produces values 1 less for several negative results). Related: vicuna/rtl/vproc_alu.sv (vxrm/rounding path).
 *  - indices 41..44: packing/unpacking order mismatch (vreg pack/unpack vs LSU wdata formation). Related: vproc_vregpack.sv, vproc_vregunpack.sv, vproc_lsu.sv.
 * RTL is left unchanged (recorded as known RTL bugs). For firmware tests: consider accepting ±1 for 25..32 or avoiding rounding-boundary inputs; for 41..44 compare element-wise or accept both pack orders.
 */
#define LEN 4

static int32_t  a32[LEN * 2] = {1, 2, 3, 4, 0x10, 0x20, 0x30, 0x40};
static int16_t  shift16[LEN] = {1, 2, 3, 4};

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

    /* vsaddu, vsadd, vssubu, vssub */
    STORE32U(__riscv_vsaddu_vv_u32m1(vu1, vu2, vl));
    STORE32S(__riscv_vsadd_vv_i32m1(v1, v2, vl));
    STORE32U(__riscv_vssubu_vv_u32m1(vu1, vu2, vl));
    STORE32S(__riscv_vssub_vv_i32m1(v1, v2, vl));

    /* vaaddu, vaadd, vasubu, vasub */
    STORE32U(__riscv_vaaddu_vv_u32m1(vu1, vu2, vl));
    STORE32S(__riscv_vaadd_vv_i32m1(v1, v2, vl));
    STORE32U(__riscv_vasubu_vv_u32m1(vu1, vu2, vl));
    STORE32S(__riscv_vasub_vv_i32m1(v1, v2, vl));

    /* vssrl, vssra (shift amounts from vu2 / v2 reinterpreted as unsigned) */
    STORE32U(__riscv_vssrl_vv_u32m1(vu1, vu2, vl));
    STORE32S(__riscv_vssra_vv_i32m1(v1, vu2, vl));

    /* vnclipu, vnclip: narrow e32m1 -> e16mf2 */
    vl = __riscv_vsetvl_e16mf2(LEN);
    {
        vuint16mf2_t sh = __riscv_vle16_v_u16mf2((const uint16_t *)shift16, vl);

        vuint16mf2_t ru = __riscv_vnclipu_wv_u16mf2(
            __riscv_vreinterpret_v_i32m1_u32m1(__riscv_vle32_v_i32m1(a32, __riscv_vsetvl_e32m1(LEN))),
            sh, vl);
        __riscv_vse16_v_u16mf2((uint16_t *)out, ru, vl);
        out += (vl * sizeof(uint16_t) + sizeof(int32_t) - 1) / sizeof(int32_t);

        vint16mf2_t rs = __riscv_vnclip_wv_i16mf2(
            __riscv_vle32_v_i32m1(a32, __riscv_vsetvl_e32m1(LEN)),
            sh, vl);
        __riscv_vse16_v_i16mf2((int16_t *)out, rs, vl);
        out += (vl * sizeof(int16_t) + sizeof(int32_t) - 1) / sizeof(int32_t);
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
