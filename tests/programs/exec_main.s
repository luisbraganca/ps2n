# exec_main.s - LoadExecPS2, part 2 of 3 (exec_boot.s): started with two
# arguments at the boot program's addresses. It blocks on three vblanks
# through its own handler, which is when anything left over from the boot
# program would run, then starts exec_extra.elf.
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

        .globl printf
        .ent printf
printf: li      $v1, 0x75
        syscall
        jr      $ra
        nop
        .end printf

# print_args(tag): "tag: argc=N argv[0]=... argv[1]=..."
        .ent print_args
print_args:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        sd      $s0, 8($sp)
        move    $a1, $a0
        lw      $a2, args
        la      $a0, s_argc
        jal     printf
        nop
        move    $s0, $zero
1:      lw      $t0, args
        slt     $t1, $s0, $t0
        beq     $t1, $zero, 2f
        sll     $t2, $s0, 2
        la      $t3, args
        addu    $t3, $t3, $t2
        lw      $a2, 4($t3)
        move    $a1, $s0
        la      $a0, s_argv
        jal     printf
        nop
        b       1b
        addiu   $s0, $s0, 1
2:      la      $a0, s_newline
        jal     printf
        nop
        ld      $s0, 8($sp)
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end print_args

        .ent vblank_handler
vblank_handler:
        lw      $a0, sema
        li      $v1, -0x43              # iSignalSema
        syscall
        jr      $ra
        move    $v0, $zero
        .end vblank_handler

        .globl main
        .ent main
main:   addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        la      $a0, s_main
        jal     print_args
        nop

        la      $t0, sparam
        li      $t1, 255
        sw      $t1, 4($t0)             # max count
        sw      $zero, 8($t0)           # initial count
        move    $a0, $t0
        li      $v1, 0x40               # CreateSema
        syscall
        sw      $v0, sema
        li      $a0, 2                  # vblank start
        la      $a1, vblank_handler
        move    $a2, $zero
        move    $a3, $zero
        li      $v1, 0x10               # AddIntcHandler
        syscall
        li      $a0, 2
        li      $v1, 0x14               # _EnableIntc
        syscall
        lw      $a0, sema
        li      $v1, 0x44               # WaitSema
        syscall
        lw      $a0, sema
        li      $v1, 0x44
        syscall
        lw      $a0, sema
        li      $v1, 0x44
        syscall
        li      $a0, 2
        li      $v1, 0x15               # _DisableIntc
        syscall
        la      $a0, s_vblanks
        jal     printf
        nop

        la      $a0, s_extra_path
        move    $a1, $zero
        move    $a2, $zero
        li      $v1, 6                  # LoadExecPS2: does not return
        syscall
        la      $a0, s_returned
        jal     printf
        nop
        li      $v0, 1
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end main

        .rodata
s_argc:         .asciz "%s: argc=%d"
s_argv:         .asciz " argv[%d]=%s"
s_newline:      .asciz "\n"
s_main:         .asciz "main"
s_vblanks:      .asciz "main: three vblanks\n"
s_extra_path:   .asciz "host:exec_extra.elf"
s_returned:     .asciz "main: LoadExecPS2 returned\n"

        .data
        .align 2
sema:           .word 0
sparam:         .word 0, 0, 0, 0, 0, 0

        .bss
        .align 4
args:           .space 324              # struct sargs { argc; argv[16]; payload[256]; }
