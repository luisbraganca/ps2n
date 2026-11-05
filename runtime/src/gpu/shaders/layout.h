// The Vulkan renderer's records, as word offsets shared by the C++
// (gpu/renderer.cpp) and the shaders, which read them from flat uint
// buffers: only #defines, valid in both languages, so the two cannot
// disagree on a layout.
#ifndef PS2N_GPU_LAYOUT_H
#define PS2N_GPU_LAYOUT_H

// ---- A drawing state (gs::detail::Draw, decoded for the shaders) -------------------------------
#define S_FLAGS 0
#define S_FRAME 1        // the frame's target image (binding 0)
#define S_DEPTH 2        // the depth buffer's target image
#define S_FPSM 3
#define S_ZPSM 4
#define S_FBMSK 5
#define S_FBMSK16 6
#define S_TPSM 7
#define S_TFX 8
#define S_WMS 9
#define S_WMT 10
#define S_TWF 11         // float bits
#define S_THF 12
#define S_LODK 13        // int
#define S_LODL 14
#define S_MAGFROM 15
#define S_MIPLEVELS 16
#define S_TA0 17
#define S_TA1 18
#define S_CLUTBASE 19    // CBP in the entries' units
#define S_CLUTTABLE 20   // where in the tables the entries' offsets start (ClutOffsets, CSA)
#define S_ATST 21
#define S_AREF 22
#define S_AFAIL 23
#define S_ZTST 24
#define S_ZMAX 25
#define S_FOGR 26
#define S_FOGG 27
#define S_FOGB 28
#define S_BLEND 29       // A | B << 2 | C << 4 | D << 6
#define S_FIX 30
#define S_SCX0 31
#define S_SCX1 32
#define S_SCY0 33
#define S_SCY1 34
#define S_FRAMEW 35      // the frame's target image, scaled pixels: nothing is drawn past it
#define S_FRAMEH 36
#define S_DEPTHW 37
#define S_DEPTHH 38
#define S_SOURCEFBW 39   // the target level 0 is read from: its pages across
#define S_LEVELS 40      // the texture's levels, L_WORDS each
#define L_SOURCE 0       // bit 31: a target image (its index below); else GS memory (the mirror)
#define L_BASE 1         // mirror: the level's first unit; target: its x in the target, texels
#define L_ROW 2          // mirror: the units in a row of pages; target: its y in the target, texels
#define L_TW 3
#define L_TH 4
#define L_MINU 5
#define L_MAXU 6
#define L_MINV 7
#define L_MAXV 8
#define L_SCALE 9        // a target's scale; 1 for the mirror
#define L_WORDS 10
#define S_FBW 110        // FRAME's FBW (the depth buffer's too): a pixel past it is on the next row of pages
// Level 0 read from a target (L_FROM_TARGET): its texels in the pages the
// target holds come from it, the others from GS memory (the mirror).
#define S_SOURCEBASE 111   // the target's first page
#define S_SOURCEUNIT 112   // level 0 in GS memory: its first unit,
#define S_SOURCEROW 113    // and the units in a row of its pages
#define S_SOURCEPAGES 116  // 16 words: a bit for each page the target holds
#define STATE_WORDS 132    // 40 + 7 levels, then the above, rounded up

#define L_FROM_TARGET 0x80000000u

#define F_TEX (1u << 0)
#define F_FST (1u << 1)
#define F_GOURAUD (1u << 2)
#define F_ZTE (1u << 3)
#define F_ZMSK (1u << 4)
#define F_ATE (1u << 5)
#define F_FGE (1u << 6)
#define F_ABE (1u << 7)
#define F_PABE (1u << 8)
#define F_COLCLAMP (1u << 9)
#define F_FBA (1u << 10)
#define F_FB16 (1u << 11)
#define F_TCC (1u << 12)
#define F_LODBYQ (1u << 13)
#define F_MIPLINEAR (1u << 14)
#define F_MAGLINEAR (1u << 15)
#define F_MINLINEAR (1u << 16)
#define F_LINEAR (1u << 17)
#define F_CLUTWIDE (1u << 18)
#define F_AEM (1u << 19)
#define F_ANYFORMAT (1u << 20)   // a texture format the GS cannot read: its texels are 0xFFFFFFFF
#define F_NODEPTH (1u << 21)     // the depth buffer is the frame's memory: depth is not tested or written

// ---- A primitive -------------------------------------------------------------------------------
#define P_KIND 0         // KIND_*
#define P_STATE 1        // the state's first word in the states buffer
#define P_V0 4           // vertex i at P_V0 + i * V_WORDS
#define V_X 0            // window coordinates (float)
#define V_Y 1
#define V_Z 2            // uint
#define V_R 3            // colour, 0..255 (float)
#define V_G 4
#define V_B 5
#define V_A 6
#define V_S 7
#define V_T 8
#define V_Q 9
#define V_U 10
#define V_V 11
#define V_FOG 12
#define V_WORDS 13
#define P_AREA 43        // a triangle's area (float)
#define P_UA 43          // a sprite's texture coordinates at its two corners (texCoord)
#define P_VA 44
#define P_UB 45
#define P_VB 46
#define PRIM_WORDS 48

#define KIND_TRIANGLE 0u
#define KIND_SPRITE 1u
#define KIND_PIXEL 2u    // one pixel of a line or a point: vertex 0 holds it and its values

// ---- The tables (uint each) --------------------------------------------------------------------
#define T_C32 0          // [2][32][64]: Swizzle::c32
#define T_C16 4096       // [4][64][64]: Swizzle::c16
#define T_C8 20480       // [64][128]
#define T_C4 28672       // [128][128]
#define T_INV32 45056    // [2][2048]: the pixel (y * 64 + x) of each word of a page
#define T_INV16 49152    // [4][4096]: the pixel of each halfword of a page
#define T_CLUT8 65536    // [3][256]: ClutOffsets::t8
#define T_CLUT4 66304    // [3][512]: ClutOffsets::t4
#define T_LOG2BEFORE 67840
#define T_LOG2START 68096
#define T_WORDS 68352

// The formats (gs::detail::Psm).
#define PSMCT32 0x00u
#define PSMCT24 0x01u
#define PSMCT16 0x02u
#define PSMCT16S 0x0Au
#define PSMT8 0x13u
#define PSMT4 0x14u
#define PSMT8H 0x1Bu
#define PSMT4HL 0x24u
#define PSMT4HH 0x2Cu
#define PSMZ32 0x30u
#define PSMZ24 0x31u
#define PSMZ16 0x32u
#define PSMZ16S 0x3Au

// ---- The compute passes' entries, in the upload buffer ----------------------------------------
// A page of a target to GS memory (resolve) or from it (expand).
#define PAGE_IMAGE 0
#define PAGE_X 1         // the page's first pixel in the target, unscaled
#define PAGE_Y 2
#define PAGE_KIND 3      // 0-1: 32-bit layout (Z); 2-5: 16-bit (S | Z << 1)
#define PAGE_PAGE 4      // the page of GS memory
#define PAGE_WORDS 5

// Units written into a target (a transfer's rectangle pushed at scale).
#define PUSH_IMAGE 0
#define PUSH_X 1         // the rectangle's first pixel, unscaled, in the transfer's coordinates
#define PUSH_Y 2
#define PUSH_W 3
#define PUSH_H 4
#define PUSH_SRC 5       // its units, row by row, in the upload buffer
#define PUSH_UNIT 6      // PUSH_UNIT_*
#define PUSH_BASEPAGE 7  // the target's first page, its width in pages and its page height
#define PUSH_FBW 8
#define PUSH_PAGEH 9
#define PUSH_MASK 10     // 16 words: the pages the target owns
#define PUSH_ORIGIN 26   // the transfer buffer's first page relative to the target's, as x and y in pixels
#define PUSH_ORIGINY 27
#define PUSH_WORDS 28
#define PUSH_UNIT_WHOLE 0u   // the whole word (32-bit) or halfword (16-bit layouts)
#define PUSH_UNIT_LOW24 1u   // CT24, Z24: the top byte stays
#define PUSH_UNIT_T8H 2u
#define PUSH_UNIT_T4HL 3u
#define PUSH_UNIT_T4HH 4u

// Units of a host-to-local transfer merged into GS memory (the mirror).
#define MERGE_X 0        // the rectangle's first pixel, in the transfer's coordinates
#define MERGE_Y 1
#define MERGE_W 2
#define MERGE_H 3
#define MERGE_SRC 4      // its units, row by row, in the upload buffer
#define MERGE_DBP 5      // the destination's first block, width and format
#define MERGE_DBW 6
#define MERGE_PSM 7
#define MERGE_MASK 8     // 16 words: the mirror's pages this transfer writes
#define MERGE_WORDS 24

#endif
