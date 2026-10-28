# exec_extra.s - LoadExecPS2, part 3 of 3 (exec_boot.s): an executable the
# game was not recompiled with, so it runs on the interpreter. It starts the
# boot program again with one argument.
        .set noreorder
        .text

        .globl _start
        .ent _start
_start: li      $a1, -1
        li      $a2, 0x20000
        move    $a0, $zero
        la      $a3, args
        move    $t0, $zero
        li      $v1, 0x3c               # SetupThread: fills args
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

        .globl main
        .ent main
main:   la      $t0, args
        lw      $a1, 0($t0)             # argc
        lw      $a2, 4($t0)             # argv[0]
        la      $a0, s_extra
        li      $v1, 0x75               # _print
        syscall
        la      $a0, s_boot_path
        li      $a1, 1
        la      $a2, boot_argv
        li      $v1, 6                  # LoadExecPS2: does not return
        syscall
        la      $a0, s_returned
        li      $v1, 0x75
        syscall
        jr      $ra
        li      $v0, 1
        .end main

        .rodata
s_extra:        .asciz "extra: argc=%d argv[0]=%s\n"
s_boot_path:    .asciz "host:exec_boot.elf"
s_again:        .asciz "again"
s_returned:     .asciz "extra: LoadExecPS2 returned\n"

        .data
        .align 2
boot_argv:      .word s_again

        .bss
        .align 4
args:           .space 324              # struct sargs { argc; argv[16]; payload[256]; }
