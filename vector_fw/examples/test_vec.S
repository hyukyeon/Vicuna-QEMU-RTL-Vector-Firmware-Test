    .text
    .global main
main:
    li      t0, 4
    vsetvli t0, t0, e32, m1, ta, ma
    
    # Simple vector op
    vadd.vi v1, v0, 5
    
    # Store result to memory at address res
    la      a0, res
    vse32.v v1, (a0)
    
    # Print first element to UART (optional, but helps)
    lw      a1, 0(a0)
    # Check if we are on QEMU (virt) or RTL
    # On QEMU virt, UART is at 0x10000000. On Vicuna RTL, it's at 0xFF000000.
    # We'll just write to both for simplicity in this test.
    li      t1, 0x10000000
    sb      a1, 0(t1) # Write low byte to UART
    li      t1, 0xFF000000
    sb      a1, 0(t1)

    # Exit QEMU
    li      t0, 0x5555
    li      t1, 0x100000
    sw      t0, 0(t1)

    # Exit RTL (jump to 0)
    li      t0, 0
    jr      t0

    .data
    .align 4
res:
    .word 0, 0, 0, 0
