# deci2.s - DECI2 as libkernel's TTY uses it: kputs, and a packet sent through
# a socket's handler (ReqSend, then polling until the handler has handed the
# packet over and been told it went out). A socket for another protocol is
# refused.
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

# deci2(function=a0, parameter words=a1) -> v0
        .ent deci2
deci2:  li      $v1, 0x7c               # Deci2Call
        syscall
        jr      $ra
        nop
        .end deci2

# The socket's handler(event=a0, param=a1, opt=a2), like libkernel's TTY
# handler: on DECI2_WRITE (3) it sends what is left of the packet, here at most
# 8 bytes at a time; on DECI2_WRITEDONE (4) it clears the flag the writer
# polls. opt points at `tty`.
        .ent handler
handler:
        addiu   $sp, $sp, -32
        sd      $ra, 0($sp)
        sd      $s0, 8($sp)
        move    $s0, $a2
        li      $t0, 3
        bne     $a0, $t0, 2f
        nop
        lw      $t1, 4($s0)             # remaining
        sltiu   $t2, $t1, 9
        bnez    $t2, 1f
        nop
        li      $t1, 8
1:      la      $a1, xparam
        lw      $t3, 0($s0)
        sw      $t3, 0($a1)             # socket
        lw      $t3, 12($s0)
        sw      $t3, 4($a1)             # buffer
        sw      $t1, 8($a1)             # size
        jal     deci2
        li      $a0, -6                 # ExSend
        lw      $t3, 12($s0)
        addu    $t3, $t3, $v0
        sw      $t3, 12($s0)
        lw      $t3, 4($s0)
        subu    $t3, $t3, $v0
        sw      $t3, 4($s0)
        lw      $t3, 16($s0)
        addiu   $t3, $t3, 1
        b       3f
        sw      $t3, 16($s0)            # pieces sent
2:      li      $t0, 4
        bne     $a0, $t0, 3f
        nop
        sw      $zero, 8($s0)           # no longer busy
3:      ld      $s0, 8($sp)
        ld      $ra, 0($sp)
        jr      $ra
        addiu   $sp, $sp, 32
        .end handler

        .globl main
        .ent main
main:   addiu   $sp, $sp, -32
        sd      $ra, 0($sp)
        sd      $s0, 8($sp)
        sd      $s1, 16($sp)

        # kputs: the kernel's own debug output.
        la      $a1, xparam
        la      $t0, s_kputs
        sw      $t0, 0($a1)
        jal     deci2
        li      $a0, 0x10

        # A socket for the TTY protocol.
        la      $s0, tty
        la      $a1, xparam
        li      $t0, 0x0210
        sw      $t0, 0($a1)             # protocol
        sw      $s0, 4($a1)             # opt
        la      $t0, handler
        sw      $t0, 8($a1)
        sw      $zero, 12($a1)          # work area
        jal     deci2
        li      $a0, 1                  # Open
        sw      $v0, 0($s0)
        slt     $a1, $v0, $zero
        xori    $a1, $a1, 1
        la      $a0, s_opened
        jal     printf
        nop

        # Send the packet: ReqSend, then poll until the handler is done.
        la      $t0, packet
        sw      $t0, 12($s0)            # next byte
        lhu     $t1, 0($t0)
        sw      $t1, 4($s0)             # remaining: the whole packet
        li      $t1, 1
        sw      $t1, 8($s0)             # busy
        la      $a1, xparam
        lw      $t0, 0($s0)
        sw      $t0, 0($a1)
        li      $t0, 0x48               # to the host ('H')
        sw      $t0, 4($a1)
        jal     deci2
        li      $a0, 3                  # ReqSend
        move    $a1, $v0
        la      $a0, s_reqsend
        jal     printf
        nop
        li      $s1, 10                 # polls before giving up
4:      lw      $t0, 8($s0)
        beqz    $t0, 5f
        nop
        beqz    $s1, 5f
        addiu   $s1, $s1, -1
        la      $a1, xparam
        lw      $t0, 0($s0)
        sw      $t0, 0($a1)
        jal     deci2
        li      $a0, 4                  # Poll
        b       4b
        nop
5:      lw      $a1, 16($s0)
        lw      $a2, 4($s0)
        lw      $a3, 8($s0)
        la      $a0, s_sent
        jal     printf
        nop

        # Only the TTY has a host behind it.
        la      $a1, xparam
        li      $t0, 0x0130
        sw      $t0, 0($a1)
        sw      $s0, 4($a1)
        la      $t0, handler
        sw      $t0, 8($a1)
        jal     deci2
        li      $a0, 1                  # Open
        slt     $a1, $v0, $zero
        la      $a0, s_refused
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
s_kputs:        .asciz "kputs works\n"
s_opened:       .asciz "TTY socket opened: %d\n"
s_reqsend:      .asciz "ReqSend: %d\n"
s_sent:         .asciz "sent in %d pieces, %d bytes left, busy %d\n"
s_refused:      .asciz "other protocol refused: %d\n"

        .data
        .align 2
xparam:         .word 0, 0, 0, 0
tty:            .word 0, 0, 0, 0, 0     # socket, remaining, busy, next byte, pieces sent
packet:         .half 29, 0             # length, header included
                .half 0x0210            # protocol: TTY
                .byte 0x45, 0x48        # from the EE ('E') to the host ('H')
                .word 0                 # TTY word
                .ascii "hello over DECI2\n"
