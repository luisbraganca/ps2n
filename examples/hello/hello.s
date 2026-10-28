# hello.s - a small PS2 program for the end-to-end example.
#
# Assembled with `ps2-uncompiler assemble` (no PS2 toolchain needed) into an
# ordinary EE ELF. It starts the way real programs do (SetupThread,
# SetupHeap, main, Exit) and exercises the parts of the pipeline that matter:
# loops, recursion, a switch jump table, function pointers, HI/LO multiply/
# divide, 64-bit ops, unaligned access, branch-likely, MMI, the FPU, VU0
# macro mode, a non-local return (setjmp/longjmp), kernel threads with
# semaphores, and a vblank interrupt handler.

        .set noreorder
        .text

# ---------------------------------------------------------------------------
# Startup
        .globl _start
        .ent _start
_start:
        la      $v0, _fbss              # clear .bss with 128-bit stores
        la      $v1, _end
1:      sltu    $at, $v0, $v1
        beq     $at, $zero, 2f
        nop
        sq      $zero, 0($v0)
        addiu   $v0, $v0, 16
        j       1b
        nop
2:      move    $a0, $zero              # SetupThread(gp, stack=-1, size, args, root)
        li      $a1, -1
        li      $a2, 0x20000
        la      $a3, _args
        move    $t0, $zero
        move    $gp, $a0
        li      $v1, 0x3c
        syscall
        move    $sp, $v0
        la      $a0, _end               # SetupHeap(_end, -1)
        li      $a1, -1
        li      $v1, 0x3d
        syscall
        jal     main
        nop
        move    $a0, $v0                # Exit(main's result)
        li      $v1, 4
        syscall
3:      b       3b
        nop
        .end _start

# printf: the kernel's _print syscall formats like printf. Because the symbol
# is called "printf", the recompiler also binds it to the runtime's printf stub
# ([stubs] auto), which produces the same output.
        .globl printf
        .ent printf
printf:
        li      $v1, 0x75
        syscall
        jr      $ra
        nop
        .end printf

# ---------------------------------------------------------------------------
        .globl main
        .ent main
main:
        addiu   $sp, $sp, -48
        sd      $ra, 32($sp)
        la      $a0, s_hello
        jal     printf
        nop
        jal     test_loop
        nop
        jal     test_recursion
        nop
        jal     test_switch
        nop
        jal     test_pointers
        nop
        jal     test_muldiv
        nop
        jal     test_64bit
        nop
        jal     test_unaligned
        nop
        jal     test_likely
        nop
        jal     test_mmi
        nop
        jal     test_fpu
        nop
        jal     test_vu0
        nop
        jal     test_longjmp
        nop
        jal     test_threads
        nop
        jal     test_vsync
        nop
        la      $a0, s_done
        jal     printf
        nop
        ld      $ra, 32($sp)
        move    $v0, $zero
        jr      $ra
        addiu   $sp, $sp, 48
        .end main

# ---------------------------------------------------------------------------
        .ent test_loop
test_loop:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        move    $a1, $zero
        li      $t0, 1
1:      addu    $a1, $a1, $t0
        slti    $t1, $t0, 100
        bne     $t1, $zero, 1b
        addiu   $t0, $t0, 1             # delay slot runs on every iteration
        la      $a0, s_loop
        jal     printf
        nop
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end test_loop

# ---------------------------------------------------------------------------
        .ent fib
fib:                                    # fib(n), recursive
        slti    $t0, $a0, 2
        beq     $t0, $zero, 1f
        nop
        jr      $ra
        move    $v0, $a0
1:      addiu   $sp, $sp, -32
        sd      $ra, 0($sp)
        sd      $s0, 8($sp)
        sd      $a0, 16($sp)
        jal     fib
        addiu   $a0, $a0, -1            # argument set in the delay slot
        move    $s0, $v0
        ld      $a0, 16($sp)
        jal     fib
        addiu   $a0, $a0, -2
        addu    $v0, $v0, $s0
        ld      $s0, 8($sp)
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 32
        .end fib

        .ent test_recursion
test_recursion:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        jal     fib
        li      $a0, 20
        move    $a1, $v0
        la      $a0, s_fib
        jal     printf
        nop
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end test_recursion

# ---------------------------------------------------------------------------
# A GCC-style switch: bounds check, then an indexed jump table in .rodata.
        .ent classify
classify:
        sltiu   $t0, $a0, 5
        beq     $t0, $zero, 9f
        sll     $t1, $a0, 2
        lui     $at, %hi(jt_classify)
        addu    $at, $at, $t1
        lw      $t2, %lo(jt_classify)($at)
        jr      $t2
        nop
case0:  la      $v0, s_zero
        jr      $ra
        nop
case1:  la      $v0, s_one
        jr      $ra
        nop
case2:  la      $v0, s_two
        jr      $ra
        nop
case3:  b       case1                   # shares case 1's code
        nop
case4:  la      $v0, s_four
        jr      $ra
        nop
9:      la      $v0, s_many
        jr      $ra
        nop
        .end classify

        .ent test_switch
test_switch:
        addiu   $sp, $sp, -32
        sd      $ra, 0($sp)
        sd      $s0, 8($sp)
        move    $s0, $zero
1:      jal     classify
        move    $a0, $s0
        move    $a2, $v0
        move    $a1, $s0
        la      $a0, s_switch
        jal     printf
        nop
        addiu   $s0, $s0, 1
        slti    $t0, $s0, 7
        bne     $t0, $zero, 1b
        nop
        ld      $s0, 8($sp)
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 32
        .end test_switch

# ---------------------------------------------------------------------------
        .ent op_add
op_add: jr      $ra
        addu    $v0, $a0, $a1
        .end op_add

        .ent op_sub
op_sub: jr      $ra
        subu    $v0, $a0, $a1
        .end op_sub

        .ent op_mul
op_mul: jr      $ra
        mult    $v0, $a0, $a1           # R5900 three-operand form also writes rd
        .end op_mul

        .ent test_pointers
test_pointers:
        addiu   $sp, $sp, -32
        sd      $ra, 0($sp)
        sd      $s0, 8($sp)
        move    $s0, $zero
1:      la      $t0, ops_table
        sll     $t1, $s0, 2
        addu    $t0, $t0, $t1
        lw      $t9, 0($t0)
        li      $a0, 7
        jalr    $t9
        li      $a1, 5
        move    $a2, $v0
        move    $a1, $s0
        la      $a0, s_pointers
        jal     printf
        nop
        addiu   $s0, $s0, 1
        slti    $t0, $s0, 3
        bne     $t0, $zero, 1b
        nop
        ld      $s0, 8($sp)
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 32
        .end test_pointers

# ---------------------------------------------------------------------------
        .ent test_muldiv
test_muldiv:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        li      $a0, 12345
        li      $a1, 6789
        mult    $a0, $a1
        mflo    $a1
        la      $a0, s_mul
        jal     printf
        nop
        li      $t0, 1000000
        li      $t1, 7
        div     $t0, $t1
        mflo    $a1
        mfhi    $a2
        la      $a0, s_div
        jal     printf
        nop
        li      $t0, -5                 # R5900 divide by zero: LO = 1 (negative dividend), HI = dividend
        div     $t0, $zero
        mflo    $a1
        mfhi    $a2
        la      $a0, s_divzero
        jal     printf
        nop
        li      $t0, 10                 # MADD: {HI,LO} += 3 * 4
        mtlo    $t0
        mthi    $zero
        li      $t1, 3
        li      $t2, 4
        madd    $a1, $t1, $t2
        la      $a0, s_madd
        jal     printf
        nop
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end test_muldiv

# ---------------------------------------------------------------------------
        .ent test_64bit
test_64bit:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        li      $t0, 0x12345678
        dsll32  $t0, $t0, 0
        ori     $t0, $t0, 0x9abc
        move    $a1, $t0                # full 64-bit value for %llx
        dsrl32  $a2, $t0, 0
        la      $a0, s_64
        jal     printf
        nop
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end test_64bit

# ---------------------------------------------------------------------------
        .ent test_unaligned
test_unaligned:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        la      $t0, bytes
        lwr     $a1, 1($t0)             # the word at bytes+1, little-endian
        lwl     $a1, 4($t0)
        la      $a0, s_unaligned
        jal     printf
        nop
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end test_unaligned

# ---------------------------------------------------------------------------
        .ent test_likely
test_likely:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        li      $t0, 5
        move    $a1, $zero
1:      addiu   $t0, $t0, -1
        bnel    $t0, $zero, 1b
        addiu   $a1, $a1, 10            # only runs when the branch is taken
        la      $a0, s_likely
        jal     printf
        nop
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end test_likely

# ---------------------------------------------------------------------------
        .ent test_mmi
test_mmi:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        la      $t0, mmi_a
        lq      $t1, 0($t0)
        la      $t0, mmi_b
        lq      $t2, 0($t0)
        paddw   $t3, $t1, $t2
        la      $t4, mmi_out
        sq      $t3, 0($t4)
        lw      $a1, 0($t4)
        lw      $a2, 4($t4)
        lw      $a3, 8($t4)
        lw      $t0, 12($t4)
        la      $a0, s_paddw
        jal     printf
        nop
        la      $t0, mmi_a
        lq      $t1, 0($t0)
        la      $t0, mmi_b
        lq      $t2, 0($t0)
        pextlw  $t3, $t1, $t2           # interleave the low words: b0 a0 b1 a1
        la      $t4, mmi_out
        sq      $t3, 0($t4)
        lw      $a1, 0($t4)
        lw      $a2, 4($t4)
        lw      $a3, 8($t4)
        lw      $t0, 12($t4)
        la      $a0, s_pextlw
        jal     printf
        nop
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end test_mmi

# ---------------------------------------------------------------------------
        .ent test_fpu
test_fpu:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        li      $t0, 2
        mtc1    $t0, $f0
        cvt.s.w $f0, $f0                # 2.0
        sqrt.s  $f1, $f0
        li      $t0, 100
        mtc1    $t0, $f2
        cvt.s.w $f2, $f2                # 100.0
        mul.s   $f3, $f1, $f2
        cvt.w.s $f4, $f3
        mfc1    $a1, $f4                # 141
        move    $a2, $zero
        c.lt.s  $f0, $f2
        bc1f    1f
        nop
        li      $a2, 1
1:      mtc1    $zero, $f6
        div.s   $f5, $f2, $f6           # no infinities on the EE: +FLT_MAX
        cvt.w.s $f5, $f5
        mfc1    $a3, $f5
        la      $a0, s_fpu
        jal     printf
        nop
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end test_fpu

# ---------------------------------------------------------------------------
        .ent test_vu0
test_vu0:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        la      $t0, vec_a
        lqc2    $vf1, 0($t0)            # (1, 2, 3, 4)
        la      $t0, vec_b
        lqc2    $vf2, 0($t0)            # (10, 20, 30, 40)
        vadd.xyzw $vf3, $vf1, $vf2
        vftoi0.xyzw $vf5, $vf3
        la      $t4, vec_out
        sqc2    $vf5, 0($t4)
        lw      $a1, 0($t4)
        lw      $a2, 4($t4)
        lw      $a3, 8($t4)
        lw      $t0, 12($t4)
        la      $a0, s_vadd
        jal     printf
        nop
        vdiv    $Q, $vf2x, $vf1y        # Q = 10 / 2
        vmulq.x $vf6, $vf1, $Q          # 1 * Q
        vftoi0.x $vf7, $vf6
        qmfc2   $a1, $vf7
        sll     $a1, $a1, 0             # keep the x component, sign-extended
        la      $a0, s_vdiv
        jal     printf
        nop
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end test_vu0

# ---------------------------------------------------------------------------
# setjmp/longjmp written by hand: longjmp returns into a frame several calls
# up the stack, which the runtime handles by unwinding host frames.
        .ent my_setjmp
my_setjmp:
        sw      $sp, 0($a0)
        sw      $ra, 4($a0)
        sw      $s0, 8($a0)
        jr      $ra
        move    $v0, $zero
        .end my_setjmp

        .ent my_longjmp
my_longjmp:
        lw      $sp, 0($a0)
        lw      $ra, 4($a0)
        lw      $s0, 8($a0)
        jr      $ra
        move    $v0, $a1
        .end my_longjmp

        .ent deep
deep:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        beq     $a0, $zero, 1f
        nop
        jal     deep
        addiu   $a0, $a0, -1
        ld      $ra, 0($sp)             # not reached: the innermost call longjmps
        jr      $ra
        addiu   $sp, $sp, 16
1:      la      $a0, jbuf
        jal     my_longjmp
        li      $a1, 42
        .end deep

        .ent test_longjmp
test_longjmp:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        la      $a0, jbuf
        jal     my_setjmp
        nop
        bne     $v0, $zero, 1f
        nop
        jal     deep
        li      $a0, 5
1:      move    $a1, $v0
        la      $a0, s_longjmp
        jal     printf
        nop
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end test_longjmp

# ---------------------------------------------------------------------------
# Threads: main (priority 1) starts a worker (priority 2) and blocks on a
# semaphore; each SignalSema from the worker preempts it in favour of main.
        .ent worker
worker:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        move    $a1, $a0
        la      $a0, s_worker_run
        jal     printf
        nop
        lw      $a0, sema_id
        li      $v1, 0x42               # SignalSema
        syscall
        la      $a0, s_worker_end
        jal     printf
        nop
        lw      $a0, sema_id
        li      $v1, 0x42
        syscall
        ld      $ra, 0($sp)
        jr      $ra                     # returning ends the thread
        addiu   $sp, $sp, 16
        .end worker

        .ent test_threads
test_threads:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        li      $v1, 0x2f               # GetThreadId
        syscall
        move    $a0, $v0
        li      $a1, 1
        li      $v1, 0x29               # ChangeThreadPriority(self, 1)
        syscall
        la      $a0, sema_param
        li      $v1, 0x40               # CreateSema
        syscall
        sw      $v0, sema_id
        la      $a0, thread_param
        li      $v1, 0x20               # CreateThread
        syscall
        move    $a0, $v0
        li      $a1, 7                  # argument passed to the worker
        li      $v1, 0x22               # StartThread
        syscall
        la      $a0, s_started
        jal     printf
        nop
        lw      $a0, sema_id
        li      $v1, 0x44               # WaitSema: blocks, the worker runs
        syscall
        la      $a0, s_woke
        jal     printf
        nop
        lw      $a0, sema_id
        li      $v1, 0x44
        syscall
        la      $a0, s_finished
        jal     printf
        nop
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end test_threads

# ---------------------------------------------------------------------------
# A vblank interrupt handler counts frames while main busy-waits on the
# counter; interrupts are delivered at the loop's back-edge safepoint.
        .ent vsync_handler
vsync_handler:
        lw      $t0, vsync_count
        addiu   $t0, $t0, 1
        sw      $t0, vsync_count
        jr      $ra
        move    $v0, $zero
        .end vsync_handler

        .ent test_vsync
test_vsync:
        addiu   $sp, $sp, -16
        sd      $ra, 0($sp)
        li      $a0, 2                  # INTC cause 2: vblank start
        la      $a1, vsync_handler
        move    $a2, $zero
        move    $a3, $zero
        li      $v1, 0x10               # AddIntcHandler
        syscall
        li      $a0, 2
        li      $v1, 0x14               # _EnableIntc
        syscall
1:      lw      $a1, vsync_count
        slti    $t0, $a1, 3
        bne     $t0, $zero, 1b
        nop
        la      $a0, s_vsync
        jal     printf
        nop
        li      $a0, 2
        li      $v1, 0x15               # _DisableIntc
        syscall
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 16
        .end test_vsync

# ---------------------------------------------------------------------------
        .rodata
jt_classify:    .word case0, case1, case2, case3, case4
s_hello:        .asciz "Hello from a recompiled PS2 program!\n"
s_loop:         .asciz "loop: sum(1..100) = %d\n"
s_fib:          .asciz "recursion: fib(20) = %d\n"
s_switch:       .asciz "switch: %d -> %s\n"
s_zero:         .asciz "zero"
s_one:          .asciz "one"
s_two:          .asciz "two"
s_four:         .asciz "four"
s_many:         .asciz "many"
s_pointers:     .asciz "pointers: op%d(7, 5) = %d\n"
s_mul:          .asciz "muldiv: 12345 * 6789 = %d\n"
s_div:          .asciz "muldiv: 1000000 / 7 = %d rem %d\n"
s_divzero:      .asciz "muldiv: -5 / 0 -> lo %d, hi %d\n"
s_madd:         .asciz "muldiv: madd 10 + 3*4 = %d\n"
s_64:           .asciz "64bit: %llx (high word %x)\n"
s_unaligned:    .asciz "unaligned: %08x\n"
s_likely:       .asciz "likely: %d\n"
s_paddw:        .asciz "mmi: paddw = %d %d %d %d\n"
s_pextlw:       .asciz "mmi: pextlw = %d %d %d %d\n"
s_fpu:          .asciz "fpu: sqrt(2)*100 = %d, 2 < 100 = %d, 100/0 = %d\n"
s_vadd:         .asciz "vu0: vadd = %d %d %d %d\n"
s_vdiv:         .asciz "vu0: 10 / 2 via Q = %d\n"
s_longjmp:      .asciz "longjmp: setjmp returned %d\n"
s_worker_run:   .asciz "thread: worker running, arg=%d\n"
s_worker_end:   .asciz "thread: worker finishing\n"
s_started:      .asciz "thread: started worker\n"
s_woke:         .asciz "thread: main woke up\n"
s_finished:     .asciz "thread: worker finished\n"
s_vsync:        .asciz "vsync: counted %d vblank interrupts\n"
s_done:         .asciz "done\n"

        .data
        .align 4
mmi_a:          .word 1, 2, 3, 4
mmi_b:          .word 10, 20, 30, 40
mmi_out:        .space 16
vec_a:          .float 1.0, 2.0, 3.0, 4.0
vec_b:          .float 10.0, 20.0, 30.0, 40.0
vec_out:        .space 16
ops_table:      .word op_add, op_sub, op_mul
bytes:          .byte 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
                .align 2
vsync_count:    .word 0
sema_id:        .word 0
sema_param:     .word 0, 1, 0, 0, 0, 0                  # count, max, init, waiters, attr, option
thread_param:   .word 0, worker, worker_stack, 0x1000, 0, 2, 0, 0, 0

        .bss
        .align 4
_fbss:
_args:          .space 336
jbuf:           .space 16
                .align 4
worker_stack:   .space 0x1000
_end:
