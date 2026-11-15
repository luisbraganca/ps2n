# SPDX-License-Identifier: GPL-3.0-or-later
#
# A program that loads other programs itself, over the same addresses, and
# runs them in turn, as a game made of several executables does (ADR 0026):
# without the BIOS's loader, so that the core never sees the executable
# change.
#
# The two programs are in tests/programs/loader/, each an executable of its
# own that the build assembles at 0x00200000. This one holds both
# executables (.incbin), and copies each one's loadable segments into place
# as a loader reads an ELF file, then calls its entry point: A, then B over
# it, then A again. What must happen:
#
#   - built with the two compiled as executables it loads itself
#     (ps2n_add_image ... LOADED_BY), the one in memory is told by its code,
#     and runs compiled: nothing of either is interpreted
#     (programs/loaded/loader);
#   - under every configuration it prints what a console prints.
#
# It talks to nothing but the EE's serial port (which the core logs as
# console output) and the kernel's FlushCache.

        .text
        .globl _start
        .ent _start
_start: la    $sp, stack_top
        la    $a0, msg_begin
        jal   puts
        nop

        la    $a0, program_a
        jal   run
        nop
        la    $a0, program_b
        jal   run
        nop
        la    $a0, program_a
        jal   run
        nop

        la    $a0, msg_end
        jal   puts
        nop
1:      b     1b                      # the host stops the machine once it has seen the last line
        nop
        .end _start

# run(elf): loads the executable at `elf` (its PT_LOAD segments, the bytes it
# has and zeros up to the size it takes) and calls its entry point.
        .ent run
run:    addiu $sp, $sp, -16
        sd    $ra, 0($sp)
        sd    $s0, 8($sp)
        move  $s0, $a0
        lw    $t0, 28($s0)            # e_phoff
        addu  $t0, $s0, $t0           # the first program header
        lhu   $t1, 44($s0)            # e_phnum
2:      beqz  $t1, 5f
        nop
        lw    $t2, 0($t0)             # p_type
        addiu $t3, $zero, 1           # PT_LOAD
        bne   $t2, $t3, 4f
        nop
        lw    $t4, 4($t0)             # p_offset
        addu  $t4, $s0, $t4           # from here
        lw    $t5, 8($t0)             # p_vaddr: to there
        lw    $t6, 16($t0)            # p_filesz
        lw    $t7, 20($t0)            # p_memsz
        move  $t8, $zero
3:      sltu  $t9, $t8, $t6           # the bytes it has
        beqz  $t9, 6f
        addu  $a1, $t4, $t8
        lbu   $a2, 0($a1)
        addu  $a1, $t5, $t8
        sb    $a2, 0($a1)
        b     3b
        addiu $t8, $t8, 1
6:      sltu  $t9, $t8, $t7           # and zeros
        beqz  $t9, 4f
        addu  $a1, $t5, $t8
        sb    $zero, 0($a1)
        b     6b
        addiu $t8, $t8, 1
4:      addiu $t0, $t0, 32            # the next program header
        b     2b
        addiu $t1, $t1, -1
5:      move  $a0, $zero              # FlushCache(WRITEBACK_DCACHE)
        addiu $v1, $zero, 100
        syscall
        addiu $a0, $zero, 2           # FlushCache(INVALIDATE_ICACHE), as a program must
        addiu $v1, $zero, 100         # on a console before it runs code it has written
        syscall
        lw    $t0, 24($s0)            # e_entry
        jalr  $t0
        nop
        ld    $ra, 0($sp)
        ld    $s0, 8($sp)
        jr    $ra
        addiu $sp, $sp, 16
        .end run

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
msg_end:    .asciz "-- TEST END\n"

# The two executables, as the build assembled them (tests/programs/loader).
        .data
        .align 4
program_a:  .incbin "loader_a.elf"
        .align 4
program_b:  .incbin "loader_b.elf"

        .bss
        .align 4
stack:  .space 4096
stack_top:
