# bindings.s - guest code the config replaces (see bindings.toml).
#
# A skipped function that would never return, a function bound to a runtime
# stub, one overridden by C++ (bindings_override.cpp) and a patched
# instruction. The interpreted run must honour all of them as well.
        .set noreorder
        .text

        .globl patched_value
        .ent patched_value
patched_value:                          # at 0x00100000; bindings.toml patches its delay slot
        jr      $ra
        li      $v0, 1                  # -> li $v0, 2
        .end patched_value

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

        .globl print
        .ent print
print:  li      $v1, 0x75               # _print
        syscall
        jr      $ra
        nop
        .end print

        .globl hang
        .ent hang
hang:   b       hang                    # [functions] skip
        nop
        .end hang

        .globl get_magic
        .ent get_magic
get_magic:                              # [stubs] bind: retm1
        jr      $ra
        li      $v0, 1234
        .end get_magic

        .globl triple
        .ent triple
triple: sll     $v0, $a0, 1             # [functions] override: triple_override()
        jr      $ra
        addu    $v0, $v0, $a0
        .end triple

        .globl main
        .ent main
main:   addiu   $sp, $sp, -16
        sd      $ra, 0($sp)

        jal     hang
        nop
        la      $a0, s_skip
        jal     print
        move    $a1, $v0

        jal     get_magic
        nop
        la      $a0, s_stub
        jal     print
        move    $a1, $v0

        jal     triple
        li      $a0, 7
        la      $a0, s_override
        jal     print
        move    $a1, $v0

        jal     patched_value
        nop
        la      $a0, s_patch
        jal     print
        move    $a1, $v0

        la      $t0, patched_value      # the patch is in RAM too
        lw      $a1, 4($t0)
        la      $a0, s_word
        jal     print
        nop

        ld      $ra, 0($sp)
        move    $v0, $zero
        jr      $ra
        addiu   $sp, $sp, 16
        .end main

        .rodata
s_skip:         .asciz "skip: hang returned %d\n"
s_stub:         .asciz "stub: get_magic returned %d\n"
s_override:     .asciz "override: triple(7) = %d\n"
s_patch:        .asciz "patch: patched_value returned %d\n"
s_word:         .asciz "patch: instruction word 0x%08x\n"
