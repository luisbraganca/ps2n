# SPDX-License-Identifier: GPL-3.0-or-later
#
# A program that rewrites one of its own functions, then puts it back.
#
# It is here for the path the proof of concept calls "unseen code": code that
# is in memory when it runs but was not in the executable when the recompiler
# read it. What must happen:
#
#   - compiled ahead of time from the executable alone, the rewritten function
#     is noticed (the write hits a protected page, the function is compared
#     with memory before it next runs), interpreted, and its code reported;
#   - compiled again with that report, both versions are compiled, and the one
#     that is in memory runs.
#
# Either way it prints what a console prints. It talks to nothing but the EE's
# serial port (which the core logs as console output) and the kernel's
# FlushCache, so that it needs no toolchain and no library to build:
# ps2n-recomp assemble makes the executable.

        .text
        .globl _start
        .ent _start
_start: la    $sp, stack_top
        la    $a0, msg_begin
        jal   puts
        nop

        jal   answer                  # as the executable has it
        nop
        jal   show
        move  $a0, $v0

        la    $a0, second             # rewritten
        jal   patch
        nop
        jal   answer
        nop
        jal   show
        move  $a0, $v0

        la    $a0, first              # and as it was
        jal   patch
        nop
        jal   answer
        nop
        jal   show
        move  $a0, $v0

        la    $a0, msg_end
        jal   puts
        nop
1:      b     1b                      # the host stops the machine once it has seen the last line
        nop
        .end _start

# The function that is rewritten: two instructions.
        .ent answer
answer: jr    $ra
        addiu $v0, $zero, 1
        .end answer

# patch(words): copies two instruction words over `answer`, and tells the
# kernel, as a program must on a console before it runs code it has written.
        .ent patch
patch:  la    $t0, answer
        lw    $t1, 0($a0)
        lw    $t2, 4($a0)
        sw    $t1, 0($t0)
        sw    $t2, 4($t0)
        addiu $a0, $zero, 2           # FlushCache(INVALIDATE_ICACHE)
        addiu $v1, $zero, 100
        syscall
        jr    $ra
        nop
        .end patch

# show(n): prints "answer = n" for a single digit.
        .ent show
show:   addiu $sp, $sp, -16
        sd    $ra, 0($sp)
        sd    $s0, 8($sp)
        move  $s0, $a0
        la    $a0, msg_answer
        jal   puts
        nop
        lui   $t1, 0x1000
        ori   $t1, $t1, 0xF180
        addiu $t0, $s0, 48            # '0' + n
        sb    $t0, 0($t1)
        addiu $t0, $zero, 10
        sb    $t0, 0($t1)
        ld    $ra, 0($sp)
        ld    $s0, 8($sp)
        jr    $ra
        addiu $sp, $sp, 16
        .end show

# puts(text): writes a zero-terminated string to the serial port.
        .ent puts
puts:   lui   $t1, 0x1000
        ori   $t1, $t1, 0xF180        # SIO_TXFIFO
1:      lbu   $t0, 0($a0)
        beqz  $t0, 2f
        addiu $a0, $a0, 1
        b     1b
        sb    $t0, 0($t1)
2:      jr    $ra
        nop
        .end puts

        .rodata
msg_begin:  .asciz "-- TEST BEGIN\n"
msg_answer: .asciz "answer = "
msg_end:    .asciz "-- TEST END\n"

# On a page of its own: nothing but `patch` writes to a page that holds code.
        .data
        .align 12
first:  .word 0x03E00008, 0x24020001  # jr $ra; addiu $v0, $zero, 1
second: .word 0x03E00008, 0x24020002  # jr $ra; addiu $v0, $zero, 2

        .bss
        .align 4
stack:  .space 4096
stack_top:
