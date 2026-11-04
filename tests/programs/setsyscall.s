# setsyscall.s - SetSyscall: a handler a program installs runs for its
# number, and the kernel's table holds it where the program can find it, the
# way Sony's libkernel finds the table at start-up.
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

# set_syscall(number=a0, handler=a1)
        .ent set_syscall
set_syscall:
        li      $v1, 0x74               # SetSyscall
        syscall
        jr      $ra
        nop
        .end set_syscall

# start_thread(entry=a0, priority=a1, arg=a2) -> thread id
        .ent start_thread
start_thread:
        la      $t0, tparam
        sw      $a0, 4($t0)             # func
        sw      $a1, 20($t0)            # initial priority
        la      $t1, thread_stack
        sw      $t1, 8($t0)             # stack
        li      $t2, 0x800
        sw      $t2, 12($t0)            # stack size
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

# ---------------------------------------------------------------------------
# Handlers. The kernel calls one like a function, with the caller's
# arguments, and hands its v0 back.

# find_word(start, end, value): the first address in [start, end) holding
# value, or 0. libkernel installs one as FindAddress (0x83).
        .ent find_word
find_word:
        move    $v0, $a0
1:      sltu    $t0, $v0, $a1
        beq     $t0, $zero, 2f
        nop
        lw      $t1, 0($v0)
        beq     $t1, $a2, 3f
        nop
        b       1b
        addiu   $v0, $v0, 4
2:      move    $v0, $zero
3:      jr      $ra
        nop
        .end find_word

# copy_words(destination, source, bytes). libkernel installs one as Copy (0x5a).
        .ent copy_words
copy_words:
        srl     $a2, $a2, 2
1:      beq     $a2, $zero, 2f
        nop
        lw      $t0, 0($a1)
        addiu   $a1, $a1, 4
        addiu   $a2, $a2, -1
        sw      $t0, 0($a0)
        b       1b
        addiu   $a0, $a0, 4
2:      jr      $ra
        move    $v0, $zero
        .end copy_words

# sum_args(a0, a1, a2, a3): their sum. It changes registers the caller keeps.
        .ent sum_args
sum_args:
        addu    $v0, $a0, $a1
        addu    $v0, $v0, $a2
        addu    $v0, $v0, $a3
        li      $a1, -1
        li      $t0, -1
        jr      $ra
        li      $t9, -1
        .end sum_args

        .ent memory_size
memory_size:
        jr      $ra
        li      $v0, 0x1234
        .end memory_size

# slow(): spins across several vblanks and returns how many vblank
# interrupts were taken meanwhile.
        .ent slow
slow:   lw      $t0, vblanks
        lui     $t1, 0x20               # 2M back-edges: more than three frames
1:      addiu   $t1, $t1, -1
        bne     $t1, $zero, 1b
        nop
        lw      $v0, vblanks
        jr      $ra
        subu    $v0, $v0, $t0
        .end slow

# wake(thread): wakes a thread of higher priority, spins, and returns whether
# that thread has run yet.
        .ent wake
wake:   li      $v1, 0x33               # WakeupThread
        syscall
        lui     $t1, 0x2                # 128K back-edges
1:      addiu   $t1, $t1, -1
        bne     $t1, $zero, 1b
        nop
        lw      $v0, woken
        jr      $ra
        nop
        .end wake

# A routine the program copies into kernel memory, as libkernel copies its
# patches for the console's kernel. Installed there, it never runs.
        .ent patch
patch:  jr      $ra
        li      $v0, 0x666
        .end patch

        .ent vblank_handler
vblank_handler:
        lw      $t0, vblanks
        addiu   $t0, $t0, 1
        sw      $t0, vblanks
        jr      $ra
        move    $v0, $zero
        .end vblank_handler

        .ent sleeper
sleeper:
        li      $v1, 0x32               # SleepThread
        syscall
        li      $t0, 1
        sw      $t0, woken
        li      $v1, 0x23               # ExitThread
        syscall
        .end sleeper

# ---------------------------------------------------------------------------
        .globl main
        .ent main
main:   addiu   $sp, $sp, -32
        sd      $ra, 0($sp)
        sd      $s0, 8($sp)
        sd      $s1, 16($sp)
        li      $v1, 0x2f               # GetThreadId
        syscall
        move    $a0, $v0
        li      $a1, 10
        li      $v1, 0x29               # ChangeThreadPriority(self, 10)
        syscall

        # 1. Install FindAddress and Copy, then find the table as libkernel
        #    does: search kernel memory for each handler, and take away its
        #    number's offset.
        li      $a0, 0x83
        la      $a1, find_word
        jal     set_syscall
        nop
        li      $a0, 0x5a
        la      $a1, copy_words
        jal     set_syscall
        nop
        lui     $a0, 0x8000
        lui     $a1, 0x8008
        la      $a2, find_word
        li      $v1, 0x83               # FindAddress: the handler above
        syscall
        move    $s0, $v0
        lui     $a0, 0x8000
        lui     $a1, 0x8008
        la      $a2, copy_words
        li      $v1, 0x83
        syscall
        sltu    $t0, $zero, $s0         # both found...
        sltu    $t1, $zero, $v0
        and     $t0, $t0, $t1
        addiu   $s0, $s0, -0x20c        # ...in one table: entry 0x83 and entry 0x5a
        addiu   $t2, $v0, -0x168
        xor     $t2, $t2, $s0
        sltiu   $t2, $t2, 1
        and     $a1, $t0, $t2
        la      $a0, s_found
        jal     printf
        nop

        # 2. Read an entry with Copy, as libkernel's GetSystemCallEntry does.
        la      $a0, word_buf
        addiu   $a1, $s0, 0x168         # entry 0x5a
        li      $a2, 4
        li      $v1, 0x5a               # Copy: the handler above
        syscall
        lw      $t0, word_buf
        la      $t1, copy_words
        xor     $t0, $t0, $t1
        sltiu   $a1, $t0, 1
        la      $a0, s_entry
        jal     printf
        nop

        # 3. Arguments, result and the caller's registers.
        li      $a0, 0x81
        la      $a1, sum_args
        jal     set_syscall
        nop
        li      $a0, 1
        li      $a1, 2
        li      $a2, 3
        li      $a3, 4
        li      $t0, 0x55
        li      $v1, 0x81
        syscall
        xori    $t1, $a1, 2
        xori    $t2, $t0, 0x55
        or      $t1, $t1, $t2
        sltiu   $a2, $t1, 1
        move    $a1, $v0
        la      $a0, s_args
        jal     printf
        nop
        li      $a0, 5
        li      $a1, 6
        li      $a2, 7
        li      $a3, 8
        li      $v1, -0x81              # the same entry, as an interrupt-context call
        syscall
        move    $a1, $v0
        la      $a0, s_negative
        jal     printf
        nop

        # 4. A handler over a syscall the runtime implements, then 0: the
        #    runtime's own again.
        li      $a0, 0x7f
        la      $a1, memory_size
        jal     set_syscall
        nop
        li      $v1, 0x7f               # GetMemorySize
        syscall
        move    $s1, $v0
        li      $a0, 0x7f
        jal     set_syscall
        move    $a1, $zero
        li      $v1, 0x7f
        syscall
        move    $a2, $v0
        move    $a1, $s1
        la      $a0, s_native
        jal     printf
        nop

        # 5. Handlers in kernel memory never run: the runtime's own syscall
        #    stands in, or the call is reported.
        lui     $a0, 0x8007
        ori     $a0, $a0, 0x6000
        la      $a1, patch
        li      $a2, 8
        li      $v1, 0x5a               # Copy the routine into kernel memory
        syscall
        li      $a0, 0x7f
        lui     $a1, 0x8007
        jal     set_syscall
        ori     $a1, $a1, 0x6000
        li      $a0, 0x81
        lui     $a1, 0x8007
        jal     set_syscall
        ori     $a1, $a1, 0x6000
        li      $v1, 0x7f
        syscall
        move    $s1, $v0
        li      $v1, 0x81
        syscall
        move    $a2, $v0
        move    $a1, $s1
        la      $a0, s_kernel
        jal     printf
        nop
        li      $a0, -0x3bfe            # libkernel's ExecPS2 patch writes outside the table: ignored
        lui     $a1, 0x8007
        jal     set_syscall
        ori     $a1, $a1, 0x4000

        # 6. Interrupts wait until a handler returns.
        li      $a0, 2                  # VBLANK_START
        la      $a1, vblank_handler
        move    $a2, $zero
        move    $a3, $zero
        li      $v1, 0x10               # AddIntcHandler
        syscall
        li      $a0, 2
        li      $v1, 0x14               # _EnableIntc
        syscall
        li      $a0, 0x84
        la      $a1, slow
        jal     set_syscall
        nop
        lw      $s1, vblanks
        li      $v1, 0x84
        syscall
        lw      $t0, vblanks            # the vblank it held came as it returned
        xor     $t0, $t0, $s1
        sltu    $a2, $zero, $t0
        move    $a1, $v0
        la      $a0, s_held
        jal     printf
        nop
        li      $a0, 2
        li      $v1, 0x15               # _DisableIntc
        syscall

        # 7. So do thread switches.
        la      $a0, sleeper
        li      $a1, 5                  # higher priority: runs at once, then sleeps
        jal     start_thread
        move    $a2, $zero
        move    $s1, $v0
        li      $a0, 0x84
        la      $a1, wake
        jal     set_syscall
        nop
        move    $a0, $s1
        li      $v1, 0x84
        syscall
        move    $a1, $v0
        lw      $a2, woken
        la      $a0, s_switch
        jal     printf
        nop

        ld      $s1, 16($sp)
        ld      $s0, 8($sp)
        ld      $ra, 0($sp)
        move    $v0, $zero
        jr      $ra
        addiu   $sp, $sp, 32
        .end main

        .rodata
s_found:        .asciz "both handlers found in one table: %d\n"
s_entry:        .asciz "entry 0x5a read with Copy holds its handler: %d\n"
s_args:         .asciz "sum %d, caller's registers kept: %d\n"
s_negative:     .asciz "-0x81 reaches the same entry: sum %d\n"
s_native:       .asciz "GetMemorySize 0x%x with a handler, 0x%x without\n"
s_kernel:       .asciz "handlers in kernel memory: GetMemorySize 0x%x, syscall 0x81 0x%x\n"
s_held:         .asciz "vblanks taken while a handler ran: %d, taken after it: %d\n"
s_switch:       .asciz "woken thread ran inside the handler: %d, after it: %d\n"

        .data
        .align 2
word_buf:       .word 0
vblanks:        .word 0
woken:          .word 0
tparam:         .word 0, 0, 0, 0, 0, 0, 0, 0, 0

        .bss
        .align 4
thread_stack:   .space 0x800
