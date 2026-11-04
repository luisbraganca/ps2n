# pnach.s - code and data that a PCSX2 patch file changes (pnach.toml, pnach.pnach).
#
# The chosen group patches an instruction's immediate under a condition that
# holds, an instruction word and a data word. A condition that fails and a
# group that is not chosen leave theirs alone. The interpreted run sees the
# same words, which are written to RAM too.
        .set noreorder
        .text

        .globl scale
        .ent scale
scale:  lui     $v0, 0x3f80             # at 0x00100000: the patch makes it 0x3fab
        jr      $ra
        nop
        .end scale

        .globl choice
        .ent choice
choice: jr      $ra
        li      $v0, 1                  # at 0x00100010: the patch makes it li $v0, 5
        .end choice

        .globl kept
        .ent kept
kept:   jr      $ra
        li      $v0, 7                  # at 0x00100018: a failing condition and another group leave it
        .end kept

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

        .globl print
        .ent print
print:  li      $v1, 0x75               # _print
        syscall
        jr      $ra
        nop
        .end print

        .globl main
        .ent main
main:   addiu   $sp, $sp, -16
        sd      $ra, 0($sp)

        jal     scale
        nop
        la      $a0, s_scale
        jal     print
        move    $a1, $v0

        jal     choice
        nop
        la      $a0, s_choice
        jal     print
        move    $a1, $v0

        jal     kept
        nop
        la      $a0, s_kept
        jal     print
        move    $a1, $v0

        la      $t0, number
        lw      $a1, 0($t0)
        la      $a0, s_number
        jal     print
        nop

        la      $t0, scale              # the patched word is in RAM too
        lw      $a1, 0($t0)
        la      $a0, s_word
        jal     print
        nop

        ld      $ra, 0($sp)
        move    $v0, $zero
        jr      $ra
        addiu   $sp, $sp, 16
        .end main

        .rodata
s_scale:        .asciz "pnach: scale returned 0x%08x\n"
s_choice:       .asciz "pnach: choice returned %d\n"
s_kept:         .asciz "pnach: kept returned %d\n"
s_number:       .asciz "pnach: number is %d\n"
s_word:         .asciz "pnach: instruction word 0x%08x\n"

        .data
number:         .word 100               # the patch makes it 200
