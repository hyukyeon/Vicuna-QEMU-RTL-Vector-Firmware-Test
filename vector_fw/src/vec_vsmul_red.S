    .section .text
    .global main
    .align 2
main:
    addi sp, sp, -16
    sw ra, 12(sp)

    li a0, 8
    vsetvli t0, a0, e32, m1

    la a1, a32
    vle32.v v1, (a1)
    la a2, b32
    vle32.v v2, (a2)

    /* fixed-point multiply vsmul.vv v3, v1, v2 */
    vsmul.vv v3, v1, v2
    la a3, c
    vse32.v v3, (a3)

    /* reduction sum: try vredsum.vs v4, v3, v0 - place scalar into c+16 */
    vredsum.vs v4, v3, v0
    /* move first element of v4 to x10 */
    vmv.x.s a5, v4
    sw a5, 16(a3)

    /* mask utilities: vid and viota into c+20 and c+24 (try vid.v and viota.m) */
    vid.v v5
    viota.m v6, v5
    /* store element 0 of v5 and v6 */
    vmv.x.s a6, v5
    sw a6, 20(a3)
    vmv.x.s a7, v6
    sw a7, 24(a3)

    lw ra, 12(sp)
    addi sp, sp, 16
    ret

    .section .data
    .align 2
    .global a32
    .global b32
    .global c
a32:
    .word 1, -2, 3, -4, 2147483647, -1, 42, -100
b32:
    .word 2, 3, -5, 6, 1, -1, 0, 7
c:
    .word 0,0,0,0,0,0,0,0, 0,0,0,0
