    .section .vectors, "ax", @progbits
    .global _vectors
_vectors:
    .option norvc;

    # address 0 is used by the RTL testbench as the program-end sentinel
    j _vectors

    .rept 30
    nop
    .endr
    j trap_loop

    # reset vector for cores starting at address 0x80
    j _start


    .section .text

    .global _start
    .type   _start, @function
_start:
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


trap_loop:
    j trap_loop
