#version 450

// Generates a single oversized triangle covering the screen from gl_VertexIndex
// (draw 3 vertices, no vertex buffer). vUV spans [0,1] across the framebuffer.
layout(location = 0) out vec2 vUV;

void main()
{
    vUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
