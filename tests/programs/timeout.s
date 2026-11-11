# SPDX-License-Identifier: GPL-3.0-or-later
#
# Two loops that count a register down, timed with the EE's count register,
# which goes up by one every cycle.
#
# The first is what PCSX2 calls a timeout loop: nothing but `nop`s, one
# decrement and the branch that closes it. PCSX2's recompiler does not run
# such a loop. It lets eight cycles pass for each count left, whatever the
# loop's instructions cost (this one costs seventeen), and PS2Native does the
# same (ps2n/waitloop.h). So it must take eight cycles a count, and leave the
# register at zero.
#
# The second differs in one thing, the decrement is in the branch's delay
# slot, so it is not a timeout loop and has to be run: sixteen cycles a count.
#
# There is no console recording of this program: what it prints is what
# PCSX2's recompiler makes of it, which is the behavior to match. PCSX2's
# interpreter runs both loops and prints another number for the first.
# It talks to nothing but the EE's serial port, so ps2n-recomp assemble makes
# the executable.

        .text
        .globl _start
        .ent _start
_start: la    $sp, stack_top
        la    $a0, msg_begin
        jal   puts
        nop

        # ---- a timeout loop: 500,000 counts ----
        lui   $s2, 0x0007
        ori   $s2, $s2, 0xA120
        move  $v0, $s2
        mfc0  $s0, $9
1:      nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        addiu $v0, $v0, -1
        bne   $v0, $zero, 1b
        nop
        mfc0  $s1, $9
        move  $s3, $v0                # what is left of the count

        la    $a0, msg_timeout
        jal   puts
        nop
        subu  $a0, $s1, $s0
        divu  $a0, $s2
        jal   putdec
        mflo  $a0
        la    $a0, msg_cycles
        jal   puts
        nop
        jal   putdec
        move  $a0, $s3
        la    $a0, msg_left
        jal   puts
        nop

        # ---- not a timeout loop: 50,000 counts, the decrement in the delay slot ----
        ori   $s2, $zero, 0xC350
        move  $v0, $s2
        mfc0  $s0, $9
2:      nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        bne   $v0, $zero, 2b
        addiu $v0, $v0, -1
        mfc0  $s1, $9

        la    $a0, msg_other
        jal   puts
        nop
        subu  $a0, $s1, $s0
        divu  $a0, $s2
        jal   putdec
        mflo  $a0
        la    $a0, msg_cycles_end
        jal   puts
        nop

        la    $a0, msg_end
        jal   puts
        nop
3:      b     3b                      # the host stops the machine once it has seen the last line
        nop
        .end _start

# putdec(n): writes an unsigned number in decimal to the serial port.
        .ent putdec
putdec: addiu $sp, $sp, -16
        addiu $t2, $sp, 16            # the digits are stored downwards from here
        addiu $t3, $zero, 10
1:      divu  $a0, $t3
        mfhi  $t0
        mflo  $a0
        addiu $t0, $t0, 48            # '0' + digit
        addiu $t2, $t2, -1
        bne   $a0, $zero, 1b
        sb    $t0, 0($t2)
        lui   $t1, 0x1000
        ori   $t1, $t1, 0xF180        # SIO_TXFIFO
        addiu $t3, $sp, 16
2:      lbu   $t0, 0($t2)
        addiu $t2, $t2, 1
        bne   $t2, $t3, 2b
        sb    $t0, 0($t1)
        jr    $ra
        addiu $sp, $sp, 16
        .end putdec

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
msg_begin:      .asciz "-- TEST BEGIN\n"
msg_timeout:    .asciz "timeout loop: "
msg_cycles:     .asciz " cycles a count, "
msg_left:       .asciz " left\n"
msg_other:      .asciz "other loop: "
msg_cycles_end: .asciz " cycles a count\n"
msg_end:        .asciz "-- TEST END\n"

        .bss
        .align 4
stack:  .space 4096
stack_top:
