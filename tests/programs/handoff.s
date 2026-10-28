# handoff.s - a boot program that loads the next one by itself and jumps
# into it, as some games' loaders do (no LoadExecPS2): handoff_next.elf,
# embedded here, has its segments copied to their addresses, the caches
# flushed, and its entry point jumped to.
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

        .globl main
        .ent main
main:   la      $a0, s_loading
        jal     printf
        nop
        la      $t0, next_elf           # Elf32_Ehdr
        lw      $s0, 24($t0)            # e_entry
        lw      $t1, 28($t0)            # e_phoff
        lhu     $t2, 44($t0)            # e_phnum
        lhu     $t3, 42($t0)            # e_phentsize
        addu    $t1, $t1, $t0
1:      beq     $t2, $zero, 4f
        nop
        lw      $t4, 0($t1)             # p_type
        li      $t5, 1                  # PT_LOAD
        bne     $t4, $t5, 3f
        nop
        lw      $t4, 4($t1)             # p_offset
        addu    $t4, $t4, $t0
        lw      $t5, 8($t1)             # p_vaddr
        lw      $t6, 16($t1)            # p_filesz
        lw      $t7, 20($t1)            # p_memsz
        subu    $t7, $t7, $t6
2:      beq     $t6, $zero, 5f          # copy the file bytes
        nop
        lbu     $t8, 0($t4)
        sb      $t8, 0($t5)
        addiu   $t4, $t4, 1
        addiu   $t5, $t5, 1
        b       2b
        addiu   $t6, $t6, -1
5:      beq     $t7, $zero, 3f          # clear the rest
        nop
        sb      $zero, 0($t5)
        addiu   $t5, $t5, 1
        b       5b
        addiu   $t7, $t7, -1
3:      addu    $t1, $t1, $t3
        b       1b
        addiu   $t2, $t2, -1
4:      move    $a0, $zero              # FlushCache(WRITEBACK_DCACHE)
        li      $v1, 0x64
        syscall
        li      $a0, 2                  # FlushCache(INVALIDATE_ICACHE)
        li      $v1, 0x64
        syscall
        jr      $s0                     # into the next program, for good
        nop
        .end main

        .rodata
s_loading:      .asciz "boot: loading the next program\n"

        .data
        .align 4
next_elf:       .incbin "handoff_next.elf"
