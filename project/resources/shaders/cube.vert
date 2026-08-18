#version 450

// Renders a unit cube (36 vertices generated from gl_VertexIndex, no vertex
// buffer). Used by the IBL precompute passes; the local position doubles as the
// sampling direction for the fragment stage.

layout(push_constant) uniform Push
{
    mat4 viewProj; // per-face view-projection
} uPush;

layout(location = 0) out vec3 vLocalPos;

// 36 vertices (12 triangles) of a [-1,1] cube.
const vec3 positions[36] = vec3[](
    // back
    vec3(-1,-1,-1), vec3( 1, 1,-1), vec3( 1,-1,-1),
    vec3( 1, 1,-1), vec3(-1,-1,-1), vec3(-1, 1,-1),
    // front
    vec3(-1,-1, 1), vec3( 1,-1, 1), vec3( 1, 1, 1),
    vec3( 1, 1, 1), vec3(-1, 1, 1), vec3(-1,-1, 1),
    // left
    vec3(-1, 1, 1), vec3(-1, 1,-1), vec3(-1,-1,-1),
    vec3(-1,-1,-1), vec3(-1,-1, 1), vec3(-1, 1, 1),
    // right
    vec3( 1, 1, 1), vec3( 1,-1,-1), vec3( 1, 1,-1),
    vec3( 1,-1,-1), vec3( 1, 1, 1), vec3( 1,-1, 1),
    // bottom
    vec3(-1,-1,-1), vec3( 1,-1,-1), vec3( 1,-1, 1),
    vec3( 1,-1, 1), vec3(-1,-1, 1), vec3(-1,-1,-1),
    // top
    vec3(-1, 1,-1), vec3( 1, 1, 1), vec3( 1, 1,-1),
    vec3( 1, 1, 1), vec3(-1, 1,-1), vec3(-1, 1, 1)
);

void main()
{
    vec3 pos = positions[gl_VertexIndex];
    vLocalPos = pos;
    gl_Position = uPush.viewProj * vec4(pos, 1.0);
}
