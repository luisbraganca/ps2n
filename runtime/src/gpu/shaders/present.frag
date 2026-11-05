#version 450
// The window's picture, filtered as PCSX2's Bilinear Filtering setting asks:
// nearest (a sampler that does not filter), smooth (a bilinear sampler) or
// sharp. Sharp keeps each texel flat across most of the pixels it covers
// and blends two texels only over the last pixel at their edge, so a
// picture scaled by a fraction stays crisp without uneven texel widths.

layout(set = 0, binding = 0) uniform sampler2D picture;

layout(push_constant) uniform Params {
    vec2 size;          // the picture, in texels
    vec2 scale;         // window pixels per texel, across and down
    uint sharp;
    float brightness;   // 1, or less to dim the game behind the menu
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 colour;

void main() {
    vec2 at = uv;
    if (pc.sharp != 0u) {
        const vec2 texel = uv * pc.size;
        const vec2 base = floor(texel);
        const vec2 offset = fract(texel) - 0.5;   // from the texel's centre, -0.5 to 0.5
        const vec2 still = 0.5 - 0.5 / max(pc.scale, vec2(1.0));   // how far from the centre it stays flat
        at = (base + (offset - clamp(offset, -still, still)) * max(pc.scale, vec2(1.0)) + 0.5) / pc.size;
    }
    colour = vec4(texture(picture, at).rgb * pc.brightness, 1.0);
}
