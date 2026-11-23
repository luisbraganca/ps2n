# SPDX-License-Identifier: GPL-3.0-or-later
#
# A program that copies different code in turn into the same memory, outside
# its executable, and runs it each time: as a game does that loads code from
# its data into memory it ran other code from before (Jak and Daxter;
# docs/adr/0057).
#
# What must happen:
#
#   - compiled ahead of time from the executable alone, the blocks it runs
#     there are interpreted, and every version of each is reported, as the
#     interpreter carried it out ([[block]] in the capture);
#   - compiled again with that report, each version is compiled, the first in
#     the image of the code run outside the executable and the other in an
#     image of its own, and the one in memory runs: nothing is interpreted.
#
# The two routines differ in shape, a loop and straight code of another
# length, so that their blocks begin and end at other places. Each is run
# twice, in turns. It prints what a console prints, and talks to nothing but
# the EE's serial port and the kernel's FlushCache, as selfmod.s does.

        .text
        .globl _start
        .ent _start
_start: la    $sp, stack_top
        la    $a0, msg_begin
        jal   puts
        nop

        addiu $s1, $zero, 2           # two rounds
round:  la    $a0, loop_code
        la    $a1, loop_end
        jal   install
        nop
        jal   run
        nop
        la    $a0, straight_code
        la    $a1, straight_end
        jal   install
        nop
        jal   run
        nop
        addiu $s1, $s1, -1
        bnez  $s1, round
        nop

        la    $a0, msg_end
        jal   puts
        nop
1:      b     1b                      # the host stops the machine once it has seen the last line
        nop
        .end _start

# install(from, to): copies the words in [from, to) to 00180000, outside the
# executable, and tells the kernel, as a program must on a console before it
# runs code it has written.
        .ent install
install: lui  $t0, 0x0018
1:      lw    $t1, 0($a0)
        sw    $t1, 0($t0)
        addiu $a0, $a0, 4
        bne   $a0, $a1, 1b
        addiu $t0, $t0, 4
        addiu $a0, $zero, 2           # FlushCache(INVALIDATE_ICACHE)
        addiu $v1, $zero, 100
        syscall
        jr    $ra
        nop
        .end install

# run(): calls the code at 00180000 and shows what it returns.
        .ent run
run:    addiu $sp, $sp, -16
        sd    $ra, 0($sp)
        lui   $t9, 0x0018
        jalr  $t9
        nop
        jal   show
        move  $a0, $v0
        ld    $ra, 0($sp)
        jr    $ra
        addiu $sp, $sp, 16
        .end run

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

# The two routines, as the words install copies.
        .data
        .align 4
loop_code:                            # 3 + 2 + 1 = 6
        .word 0x24020000              # addiu $v0, $zero, 0
        .word 0x24080003              # addiu $t0, $zero, 3
        .word 0x00481021              # 1: addu $v0, $v0, $t0
        .word 0x2508FFFF              # addiu $t0, $t0, -1
        .word 0x1500FFFD              # bnez $t0, 1b
        .word 0x00000000              # nop
        .word 0x03E00008              # jr $ra
        .word 0x00000000              # nop
loop_end:
straight_code:                        # 3 + 4 = 7
        .word 0x24020003              # addiu $v0, $zero, 3
        .word 0x24420004              # addiu $v0, $v0, 4
        .word 0x03E00008              # jr $ra
        .word 0x00000000              # nop
straight_end:

        .bss
        .align 4
stack:  .space 4096
stack_top:
