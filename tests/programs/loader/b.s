# SPDX-License-Identifier: GPL-3.0-or-later
#
# The second of the two programs tests/programs/loader.s loads itself, over
# the first: the build assembles it at 0x00200000 too, with its functions in
# another order, so that their places differ from the first's. It prints
# what it counts down from and returns to the loader.

        .text
# puts(text): writes a zero-terminated string to the serial port.
        .ent puts
puts:   lui   $t1, 0x1000
        ori   $t1, $t1, 0xF180
1:      lbu   $t0, 0($a0)
        beqz  $t0, 2f
        addiu $a0, $a0, 1
        b     1b
        sb    $t0, 0($t1)
2:      jr    $ra
        nop
        .end puts

        .globl _start
        .ent _start
_start: addiu $sp, $sp, -16
        sd    $ra, 0($sp)
        la    $a0, msg_name
        jal   puts
        nop
        addiu $t0, $zero, 7           # count down from 7
        move  $t1, $t0
1:      addiu $t1, $t1, -1
        bnez  $t1, 1b
        nop
        lui   $t2, 0x1000
        ori   $t2, $t2, 0xF180        # SIO_TXFIFO
        addiu $t0, $t0, 48            # '0' + 7
        sb    $t0, 0($t2)
        addiu $t0, $zero, 10
        sb    $t0, 0($t2)
        ld    $ra, 0($sp)
        jr    $ra
        addiu $sp, $sp, 16
        .end _start

        .rodata
msg_name:   .asciz "program b: "
