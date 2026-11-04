# kernel.s - EE kernel HLE: scheduling, semaphores, interrupts, alarms, timers.
        .set noreorder
        .text

        .globl _start
        .ent _start
_start: li      $a1, -1
        li      $a2, 0x20000
        move    $a0, $zero
        move    $a3, $zero
        move    $t0, $zero
        li      $v1, 0x3c               # SetupThread
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

# start_thread(entry=a0, priority=a1, arg=a2) -> thread id
        .ent start_thread
start_thread:
        la      $t0, tparam
        sw      $a0, 4($t0)             # func
        sw      $a1, 20($t0)            # initial priority
        lw      $t1, next_stack
        sw      $t1, 8($t0)             # stack
        li      $t2, 0x800
        sw      $t2, 12($t0)            # stack size
        addu    $t1, $t1, $t2
        sw      $t1, next_stack
        move    $t3, $a2
        move    $a0, $t0
        li      $v1, 0x20               # CreateThread
        syscall
        move    $a0, $v0
        move    $a1, $t3
        li      $v1, 0x22               # StartThread (may switch right away)
        syscall
        jr      $ra
        nop
        .end start_thread

# new_sema(initial count=a0) -> id
        .ent new_sema
new_sema:
        la      $t0, sparam
        sw      $a0, 8($t0)
        li      $t1, 255
        sw      $t1, 4($t0)
        move    $a0, $t0
        li      $v1, 0x40               # CreateSema
        syscall
        jr      $ra
        nop
        .end new_sema

# ---------------------------------------------------------------------------
        .ent thread_a
thread_a:
        li      $v1, 0x32               # SleepThread
        syscall
        la      $a0, s_a_woke
        li      $v1, 0x75
        syscall
        li      $v1, 0x23               # ExitThread
        syscall
        .end thread_a

        .ent thread_b
thread_b:
        li      $v1, 0x32               # wakeups were queued: neither call blocks
        syscall
        li      $v1, 0x32
        syscall
        la      $a0, s_b_slept
        li      $v1, 0x75
        syscall
        lw      $a0, sema_done
        li      $v1, 0x42               # SignalSema
        syscall
        jr      $ra                     # returning from the entry function exits the thread
        nop
        .end thread_b

        .ent vblank_handler
vblank_handler:
        sw      $gp, handler_gp
        lw      $a0, sema_vb
        li      $v1, -0x43              # iSignalSema
        syscall
        jr      $ra
        move    $v0, $zero
        .end vblank_handler

        .ent alarm_callback
alarm_callback:                         # (alarm id, time, arg = thread to wake)
        sw      $gp, alarm_gp
        move    $a0, $a2
        li      $v1, -0x34              # iWakeupThread
        syscall
        jr      $ra
        nop
        .end alarm_callback

        .ent rotator
rotator:                                # a0 = letter
        addiu   $sp, $sp, -32
        sd      $ra, 0($sp)
        sd      $s0, 8($sp)
        sd      $s1, 16($sp)
        move    $s1, $a0
        move    $s0, $zero
1:      la      $a0, s_rotate
        move    $a1, $s1
        jal     printf
        move    $a2, $s0
        li      $a0, 12
        li      $v1, 0x2b               # RotateThreadReadyQueue(12)
        syscall
        addiu   $s0, $s0, 1
        slti    $t0, $s0, 3
        bne     $t0, $zero, 1b
        nop
        lw      $a0, sema_rot
        li      $v1, 0x42
        syscall
        ld      $s1, 16($sp)
        ld      $s0, 8($sp)
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 32
        .end rotator

        .ent thread_e
thread_e:
        lw      $a0, sema_e
        li      $v1, 0x44               # WaitSema: blocks until main signals
        syscall
        la      $a0, s_e_resumed
        li      $v1, 0x75
        syscall
        li      $v1, 0x23
        syscall
        .end thread_e

        .ent thread_f
thread_f:
        lw      $a0, sema_del
        li      $v1, 0x44               # woken by DeleteSema with an error
        syscall
        move    $a1, $v0
        la      $a0, s_f_result
        li      $v1, 0x75
        syscall
        li      $v1, 0x23
        syscall
        .end thread_f

# ---------------------------------------------------------------------------
        .globl main
        .ent main
main:   addiu   $sp, $sp, -32
        sd      $ra, 0($sp)
        sd      $s0, 8($sp)
        li      $v1, 0x2f               # GetThreadId
        syscall
        sw      $v0, main_tid
        move    $a0, $v0
        li      $a1, 10
        li      $v1, 0x29               # ChangeThreadPriority(self, 10)
        syscall

        # 1. sleep / wakeup
        la      $a0, thread_a
        li      $a1, 5                  # higher priority: runs at once, then sleeps
        jal     start_thread
        move    $a2, $zero
        sw      $v0, tid_a
        la      $a0, s_a_started
        jal     printf
        nop
        lw      $a0, tid_a
        li      $v1, 0x33               # WakeupThread: A preempts us
        syscall
        la      $a0, s_after_wakeup
        jal     printf
        nop

        # 2. queued wakeups
        jal     new_sema
        move    $a0, $zero
        sw      $v0, sema_done
        la      $a0, thread_b
        li      $a1, 20                 # lower priority: waits for us to block
        jal     start_thread
        move    $a2, $zero
        move    $s0, $v0
        move    $a0, $s0
        li      $v1, 0x33
        syscall
        move    $a0, $s0
        li      $v1, 0x33
        syscall
        lw      $a0, sema_done
        li      $v1, 0x44               # WaitSema: B runs now
        syscall
        la      $a0, s_b_done
        jal     printf
        nop

        # 3. a vblank handler signalling a semaphore. It runs with the $gp it
        #    was registered with, not the one of the code it interrupts.
        jal     new_sema
        move    $a0, $zero
        sw      $v0, sema_vb
        li      $a0, 2
        la      $a1, vblank_handler
        move    $a2, $zero
        move    $a3, $zero
        lui     $gp, 0x1234
        li      $v1, 0x10               # AddIntcHandler
        syscall
        move    $gp, $zero
        move    $s0, $v0
        li      $a0, 2
        li      $v1, 0x14               # _EnableIntc
        syscall
        lw      $a0, sema_vb
        li      $v1, 0x44
        syscall
        lw      $a0, sema_vb
        li      $v1, 0x44
        syscall
        lw      $a0, sema_vb
        li      $v1, 0x44
        syscall
        li      $a0, 2
        li      $v1, 0x15               # _DisableIntc
        syscall
        li      $a0, 2
        move    $a1, $s0
        li      $v1, 0x11               # RemoveIntcHandler
        syscall
        la      $a0, s_vblank
        lw      $a1, handler_gp
        jal     printf
        nop

        # 4. an alarm waking us from SleepThread, also with its own $gp
        li      $a0, 100                # in horizontal blanks
        la      $a1, alarm_callback
        lw      $a2, main_tid
        lui     $gp, 0x5678
        li      $v1, 0xfc               # SetAlarm
        syscall
        move    $gp, $zero
        li      $v1, 0x32               # SleepThread
        syscall
        la      $a0, s_alarm
        lw      $a1, alarm_gp
        jal     printf
        nop

        # 5. two threads sharing a priority, rotating the ready queue
        jal     new_sema
        move    $a0, $zero
        sw      $v0, sema_rot
        la      $a0, rotator
        li      $a1, 12
        jal     start_thread
        li      $a2, 'C'
        la      $a0, rotator
        li      $a1, 12
        jal     start_thread
        li      $a2, 'D'
        lw      $a0, sema_rot
        li      $v1, 0x44
        syscall
        lw      $a0, sema_rot
        li      $v1, 0x44
        syscall
        la      $a0, s_rotated
        jal     printf
        nop

        # 6. suspend a waiting thread, release it, resume it
        jal     new_sema
        move    $a0, $zero
        sw      $v0, sema_e
        la      $a0, thread_e
        li      $a1, 5
        jal     start_thread
        move    $a2, $zero
        move    $s0, $v0
        move    $a0, $s0
        li      $v1, 0x37               # SuspendThread
        syscall
        lw      $a0, sema_e
        li      $v1, 0x42               # SignalSema: E stays suspended
        syscall
        la      $a0, s_e_suspended
        jal     printf
        nop
        move    $a0, $s0
        li      $v1, 0x39               # ResumeThread: E preempts us
        syscall
        move    $a0, $s0
        la      $a1, status_buf
        li      $v1, 0x30               # ReferThreadStatus
        syscall
        move    $a1, $v0
        la      $a0, s_e_status
        jal     printf
        nop

        # 7. deleting a semaphore releases its waiters with an error
        jal     new_sema
        move    $a0, $zero
        sw      $v0, sema_del
        la      $a0, thread_f
        li      $a1, 5
        jal     start_thread
        move    $a2, $zero
        lw      $a0, sema_del
        li      $v1, 0x41               # DeleteSema
        syscall

        # 8. timer 0 counts at BUSCLK/256 while we spin
        li      $t0, 0x82               # CUE | CLKS=2
        lui     $t1, 0x1000
        sw      $zero, 0($t1)           # T0_COUNT
        sw      $t0, 0x10($t1)          # T0_MODE
        lw      $s0, 0($t1)
        li      $t2, 20000
1:      addiu   $t2, $t2, -1
        bne     $t2, $zero, 1b
        nop
        lw      $t3, 0($t1)
        sltu    $a1, $s0, $t3
        la      $a0, s_timer
        jal     printf
        nop

        # 9. SetVSyncFlag: the next vblank sets the flag and stores the GS
        #    CSR, once. libkernel's VSync2 registers two of its locals and
        #    returns without unregistering them.
        la      $a0, vs_flag
        la      $a1, vs_csr
        li      $v1, 0x73               # SetVSyncFlag
        syscall
1:      lw      $t0, vs_flag
        beq     $t0, $zero, 1b
        nop
        sw      $zero, vs_flag
        li      $s0, 2                  # two more vblanks, seen in INTC_STAT
        lui     $t1, 0x1000
        ori     $t1, $t1, 0xf000
2:      li      $t2, 4                  # VBLANK_START: write 1 to clear
        sw      $t2, 0($t1)
3:      lw      $t3, 0($t1)
        andi    $t3, $t3, 4
        beq     $t3, $zero, 3b
        nop
        addiu   $s0, $s0, -1
        bne     $s0, $zero, 2b
        nop
        la      $a0, s_vsync_flag
        lw      $a1, vs_flag
        jal     printf
        nop

        ld      $s0, 8($sp)
        ld      $ra, 0($sp)
        move    $v0, $zero
        jr      $ra
        addiu   $sp, $sp, 32
        .end main

        .rodata
s_a_started:    .asciz "A started and went to sleep\n"
s_a_woke:       .asciz "A woke up\n"
s_after_wakeup: .asciz "main after waking A\n"
s_b_slept:      .asciz "B slept twice without blocking\n"
s_b_done:       .asciz "main woke after B\n"
s_vblank:       .asciz "three vblank signals received, handler $gp 0x%08x\n"
s_alarm:        .asciz "alarm woke main, callback $gp 0x%08x\n"
s_rotate:       .asciz "%c %d\n"
s_rotated:      .asciz "rotation done\n"
s_e_suspended:  .asciz "E signalled while suspended\n"
s_e_resumed:    .asciz "E resumed\n"
s_e_status:     .asciz "E status after exit: %d\n"
s_f_result:     .asciz "F wait result %d\n"
s_timer:        .asciz "timer advanced: %d\n"
s_vsync_flag:   .asciz "vsync flag after two more vblanks: %d\n"

        .data
        .align 2
next_stack:     .word thread_stacks
main_tid:       .word 0
tid_a:          .word 0
sema_done:      .word 0
sema_vb:        .word 0
handler_gp:     .word 0
alarm_gp:       .word 0
vs_flag:        .word 0
        .align 3
vs_csr:         .word 0, 0
sema_rot:       .word 0
sema_e:         .word 0
sema_del:       .word 0
tparam:         .word 0, 0, 0, 0, 0, 0, 0, 0, 0
sparam:         .word 0, 0, 0, 0, 0, 0
status_buf:     .space 48

        .bss
        .align 4
thread_stacks:  .space 0x800 * 8
