# split_loop.s - a polling loop that an extra entry point splits in two.
#
# main waits for a vblank handler to clear a flag, with a timeout, the way
# Agent Under Fire waits for its GS interrupt. split_loop.toml adds an entry
# point in the middle of the loop (the runtime records one when an interrupt
# returns there), so the recompiled loop is two functions and its back-edge a
# jump from one into the other. That jump must still be a safepoint, where
# time passes and the vblank interrupt is taken: the flag clears long before
# the timeout. Assembled without symbols, so the entry splits the function.
        .set noreorder
        .text
_start: li      $a1, -1
        li      $a2, 0x20000
        move    $a0, $zero
        move    $a3, $zero
        move    $t0, $zero
        li      $v1, 0x3c               # SetupThread
        syscall
        move    $sp, $v0
        jal     main
        nop
        move    $a0, $v0
        li      $v1, 4                  # Exit
        syscall
1:      b       1b
        nop

print:  li      $v1, 0x75               # _print
        syscall
        jr      $ra
        nop

main:   addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        li      $a0, 2                  # vblank start
        la      $a1, handler
        move    $a2, $zero
        move    $a3, $zero
        li      $v1, 0x10               # AddIntcHandler
        syscall
        li      $a0, 2
        li      $v1, 0x14               # _EnableIntc
        syscall
        li      $t0, 1
        la      $t1, flag
        jal     wait_flag
        sw      $t0, 0($t1)             # set; the handler clears it
        la      $a0, s_timeout
        beqz    $v0, 1f
        nop
        la      $a0, s_cleared
1:      jal     print
        nop
        ld      $ra, 0($sp)
        move    $v0, $zero
        jr      $ra
        addiu   $sp, $sp, 16

handler:                                # vblank: clears the flag
        la      $t0, flag
        sw      $zero, 0($t0)
        jr      $ra
        move    $v0, $zero

        .balign 0x100
wait_flag:                              # 0x00100100 -> iterations left, 0 on timeout
        li      $v0, 3000000
1:      addiu   $v0, $v0, -1
        beqz    $v0, 2f
        nop
        lui     $t1, %hi(flag)          # 0x00100114: the extra entry (split_loop.toml)
        lw      $t0, %lo(flag)($t1)
        bnez    $t0, 1b                 # the back-edge, into wait_flag
        nop
2:      jr      $ra
        nop

        .data
flag:       .word 0
s_cleared:  .asciz "the vblank handler cleared the flag before the timeout\n"
s_timeout:  .asciz "timed out: no interrupt was taken in the loop\n"
