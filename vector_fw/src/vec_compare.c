#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

#define VEC_COMPARE_LEN 8

static int32_t a[VEC_COMPARE_LEN] = {1, 2, 3, 4, 5, 6, 7, 8};
static int32_t b[VEC_COMPARE_LEN] = {10, 20, 30, 40, 50, 60, 70, 80};
int32_t c[VEC_COMPARE_LEN] = {0};

static void vec_add(const int32_t *lhs, const int32_t *rhs, int32_t *dst, size_t n)
{
    for (size_t vl; n > 0; n -= vl, lhs += vl, rhs += vl, dst += vl) {
        vl = __riscv_vsetvl_e32m1(n);
        vint32m1_t va = __riscv_vle32_v_i32m1(lhs, vl);
        vint32m1_t vb = __riscv_vle32_v_i32m1(rhs, vl);
        vint32m1_t vc = __riscv_vadd_vv_i32m1(va, vb, vl);
        __riscv_vse32_v_i32m1(dst, vc, vl);
    }
}

#if defined(VEC_COMPARE_QEMU)
static inline void qemu_putc(char ch)
{
    volatile uint8_t *const uart_thr = (volatile uint8_t *)0x10000000;
    volatile uint8_t *const uart_lsr = (volatile uint8_t *)0x10000005;

    while (((*uart_lsr) & 0x20u) == 0u) {
    }
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

static void finish(void)
{
    for (;;) {
    }
}
#else
static void finish(void)
{
    __asm__ volatile("jr zero");
}
#endif

int main(void)
{
    vec_add(a, b, c, VEC_COMPARE_LEN);

#if defined(VEC_COMPARE_QEMU)
    for (size_t i = 0; i < VEC_COMPARE_LEN; ++i) {
        qemu_puthex32((uint32_t)c[i]);
    }
#endif

    finish();
    return 0;
}
