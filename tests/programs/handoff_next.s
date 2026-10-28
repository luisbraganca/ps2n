# handoff_next.s - the program handoff.s loads by itself and jumps into,
# without LoadExecPS2 (linked at 0x00200000). Its strlen is a fake that
# answers 99; the runtime's strlen stub replaces it, which only happens while
# the runtime runs this program's recompiled code. It then rewrites its own
# strlen and invalidates the instruction cache: from then on that function
# runs from RAM, on the interpreter.
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

        .globl strlen
        .ent strlen
strlen: jr      $ra                     # not a strlen: the runtime's stub replaces it
        li      $v0, 99
        .end strlen

        .globl main
        .ent main
main:   addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        la      $t9, strlen
        la      $a0, s_abc
        jalr    $t9                     # through a pointer: the dispatcher picks what runs
        nop
        move    $a1, $v0
        la      $a0, s_first
        jal     printf
        nop

        la      $t0, strlen             # strlen's delay slot becomes li $v0, 98
        li      $t1, 0x24020062
        sw      $t1, 4($t0)
        li      $a0, 2                  # FlushCache(INVALIDATE_ICACHE)
        li      $v1, 0x64
        syscall
        la      $t9, strlen
        la      $a0, s_abc
        jalr    $t9
        nop
        move    $a1, $v0
        la      $a0, s_second
        jal     printf
        nop

        move    $v0, $zero
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end main

        .rodata
s_abc:          .asciz "abc"
s_first:        .asciz "next: strlen(\"abc\") = %d\n"
s_second:       .asciz "next: after rewriting strlen: %d\n"
