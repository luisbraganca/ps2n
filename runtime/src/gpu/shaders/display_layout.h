// The display passes' records, shared by the C++ (gpu/renderer.cpp) and
// the shaders: only #defines.
#ifndef PS2N_GPU_DISPLAY_LAYOUT_H
#define PS2N_GPU_DISPLAY_LAYOUT_H

// A display circuit (gs::detail::Circuit) and where each page of its buffer
// is read: CIRCUIT_PAGES + page holds CIRCUIT_PAGE_AT | the target's PAGE_KIND
// << 27 | image (binding 0) << 15 | the page's row in it << 6 | its column,
// for a page a target can supply; 0 for one read from the mirror. For Z32
// shown as colour the shader translates swizzles; for CT16/CT16S it joins the
// two target halfwords of each displayed word.
#define CIRCUIT_PSM 0
#define CIRCUIT_FBP 1      // pages
#define CIRCUIT_FBW 2
#define CIRCUIT_DBX 3
#define CIRCUIT_DBY 4
#define CIRCUIT_DX 5
#define CIRCUIT_DY 6
#define CIRCUIT_MAGH 7
#define CIRCUIT_MAGV 8
#define CIRCUIT_WIDTH 9
#define CIRCUIT_HEIGHT 10
#define CIRCUIT_PAGES 16
#define CIRCUIT_WORDS (16 + 512)
#define CIRCUIT_PAGE_AT 0x80000000u
#define CIRCUIT_PAGE_KIND_SHIFT 27

#define DISPLAY_EN1 1u
#define DISPLAY_EN2 2u
#define DISPLAY_BLEND 4u
#define DISPLAY_MMOD 8u
#define DISPLAY_SLBG 16u
#define DISPLAY_FIELD 32u

// The deinterlacer's control buffer. Field slot i is [parity][age]: i =
// parity * 2 + age (age 0 the newest of that parity).
#define C_VALID 0          // 4 slots
#define C_WHEN 4
#define C_IMAGE 8          // each slot's image (binding 6)
#define C_NEWEST 12
#define C_FREE 13          // the image the next field goes into
#define C_PICVALID 14      // the last picture (hist.picture) is there
#define C_PICIMAGE 15
#define C_DIFFERS 16       // set by the compare pass: the field differs from the newest one shown
// This vsync's decisions, for the pixel pass, with the images before the history moves on.
#define C_D_SAME 20
#define C_D_HAVEPREV 21
#define C_D_HAVEPREV2 22
#define C_D_HAVEPREV3 23
#define C_D_AGAIN 24
#define C_D_CUR 25
#define C_D_PREV 26
#define C_D_PREV2 27
#define C_D_PREV3 28
#define C_D_LAST 29
#define C_D_PIC 30
#define C_D_LASTSHOWN 31
#define C_WORDS 32

// deinterlace.comp's passes (pc.p[1]).
#define DEINTERLACE_COMPARE 0u
#define DEINTERLACE_CONTROL 1u
#define DEINTERLACE_PIXELS 2u

#endif
