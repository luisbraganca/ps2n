# control_flow.s - control-flow shapes that stress the recompiler.
        .set noreorder
        .text

        .globl _start
        .ent _start
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
        .end _start

        .globl printf
        .ent printf
printf: li      $v1, 0x75
        syscall
        jr      $ra
        nop
        .end printf

# ---------------------------------------------------------------------------
# A jump into the delay slot of another branch.
        .ent delay_entry
delay_entry:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        li      $s0, 0
        li      $a1, 100
        b       2f                      # enter at the delay slot below
        nop
1:      b       3f
2:      addiu   $a1, $a1, 5             # delay slot of "b 3f" and a branch target
3:      la      $a0, s_delay
        jal     printf
        nop
        bne     $s0, $zero, 4f
        li      $s0, 1
        b       1b                      # second pass: take the branch with its delay slot
        nop
4:      ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end delay_entry

# ---------------------------------------------------------------------------
# bal: get-PC idiom and a local subroutine inside one function.
        .ent bal_tests
bal_tests:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        bal     1f
        nop
1:      move    $t0, $ra
        la      $t1, 1b
        subu    $a1, $t0, $t1
        li      $s1, 0
        bal     local_sub
        nop
        bal     local_sub
        nop
        move    $a2, $s1
        la      $a0, s_bal
        jal     printf
        nop
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
local_sub:
        jr      $ra
        addiu   $s1, $s1, 7
        .end bal_tests

# ---------------------------------------------------------------------------
# Tail calls: an unconditional jump and a conditional branch into other functions.
        .ent tail_target
tail_target:
        jr      $ra
        addiu   $v0, $a0, 1000
        .end tail_target

        .ent tail_jump
tail_jump:
        j       tail_target
        addiu   $a0, $a0, 1
        .end tail_jump

        .ent tail_branch
tail_branch:
        beq     $a0, $zero, tail_target
        li      $a0, 50
        jr      $ra
        li      $v0, -1
        .end tail_branch

# Fall-through: ft_first has no return and runs into ft_second.
        .ent ft_first
ft_first:
        li      $v0, 1
        .end ft_first
        .ent ft_second
ft_second:
        jr      $ra
        addiu   $v0, $v0, 2
        .end ft_second

# A callee returning through a link register other than $ra.
        .ent odd_link
odd_link:
        jr      $t1
        li      $v0, 77
        .end odd_link

        .ent tail_tests
tail_tests:
        addiu   $sp, $sp, -32
        sd      $ra, 0($sp)
        sd      $s0, 8($sp)
        jal     tail_jump
        li      $a0, 1
        move    $s0, $v0
        jal     tail_branch
        move    $a0, $zero
        move    $a2, $v0
        move    $a1, $s0
        la      $a0, s_tail
        jal     printf
        nop
        jal     ft_first
        nop
        move    $s0, $v0
        li      $v0, 10
        jal     ft_second
        nop
        move    $a2, $v0
        move    $a1, $s0
        la      $a0, s_fall
        jal     printf
        nop
        la      $t9, odd_link
        jalr    $t1, $t9
        nop
        move    $a1, $v0
        la      $a0, s_link
        jal     printf
        nop
        ld      $s0, 8($sp)
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 32
        .end tail_tests

# ---------------------------------------------------------------------------
# Branch-and-link variants: likely+link taken, link not taken (ra still written).
        .ent link_tests
link_tests:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        li      $s1, 0
        li      $t0, 5
        bgezall $t0, 1f
        addiu   $s1, $s1, 1             # likely delay slot: runs (taken)
        b       2f
        nop
1:      addiu   $s1, $s1, 10
        b       3f
        nop
2:      li      $s1, -1
3:      li      $t0, 1
        move    $ra, $zero
        bltzal  $t0, 4f                 # not taken, but $ra is still written
        nop
6:      la      $t1, 6b
        subu    $a2, $ra, $t1
        b       5f
        nop
4:      li      $a2, -99
5:      move    $a1, $s1
        la      $a0, s_links
        jal     printf
        nop
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end link_tests

# ---------------------------------------------------------------------------
# Deep recursion: one host frame per guest frame.
        .ent sum_rec
sum_rec:
        beq     $a0, $zero, 1f
        nop
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        sd      $a0, 8($sp)
        jal     sum_rec
        addiu   $a0, $a0, -1
        ld      $a0, 8($sp)
        ld      $ra, 0($sp)
        addiu   $sp, $sp, 16
        jr      $ra
        addu    $v0, $v0, $a0
1:      jr      $ra
        move    $v0, $zero
        .end sum_rec

        .ent recursion_test
recursion_test:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        jal     sum_rec
        li      $a0, 20000
        move    $a1, $v0
        la      $a0, s_deep
        jal     printf
        nop
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end recursion_test

# ---------------------------------------------------------------------------
# A computed goto through a table that does not match the switch pattern
# (pointer arithmetic), so the targets come from data references.
        .ent computed_goto
computed_goto:
        la      $t0, goto_table
        sll     $t1, $a0, 2
        addu    $t0, $t0, $t1
        lw      $t2, 0($t0)
        addiu   $t2, $t2, 0             # obscures the load-jump pattern
        jr      $t2
        nop
g0:     jr      $ra
        li      $v0, 1000
g1:     jr      $ra
        li      $v0, 2000
        .end computed_goto

        .ent goto_tests
goto_tests:
        addiu   $sp, $sp, -32
        sd      $ra, 0($sp)
        sd      $s0, 8($sp)
        jal     computed_goto
        li      $a0, 1
        move    $s0, $v0
        jal     computed_goto
        li      $a0, 0
        move    $a2, $v0
        move    $a1, $s0
        la      $a0, s_goto
        jal     printf
        nop
        ld      $s0, 8($sp)
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 32
        .end goto_tests

# ---------------------------------------------------------------------------
# A syscall inside a loop (each one is a resume point).
        .ent syscall_loop
syscall_loop:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        li      $s0, 0
        li      $s1, 0
1:      li      $v1, 0x2f               # GetThreadId
        syscall
        addu    $s1, $s1, $v0
        addiu   $s0, $s0, 1
        slti    $t0, $s0, 4
        bne     $t0, $zero, 1b
        nop
        move    $a1, $s1
        la      $a0, s_sysloop
        jal     printf
        nop
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end syscall_loop

        .globl main
        .ent main
main:   addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        jal     delay_entry
        nop
        jal     bal_tests
        nop
        jal     tail_tests
        nop
        jal     link_tests
        nop
        jal     recursion_test
        nop
        jal     goto_tests
        nop
        jal     syscall_loop
        nop
        ld      $ra, 0($sp)
        move    $v0, $zero
        jr      $ra
        addiu   $sp, $sp, 16
        .end main

        .rodata
s_delay:        .asciz "delay slot entry: %d\n"
s_bal:          .asciz "bal: get-pc delta %d, local subroutine x2 = %d\n"
s_tail:         .asciz "tail: jump %d, branch %d\n"
s_fall:         .asciz "fallthrough: %d, direct %d\n"
s_link:         .asciz "jalr $t1: %d\n"
s_links:        .asciz "likely+link: %d, not-taken bltzal ra delta %d\n"
s_deep:         .asciz "recursion: sum(1..20000) = %d\n"
s_goto:         .asciz "computed goto: %d %d\n"
s_sysloop:      .asciz "syscall loop: %d\n"
        .data
goto_table:     .word g0, g1
