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

/* Scalar ALU, widening, and narrowing ops.
 * __attribute__((noinline)) prevents the compiler from hoisting the
 * vsetvli e32,m1 of do_ext_ops() into this function, which would execute
 * the narrowing vse16.v stores with the wrong vtype and corrupt results. */
static int32_t * __attribute__((noinline)) do_alu_ops(int32_t *out)
{
    size_t vl = __riscv_vsetvl_e32m1(LEN);

    vint32m1_t  v1  = __riscv_vle32_v_i32m1(a32,       vl);
    vint32m1_t  v2  = __riscv_vle32_v_i32m1(a32 + LEN, vl);
    vuint32m1_t vu1 = __riscv_vle32_v_u32m1((const uint32_t *)a32,       vl);
    vuint32m1_t vu2 = __riscv_vle32_v_u32m1((const uint32_t *)(a32 + LEN), vl);

#define STORE32S(r) do { __riscv_vse32_v_i32m1(out, (r), vl); out += vl; } while (0)
#define STORE32U(r) do { __riscv_vse32_v_u32m1((uint32_t *)out, (r), vl); out += vl; } while (0)

    STORE32S(__riscv_vadd_vv_i32m1(v1, v2, vl));
    STORE32S(__riscv_vsub_vv_i32m1(v1, v2, vl));
    STORE32S(__riscv_vrsub_vx_i32m1(v1, 10, vl));
    STORE32U(__riscv_vminu_vv_u32m1(vu1, vu2, vl));
    STORE32S(__riscv_vmin_vv_i32m1(v1, v2, vl));
    STORE32U(__riscv_vmaxu_vv_u32m1(vu1, vu2, vl));
    STORE32S(__riscv_vmax_vv_i32m1(v1, v2, vl));
    STORE32S(__riscv_vand_vv_i32m1(v1, v2, vl));
    STORE32S(__riscv_vor_vv_i32m1(v1, v2, vl));
    STORE32S(__riscv_vxor_vv_i32m1(v1, v2, vl));
    STORE32S(__riscv_vsll_vx_i32m1(v1, 2, vl));
    STORE32U(__riscv_vsrl_vx_u32m1(vu1, 2, vl));
    STORE32S(__riscv_vsra_vx_i32m1(v1, 2, vl));

#undef STORE32S
#undef STORE32U

    /* widening ops: e16,mf2 -> e32,m1 */
    vl = __riscv_vsetvl_e16mf2(LEN);
    vint16mf2_t  w1  = __riscv_vle16_v_i16mf2(a16,       vl);
    vint16mf2_t  w2  = __riscv_vle16_v_i16mf2(a16 + LEN, vl);
    vuint16mf2_t wu1 = __riscv_vle16_v_u16mf2((const uint16_t *)a16,       vl);
    vuint16mf2_t wu2 = __riscv_vle16_v_u16mf2((const uint16_t *)(a16 + LEN), vl);

#define STORE_W32S(r) do { __riscv_vse32_v_i32m1(out, (r), vl); out += vl; } while (0)
#define STORE_W32U(r) do { __riscv_vse32_v_u32m1((uint32_t *)out, (r), vl); out += vl; } while (0)

    STORE_W32U(__riscv_vwaddu_vv_u32m1(wu1, wu2, vl));
    STORE_W32S(__riscv_vwadd_vv_i32m1(w1, w2, vl));
    STORE_W32U(__riscv_vwsubu_vv_u32m1(wu1, wu2, vl));
    STORE_W32S(__riscv_vwsub_vv_i32m1(w1, w2, vl));

#undef STORE_W32S
#undef STORE_W32U

    /* narrowing ops: e32,m1 -> e16,mf2
     * RTL BUG: vnsrl and vnsra produce incorrect results in Vicuna RTL.
     * vproc_decoder.sv line 1577: vl_o = {vl_i[CFG_VL_W-2:0], 1'b1}
     * For vl_i=4 this yields vl_o=9, exceeding VLMAX=8 for e32,m1 (VLEN=256).
     * The pipeline produces unshifted source values in even lanes and zeros in
     * odd lanes instead of the narrowed/shifted result. */
    vl = __riscv_vsetvl_e16mf2(LEN);
    vuint32m1_t wide_u = __riscv_vwaddu_vv_u32m1(wu1, wu2, vl);
    vint32m1_t  wide_s = __riscv_vwadd_vv_i32m1(w1, w2, vl);

    vuint16mf2_t rnsrl = __riscv_vnsrl_wx_u16mf2(wide_u, 2, vl);
    __riscv_vse16_v_u16mf2((uint16_t *)out, rnsrl, vl);
    out += (vl * sizeof(uint16_t) + sizeof(int32_t) - 1) / sizeof(int32_t);

    vint16mf2_t rnsra = __riscv_vnsra_wx_i16mf2(wide_s, 2, vl);
    __riscv_vse16_v_i16mf2((int16_t *)out, rnsra, vl);
    out += (vl * sizeof(int16_t) + sizeof(int32_t) - 1) / sizeof(int32_t);

    return out;
}

/* RTL BUG: vzext.vf2 and vsext.vf2 do not work correctly on Vicuna RTL.
 *
 *   vzext.vf2: OP_WIDENING path in vproc_decoder.sv (lines 1562-1566) has
 *              no VSEW_32 case, leaving vsew_o as don't-care when
 *              vtype=e32,m1.  The pipeline produces all-zero output.
 *              Additionally, the vzext store instruction appears after vsext
 *              in the compiled code; when vsext triggers an illegal-instruction
 *              trap (see below), the vzext store is never executed either.
 *
 *   vsext.vf2: OP_WIDENING bumps emul from m1 to m2 (EMUL_2), which
 *              requires an even-aligned vd register.  When the compiler
 *              assigns an odd-numbered register (e.g. v25), op_illegal=1
 *              and Ibex raises an illegal-instruction exception, causing
 *              the hart to enter trap_loop and halt.
 *
 * Kept in a separate noinline function so the required vsetvli e32,m1 cannot
 * be hoisted into do_alu_ops() by the compiler, which would corrupt the
 * vnsrl/vnsra vse16.v stores that precede it.
 * Both instructions are kept so the QEMU reference output captures the
 * expected values; RTL results will differ. */
static void __attribute__((noinline)) do_ext_ops(int32_t *out)
{
    size_t vl = __riscv_vsetvl_e16mf2(LEN);
    vuint16mf2_t wu1 = __riscv_vle16_v_u16mf2((const uint16_t *)a16, vl);
    vint16mf2_t  w1  = __riscv_vle16_v_i16mf2(a16, vl);

#define STORE_W32S(r) do { __riscv_vse32_v_i32m1(out, (r), vl); out += vl; } while (0)
#define STORE_W32U(r) do { __riscv_vse32_v_u32m1((uint32_t *)out, (r), vl); out += vl; } while (0)

    STORE_W32U(__riscv_vzext_vf2_u32m1(wu1, vl)); /* RTL BUG: outputs zeros */
    STORE_W32S(__riscv_vsext_vf2_i32m1(w1, vl));  /* RTL BUG: illegal insn  */

#undef STORE_W32S
#undef STORE_W32U
}

static void do_ops(void)
{
    int32_t *p = do_alu_ops(vdata_start);
    do_ext_ops(p);
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
