#version 450

// Fullscreen triangle sky dome (no vertex buffer needed — uses gl_VertexIndex)
layout(location = 0) out vec2 vNDC;

void main() {
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    vNDC = positions[gl_VertexIndex];
    gl_Position = vec4(vNDC, 0.9999, 1.0);
}
