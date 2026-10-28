# stripped.s - assembled without a symbol table, like a retail game.
#
# Function starts must come from heuristics: the entry point, JAL targets, and
# code pointers built with lui/addiu. One routine is reached only through a
# pointer computed at run time, which no static analysis can see: the runtime
# interprets it and records its address in the missing-entries file.
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

main:   addiu   $sp, $sp, -32
        sd      $ra, 0($sp)
        sd      $s0, 8($sp)
        jal     square                  # JAL target
        li      $a0, 12
        move    $s0, $v0
        la      $t9, callback           # code pointer (lui/addiu)
        jalr    $t9
        li      $a0, 5
        addu    $s0, $s0, $v0
        la      $t9, hidden_minus_8     # a pointer the analysis cannot see
        addiu   $t9, $t9, 8
        jalr    $t9
        li      $a0, 3
        addu    $a1, $s0, $v0
        la      $a0, s_result
        jal     print
        nop
        ld      $s0, 8($sp)
        ld      $ra, 0($sp)
        move    $v0, $zero
        jr      $ra
        addiu   $sp, $sp, 32

square: mult    $v0, $a0, $a0
        jr      $ra
        nop

callback:
        addiu   $sp, $sp, -16
        sll     $v0, $a0, 4
        jr      $ra
        addiu   $sp, $sp, 16

hidden_minus_8:
        nop
        nop
hidden: li      $t0, 3                  # only reached through the computed pointer
        mult    $v0, $a0, $t0
        jr      $ra
        addiu   $v0, $v0, 1000

        .rodata
s_result:
        .asciz "stripped: %d\n"
