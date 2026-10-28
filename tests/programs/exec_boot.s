# exec_boot.s - LoadExecPS2, part 1 of 3 (exec_main.s, exec_extra.s): the
# boot program. Its first run leaves a ready thread and a vblank handler behind,
# both of which would print if they ever ran again, and starts exec_main.elf
# with two arguments. exec_extra.elf starts it again with one argument.
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

        .globl main
        .ent main
main:   addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        la      $a0, s_boot
        jal     print_args
        nop
        lw      $a1, counter            # .data is reloaded on every start: always 1
        addiu   $a1, $a1, 1
        sw      $a1, counter
        la      $a0, s_counter
        jal     printf
        nop
        lw      $t0, args
        li      $t1, 1
        bne     $t0, $t1, again         # started with an argument: the second run
        nop

        la      $t0, tparam             # a thread that is ready but below main's priority
        la      $t1, leftover_thread
        sw      $t1, 4($t0)
        la      $t1, thread_stack
        sw      $t1, 8($t0)
        li      $t1, 0x800
        sw      $t1, 12($t0)
        li      $t1, 100
        sw      $t1, 20($t0)
        move    $a0, $t0
        li      $v1, 0x20               # CreateThread
        syscall
        move    $a0, $v0
        move    $a1, $zero
        li      $v1, 0x22               # StartThread
        syscall
        li      $a0, 2                  # vblank start
        la      $a1, leftover_handler
        move    $a2, $zero
        move    $a3, $zero
        li      $v1, 0x10               # AddIntcHandler
        syscall
        li      $a0, 2
        li      $v1, 0x14               # _EnableIntc
        syscall

        la      $a0, s_main_path
        li      $a1, 2
        la      $a2, main_argv
        li      $v1, 6                  # LoadExecPS2: does not return
        syscall
        la      $a0, s_returned
        jal     printf
        nop
        b       done
        li      $v0, 1

again:  la      $a0, s_done
        jal     printf
        nop
        move    $v0, $zero
done:   ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end main

# What the first run leaves behind sits above everything the other programs
# load. LoadExecPS2 keeps the RAM outside the new program's segments, so if
# the thread or the handler survived, this code would still be here to run.
        .space  0x4000

        .ent leftover_thread
leftover_thread:
        la      $a0, s_leftover_thread
        li      $v1, 0x75
        syscall
        li      $v1, 0x23               # ExitThread
        syscall
        .end leftover_thread

        .ent leftover_handler
leftover_handler:
        la      $a0, s_leftover_handler
        li      $v1, 0x75
        syscall
        jr      $ra
        move    $v0, $zero
        .end leftover_handler

        .rodata
s_argc:             .asciz "%s: argc=%d"
s_argv:             .asciz " argv[%d]=%s"
s_newline:          .asciz "\n"
s_boot:             .asciz "boot"
s_counter:          .asciz "boot: counter=%d\n"
s_main_path:        .asciz "host:exec_main.elf"
s_one:              .asciz "one"
s_two:              .asciz "two"
s_returned:         .asciz "boot: LoadExecPS2 returned\n"
s_done:             .asciz "boot: done\n"
s_leftover_thread:  .asciz "boot: leftover thread ran\n"
s_leftover_handler: .asciz "boot: leftover vblank handler ran\n"

        .data
        .align 2
counter:        .word 0
main_argv:      .word s_one, s_two
tparam:         .word 0, 0, 0, 0, 0, 0, 0, 0, 0

        .bss
        .align 4
args:           .space 324              # struct sargs { argc; argv[16]; payload[256]; }
thread_stack:   .space 0x800
