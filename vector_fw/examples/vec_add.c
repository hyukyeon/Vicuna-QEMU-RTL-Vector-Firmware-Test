#include <stdint.h>
#include <riscv_vector.h>

int32_t a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
int32_t b[8] = {10, 20, 30, 40, 50, 60, 70, 80};
int32_t c[8] = {0};

void vec_add(int32_t *a, int32_t *b, int32_t *c, size_t n) {
    for (size_t vl; n > 0; n -= vl, a += vl, b += vl, c += vl) {
        vl = __riscv_vsetvl_e32m1(n);
        vint32m1_t va = __riscv_vle32_v_i32m1(a, vl);
        vint32m1_t vb = __riscv_vle32_v_i32m1(b, vl);
        vint32m1_t vc = __riscv_vadd_vv_i32m1(va, vb, vl);
        __riscv_vse32_v_i32m1(c, vc, vl);
    }
}

int main() {
    vec_add(a, b, c, 8);
    
    // Jump to address 0 to signal end of simulation
    void (*exit_ptr)() = (void (*)())0;
    exit_ptr();
    
    return 0;
}
