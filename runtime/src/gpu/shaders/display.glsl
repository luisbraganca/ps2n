// What the display's passes share (display.comp, deinterlace.comp): the
// circuits' entries, the flags, and the deinterlacer's control buffer, which
// holds the field history's bookkeeping on the card (gs.cpp's FieldHistory)
// so that its decisions can depend on the fields' pixels.
#include "display_layout.h"

layout(set = 0, binding = 7, std430) buffer Control { uint control[]; };

vec4 unpackPixel(uint px) { return vec4(px & 0xFFu, (px >> 8) & 0xFFu, (px >> 16) & 0xFFu, px >> 24) / 255.0; }

uint packPixel(vec4 c) {
    const uvec4 v = uvec4(round(clamp(c, 0.0, 1.0) * 255.0));
    return v.r | (v.g << 8) | (v.b << 16) | (v.a << 24);
}
