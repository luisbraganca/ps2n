#version 450
// The window's picture: one triangle that covers the viewport, which the
// presenter sets to the picture's rectangle in the window.

layout(location = 0) out vec2 uv;

void main() {
    const vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
