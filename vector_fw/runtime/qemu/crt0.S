    .section .vectors, "ax", @progbits
    .global _vectors
_vectors:
    .option norvc;

    # reset vector for systems starting at address 0
    j _start

    # redirect interrupts/exceptions away from the default ROM
    .rept 30
    nop
    .endr
    j exc_wrapper

    # reset vector for systems starting at address 0x80
    j _start


    .section .text

    .global _start
    .type   _start, @function
_start:
    la t0, _vectors
    csrw mtvec, t0

    # Enable vector state on QEMU before executing vector instructions.
    li t0, 0x200
    csrs mstatus, t0

    la sp, _stack

    la t1, _bss_start
    la t2, _bss_end
    bge t1, t2, bss_clear_end

bss_clear_loop:
    sw x0, 0(t1)
    addi t1, t1, 4
    blt t1, t2, bss_clear_loop

bss_clear_end:
    li a0, 0
    li a1, 0
    la ra, _start
    j main


    .weak exception_handler
    .type exception_handler, @function
exception_handler:
    j exception_handler


exc_wrapper:
    addi sp, sp, -80
    sw ra, 76(sp)
    sw s0, 72(sp)
    addi s0, sp, 80
    sw t0, 68(sp)
    sw t1, 64(sp)
    sw t2, 60(sp)
    sw t3, 56(sp)
    sw t4, 52(sp)
    sw t5, 48(sp)
    sw t6, 44(sp)
    sw a0, 40(sp)
    sw a1, 36(sp)
    sw a2, 32(sp)
    sw a3, 28(sp)
    sw a4, 24(sp)
    sw a5, 20(sp)
    sw a6, 16(sp)
    sw a7, 12(sp)

    csrr t0, mie
    csrr t1, mstatus
    csrr a1, mepc
    sw t0, 0(sp)
    sw t1, 4(sp)
    sw a1, 8(sp)

    csrr a0, mcause
    csrr a2, mtval
    lw a3, 72(sp)
    call exception_handler

    lw t0, 0(sp)
    lw t1, 4(sp)
    lw t2, 8(sp)
    csrw mie, t0
    csrw mstatus, t1
    csrw mepc, t2

    lw a7, 12(sp)
    lw a6, 16(sp)
    lw a5, 20(sp)
    lw a4, 24(sp)
    lw a3, 28(sp)
    lw a2, 32(sp)
    lw a1, 36(sp)
    lw a0, 40(sp)
    lw t6, 44(sp)
    lw t5, 48(sp)
    lw t4, 52(sp)
    lw t3, 56(sp)
    lw t2, 60(sp)
    lw t1, 64(sp)
    lw t0, 68(sp)
    lw s0, 72(sp)
    lw ra, 76(sp)
    addi sp, sp, 80
    mret
