/* test_coding_style_example.c
 *
 * H × H^H (Hermitian product) kernel — four implementations.
 *
 * For real int32, H^H = H^T (conjugate transpose = transpose).
 * H is a [4][16] int32 matrix.  R = H × H^T is a [4][4] symmetric matrix.
 *
 *   R[i][j] = dot(H[i], H[j]) = sum_{k=0}^{31}  H[i][k] * H[j][k]
 *
 * Vicuna hardware notes (VLEN=128):
 *   - SEW=32, LMUL=4  →  VLMAX = 128*4/32 = 16 elements per group     (v0–v3, v4–v7, ...)
 *   - vmul.vv  (LMUL=4, non-widening): no known RTL bug
 *   - vredsum.vs                      : confirmed correct (CH11)
 *   - vle32.v, vmv.s.x, vmv.x.s, sw  : all functional
 *   => All four implementations should produce correct RTL output.
 *
 * Four implementations write to vdata_start[]:
 *   [impl 1 — intrinsics          ]  vdata_start[0 ..15]
 *   [impl 2 — asm regs            ]  vdata_start[16..31]
 *   [impl 3 — pure asm (one block)]  vdata_start[32..47]
 *   [impl 4 — per-insn asm + loop ]  vdata_start[48..63]
 *
 * Expected 4×4 result (row-major, same for all four implementations):
 *   [  8,   8,  16,   8 ]    (0x00000008, 0x00000008, 0x00000010, 0x00000008)
 *   [  8,  16,  40,   0 ]    (0x00000008, 0x00000010, 0x00000028, 0x00000000)
 *   [ 16,  40, 120,  -8 ]    (0x00000010, 0x00000028, 0x00000078, 0xfffffff8)
 *   [  8,   0,  -8,  16 ]    (0x00000008, 0x00000000, 0xfffffff8, 0x00000010)
 */

#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

#define ROWS 4
#define COLS 16   /* VLMAX for e32,m4 with VLEN=128 (Vicuna default) */

/* -------------------------------------------------------------------------
 * Input matrix H[4][16] — values chosen so products stay well within int32.
 *   row 0: 1s at even indices only                → norm² = 8
 *   row 1: all ones                               → norm² = 16
 *   row 2: repeating {1,2,3,4} × 4               → norm² = 4*(1+4+9+16) = 120
 *   row 3: alternating ±1                         → norm² = 16
 * ------------------------------------------------------------------------- */
static const int32_t H[ROWS][COLS] = {
    { 1, 0, 1, 0,  1, 0, 1, 0,  1, 0, 1, 0,  1, 0, 1, 0 },
    { 1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1 },
    { 1, 2, 3, 4,  1, 2, 3, 4,  1, 2, 3, 4,  1, 2, 3, 4 },
    { 1,-1, 1,-1,  1,-1, 1,-1,  1,-1, 1,-1,  1,-1, 1,-1 },
};

int32_t vdata_start[64];  /* impl1→[0..15], impl2→[16..31], impl3→[32..47], impl4→[48..63] */

/* -------------------------------------------------------------------------
 * QEMU output helpers (VEC_COMPARE_QEMU mode)
 * ------------------------------------------------------------------------- */
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
 * Implementation 1: RVV C intrinsics  (vint32m4_t, vmul, vredsum)
 * =========================================================================
 *
 * Uses only the official RISC-V V intrinsic API.  The compiler handles all
 * register allocation and vsetvli emission.
 *
 *   vsetvl_e32m4(32)        → set vl=32, SEW=32, LMUL=4
 *   vle32_v_i32m4           → unit-stride vector load
 *   vmul_vv_i32m4           → element-wise 32-bit multiply (lower 32 bits)
 *   vmv_s_x_i32m1(0,1)      → scalar splat: v_zero[0] = 0
 *   vredsum_vs_i32m4_i32m1  → horizontal sum into scalar register
 *   vmv_x_s_i32m1_i32       → extract element 0 to int32
 * ========================================================================= */
static void hht_intrinsic(const int32_t Hm[][COLS], int32_t out[ROWS][ROWS])
{
    for (int i = 0; i < ROWS; i++) {
        const size_t vl = __riscv_vsetvl_e32m4(COLS);
        vint32m4_t vi = __riscv_vle32_v_i32m4(Hm[i], vl);

        for (int j = i; j < ROWS; j++) {
            /* element-wise multiply */
            vint32m4_t vj   = __riscv_vle32_v_i32m4(Hm[j], vl);
            vint32m4_t prod = __riscv_vmul_vv_i32m4(vi, vj, vl);

            /* init scalar accumulator: vmv.s.x vd, 0  (vl=1 → element 0 only) */
            vint32m1_t zero = __riscv_vmv_s_x_i32m1(0, 1);

            /* horizontal reduction: result in element 0 of 's' */
            vint32m1_t s   = __riscv_vredsum_vs_i32m4_i32m1(prod, zero, vl);
            int32_t    val = __riscv_vmv_x_s_i32m1_i32(s);

            out[i][j] = val;
            out[j][i] = val;   /* R is symmetric: fill both halves */
        }
    }
}


/* =========================================================================
 * Implementation 2: explicit register allocation + inline asm kernel
 * =========================================================================
 *
 * All four rows are loaded via inline asm with named output constraints
 * (vint32m4_t variables r0..r3).  The compiler allocates aligned LMUL=4
 * register groups and substitutes the group base name (e.g. "v0") wherever
 * %[va] / %[vb] appear in the asm template.  This gives us explicit control
 * over which register groups participate in each instruction while still
 * letting the compiler resolve the final register numbers.
 *
 * Per-pair dot product sequence:
 *   vsetvli  zero, vl, e32, m4, ta, ma
 *   vmul.vv  prod, va, vb        ← element-wise multiply
 *   vmv.s.x  acc,  zero          ← acc[0] = 0  (scalar move, ignores LMUL)
 *   vredsum.vs acc, prod, acc    ← acc[0] = 0 + sum(prod[0..vl-1])
 *   vmv.x.s  result, acc         ← extract scalar
 * ========================================================================= */

/* DOT_VV: compute dot(va, vb) → *ptr.
 *
 * Uses positional operand references (%0..%5) to avoid the GCC restriction
 * that named operands must be unique — this allows DOT_VV(rN, rN, ...) where
 * both arguments are the same variable (diagonal entries).
 *
 * Operand map:
 *   %0 = _prod (=&vr, early-clobber: may not alias va/vb)
 *   %1 = _acc  (=&vr, early-clobber: written by vmv.s.x, then read+written by vredsum)
 *   %2 = _r    (=r,   scalar output)
 *   %3 = COLS  (r,    scalar input for vsetvli)
 *   %4 = va    (vr,   vector input)
 *   %5 = vb    (vr,   vector input; may equal %4 for diagonal)
 */
#define DOT_VV(va, vb, ptr)                                              \
    do {                                                                 \
        vint32m4_t _prod;                                                \
        vint32m1_t _acc;                                                 \
        int32_t    _r;                                                   \
        __asm__ volatile(                                                \
            "vsetvli  zero, %3, e32, m4, ta, ma\n\t"                    \
            "vmul.vv  %0, %4, %5\n\t"                                   \
            "vmv.s.x  %1, zero\n\t"                                     \
            "vredsum.vs %1, %0, %1\n\t"                                 \
            "vmv.x.s  %2, %1"                                           \
            : "=&vr"(_prod), "=&vr"(_acc), "=r"(_r)                    \
            : "r"((int)(COLS)), "vr"(va), "vr"(vb)                      \
        );                                                               \
        *(ptr) = _r;                                                     \
    } while (0)

static void hht_asm_reg(const int32_t Hm[][COLS], int32_t out[ROWS][ROWS])
{
    vint32m4_t r0, r1, r2, r3;

    /* Load all rows; named constraints bind rows to specific register groups
     * so the compiler knows which vector register holds which row. */
    __asm__ volatile(
        "vsetvli  zero, %[vl], e32, m4, ta, ma\n\t"
        "vle32.v  %[r0], (%[p0])\n\t"
        "vle32.v  %[r1], (%[p1])\n\t"
        "vle32.v  %[r2], (%[p2])\n\t"
        "vle32.v  %[r3], (%[p3])"
        : [r0] "=vr"(r0), [r1] "=vr"(r1), [r2] "=vr"(r2), [r3] "=vr"(r3)
        : [vl] "r"(COLS),
          [p0] "r"(Hm[0]), [p1] "r"(Hm[1]), [p2] "r"(Hm[2]), [p3] "r"(Hm[3])
        : "memory"
    );

    /* 10 unique dot products (upper triangle + diagonal); mirror for symmetry */
    DOT_VV(r0, r0, &out[0][0]);
    DOT_VV(r0, r1, &out[0][1]);  out[1][0] = out[0][1];
    DOT_VV(r0, r2, &out[0][2]);  out[2][0] = out[0][2];
    DOT_VV(r0, r3, &out[0][3]);  out[3][0] = out[0][3];
    DOT_VV(r1, r1, &out[1][1]);
    DOT_VV(r1, r2, &out[1][2]);  out[2][1] = out[1][2];
    DOT_VV(r1, r3, &out[1][3]);  out[3][1] = out[1][3];
    DOT_VV(r2, r2, &out[2][2]);
    DOT_VV(r2, r3, &out[2][3]);  out[3][2] = out[2][3];
    DOT_VV(r3, r3, &out[3][3]);
}

#undef DOT_VV


/* =========================================================================
 * Implementation 3: pure inline assembly
 * =========================================================================
 *
 * The entire kernel is a single inline asm block — no intrinsics, no C
 * control flow inside the vector section.
 *
 * Register layout (fixed for the whole kernel):
 *   v0 –v3 : H[0]   (LMUL=4 group)
 *   v4 –v7 : H[1]
 *   v8 –v11: H[2]
 *   v12–v15: H[3]
 *   v16–v19: product register (reused for each pair)
 *   v20    : scalar accumulator (LMUL=1, element 0 holds the sum)
 *   t0     : COLS constant (32)
 *   t1     : scalar dot-product result (temp before sw)
 *
 * out_flat points to 16 consecutive int32_t in row-major order.
 * Byte offsets:  [i][j] → (i*4 + j)*4
 * ========================================================================= */
static void hht_asm_pure(const int32_t Hm[][COLS], int32_t *out_flat)
{
    __asm__ volatile(
        /* ---- vtype / vl setup ---- */
        "li       t0, 32\n\t"
        "vsetvli  zero, t0, e32, m4, ta, ma\n\t"

        /* ---- load all 4 rows into fixed register groups ---- */
        "vle32.v  v0,  (%[p0])\n\t"   /* v0 –v3  = H[0] */
        "vle32.v  v4,  (%[p1])\n\t"   /* v4 –v7  = H[1] */
        "vle32.v  v8,  (%[p2])\n\t"   /* v8 –v11 = H[2] */
        "vle32.v  v12, (%[p3])\n\t"   /* v12–v15 = H[3] */

        /* ---- dot-product macro (inlined for each of the 10 pairs) ----
         *   vmul.vv  v16, vA, vB    → element-wise multiply
         *   vmv.s.x  v20, zero      → accumulator[0] = 0
         *   vredsum.vs v20, v16, v20→ accumulator[0] = sum(v16[0..31])
         *   vmv.x.s  t1,  v20       → t1 = scalar result
         *   sw       t1, N(%[out])  → store to output array
         * ---------------------------------------------------------------- */

        /* (0,0): R[0][0] = dot(row0,row0) → offset 0 */
        "vmul.vv  v16, v0,  v0\n\t"
        "vmv.s.x  v20, zero\n\t"
        "vredsum.vs v20, v16, v20\n\t"
        "vmv.x.s  t1, v20\n\t"
        "sw       t1,  0(%[out])\n\t"

        /* (0,1): R[0][1]=R[1][0] → offsets 4, 16 */
        "vmul.vv  v16, v0,  v4\n\t"
        "vmv.s.x  v20, zero\n\t"
        "vredsum.vs v20, v16, v20\n\t"
        "vmv.x.s  t1, v20\n\t"
        "sw       t1,  4(%[out])\n\t"
        "sw       t1, 16(%[out])\n\t"

        /* (0,2): R[0][2]=R[2][0] → offsets 8, 32 */
        "vmul.vv  v16, v0,  v8\n\t"
        "vmv.s.x  v20, zero\n\t"
        "vredsum.vs v20, v16, v20\n\t"
        "vmv.x.s  t1, v20\n\t"
        "sw       t1,  8(%[out])\n\t"
        "sw       t1, 32(%[out])\n\t"

        /* (0,3): R[0][3]=R[3][0] → offsets 12, 48 */
        "vmul.vv  v16, v0,  v12\n\t"
        "vmv.s.x  v20, zero\n\t"
        "vredsum.vs v20, v16, v20\n\t"
        "vmv.x.s  t1, v20\n\t"
        "sw       t1, 12(%[out])\n\t"
        "sw       t1, 48(%[out])\n\t"

        /* (1,1): R[1][1] → offset 20 */
        "vmul.vv  v16, v4,  v4\n\t"
        "vmv.s.x  v20, zero\n\t"
        "vredsum.vs v20, v16, v20\n\t"
        "vmv.x.s  t1, v20\n\t"
        "sw       t1, 20(%[out])\n\t"

        /* (1,2): R[1][2]=R[2][1] → offsets 24, 36 */
        "vmul.vv  v16, v4,  v8\n\t"
        "vmv.s.x  v20, zero\n\t"
        "vredsum.vs v20, v16, v20\n\t"
        "vmv.x.s  t1, v20\n\t"
        "sw       t1, 24(%[out])\n\t"
        "sw       t1, 36(%[out])\n\t"

        /* (1,3): R[1][3]=R[3][1] → offsets 28, 52 */
        "vmul.vv  v16, v4,  v12\n\t"
        "vmv.s.x  v20, zero\n\t"
        "vredsum.vs v20, v16, v20\n\t"
        "vmv.x.s  t1, v20\n\t"
        "sw       t1, 28(%[out])\n\t"
        "sw       t1, 52(%[out])\n\t"

        /* (2,2): R[2][2] → offset 40 */
        "vmul.vv  v16, v8,  v8\n\t"
        "vmv.s.x  v20, zero\n\t"
        "vredsum.vs v20, v16, v20\n\t"
        "vmv.x.s  t1, v20\n\t"
        "sw       t1, 40(%[out])\n\t"

        /* (2,3): R[2][3]=R[3][2] → offsets 44, 56 */
        "vmul.vv  v16, v8,  v12\n\t"
        "vmv.s.x  v20, zero\n\t"
        "vredsum.vs v20, v16, v20\n\t"
        "vmv.x.s  t1, v20\n\t"
        "sw       t1, 44(%[out])\n\t"
        "sw       t1, 56(%[out])\n\t"

        /* (3,3): R[3][3] → offset 60 */
        "vmul.vv  v16, v12, v12\n\t"
        "vmv.s.x  v20, zero\n\t"
        "vredsum.vs v20, v16, v20\n\t"
        "vmv.x.s  t1, v20\n\t"
        "sw       t1, 60(%[out])\n\t"

        : /* no C outputs */
        : [p0] "r"(Hm[0]), [p1] "r"(Hm[1]), [p2] "r"(Hm[2]), [p3] "r"(Hm[3]),
          [out] "r"(out_flat)
        : "t0", "t1",
          "v0","v1","v2","v3","v4","v5","v6","v7",
          "v8","v9","v10","v11","v12","v13","v14","v15",
          "v16","v17","v18","v19","v20",
          "memory"
    );
}


/* =========================================================================
 * Implementation 4: per-instruction inline asm with C for loops
 * =========================================================================
 *
 * Like style 3 every vector operation is expressed as a raw RISC-V V
 * instruction, but here each instruction is its own asm volatile statement
 * rather than one large monolithic asm block.  C for loops replace the
 * hand-unrolled sequence of 10 (i,j) pairs.
 *
 * Register allocation for the row vectors is handled by the compiler via the
 * "vr" constraint (same as style 2).  The inner dot-product sequence is:
 *
 *   asm("vsetvli  zero, %vl, e32, m4, ta, ma")  — re-arm vl/vtype per pair
 *   asm("vle32.v  vi, (Hm[i])")                 — load row[i] (outer loop)
 *   asm("vle32.v  vj, (Hm[j])")                 — load row[j] (inner loop)
 *   asm("vmul.vv  prod, vi, vj")                 — element-wise product
 *   asm("vmv.s.x  acc,  zero")                   — acc[0] = 0  (LMUL=1)
 *   asm("vredsum.vs acc, prod, acc")              — acc[0] = 0 + sum(prod)
 *   asm("vmv.x.s  result, acc")                  — scalar extract
 *
 * GCC does not allow arrays of RVV types ("array elements cannot have RVV
 * type"), so rows cannot be stored in vint32m4_t row[ROWS].  Instead, row[i]
 * is a single variable vi loaded once in the outer loop; row[j] is a separate
 * variable vj reloaded for each j.  The compiler keeps vi live in a vector
 * register across the inner loop; a spill would be correct but add overhead.
 *
 * Trade-offs vs style 3:
 *   + Each instruction is independently visible to the compiler — easier to
 *     add perf-counters, debug, or selectively replace one instruction.
 *   + For loops keep the source compact and easy to adapt (e.g. variable ROWS).
 *   - vsetvli must be re-issued explicitly since the compiler does not track
 *     vtype state across separate asm volatile boundaries.
 * ========================================================================= */
static void hht_asm_instr(const int32_t Hm[][COLS], int32_t out[ROWS][ROWS])
{
    /* Outer loop: load row[i] once; inner loop: load row[j] per j. */
    for (int i = 0; i < ROWS; i++) {
        vint32m4_t vi;

        __asm__ volatile("vsetvli zero, %[vl], e32, m4, ta, ma"
                         : : [vl] "r"(COLS) :);
        __asm__ volatile("vle32.v %[vd], (%[rs1])"
                         : [vd] "=vr"(vi)
                         : [rs1] "r"(Hm[i])
                         : "memory");

        for (int j = i; j < ROWS; j++) {
            vint32m4_t vj, prod;
            vint32m1_t acc;
            int32_t    result;

            /* Re-establish vtype; then load row[j]. */
            __asm__ volatile("vsetvli zero, %[vl], e32, m4, ta, ma"
                             : : [vl] "r"(COLS) :);
            __asm__ volatile("vle32.v %[vd], (%[rs1])"
                             : [vd] "=vr"(vj)
                             : [rs1] "r"(Hm[j])
                             : "memory");

            /* Element-wise multiply: prod[k] = vi[k] * vj[k] */
            __asm__ volatile("vmul.vv %[vd], %[vs2], %[vs1]"
                             : [vd] "=vr"(prod)
                             : [vs2] "vr"(vi), [vs1] "vr"(vj));

            /* Scalar init: acc[0] = 0  (vmv.s.x ignores current LMUL) */
            __asm__ volatile("vmv.s.x %[vd], zero"
                             : [vd] "=vr"(acc));

            /* Horizontal sum: acc[0] = acc[0] + sum(prod[0..vl-1])
             * "+vr" makes acc both the vs1 initializer and the vd result. */
            __asm__ volatile("vredsum.vs %[vd], %[vs2], %[vd]"
                             : [vd] "+vr"(acc)
                             : [vs2] "vr"(prod));

            /* Extract scalar result from element 0. */
            __asm__ volatile("vmv.x.s %[rd], %[vs1]"
                             : [rd] "=r"(result)
                             : [vs1] "vr"(acc));

            out[i][j] = result;
            out[j][i] = result;
        }
    }
}



static void do_ops(void)
{
    /* Each implementation writes a 4×4 = 16-element row-major result block. */
    hht_intrinsic(H, (int32_t (*)[ROWS])&vdata_start[0]);   /* [0 ..15] */
    hht_asm_reg  (H, (int32_t (*)[ROWS])&vdata_start[16]);  /* [16..31] */
    hht_asm_pure (H, &vdata_start[32]);                      /* [32..47] */
    hht_asm_instr(H, (int32_t (*)[ROWS])&vdata_start[48]);  /* [48..63] */
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
