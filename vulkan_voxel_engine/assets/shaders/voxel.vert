#version 450

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 viewProj;
    mat4 invViewProj;
    vec4 cameraPosTime;          // xyz = camera world pos, w = elapsed seconds
    vec4 sunDirIntensity;        // xyz = normalized sun dir, w = sky intensity [0.06..1.0]
    vec4 sunColorMoon;           // rgb = direct sun color, w = moon intensity [0..1]
    vec4 skyZenithTimeOfDay;     // rgb = sky zenith color, w = timeOfDay [0..1)
    vec4 skyHorizonCloudLit;     // rgb = sky horizon / aerial fog color, w = twilight warm glow
    vec4 skyAmbientFogDensity;   // rgb = upper sky hemisphere fill, w = base fog density
    vec4 mieGlowDebugMode;       // rgb = sunrise/sunset Mie horizon glow, w = debugViewMode
    vec4 heldLightPosStrength;   // xyz = handheld light world pos, w = strength [0..1]
    vec4 dynOriginFlags;         // xyz = 3D dynLight volume origin, w = graphics flags bitmask
    vec4 renderFogParams;        // x = eyeWaterDepth (meters, 0 above water), y = borderStart, z = borderEnd, w = fogStrength
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inLightAO;   // x = skyLight [0..1], y = static blockLight [0..1], z = AO [0..1], w = smoothWaterDepth [0..1]
layout(location = 4) in uint inPackedMat; // bits 0..7=blockId, 8..10=faceDir, 11=submerged/waterMeniscus

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec4 vLightAO;
layout(location = 4) flat out uint vBlockId;
layout(location = 5) flat out uint vFaceDir;
layout(location = 6) flat out uint vSubmerged;

void main() {
    vWorldPos  = inPosition;
    vNormal    = inNormal;
    vUV        = inUV;
    vLightAO   = inLightAO;
    vBlockId   = inPackedMat & 0xFFu;
    vFaceDir   = (inPackedMat >> 8u) & 0x7u;
    vSubmerged = (inPackedMat >> 11u) & 0x1u;

    gl_Position = ubo.viewProj * vec4(inPosition, 1.0);
}
