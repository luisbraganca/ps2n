# gs.s - draws through DMA -> GIF -> GS, as a game's render loop would.
#
# Sets up a 320x224 PSMCT32 display, sends one GIF packet over DMA channel 2
# (a clearing sprite, a red square and a Gouraud triangle), waits for the GS
# FINISH event, then idles. Run with --dump-frames to get the picture.
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

        lui     $t0, 0x1200             # GS privileged registers
        ld      $t1, pmode
        sd      $t1, 0x00($t0)          # PMODE
        sd      $zero, 0x20($t0)        # SMODE2: progressive
        ld      $t1, dispfb1
        sd      $t1, 0x70($t0)          # DISPFB1
        ld      $t1, display1
        sd      $t1, 0x80($t0)          # DISPLAY1

        li      $t0, 0x1000a000         # DMA channel 2 (GIF), normal mode
        la      $t1, packet
        sw      $t1, 0x10($t0)          # D2_MADR
        li      $t1, (packet_end - packet) / 16
        sw      $t1, 0x20($t0)          # D2_QWC
        li      $t1, 0x101              # DIR=from memory, STR
        sw      $t1, 0x00($t0)          # D2_CHCR

        lui     $t0, 0x1200             # wait for FINISH in GS CSR
1:      ld      $t1, 0x1000($t0)
        andi    $t1, $t1, 2
        beq     $t1, $zero, 1b
        nop
        li      $t1, 2
        sd      $t1, 0x1000($t0)        # acknowledge

        la      $a0, s_done
        li      $v1, 0x75               # _print
        syscall
2:      b       2b                      # --max-vsyncs ends the run
        nop
        .end _start

        .data
        .align 3
pmode:          .dword 0x8025                    # EN1, CRTMD=1, MMOD, ALP=0x80
dispfb1:        .dword (5 << 9)                  # FBP 0, FBW 5 (320 px), PSMCT32
display1:       .dword 656 | (36 << 12) | (3 << 23) | (1279 << 32) | (223 << 44)

        .align 4
packet:
        .dword 20 | (1 << 15) | (1 << 60)        # GIFtag: NLOOP 20, EOP, PACKED, NREG 1
        .dword 0xe                               # REGS: A+D
        # A+D register writes: data, register address
        .dword (5 << 16)                         # FRAME_1: FBW 5, PSMCT32
        .dword 0x4c
        .dword 0                                 # XYOFFSET_1
        .dword 0x18
        .dword (319 << 16) | (223 << 48)         # SCISSOR_1
        .dword 0x40
        .dword 0                                 # TEST_1
        .dword 0x47
        .dword 1                                 # PRMODECONT: use PRIM attributes
        .dword 0x1a
        .dword 6                                 # PRIM: sprite
        .dword 0x00
        .dword 0x80803020                        # RGBAQ: background
        .dword 0x01
        .dword 0                                 # XYZ2 (0, 0)
        .dword 0x05
        .dword (320 << 4) | ((224 << 4) << 16)   # XYZ2 (320, 224)
        .dword 0x05
        .dword 0x800000ff                        # RGBAQ: red
        .dword 0x01
        .dword (32 << 4) | ((32 << 4) << 16)
        .dword 0x05
        .dword (96 << 4) | ((96 << 4) << 16)
        .dword 0x05
        .dword 0x0b                              # PRIM: triangle, Gouraud
        .dword 0x00
        .dword 0x800000ff                        # red
        .dword 0x01
        .dword (160 << 4) | ((32 << 4) << 16)
        .dword 0x05
        .dword 0x8000ff00                        # green
        .dword 0x01
        .dword (288 << 4) | ((200 << 4) << 16)
        .dword 0x05
        .dword 0x80ff0000                        # blue
        .dword 0x01
        .dword (160 << 4) | ((200 << 4) << 16)
        .dword 0x05
        .dword 0                                 # FINISH
        .dword 0x61
packet_end:

        .rodata
s_done:         .asciz "gs: drawing finished\n"
