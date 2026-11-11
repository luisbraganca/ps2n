# SPDX-License-Identifier: GPL-3.0-or-later
#
# The EE's count register overflowing while it is read.
#
# The count register (COP0 register 9) is 32 bits wide and goes up by one
# every cycle. The register that follows it in PCSX2's memory is EntryHi (COP0
# register 10). PCSX2's recompiler brings the count register up to date, when
# it is read, with a 64-bit addition, so that when the count overflows the
# carry goes into EntryHi: it goes up by one (recMFC0, pcsx2/x86/iCOP0.cpp,
# PCSX2 v2.9.170). PCSX2's interpreter adds 32 bits and leaves EntryHi alone,
# and nothing says a console does otherwise.
#
# This program writes a value of its own to EntryHi, sets the count register
# one short of overflowing, reads it, and prints how far EntryHi moved. All of
# that is in one block of instructions with no branch in it, so that no
# interrupt comes in between and nothing else touches either register.
#
# There is no console recording of this program: what it prints is what
# PCSX2's recompiler makes of it, which PS2Native reproduces on purpose
# (docs/adr/0013-pcsx2s-count-register-carry-mirrored-until-fixed.md).
# PCSX2's interpreter prints 0, which tests/programs-known-differences.cmake
# records. The day the pinned PCSX2 no longer has the defect, this test fails
# and says so.
#
# It talks to nothing but the EE's serial port, so ps2n-recomp assemble makes
# the executable.

        .text
        .globl _start
        .ent _start
_start: la    $sp, stack_top
        la    $a0, msg_begin
        jal   puts
        nop

        mfc0  $s1, $10                # EntryHi as the kernel left it
        addiu $t0, $zero, 16
        mtc0  $t0, $10                # a value of our own
        addiu $t1, $zero, -1
        mtc0  $t1, $9                 # the count register one short of overflowing
        mfc0  $t2, $9                 # read: at least one cycle has passed, it overflows
        mfc0  $s0, $10                # EntryHi now
        mtc0  $s1, $10                # the kernel's value back
        addiu $s0, $s0, -16           # how far it moved

        la    $a0, msg_moved
        jal   puts
        nop
        lui   $t1, 0x1000
        ori   $t1, $t1, 0xF180        # SIO_TXFIFO
        addiu $t0, $s0, 48            # '0' + n
        sb    $t0, 0($t1)
        addiu $t0, $zero, 10
        sb    $t0, 0($t1)

        la    $a0, msg_end
        jal   puts
        nop
1:      b     1b                      # the host stops the machine once it has seen the last line
        nop
        .end _start

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
msg_moved:  .asciz "EntryHi moved by "
msg_end:    .asciz "-- TEST END\n"

        .bss
        .align 4
stack:  .space 4096
stack_top:
