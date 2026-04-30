#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

#define LEN 4

static uint32_t src_data[8] = {
    0x11111111, 0x22222222, 0x33333333, 0x44444444,
    0x55555555, 0x66666666, 0x77777777, 0x88888888,
};
static uint32_t index_data[LEN] = {0, 4, 8, 12};

int32_t vdata_start[64];

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
    uint32_t *out = (uint32_t *)vdata_start;
    size_t vl = __riscv_vsetvl_e32m1(LEN);

    /* vle32 / vse32 */
    {
        vuint32m1_t v = __riscv_vle32_v_u32m1(src_data, vl);
        __riscv_vse32_v_u32m1(out, v, vl);
        out += vl;
    }

    /* vlse32 / vsse32 (stride = 4 bytes = 1 element) */
    {
        vuint32m1_t v = __riscv_vlse32_v_u32m1(src_data, sizeof(uint32_t), vl);
        __riscv_vsse32_v_u32m1(out, sizeof(uint32_t), v, vl);
        out += vl;
    }

    /* vloxei32 / vsoxei32 (byte offsets {0,4,8,12})
     * RTL BUG: Vicuna RTL interprets index values as element indices and
     * internally multiplies by eew_bytes (×4 for e32).  The RVV spec requires
     * byte offsets.  With byte-offset index_data={0,4,8,12}, RTL computes
     * base+0*4=0 (correct), base+4*4=16, base+8*4=32, base+12*4=48.
     * Addresses 16/32/48 are out of the src_data array, so RTL returns 0 for
     * elements 1–3.  QEMU (byte-offset) returns the correct values.
     * To use Vicuna correctly, index_data should be {0,1,2,3} (element index);
     * however that would make QEMU read unaligned bytes, so a single test
     * covering both modes is not possible.  See vicuna_vector_isa.md §4.3. */
    {
        vuint32m1_t idx = __riscv_vle32_v_u32m1(index_data, vl);
        vuint32m1_t v   = __riscv_vloxei32_v_u32m1(src_data, idx, vl);
        __riscv_vsoxei32_v_u32m1(out, idx, v, vl);
        out += vl;
    }

    /* vlseg2e32 / vsseg2e32 (nf=2): m2 타입으로 연속 레지스터 쌍 보장 */
    {
        /* vuint32m2_t → GCC가 짝수 번호 레지스터(v0, v2, ...)에 할당.
         * vlseg2e32.v vd, (ptr): vd = field0[0..vl-1], vd+1 = field1[0..vl-1]
         * vsseg2e32.v vd, (ptr): vd, vd+1 을 읽어 메모리에 인터리브 저장 */
        vuint32m2_t seg;
        __asm__ volatile(
            "vlseg2e32.v %0, (%1)"
            : "=vr"(seg)
            : "r"(src_data)
            : "memory"
        );
        __asm__ volatile(
            "vsseg2e32.v %0, (%1)"
            :
            : "vr"(seg), "r"(out)
            : "memory"
        );
        out += vl * 2;
    }

    /* vle32ff (fault-only-first) / vse32 */
    {
        size_t new_vl;
        vuint32m1_t v = __riscv_vle32ff_v_u32m1(src_data, &new_vl, vl);
        __riscv_vse32_v_u32m1(out, v, new_vl);
        out += vl;
    }

    /* vl1re32 / vs1r (whole register) */
    {
        vuint32m1_t v;
        __asm__ volatile("vl1re32.v %0, (%1)" : "=vr"(v) : "r"(src_data));
        __asm__ volatile("vs1r.v %0, (%1)" : : "vr"(v), "r"(out));
        out += vl;
    }

    /* vlm / vsm (mask load/store) */
    {
        vbool32_t m = __riscv_vlm_v_b32((const uint8_t *)src_data, vl);
        __riscv_vsm_v_b32((uint8_t *)out, m, vl);
        out += 1;
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
