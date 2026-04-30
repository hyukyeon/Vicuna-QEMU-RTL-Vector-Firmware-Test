/* example_det3x3.c
 *
 * Benchmark: 3×3 integer determinant for 16 matrices in parallel.
 *
 * Hardware target: Vicuna (VLEN=128), SEW=32, LMUL=4  →  VLMAX=16
 *
 * Input layout — Structure of Arrays (SoA):
 *   Each of the 9 matrix components is a 16-element int32 vector.
 *
 *     | a  b  c |
 *     | d  e  f |      det = a(ei−fh) − b(di−fg) + c(dh−eg)
 *     | g  h  i |
 *
 * Two implementations:
 *   det3x3_generated  — RVV C intrinsics; compiler manages register allocation
 *   det3x3_asm        — pure inline asm; hand-allocated to v0–v31 (8 LMUL=4 groups)
 *
 * Hand-allocated register map (asm version):
 *   Phase A — load 6 components, compute 3 minors (max 8 live groups):
 *     v0 –v3  : e  → then reused for e×g
 *     v4 –v7  : i  → reused for f×g (minor2), then d×h (minor3)
 *     v8 –v11 : f  → reused for e×g result
 *     v12–v15 : h  → last used in d×h
 *     v16–v19 : d  → last used in d×h
 *     v20–v23 : g  → last used in e×g
 *     v24–v27 : minor1 = ei−fh
 *     v28–v31 : minor2 = di−fg  (temp for f×h and d×i)
 *   Phase B — load a,b,c into freed groups, compute final result:
 *     v0  : a  → a×minor1 → final det
 *     v4  : minor3 = dh−eg
 *     v8  : b  → b×minor2
 *     v12 : c  → c×minor3
 *
 * Output (vdata_start[], int32):
 *   [0 ..15] : determinants, intrinsics version
 *   [16..31] : determinants, asm version
 *   [32]     : cycles for intrinsics  (ITERATIONS passes, lower 32 bits)
 *   [33]     : cycles for asm         (ITERATIONS passes, lower 32 bits)
 *
 * Test matrices: diagonal diag(k+1, k+1, k+1), k=0..15
 *   expected det[k] = (k+1)^3
 *   = { 1, 8, 27, 64, 125, 216, 343, 512,
 *       729, 1000, 1331, 1728, 2197, 2744, 3375, 4096 }
 */

#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

#define N_MAT      16     /* matrices per batch = VLMAX for e32m4 @ VLEN=128 */
#define ITERATIONS 10     /* benchmark repeat count */

/* ---- test input: diagonal matrices diag(k+1, k+1, k+1) ----- */
static const int32_t mat_a[N_MAT] = { 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16 };
static const int32_t mat_b[N_MAT] = { 0 };  /* zero-initialized */
static const int32_t mat_c[N_MAT] = { 0 };
static const int32_t mat_d[N_MAT] = { 0 };
static const int32_t mat_e[N_MAT] = { 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16 };
static const int32_t mat_f[N_MAT] = { 0 };
static const int32_t mat_g[N_MAT] = { 0 };
static const int32_t mat_h[N_MAT] = { 0 };
static const int32_t mat_i[N_MAT] = { 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16 };

/* output buffer: [0..15] intrinsics, [16..31] asm, [32] cycles_gen, [33] cycles_asm */
int32_t vdata_start[34];

/* ---- cycle counter ----
 * RTL BUG / Ibex limitation: Neither rdcycle (CSR 0xC00) nor mcycle (CSR 0xB00)
 * is accessible in the Vicuna/Ibex simulation — both raise an illegal-instruction
 * exception.  Ibex is instantiated in vproc_top.sv without explicit MHPMCounterNum,
 * and the performance counter CSRs appear to be disabled.
 *
 * Workaround: cycle counting is only enabled in QEMU mode (VEC_COMPARE_QEMU defined).
 * Output slots [32] and [33] are left as zero on RTL.
 */
#if defined(VEC_COMPARE_QEMU)
static inline uint32_t rdcycle_u32(void)
{
    uint32_t v;
    __asm__ volatile("csrrs %0, 0xB00, zero" : "=r"(v));
    return v;
}
#else
static inline uint32_t rdcycle_u32(void) { return 0; }
#endif

/* =========================================================================
 * QEMU output helper
 * ========================================================================= */
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


/* =========================================================================
 * Implementation 1: RVV C intrinsics  (compiler-managed register allocation)
 * =========================================================================
 *
 * The compiler is free to allocate and spill the 9 source vectors plus
 * temporaries as it sees fit.  On Vicuna (8 LMUL=4 groups = 32 registers),
 * a good compiler should find a spill-free schedule similar to the asm version.
 * ========================================================================= */
static void det3x3_generated(
        const int32_t a[], const int32_t b[], const int32_t c[],
        const int32_t d[], const int32_t e[], const int32_t f[],
        const int32_t g[], const int32_t h[], const int32_t ii[],
        int32_t det[])
{
    const size_t vl = __riscv_vsetvl_e32m4(N_MAT);

    vint32m4_t va = __riscv_vle32_v_i32m4(a,  vl);
    vint32m4_t vb = __riscv_vle32_v_i32m4(b,  vl);
    vint32m4_t vc = __riscv_vle32_v_i32m4(c,  vl);
    vint32m4_t vd = __riscv_vle32_v_i32m4(d,  vl);
    vint32m4_t ve = __riscv_vle32_v_i32m4(e,  vl);
    vint32m4_t vf = __riscv_vle32_v_i32m4(f,  vl);
    vint32m4_t vg = __riscv_vle32_v_i32m4(g,  vl);
    vint32m4_t vh = __riscv_vle32_v_i32m4(h,  vl);
    vint32m4_t vi = __riscv_vle32_v_i32m4(ii, vl);

    /* 3 cofactor minors */
    vint32m4_t m0 = __riscv_vsub_vv_i32m4(           /* ei − fh */
                        __riscv_vmul_vv_i32m4(ve, vi, vl),
                        __riscv_vmul_vv_i32m4(vf, vh, vl), vl);
    vint32m4_t m1 = __riscv_vsub_vv_i32m4(           /* di − fg */
                        __riscv_vmul_vv_i32m4(vd, vi, vl),
                        __riscv_vmul_vv_i32m4(vf, vg, vl), vl);
    vint32m4_t m2 = __riscv_vsub_vv_i32m4(           /* dh − eg */
                        __riscv_vmul_vv_i32m4(vd, vh, vl),
                        __riscv_vmul_vv_i32m4(ve, vg, vl), vl);

    /* det = a*m0 − b*m1 + c*m2 */
    vint32m4_t result = __riscv_vadd_vv_i32m4(
                            __riscv_vsub_vv_i32m4(
                                __riscv_vmul_vv_i32m4(va, m0, vl),
                                __riscv_vmul_vv_i32m4(vb, m1, vl), vl),
                            __riscv_vmul_vv_i32m4(vc, m2, vl), vl);

    __riscv_vse32_v_i32m4(det, result, vl);
}


/* =========================================================================
 * Implementation 2: pure inline asm — one instruction per asm volatile
 * =========================================================================
 *
 * All 9 input components and all temporaries fit within the 8 available
 * LMUL=4 register groups (v0–v31) with no spills.
 *
 * Detailed live-range trace (each row = state after that instruction):
 *
 *   Instr                    | live groups (peak = 8 at steps marked *)
 *   -------------------------|------------------------------------------
 *   vle32.v v0 , (e)         | e(v0)
 *   vle32.v v4 , (i)         | e(v0) i(v4)
 *   vle32.v v8 , (f)         | e(v0) i(v4) f(v8)
 *   vle32.v v12, (h)         | e(v0) i(v4) f(v8) h(v12)
 *   vle32.v v16, (d)         | e(v0) i(v4) f(v8) h(v12) d(v16)
 *   vle32.v v20, (g)         | e(v0) i(v4) f(v8) h(v12) d(v16) g(v20)
 *   vmul.vv v24, v0,  v4     | +ei(v24)  → 7
 *   vmul.vv v28, v8,  v12    | +fh(v28)  → 8 *
 *   vsub.vv v24, v24, v28    | -fh(v28)  → 7  [minor1=v24]
 *   vmul.vv v28, v16, v4     | +di(v28)  → 8 *
 *   vmul.vv v4,  v8,  v20    | v4=fg; i freed, f/g last use → 8 *
 *   vsub.vv v28, v28, v4     | -fg(v4)   → 6  [minor2=v28]
 *   vmul.vv v4,  v16, v12    | v4=dh; d,h last use          → 6
 *   vmul.vv v8,  v0,  v20    | v8=eg; e,g last use          → 5
 *   vsub.vv v4,  v4,  v8     | v4=dh-eg  → 3  [minor3=v4]
 *   vle32.v v0 , (a)         | +a(v0)    → 4
 *   vle32.v v8 , (b)         | +b(v8)    → 5
 *   vle32.v v12, (c)         | +c(v12)   → 6
 *   vmul.vv v0,  v0,  v24    | v0=a*m1   → 5  (m1/v24 freed)
 *   vmul.vv v8,  v8,  v28    | v8=b*m2   → 4  (m2/v28 freed)
 *   vmul.vv v12, v12, v4     | v12=c*m3  → 3  (m3/v4 freed)
 *   vsub.vv v0,  v0,  v8     | v0=a*m1-b*m2  → 2
 *   vadd.vv v0,  v0,  v12    | v0=det    → 1
 *   vse32.v v0,  (det)       | done
 * ========================================================================= */
static void det3x3_asm(
        const int32_t a[], const int32_t b[], const int32_t c[],
        const int32_t d[], const int32_t e[], const int32_t f[],
        const int32_t g[], const int32_t h[], const int32_t ii[],
        int32_t det[])
{
    __asm__ volatile("li      t0, 16"                   : : : "t0");
    __asm__ volatile("vsetvli zero, t0, e32, m4, ta, ma": : :);

    /* Phase A: load 6 components into v0/v4/v8/v12/v16/v20 */
    __asm__ volatile("vle32.v v0,  (%[p])": : [p]"r"(e)  : "v0","v1","v2","v3","memory");
    __asm__ volatile("vle32.v v4,  (%[p])": : [p]"r"(ii) : "v4","v5","v6","v7","memory");
    __asm__ volatile("vle32.v v8,  (%[p])": : [p]"r"(f)  : "v8","v9","v10","v11","memory");
    __asm__ volatile("vle32.v v12, (%[p])": : [p]"r"(h)  : "v12","v13","v14","v15","memory");
    __asm__ volatile("vle32.v v16, (%[p])": : [p]"r"(d)  : "v16","v17","v18","v19","memory");
    __asm__ volatile("vle32.v v20, (%[p])": : [p]"r"(g)  : "v20","v21","v22","v23","memory");

    /* Minor 1: ei − fh → v24 */
    __asm__ volatile("vmul.vv v24, v0,  v4" : : : "v24","v25","v26","v27");  /* v24 = e*i */
    __asm__ volatile("vmul.vv v28, v8,  v12": : : "v28","v29","v30","v31");  /* v28 = f*h */
    __asm__ volatile("vsub.vv v24, v24, v28": : : "v24","v25","v26","v27");  /* v24 = ei−fh */

    /* Minor 2: di − fg → v28  (v4 reused for f×g; i is last-used in d×i above) */
    __asm__ volatile("vmul.vv v28, v16, v4" : : : "v28","v29","v30","v31");  /* v28 = d*i */
    __asm__ volatile("vmul.vv v4,  v8,  v20": : : "v4","v5","v6","v7");      /* v4  = f*g */
    __asm__ volatile("vsub.vv v28, v28, v4" : : : "v28","v29","v30","v31");  /* v28 = di−fg */

    /* Minor 3: dh − eg → v4  (v4/v8 reused; d,h,e,g all last-used here) */
    __asm__ volatile("vmul.vv v4,  v16, v12": : : "v4","v5","v6","v7");      /* v4  = d*h */
    __asm__ volatile("vmul.vv v8,  v0,  v20": : : "v8","v9","v10","v11");    /* v8  = e*g */
    __asm__ volatile("vsub.vv v4,  v4,  v8" : : : "v4","v5","v6","v7");      /* v4  = dh−eg */

    /* Phase B: load a/b/c into freed groups */
    __asm__ volatile("vle32.v v0,  (%[p])": : [p]"r"(a)  : "v0","v1","v2","v3","memory");
    __asm__ volatile("vle32.v v8,  (%[p])": : [p]"r"(b)  : "v8","v9","v10","v11","memory");
    __asm__ volatile("vle32.v v12, (%[p])": : [p]"r"(c)  : "v12","v13","v14","v15","memory");

    /* det = a*minor1 − b*minor2 + c*minor3 */
    __asm__ volatile("vmul.vv v0,  v0,  v24": : : "v0","v1","v2","v3");      /* v0  = a*(ei−fh) */
    __asm__ volatile("vmul.vv v8,  v8,  v28": : : "v8","v9","v10","v11");    /* v8  = b*(di−fg) */
    __asm__ volatile("vmul.vv v12, v12, v4" : : : "v12","v13","v14","v15");  /* v12 = c*(dh−eg) */
    __asm__ volatile("vsub.vv v0,  v0,  v8" : : : "v0","v1","v2","v3");      /* v0  = a*m1−b*m2 */
    __asm__ volatile("vadd.vv v0,  v0,  v12": : : "v0","v1","v2","v3");      /* v0  = det */

    __asm__ volatile("vse32.v v0, (%[p])": : [p]"r"(det) : "memory");
}


/* =========================================================================
 * Benchmark driver
 * ========================================================================= */
static void do_ops(void)
{
    uint32_t t0, t1;

    /* ---- generated (intrinsics) ---- */
    t0 = rdcycle_u32();
    for (int k = 0; k < ITERATIONS; k++)
        det3x3_generated(mat_a, mat_b, mat_c, mat_d, mat_e, mat_f,
                         mat_g, mat_h, mat_i, &vdata_start[0]);
    t1 = rdcycle_u32();
    vdata_start[32] = (int32_t)(t1 - t0);

    /* ---- hand-allocated asm ---- */
    t0 = rdcycle_u32();
    for (int k = 0; k < ITERATIONS; k++)
        det3x3_asm(mat_a, mat_b, mat_c, mat_d, mat_e, mat_f,
                   mat_g, mat_h, mat_i, &vdata_start[16]);
    t1 = rdcycle_u32();
    vdata_start[33] = (int32_t)(t1 - t0);
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
