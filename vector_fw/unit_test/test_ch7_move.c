#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

#define LEN 4

static int32_t src_a[LEN * 3] = {
    1, 2, 3, 4,
    0x10, 0x20, 0x30, 0x40,
    0x55555555, (int32_t)0xAAAAAAAA, 0, (int32_t)0xFFFFFFFF,
};

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

    vint32m1_t v1 = __riscv_vle32_v_i32m1(src_a,           vl);
    vint32m1_t v2 = __riscv_vle32_v_i32m1(src_a + LEN,     vl);
    vint32m1_t v0 = __riscv_vle32_v_i32m1(src_a + LEN * 2, vl);
    /* construct mask from v0: non-zero elements are active */
    vbool32_t mask = __riscv_vmsne_vx_i32m1_b32(v0, 0, vl);

#define STORE32(r) do { __riscv_vse32_v_i32m1(out, (r), vl); out += vl; } while (0)

    /* vmerge */
    STORE32(__riscv_vmerge_vvm_i32m1(v1, v2, mask, vl));
    /* vmv.v.v */
    STORE32(__riscv_vmv_v_v_i32m1(v1, vl));
    /* vmv.v.x */
    STORE32(__riscv_vmv_v_x_i32m1(42, vl));
    /* vmv.v.i (immediate via vmv_v_x) */
    STORE32(__riscv_vmv_v_x_i32m1(10, vl));
    /* vmv.s.x (set element 0 only) */
    STORE32(__riscv_vmv_s_x_i32m1(99, vl));

    /* vmv.x.s (extract element 0 to scalar) */
    {
        int32_t scalar;
        __asm__ volatile("vmv.x.s %0, %1" : "=r"(scalar) : "vr"(v1));
        *out++ = scalar;
        out += vl - 1; /* pad to full slot */
    }

    /* vmv1r.v */
    {
        vint32m1_t r;
        __asm__ volatile("vmv1r.v %0, %1" : "=vr"(r) : "vr"(v1));
        STORE32(r);
    }

    /* vmv2r.v: copy m2 group, store first m1 */
    {
        size_t vl2 = __riscv_vsetvl_e32m2(LEN * 2);
        vint32m2_t src2 = __riscv_vle32_v_i32m2(src_a, vl2);
        vint32m2_t dst2;
        __asm__ volatile("vmv2r.v %0, %1" : "=vr"(dst2) : "vr"(src2));
        vl = __riscv_vsetvl_e32m1(LEN);
        STORE32(__riscv_vlmul_trunc_v_i32m2_i32m1(dst2));
    }

    /* vmv4r.v */
    {
        size_t vl4 = __riscv_vsetvl_e32m4(LEN * 4);
        vint32m4_t src4 = __riscv_vle32_v_i32m4(src_a, vl4);
        vint32m4_t dst4;
        __asm__ volatile("vmv4r.v %0, %1" : "=vr"(dst4) : "vr"(src4));
        vl = __riscv_vsetvl_e32m1(LEN);
        STORE32(__riscv_vlmul_trunc_v_i32m4_i32m1(dst4));
    }

    /* vmv8r.v */
    {
        size_t vl8 = __riscv_vsetvl_e32m8(LEN * 8);
        vint32m8_t src8 = __riscv_vle32_v_i32m8(src_a, vl8);
        vint32m8_t dst8;
        __asm__ volatile("vmv8r.v %0, %1" : "=vr"(dst8) : "vr"(src8));
        vl = __riscv_vsetvl_e32m1(LEN);
        STORE32(__riscv_vlmul_trunc_v_i32m8_i32m1(dst8));
    }

#undef STORE32
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
