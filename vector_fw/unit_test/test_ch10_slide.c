#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

#define LEN 4

static int32_t  src_data[LEN]     = {1, 2, 3, 4};
static uint32_t indices[LEN]      = {3, 2, 1, 0};
static uint32_t compress_mask_word = 0x05; /* 0101b -> elements 0,2 */

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

    vint32m1_t  v1   = __riscv_vle32_v_i32m1(src_data, vl);
    vuint32m1_t vidx = __riscv_vle32_v_u32m1(indices, vl);
    vint32m1_t  vzero = __riscv_vmv_v_x_i32m1(0, vl);

#define STORE32(r) do { __riscv_vse32_v_i32m1(out, (r), vl); out += vl; } while (0)

    /* vslideup.vx: dst = {vzero[0], v1[0], v1[1], v1[2]} (slide up by 1) */
    STORE32(__riscv_vslideup_vx_i32m1(vzero, v1, 1, vl));

    /* vslidedown.vx: {v1[1], v1[2], v1[3], 0} */
    STORE32(__riscv_vslidedown_vx_i32m1(v1, 1, vl));

    /* vslide1up.vx: insert scalar 5 at element 0, shift rest up */
    STORE32(__riscv_vslide1up_vx_i32m1(v1, 5, vl));

    /* vslide1down.vx: shift down by 1, insert scalar 6 at last */
    STORE32(__riscv_vslide1down_vx_i32m1(v1, 6, vl));

    /* vrgather.vv: gather v1 elements at positions given by vidx */
    STORE32(__riscv_vrgather_vv_i32m1(v1, vidx, vl));

    /* vcompress.vm: compress active elements (mask = 0101b -> elem 0,2)
     * RTL BUG: element [0] returns a stale value from the previous vd
     * register contents (observed: 5, the scalar from the preceding
     * vslide1up) instead of the correct vs2[0] value (1).
     * Elements [1..] and the overall compress operation are correct.
     * See vicuna_vector_isa.md "알려진 RTL 미동작 명령어". */
    {
        vbool32_t cmask = __riscv_vlm_v_b32((const uint8_t *)&compress_mask_word, vl);
        STORE32(__riscv_vcompress_vm_i32m1(v1, cmask, vl));
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
