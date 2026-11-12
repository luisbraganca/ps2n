# SPDX-License-Identifier: GPL-3.0-or-later
#
# A loop that waits for an interrupt and, while it waits, stores what memory
# already holds.
#
# A handler of the vertical blank's start counts a word up. The program waits
# for the word to change eight times, each time in a loop of one block that
# loads the word, stores the same three values over and over, and goes back
# while the word has not changed. Once a time round has stored only what
# memory held, every time round after it does exactly the same until the
# next event test, which is where the interrupt comes: nothing else runs on
# the machine in between. Compiled code does not run those times round. It
# lets their cycles pass at once and makes the event test at the cycle count
# at which PCSX2's recompiler, which runs them all, makes it
# (PS2N_BLOCK_END_FIXED in runtime/include/ps2n/recomp.h). The same-as-stock
# test holds it to that at every sync point, and the lockstep test to the
# interpreter, which runs them all too, at every event test.
#
# There is no console recording of this program: what it prints is what
# PCSX2's recompiler makes of it. It talks to the kernel, for the handler,
# and to the EE's serial port, so ps2n-recomp assemble makes the executable.

        .text
        .globl _start
        .ent _start
_start: la    $sp, stack_top
        la    $a0, msg_begin
        jal   puts
        nop

        # AddIntcHandler(INTC_VBLANK_S, vblank, 0, 0); EnableIntc(INTC_VBLANK_S).
        addiu $a0, $zero, 2
        la    $a1, vblank
        move  $a2, $zero
        move  $a3, $zero
        addiu $v1, $zero, 0x10
        syscall
        nop
        addiu $a0, $zero, 2
        addiu $v1, $zero, 0x14
        syscall
        nop
        # Interrupts on. The kernel hands a program over with
        # "di; mfc0 $k0, Status; ori; mtc0 $k0, Status; eret" (80002FA8 in this
        # BIOS): under PCSX2's recompiler, where DI takes effect an instruction
        # late (docs/execution-model.md, rule 8), the program starts with them
        # on; under its interpreter, with them off. Status.EDI lets a program
        # switch them on itself.
        ei

        # Eight waits for the word to change.
        addiu $s0, $zero, 8
        la    $s1, counter
        la    $s2, copies
        lw    $s4, 0($s1)             # the word before the first wait
1:      lw    $s3, 0($s1)             # the word when this wait begins
2:      lw    $t0, 0($s1)             # from its second time round on, the loop stores what memory holds
        sw    $s3, 0($s2)
        sw    $zero, 4($s2)
        beq   $t0, $s3, 2b
        sw    $s3, 8($s2)
        addiu $s0, $s0, -1
        bnez  $s0, 1b
        nop

        # How far the word moved, and the values the last wait stored, from
        # where the word was before the first.
        la    $a0, msg_moved
        jal   puts
        nop
        lw    $a0, 0($s1)
        jal   putdec
        subu  $a0, $a0, $s4
        la    $a0, msg_stored
        jal   puts
        nop
        lw    $a0, 0($s2)
        jal   putdec
        subu  $a0, $a0, $s4
        la    $a0, msg_space
        jal   puts
        nop
        jal   putdec
        lw    $a0, 4($s2)
        la    $a0, msg_space
        jal   puts
        nop
        lw    $a0, 8($s2)
        jal   putdec
        subu  $a0, $a0, $s4
        la    $a0, msg_newline
        jal   puts
        nop

        la    $a0, msg_end
        jal   puts
        nop
3:      b     3b                      # the host stops the machine once it has seen the last line
        nop
        .end _start

# The handler of the vertical blank's start: counts the word up.
        .ent vblank
vblank: la    $t0, counter
        lw    $t1, 0($t0)
        addiu $t1, $t1, 1
        sw    $t1, 0($t0)
        sync.l
        ei
        jr    $ra
        move  $v0, $zero
        .end vblank

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

# putdec(n): writes n, unsigned, in decimal.
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

        .rodata
msg_begin:   .asciz "-- TEST BEGIN\n"
msg_moved:   .asciz "the word moved by "
msg_stored:  .asciz "; the last wait stored "
msg_space:   .asciz " "
msg_newline: .asciz "\n"
msg_end:     .asciz "-- TEST END\n"

        # A page of their own: in the code's page the loop's stores would keep
        # that page from being write-protected, and a block there is checked
        # against memory every time it runs, which is no time round to skip.
        .data
        .align 12
counter:    .word 0
copies:     .word 0, 0, 0

        .bss
        .align 4
stack:  .space 4096
stack_top:
