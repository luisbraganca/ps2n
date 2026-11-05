#version 460
// A GS primitive's corners, from its record (layout.h): an instance per
// primitive, six corners each. A triangle uses three (the other three meet
// at one point and draw nothing); a sprite and a line's or point's pixel
// are rectangles of two triangles.
//
// The GS samples a pixel at its integer coordinates. At N times its
// resolution a corner at x goes to x * N + 0.5, so that pixel k of the
// image, which the card samples at k + 0.5, is covered when the GS would
// cover position k / N: pixel N * px exactly when the GS covers pixel px.

#include "common.glsl"

layout(location = 0) flat out uint primIndex;

void main() {
    const uint prim = uint(gl_InstanceIndex);
    const uint p = prim * PRIM_WORDS;
    const uint kind = prims[p + P_KIND];
    const uint corner = uint(gl_VertexIndex);
    const float n = float(pc.p[0]);
    vec2 at;
    if (kind == KIND_TRIANGLE) {
        const uint v = P_V0 + (corner < 3u ? corner : 0u) * V_WORDS;
        at = vec2(wordFloat(prims[p + v + V_X]), wordFloat(prims[p + v + V_Y])) * n + 0.5;
    } else {
        // Corners 0 1 2 / 3 4 5: (left, top) (right, top) (left, bottom) / (right, top) (right, bottom) (left, bottom).
        const bool right = corner == 1u || corner == 3u || corner == 4u;
        const bool bottom = corner == 2u || corner == 4u || corner == 5u;
        const float ax = wordFloat(prims[p + P_V0 + V_X]), ay = wordFloat(prims[p + P_V0 + V_Y]);
        if (kind == KIND_SPRITE) {
            const float bx = wordFloat(prims[p + P_V0 + V_WORDS + V_X]), by = wordFloat(prims[p + P_V0 + V_WORDS + V_Y]);
            at = vec2(right ? max(ax, bx) : min(ax, bx), bottom ? max(ay, by) : min(ay, by)) * n + 0.5;
        } else {   // a whole pixel: N x N of the image
            at = vec2(right ? ax + 1.0 : ax, bottom ? ay + 1.0 : ay) * n;
        }
    }
    primIndex = prim;
    // The viewport spans -h..h on both axes (h = pc.p[1]'s float), so this is exact.
    const float h = uintBitsToFloat(pc.p[1]);
    gl_Position = vec4(at / h, 0.0, 1.0);
}
