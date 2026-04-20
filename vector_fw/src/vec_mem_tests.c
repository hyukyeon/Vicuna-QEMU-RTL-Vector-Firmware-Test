#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

#define LEN 8

static int32_t a32[LEN] = {1, -2, 3, -4, 0x7fffffff, -1, 42, -100};
static int32_t b32[LEN] = {0, 0, 0, 0, 0, 0, 0, 0};

int32_t c[4] = {0};

#if defined(VEC_COMPARE_QEMU)
static inline void qemu_putc(char ch)
{
    volatile uint8_t *const uart_thr = (volatile uint8_t *)0x10000000;
    volatile uint8_t *const uart_lsr = (volatile uint8_t *)0x10000005;
    while (((*uart_lsr) & 0x20u) == 0u) {}
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
static void finish(void) { for(;;) {} }
#else
static void finish(void) { __asm__ volatile("jr zero"); }
#endif

static void do_ops(void)
{
    size_t n = LEN;
    size_t vl;
    int32_t tmp[LEN];

    /* Test 0: simple vector store/load (aligned) */
    vl = __riscv_vsetvl_e32m1(n);
    {
        vint32m1_t va = __riscv_vle32_v_i32m1(a32, vl);
        __riscv_vse32_v_i32m1((int32_t*)tmp, va, vl); /* store into tmp memory */
        __riscv_vse32_v_i32m1((int32_t*)tmp, va, vl); /* redundant store to exercise path */
        c[0] = tmp[0];
    }

    /* Test 1: masked store (emulated scalar mask, then vector load) */
    {
        /* mem region initialized */
        static int32_t mem[LEN];
        for (size_t i = 0; i < LEN; ++i) mem[i] = 0xdeadbeef;

        /* mask: only store positive elements of a32 */
        for (size_t i = 0; i < LEN; ++i) {
            if (a32[i] > 0) mem[i] = a32[i];
        }
        /* load back with vector load */
        vl = __riscv_vsetvl_e32m1(n);
        vint32m1_t vmem = __riscv_vle32_v_i32m1(mem, vl);
        __riscv_vse32_v_i32m1((int32_t*)tmp, vmem, vl);
        c[1] = tmp[0];
    }

    /* Test 2: masked load (emulated): load full vector then apply mask to zero out lanes */
    {
        static int32_t mem2[LEN];
        for (size_t i = 0; i < LEN; ++i) mem2[i] = (int32_t)(i + 10);
        vl = __riscv_vsetvl_e32m1(n);
        vint32m1_t vfull = __riscv_vle32_v_i32m1(mem2, vl);
        /* apply mask: keep values where a32 is odd, else set -1 */
        int32_t masked[LEN];
        for (size_t i = 0; i < vl; ++i) {
            int32_t val = mem2[i];
            if ((a32[i] & 1) != 0) masked[i] = val; else masked[i] = -1;
        }
        /* store masked result via vector store */
        vint32m1_t vm = __riscv_vle32_v_i32m1(masked, vl);
        __riscv_vse32_v_i32m1((int32_t*)tmp, vm, vl);
        c[2] = tmp[0];
    }

    /* Test 3: partial-width stores/loads (16-bit elements) */
    {
        int16_t mem16[LEN];
        for (size_t i = 0; i < LEN; ++i) mem16[i] = (int16_t)(a32[i] & 0xffff);
        vl = __riscv_vsetvl_e16m1(n);
        vint16m1_t v16 = __riscv_vle16_v_i16m1(mem16, vl);
        __riscv_vse16_v_i16m1((int16_t*)mem16, v16, vl);
        /* read back first element into tmp and then into c[3] */
        c[3] = (int32_t)mem16[0];
    }
}

int main(void)
{
    do_ops();
#if defined(VEC_COMPARE_QEMU)
    for (size_t i = 0; i < (sizeof(c)/sizeof(c[0])); ++i) qemu_puthex32((uint32_t)c[i]);
#endif
    finish();
    return 0;
}
