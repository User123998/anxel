#version 450

// Real PNG item icons loaded from assets/texture/items/*.png (14 layers, NEAREST + CLAMP)
layout(set = 0, binding = 0) uniform sampler2DArray uItemTextures;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 2) in float vTexLayer;

layout(location = 0) out vec4 outColor;

void main() {
    if (vTexLayer >= 0.0) {
        vec4 tex = texture(uItemTextures, vec3(vUV, vTexLayer));
        vec4 col = tex * vColor;
        if (col.a <= 0.004) discard;
        outColor = col;
    } else {
        outColor = vColor;
    }
}
