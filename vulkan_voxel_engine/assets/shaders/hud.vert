#version 450

// Pure 2D screen-space HUD & Inventory overlay vertex shader (NDC + UV + texture layer)
layout(location = 0) in vec2 inPosNDC;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(location = 3) in float inTexLayer;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;
layout(location = 2) out float vTexLayer;

void main() {
    vUV       = inUV;
    vColor    = inColor;
    vTexLayer = inTexLayer;
    gl_Position = vec4(inPosNDC, 0.0, 1.0);
}
