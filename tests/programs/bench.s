# SPDX-License-Identifier: GPL-3.0-or-later
#
# CPU-bound loops for measuring how fast the EE's code runs: step P4 of the
# proof of concept. Each workload prints
#
#     bench <name> start
#     bench <name> = <checksum>
#
# and ps2n-host reports the wall-clock time and the EE cycles between the two
# lines. The checksums must be the same under every CPU configuration, so the
# program is also a test: the floating-point workload only does arithmetic
# that is exact, so that it does not depend on how a configuration rounds.
#
# It talks to nothing but the EE's serial port and needs no toolchain:
# ps2n-recomp assemble makes the executable.

        .text
        .globl _start
        .ent _start
_start: la    $sp, stack_top
        la    $a0, msg_begin
        jal   puts
        nop

        la    $a0, name_sum
        la    $a1, bench_sum
        jal   run
        nop
        la    $a0, name_mem
        la    $a1, bench_mem
        jal   run
        nop
        la    $a0, name_call
        la    $a1, bench_call
        jal   run
        nop
        la    $a0, name_fpu
        la    $a1, bench_fpu
        jal   run
        nop
        la    $a0, name_mmi
        la    $a1, bench_mmi
        jal   run
        nop
        la    $a0, name_jump
        la    $a1, bench_jump
        jal   run
        nop

        la    $a0, msg_end
        jal   puts
        nop
1:      b     1b                      # the host stops the machine once it has seen the last line
        nop
        .end _start

# ---- sum: register arithmetic and one branch per iteration ---------------------
        .ent bench_sum
bench_sum:
        lui   $t0, 0x0200             # 33,554,432 iterations
        move  $v0, $zero
        move  $t1, $zero
1:      sll   $t2, $v0, 5
        subu  $t2, $t2, $v0           # v0 * 31
        addu  $v0, $t2, $t1
        xor   $v0, $v0, $t0
        addiu $t1, $t1, 1
        bne   $t1, $t0, 1b
        srl   $t3, $v0, 3
        jr    $ra
        nop
        .end bench_sum

# ---- mem: word stores and loads over 64 KB ---------------------------------------
        .ent bench_mem
bench_mem:
        addiu $t9, $zero, 512         # passes
        move  $v0, $zero
1:      la    $t0, buffer
        ori   $t1, $zero, 0x4000      # 16,384 words
        move  $t2, $t9
2:      sw    $t2, 0($t0)
        lw    $t3, 0($t0)
        addu  $v0, $v0, $t3
        addiu $t2, $t2, 7
        addiu $t1, $t1, -1
        bnez  $t1, 2b
        addiu $t0, $t0, 4
        addiu $t9, $t9, -1
        bnez  $t9, 1b
        nop
        jr    $ra
        nop
        .end bench_mem

# ---- call: calls, returns and the stack ---------------------------------------------
        .ent bench_call
bench_call:
        addiu $sp, $sp, -16
        sd    $ra, 0($sp)
        jal   fib
        addiu $a0, $zero, 32
        ld    $ra, 0($sp)
        jr    $ra
        addiu $sp, $sp, 16
        .end bench_call

        .ent fib
fib:    slti  $t0, $a0, 2
        beqz  $t0, 1f
        nop
        jr    $ra
        move  $v0, $a0
1:      addiu $sp, $sp, -32
        sd    $ra, 0($sp)
        sd    $s0, 8($sp)
        sd    $s1, 16($sp)
        move  $s0, $a0
        jal   fib
        addiu $a0, $s0, -1
        move  $s1, $v0
        jal   fib
        addiu $a0, $s0, -2
        addu  $v0, $v0, $s1
        ld    $ra, 0($sp)
        ld    $s0, 8($sp)
        ld    $s1, 16($sp)
        jr    $ra
        addiu $sp, $sp, 32
        .end fib

# ---- fpu: loads, multiplies and adds; every value an integer below 2^24 ---------------
        .ent bench_fpu
bench_fpu:
        la    $t0, fbuf
        addiu $t1, $zero, 1024
        move  $t2, $zero
1:      andi  $t3, $t2, 15
        mtc1  $t3, $f0
        cvt.s.w $f0, $f0
        swc1  $f0, 0($t0)
        addiu $t2, $t2, 1
        addiu $t1, $t1, -1
        bnez  $t1, 1b
        addiu $t0, $t0, 4

        addiu $t9, $zero, 16384       # passes
        move  $v0, $zero
2:      la    $t0, fbuf
        addiu $t1, $zero, 512         # pairs
        mtc1  $zero, $f2
3:      lwc1  $f0, 0($t0)
        lwc1  $f1, 4($t0)
        mul.s $f3, $f0, $f1
        add.s $f2, $f2, $f3
        addiu $t1, $t1, -1
        bnez  $t1, 3b
        addiu $t0, $t0, 8
        cvt.w.s $f4, $f2
        mfc1  $t4, $f4
        addu  $v0, $v0, $t4
        addu  $v0, $v0, $t9
        addiu $t9, $t9, -1
        bnez  $t9, 2b
        nop
        jr    $ra
        nop
        .end bench_fpu

# ---- mmi: quadword loads and stores, parallel add, shift and exclusive-or --------------
        .ent bench_mmi
bench_mmi:
        addiu $t9, $zero, 1024        # passes
        la    $t2, vector
        lq    $t3, 0($t2)
1:      la    $t0, buffer
        addiu $t1, $zero, 4096        # quadwords
2:      lq    $t4, 0($t0)
        paddw $t4, $t4, $t3
        psrlw $t5, $t4, 3
        pxor  $t4, $t4, $t5
        sq    $t4, 0($t0)
        addiu $t1, $t1, -1
        bnez  $t1, 2b
        addiu $t0, $t0, 16
        addiu $t9, $t9, -1
        bnez  $t9, 1b
        nop

        la    $t0, buffer             # fold the buffer into the checksum
        ori   $t1, $zero, 0x4000
        move  $v0, $zero
3:      lw    $t3, 0($t0)
        addu  $v0, $v0, $t3
        addiu $t1, $t1, -1
        bnez  $t1, 3b
        addiu $t0, $t0, 4
        jr    $ra
        nop
        .end bench_mmi

# ---- jump: a jump through a table every iteration ------------------------------------
        .ent bench_jump
bench_jump:
        lui   $t0, 0x0100             # 16,777,216 iterations
        move  $v0, $zero
        la    $t5, jump_table
1:      andi  $t1, $t0, 3
        sll   $t1, $t1, 2
        addu  $t1, $t1, $t5
        lw    $t2, 0($t1)
        jr    $t2
        nop
case0:  b     9f
        addiu $v0, $v0, 1
case1:  b     9f
        addiu $v0, $v0, 3
case2:  b     9f
        xori  $v0, $v0, 0x55
case3:  sll   $v0, $v0, 1
9:      addiu $t0, $t0, -1
        bnez  $t0, 1b
        nop
        jr    $ra
        nop
        .end bench_jump

# ---- run(name, function): the two lines around a workload -------------------------------
        .ent run
run:    addiu $sp, $sp, -32
        sd    $ra, 0($sp)
        sd    $s0, 8($sp)
        sd    $s1, 16($sp)
        move  $s0, $a0
        move  $s1, $a1
        la    $a0, msg_bench
        jal   puts
        nop
        move  $a0, $s0
        jal   puts
        nop
        la    $a0, msg_start
        jal   puts
        nop
        jalr  $s1
        nop
        move  $s1, $v0
        la    $a0, msg_bench
        jal   puts
        nop
        move  $a0, $s0
        jal   puts
        nop
        la    $a0, msg_equals
        jal   puts
        nop
        move  $a0, $s1
        jal   puthex
        nop
        ld    $ra, 0($sp)
        ld    $s0, 8($sp)
        ld    $s1, 16($sp)
        jr    $ra
        addiu $sp, $sp, 32
        .end run

# puthex(value): eight hexadecimal digits and a line end.
        .ent puthex
puthex: lui   $t1, 0x1000
        ori   $t1, $t1, 0xF180        # SIO_TXFIFO
        addiu $t2, $zero, 28
1:      srlv  $t0, $a0, $t2
        andi  $t0, $t0, 15
        sltiu $t3, $t0, 10
        bnez  $t3, 2f
        addiu $t0, $t0, 48            # '0' + digit
        addiu $t0, $t0, 39            # 'a' - '0' - 10
2:      sb    $t0, 0($t1)
        bnez  $t2, 1b
        addiu $t2, $t2, -4
        addiu $t0, $zero, 10
        jr    $ra
        sb    $t0, 0($t1)
        .end puthex

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

        .rodata
msg_begin:  .asciz "-- TEST BEGIN\n"
msg_end:    .asciz "-- TEST END\n"
msg_bench:  .asciz "bench "
msg_start:  .asciz " start\n"
msg_equals: .asciz " = "
name_sum:   .asciz "sum"
name_mem:   .asciz "mem"
name_call:  .asciz "call"
name_fpu:   .asciz "fpu"
name_mmi:   .asciz "mmi"
name_jump:  .asciz "jump"
        .align 2
jump_table: .word case0, case1, case2, case3

# Data on pages of its own: nothing writes to a page that holds code.
        .data
        .align 12
vector: .word 0x00000001, 0x00010000, 0x7FFFFFFF, 0x80000001

        .bss
        .align 4
buffer: .space 65536
fbuf:   .space 4096
stack:  .space 65536
stack_top:
