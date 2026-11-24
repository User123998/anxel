#version 450

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 viewProj;
    mat4 invViewProj;
    vec4 cameraPosTime;
    vec4 sunDirIntensity;
    vec4 sunColorMoon;
    vec4 skyZenithTimeOfDay;
    vec4 skyHorizonCloudLit;
    vec4 skyAmbientFogDensity;
    vec4 mieGlowDebugMode;
    vec4 heldLightPosStrength;
    vec4 dynOriginFlags;
    vec4 renderFogParams;
} ubo;

// Hardware-filtered 32x32x32 R8_UNORM occluded dynamic light volume (256 smooth Euclidean levels!)
layout(set = 0, binding = 1) uniform sampler3D uDynLightTex;

// Real PNG block textures loaded from assets/texture/block/*.png (18 layers, REPEAT + NEAREST)
layout(set = 0, binding = 2) uniform sampler2DArray uBlockTextures;

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec4 vLightAO;
layout(location = 4) flat in uint vBlockId;
layout(location = 5) flat in uint vFaceDir;
layout(location = 6) flat in uint vSubmerged;

layout(location = 0) out vec4 outColor;

// Zero-anchored exponential light curve matching src/math/lighting_math.hpp:
// Strictly 0.0000 when level == 0 (eliminates phantom glow in pitch-black caves!)
float lightCurve(float norm, float base) {
    if (norm <= 0.0005) return 0.0;
    float raw = pow(base, (1.0 - norm) * 15.0);
    float floorVal = pow(base, 15.0);
    return clamp((raw - floorVal) / (1.0 - floorVal), 0.0, 1.0);
}

// Multi-directional animated water surface wave normal (fast analytical sum)
vec3 evalWaterNormal(vec2 p, float t, float signY) {
    float w1 = cos(p.x * 1.45 + p.y * 0.95 + t * 2.15) * 0.030;
    float w2 = cos(p.x * 2.65 - p.y * 2.10 - t * 2.85) * 0.018;
    float w3 = sin(p.x * 0.72 - p.y * 1.60 + t * 1.45) * 0.024;
    float dx = w1 * 1.45 + w2 * 2.65 + w3 * 0.72;
    float dz = w1 * 0.95 - w2 * 2.10 - w3 * 1.60;
    return normalize(vec3(-dx * 0.42, signY, -dz * 0.42));
}

// Dual-layer drifting sunlight caustics projected onto shallow riverbeds & seabeds
float evalCaustics(vec2 p, float t) {
    vec2 a = p * 1.15 + vec2(t * 0.58, t * 0.41);
    vec2 b = p * 1.52 + vec2(-t * 0.49, t * 0.63);
    float c1 = sin(a.x + sin(a.y * 1.35 + t * 0.75)) * cos(a.y + cos(a.x * 1.20 - t * 0.65));
    float c2 = sin(b.x * 1.25 - cos(b.y * 1.10 + t * 0.90)) * cos(b.y * 1.15 + sin(b.x * 0.95));
    float peak = clamp(1.0 - abs(c1 + c2) * 1.15, 0.0, 1.0);
    return peak * peak * peak;
}

// Maps BlockType + faceDir -> BlockTexLayer in uBlockTextures (assets/texture/block/*.png)
// Face directions: 0=+X, 1=-X, 2=+Y(Top), 3=-Y(Bottom), 4=+Z, 5=-Z
uint resolveBlockTexLayer(uint id, uint faceDir) {
    if (id == 1u) { // Grass
        return (faceDir == 2u) ? 0u : ((faceDir == 3u) ? 2u : 1u);
    } else if (id == 2u) { return 3u; } // Dirt
    else if (id == 3u)   { return 4u; } // Stone
    else if (id == 4u)   { return 5u; } // DeepSlate
    else if (id == 5u)   { return (faceDir == 2u || faceDir == 3u) ? 6u : 7u; } // Timber
    else if (id == 6u)   { return 8u; } // Leaves
    else if (id == 7u)   { return (faceDir == 2u) ? 10u : 9u; } // Torch
    else if (id == 8u)   { return 11u; } // Glowstone
    else if (id == 9u)   { return 12u; } // Water
    else if (id == 10u)  { return 13u; } // Sand
    else if (id == 11u)  { return 14u; } // Magma
    else if (id == 12u)  { return 15u; } // Crystal
    else if (id == 13u)  { return 16u; } // Snow
    else if (id == 14u)  { return 17u; } // Glass
    return 4u;
}

// Samples real PNG textures from uBlockTextures (with top-to-bottom V flip for PNG orientation)
vec4 sampleVoxelTexture(uint id, uint faceDir, vec2 uv, vec3 worldPos,
                        out float emissiveMask, out float specStrength) {
    emissiveMask = 0.0;
    specStrength = 0.0;

    uint layer = resolveBlockTexLayer(id, faceDir);
    vec2 sampleUV = vec2(uv.x, 1.0 - uv.y);
    vec4 texel = texture(uBlockTextures, vec3(sampleUV, float(layer)));

    if (id == 7u) {
        // Torch: glowing flame pixels on top face and upper side rows
        float flamePix = step(0.72, texel.r) * step(0.38, texel.g);
        emissiveMask = (faceDir == 2u) ? 1.0 : clamp(flamePix, 0.0, 1.0);
    } else if (id == 8u) {
        // Glowstone: warm luminous interior cells
        float luma = dot(texel.rgb, vec3(0.299, 0.587, 0.114));
        emissiveMask = smoothstep(0.38, 0.80, luma) * 0.94;
    } else if (id == 9u) {
        specStrength = 0.90;
    } else if (id == 11u) {
        // Magma: animated glowing molten fissures contrasting dark basalt crust
        float pulse = 0.88 + 0.18 * sin(ubo.cameraPosTime.w * 2.8 + worldPos.x + worldPos.z);
        float molten = smoothstep(0.28, 0.62, texel.r - texel.b * 0.85);
        emissiveMask = mix(0.10, 0.95, molten);
        texel.rgb *= mix(1.0, pulse, molten);
    } else if (id == 12u) {
        // Crystal: bioluminescent cyan-teal veins
        emissiveMask = 0.85;
        specStrength = 0.45;
    } else if (id == 13u) {
        specStrength = 0.24;
    } else if (id == 14u) {
        specStrength = 0.95;
    }

    return texel;
}

// Evaluates the exact atmospheric sky color in rayDir for seamless Border Fog & Fresnel reflections
vec3 evalSkyFogColor(vec3 rayDir, vec3 sunDir) {
    vec3 zenith  = pow(max(ubo.skyZenithTimeOfDay.rgb, vec3(0.001)), vec3(2.05));
    vec3 horizon = pow(max(ubo.skyHorizonCloudLit.rgb, vec3(0.001)), vec3(2.05));
    vec3 nadir   = mix(zenith * 0.34, horizon * 0.22, 0.40);
    vec3 mieCol  = pow(max(ubo.mieGlowDebugMode.rgb, vec3(0.001)), vec3(1.85));
    float skyInt   = ubo.sunDirIntensity.w;
    float warmGlow = ubo.skyHorizonCloudLit.w;

    float y = clamp(rayDir.y, -1.0, 1.0);
    vec3 sky = (y >= 0.0)
        ? mix(horizon, zenith, pow(y, 0.52))
        : mix(horizon, nadir,  pow(-y, 0.60));

    float zenithDeep = pow(max(y, 0.0), 1.35) * skyInt * (1.0 - warmGlow * 0.5);
    sky = mix(sky, zenith * 0.84, zenithDeep * 0.35);

    float belt       = exp(-pow(abs(y) * 3.8, 1.55));
    float sunCos     = max(dot(rayDir, sunDir), 0.0);
    float antiSunCos = max(dot(rayDir, -sunDir), 0.0);
    sky += mieCol * belt * (0.24 + 0.82 * pow(sunCos, 2.4)) * warmGlow;
    sky += vec3(0.32, 0.16, 0.36) * belt * pow(antiSunCos, 2.0) * warmGlow * 0.38;
    return sky;
}

vec3 acesToneMap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    float emissiveMask = 0.0;
    float specStrength = 0.0;
    vec4 texSample = sampleVoxelTexture(vBlockId, vFaceDir, vUV, vWorldPos, emissiveMask, specStrength);
    vec3 srgbAlbedo = texSample.rgb;
    vec3 albedo = pow(max(srgbAlbedo, vec3(0.001)), vec3(2.15));

    int gfxFlags          = int(ubo.dynOriginFlags.w + 0.5);
    bool enableSmoothAO   = (gfxFlags & 1) != 0;
    bool enableSpecular   = (gfxFlags & 2) != 0;
    bool enableBorderFog  = (gfxFlags & 4) != 0;
    bool cameraUnderwater = (gfxFlags & 8) != 0;

    float skyNorm   = clamp(vLightAO.x, 0.0, 1.0);
    float blockNorm = clamp(vLightAO.y, 0.0, 1.0);
    float aoNorm    = enableSmoothAO ? clamp(vLightAO.z, 0.0, 1.0) : 1.0;

    float t = ubo.cameraPosTime.w;
    vec3 N = normalize(vNormal);
    vec3 viewVec = vWorldPos - ubo.cameraPosTime.xyz;
    float viewDist = length(viewVec);
    vec3 viewDir = viewVec / max(viewDist, 0.001);
    vec3 V = -viewDir;
    float faceShade = (N.y > 0.5) ? 1.0 : ((N.y < -0.5) ? 0.44 : (abs(N.x) > 0.5 ? 0.76 : 0.65));

    // Pure time-only subtle firelight breathing (zero spatial sine-wave -> zero moving bent lines!)
    float flicker = 1.0 + 0.020 * sin(t * 6.2) + 0.012 * sin(t * 13.4 + 1.1);

    // -------------------------------------------------------------------------
    // 1. Silky-Smooth Zero-Blockiness Hardware-Trilinear 3D Occluded Dynamic Light
    //    - cellReach (texelFetch) is ONLY a binary solid-wall visibility gate (ZERO wall bleed!)
    //    - NEVER mixes discrete cellReach into brightness -> ZERO 1x1 square grid artifacts!
    //    - Pure hardware trilinear sample of 256-level Euclidean field + continuous radial falloff
    // -------------------------------------------------------------------------
    float dynIntensity = 0.0;
    vec3 heldDynLight = vec3(0.0);
    float dynStrength = ubo.heldLightPosStrength.w;
    if (dynStrength > 0.001) {
        vec3 toLight = ubo.heldLightPosStrength.xyz - vWorldPos;
        float dEuclid = length(toLight);
        float maxRange = dynStrength * 14.5;

        if (dEuclid < maxRange) {
            vec3 localPos = (vWorldPos + N * 0.50) - ubo.dynOriginFlags.xyz;
            if (all(greaterThanEqual(localPos, vec3(0.0))) &&
                all(lessThan(localPos, vec3(32.0)))) {
                ivec3 cell = clamp(ivec3(floor(localPos)), ivec3(0), ivec3(31));
                float cellReach = texelFetch(uDynLightTex, cell, 0).r;

                if (cellReach > 0.001) {
                    // Pure hardware trilinear reach (normalized 0..1 across 256 smooth Euclidean steps)
                    float triReach = texture(uDynLightTex, localPos / 32.0).r;

                    // Only apply continuous path-detour attenuation when light genuinely bends around a wall/doorway
                    float dPath  = (1.0 - triReach) * maxRange;
                    float detour = max(0.0, dPath - dEuclid - 2.35);
                    float dEff   = dEuclid + detour * 1.05;

                    // Soft penumbra near occluded doorways without any discrete 1x1 stair-stepping
                    float occGate = smoothstep(0.001, 0.065, triReach);

                    // C2-continuous spherical radial falloff hitting exact 0.0 at dEff >= maxRange
                    float u = clamp(1.0 - dEff / maxRange, 0.0, 1.0);
                    float smoothRadial = (u * u * (3.0 - 2.0 * u)) / (1.0 + 0.050 * dEff * dEff);

                    // Soft wrap-Lambert facing shade so walls/floors curve smoothly toward the held torch
                    vec3 L = toLight / max(dEuclid, 0.001);
                    float ndl = clamp(dot(N, L) * 0.52 + 0.48, 0.28, 1.0);

                    dynIntensity = smoothRadial * occGate * ndl * flicker * (0.45 + 0.55 * dynStrength);
                    vec3 dynWarmNear = vec3(1.00, 0.74, 0.36);
                    vec3 dynWarmFar  = vec3(0.95, 0.36, 0.08);
                    vec3 dynCol = mix(dynWarmFar, dynWarmNear, clamp(dynIntensity * 1.4, 0.0, 1.0));
                    heldDynLight = dynCol * dynIntensity * 2.55;
                }
            }
        }
    }

    int debugMode = int(ubo.mieGlowDebugMode.w + 0.5);
    if (debugMode == 1) {
        vec3 skyVisColor = mix(vec3(0.01, 0.02, 0.04), vec3(0.45, 0.82, 1.0), pow(skyNorm, 1.4));
        outColor = vec4(skyVisColor, 1.0);
        return;
    } else if (debugMode == 2) {
        float visBlk = clamp(max(blockNorm, pow(clamp(dynIntensity, 0.0, 1.0), 0.55)), 0.0, 1.0);
        vec3 blkVisColor = mix(vec3(0.01, 0.01, 0.02), vec3(1.0, 0.68, 0.22), pow(visBlk, 1.25));
        outColor = vec4(blkVisColor, 1.0);
        return;
    } else if (debugMode == 3) {
        outColor = vec4(vec3(mix(0.14, 1.0, aoNorm)), 1.0);
        return;
    }

    // 2. Zero-Anchored Perceptual Lightfield Attenuation (EXACT 0.0 at level 0!)
    float skyFalloff   = lightCurve(skyNorm, 0.76);
    float blockFalloff = lightCurve(blockNorm, 0.74) * mix(1.0, flicker, step(0.01, blockNorm));

    // 3. Sky-Visibility-Coupled Direct Sunlight & Natural Moonlight (strictly open-sky!)
    vec3 sunDir         = normalize(ubo.sunDirIntensity.xyz);
    float skyIntensity  = ubo.sunDirIntensity.w;
    float moonIntensity = ubo.sunColorMoon.w;
    float sunVisibility = smoothstep(0.86, 0.99, skyNorm);

    float sunHorizFade = smoothstep(-0.05, 0.14, sunDir.y);
    float sunNdotL     = max(dot(N, sunDir), 0.0);
    vec3 directSun     = ubo.sunColorMoon.rgb * sunNdotL * sunHorizFade * sunVisibility * skyIntensity * 1.42;

    vec3 moonDir     = normalize(vec3(-sunDir.x, max(-sunDir.y, 0.20), -sunDir.z));
    float moonNdotL  = max(dot(N, moonDir), 0.0);
    vec3 directMoon  = vec3(0.18, 0.30, 0.58) * moonNdotL * sunVisibility * moonIntensity * 0.16;

    // Area-aware Hemisphere Sky/Ground bounce fill
    vec3 skyFillColor = mix(ubo.skyAmbientFogDensity.rgb * 0.38, ubo.skyAmbientFogDensity.rgb, N.y * 0.5 + 0.5);
    vec3 skyFill = skyFillColor * skyFalloff * skyIntensity * faceShade * 0.68;
    vec3 starlightOpen = vec3(0.0045, 0.0072, 0.0155) * skyFalloff * moonIntensity * faceShade;

    // 4. Static Placed Block Light + Silky-Smooth Occluded Held-Block Dynamic Light
    vec3 warmNear = vec3(1.00, 0.72, 0.34);
    vec3 warmFar  = vec3(0.95, 0.32, 0.07);
    vec3 blockColor = mix(warmFar, warmNear, clamp(blockFalloff * 1.35, 0.0, 1.0));
    vec3 localBlockLight = blockColor * blockFalloff * 2.55 * mix(0.84, 1.0, faceShade);
    vec3 totalEmissiveLight = localBlockLight + heldDynLight;

    float aoAmbient  = mix(0.14, 1.0, aoNorm);
    float aoEmissive = mix(0.48, 1.0, aoNorm);
    vec3 deepCaveFloor = vec3(0.00018, 0.00024, 0.00042) * faceShade * aoAmbient;

    vec3 litColor = vec3(0.0);
    float outAlpha = 1.0;

    // =========================================================================
    // SPECIAL MATERIAL A: Seamless Connected Optical Water (Smooth Depth + Fresnel + Snell's Window)
    // =========================================================================
    if (vBlockId == 9u) {
        // Smoothly interpolated bilinear water depth (0..12m) across every pixel -> ZERO blocky patches!
        float colDepth = clamp(vLightAO.w, 0.0, 1.0) * 12.0;
        float skyAccess = smoothstep(0.02, 0.28, skyNorm);
        vec3 envLight = deepCaveFloor + (skyFill + starlightOpen + directSun * 0.65 + directMoon) + totalEmissiveLight * 0.72;

        if (N.y < -0.5 && cameraUnderwater && vSubmerged == 1u) {
            // Underwater looking UP through the true open-sky water ceiling -> Depth-Dependent Snell's Window!
            float eyeDepth = max(ubo.renderFogParams.x, 0.0);
            float depthDark = exp(-eyeDepth * 0.105);
            float surfaceClarity = exp(-viewDist * 0.135);
            vec3 surfaceTrans = exp(-viewDist * vec3(0.24, 0.11, 0.075));

            vec3 waveN = evalWaterNormal(vWorldPos.xz, t, -1.0);
            float cosTheta = clamp(dot(V, vec3(0.0, -1.0, 0.0)), 0.0, 1.0);
            float wavePerturb = (waveN.x * viewDir.x + waveN.z * viewDir.z) * 0.28;
            // Water-to-air critical angle is 48.6 deg (cos ~ 0.66)
            float snellMask = smoothstep(0.56, 0.74, cosTheta + wavePerturb);

            vec3 refractedSky = evalSkyFogColor(normalize(vec3(viewDir.x * 0.75, max(viewDir.y, 0.14), viewDir.z * 0.75)), sunDir)
                              * skyAccess * surfaceTrans;
            vec3 tirDay   = vec3(0.032, 0.205, 0.305);
            vec3 tirNight = vec3(0.004, 0.015, 0.034);
            vec3 tirAbyss = mix(tirNight, tirDay, skyIntensity) * mix(0.14, 1.0, depthDark) * skyAccess;
            vec3 sunShimmer = ubo.sunColorMoon.rgb * pow(max(dot(normalize(viewDir + waveN * 0.25), sunDir), 0.0), 28.0)
                            * sunHorizFade * skyIntensity * skyAccess * surfaceClarity * 0.95;

            litColor = mix(tirAbyss + totalEmissiveLight * 0.16, refractedSky * 0.92 + sunShimmer, snellMask);
            outAlpha = mix(0.82, mix(0.92, 0.22, surfaceClarity), snellMask);
        } else {
            // Above-water surface (or exposed cliff waterfall) -> Smooth connected Beer-Lambert depth + Fresnel
            // Attenuate wave normal gently at night so night water stays calm, unified, and line-free!
            float rippleScale = mix(0.32, 1.0, skyIntensity);
            vec3 rawWaveN = (N.y > 0.5) ? evalWaterNormal(vWorldPos.xz, t, 1.0) : N;
            vec3 waveN = normalize(mix(N, rawWaveN, rippleScale));
            float viewCos = clamp(dot(waveN, V), 0.05, 1.0);
            float fresnel = 0.02 + 0.98 * pow(1.0 - viewCos, 5.0);

            // Continuous shoreline -> shallow turquoise -> coastal cerulean -> deep marine, modulated by real water.png
            vec2 waterDriftUV = vec2(vUV.x + t * 0.04, 1.0 - (vUV.y + t * 0.03));
            vec3 waterTex = pow(max(texture(uBlockTextures, vec3(waterDriftUV, 12.0)).rgb, vec3(0.001)), vec3(2.15));
            float shallowT = smoothstep(0.0, 4.5, colDepth);
            float deepT    = smoothstep(3.5, 11.5, colDepth);
            vec3 shallowCol = mix(vec3(0.09, 0.44, 0.50), waterTex * 1.15, 0.45);
            vec3 midCol     = mix(vec3(0.032, 0.22, 0.38), waterTex * 0.65, 0.35);
            vec3 deepCol    = vec3(0.015, 0.105, 0.225);
            vec3 waterTint  = mix(mix(shallowCol, midCol, shallowT), deepCol, deepT);

            // Natural readable body scattering: clear sunlit azure by day, calm dark moonlit navy at night
            vec3 dayGlow = vec3(0.010, 0.032, 0.052) * skyIntensity * skyAccess;
            vec3 nightGlow = vec3(0.0018, 0.0042, 0.0095) * moonIntensity * skyAccess;
            vec3 waterBody = waterTint * (envLight * 1.05 + dayGlow + nightGlow);

            // Fresnel-reflected 360-degree clear sky (strictly gated by skyAccess -> ZERO cave white spots!)
            vec3 reflDir = reflect(-V, waveN);
            reflDir.y = max(reflDir.y, 0.02);
            vec3 skyRefl = evalSkyFogColor(reflDir, sunDir) * skyAccess * (0.18 + 0.82 * skyFalloff);

            // Crisp sun & moon specular sparkle path across water ripples
            vec3 spec = vec3(0.0);
            if (enableSpecular && sunVisibility > 0.04) {
                vec3 Hsun = normalize(sunDir + V);
                float sunSpec = pow(max(dot(waveN, Hsun), 0.0), 115.0) * 1.75
                              + pow(max(dot(waveN, Hsun), 0.0), 24.0)  * 0.18;
                vec3 Hmoon = normalize(moonDir + V);
                float moonSpec = pow(max(dot(waveN, Hmoon), 0.0), 82.0) * 0.36;
                spec = (ubo.sunColorMoon.rgb * sunSpec * sunHorizFade * skyIntensity
                      + vec3(0.42, 0.62, 0.96) * moonSpec * moonIntensity) * sunVisibility;
            }

            // Smooth continuous optical depth visibility:
            // shoreline feathers effortlessly from crystal-clear 0.18 to natural readable deep ocean 0.86
            float opticalPath = max(colDepth, 0.35) / mix(0.48, 1.0, viewCos);
            float bodyAlpha   = (N.y > 0.5)
                ? (1.0 - exp(-opticalPath * 0.19))
                : 0.54;

            // Night fresnel slightly calmer so dark water reflects softly without harsh streaks
            float fresnelWeight = mix(0.55, 0.86, skyIntensity);
            litColor = mix(waterBody, skyRefl, fresnel * fresnelWeight) + spec;
            outAlpha = clamp(mix(bodyAlpha, 0.92, fresnel * 0.72), 0.16, 0.93);
        }
    }
    // =========================================================================
    // SPECIAL MATERIAL B: Textured Transparent Glass Block (ID = 14, assets/texture/block/glass.png)
    // =========================================================================
    else if (vBlockId == 14u) {
        float viewCos = clamp(abs(dot(N, V)), 0.05, 1.0);
        float fresnel = 0.04 + 0.96 * pow(1.0 - viewCos, 4.5);

        vec3 reflDir = reflect(-V, N);
        vec3 skyRefl = evalSkyFogColor(reflDir, sunDir) * (0.25 + 0.75 * skyFalloff);

        vec3 glassSpec = vec3(0.0);
        if (enableSpecular) {
            vec3 Hsun = normalize(sunDir + V);
            float s = pow(max(dot(N, Hsun), 0.0), 96.0) * sunHorizFade * skyIntensity * sunVisibility;
            vec3 Hmoon = normalize(moonDir + V);
            float m = pow(max(dot(N, Hmoon), 0.0), 64.0) * moonIntensity * sunVisibility;
            glassSpec = ubo.sunColorMoon.rgb * s * 1.45 + vec3(0.55, 0.72, 1.0) * m * 0.35;
        }

        vec3 glassIrrad = deepCaveFloor + (skyFill + starlightOpen + directSun * 0.6 + directMoon) + totalEmissiveLight * 0.75;
        vec3 texLinear = albedo * (glassIrrad * 0.95 + vec3(0.05) * skyIntensity);
        float texOpacity = texSample.a;

        litColor = mix(texLinear, skyRefl, fresnel * (1.0 - texOpacity * 0.55)) + glassSpec;
        outAlpha = clamp(max(texOpacity, 0.14 + fresnel * 0.38), 0.14, 0.92);
    }
    // =========================================================================
    // STANDARD OPAQUE VOXELS (with Submerged Beer-Lambert Absorption & Animated Caustics!)
    // =========================================================================
    else {
        vec3 specular = vec3(0.0);
        if (enableSpecular && specStrength > 0.01 && sunVisibility > 0.05) {
            vec3 Hsun = normalize(sunDir + V);
            float sunSpec = pow(max(dot(N, Hsun), 0.0), 42.0) * sunHorizFade * skyIntensity;
            vec3 Hmoon = normalize(moonDir + V);
            float moonSpec = pow(max(dot(N, Hmoon), 0.0), 36.0) * moonIntensity;
            specular = (ubo.sunColorMoon.rgb * sunSpec * 0.75 + vec3(0.36, 0.52, 0.85) * moonSpec * 0.25)
                       * specStrength * sunVisibility;
        }

        vec3 totalIrradiance =
            deepCaveFloor +
            (skyFill + starlightOpen + directSun + directMoon) * aoAmbient +
            totalEmissiveLight * aoEmissive;

        // Submerged solid blocks receive realistic sunlight caustics + gentle Beer-Lambert optical tint
        if (vSubmerged == 1u) {
            float subDepth = clamp(vLightAO.w, 0.0, 1.0) * 12.0;
            // Natural underwater spectral tint (red attenuates gently, turquoise/blue stays clearly readable!)
            vec3 beerTrans = exp(-subDepth * vec3(0.115, 0.044, 0.022));
            totalIrradiance *= mix(vec3(1.0), beerTrans, 0.68);
            // Soft aquatic scatter fill so sunlit deep ocean floors remain naturally visible
            totalIrradiance += vec3(0.012, 0.045, 0.068) * skyIntensity * skyFalloff * aoAmbient;

            if (sunHorizFade > 0.02 && skyNorm > 0.12) {
                float caust = evalCaustics(vWorldPos.xz + vWorldPos.y * 0.25, t);
                float depthFade = exp(-subDepth * 0.14);
                float upMask = clamp(N.y * 0.65 + 0.35, 0.15, 1.0);
                totalIrradiance += ubo.sunColorMoon.rgb * caust * depthFade * upMask
                                 * sunHorizFade * skyIntensity * skyFalloff * 0.85;
            }
        }

        litColor = albedo * totalIrradiance + specular;

        if (emissiveMask > 0.01) {
            litColor = mix(litColor, srgbAlbedo * (1.85 * flicker), emissiveMask);
        }
    }

    // -------------------------------------------------------------------------
    // 5. Natural Volumetric Underwater Visibility vs Two-Layer Outdoor / Border Fog
    // -------------------------------------------------------------------------
    if (cameraUnderwater) {
        // Depth-dependent underwater visibility: clear turquoise shallows (~38m) -> dense dark abyss (~14m)
        float eyeDepth = max(ubo.renderFogParams.x, 0.0);
        float depthDark = exp(-eyeDepth * 0.105);
        float uwDensity = mix(0.026, 0.068, clamp(eyeDepth / 24.0, 0.0, 1.0));

        vec3 uwDay   = vec3(0.042, 0.265, 0.365);
        vec3 uwDusk  = vec3(0.048, 0.125, 0.215);
        vec3 uwNight = vec3(0.005, 0.018, 0.040);
        vec3 uwBase  = mix(uwNight, mix(uwDay, uwDusk, ubo.skyHorizonCloudLit.w * 0.62), skyIntensity)
                     * mix(0.14, 1.0, depthDark);
        // Only sealed pitch-black flooded caves (skyNorm == 0) darken fully; open oceans darken smoothly with depth
        float uwCave = smoothstep(0.0, 0.18, skyNorm);
        vec3 uwFogCol = mix(vec3(0.001, 0.003, 0.007), uwBase, uwCave);

        float uwFog = clamp(1.0 - exp(-viewDist * uwDensity), 0.0, 0.96);
        // Gentle Beer-Lambert color absorption with distance underwater (keeps terrain readable!)
        vec3 distAbsorb = exp(-viewDist * vec3(0.032, 0.012, 0.006));
        litColor *= mix(vec3(1.0), distAbsorb, 0.55);
        litColor = mix(litColor, uwFogCol, uwFog);
        // Deep water ceiling smoothly merges into the volumetric underwater fog
        if (vBlockId == 9u && N.y < -0.5) {
            outAlpha = mix(outAlpha, 0.96, uwFog * 0.72);
        }
    } else {
        float horizDist = length(viewVec.xz);
        float fogStrength = clamp(ubo.renderFogParams.w, 0.0, 1.0);
        float atmoDensity = ubo.skyAmbientFogDensity.w * fogStrength;
        float atmoFog     = (fogStrength > 0.001)
            ? clamp(1.0 - exp(-pow(viewDist * atmoDensity, 1.42)), 0.0, 0.85)
            : 0.0;

        float borderFog = 0.0;
        if (enableBorderFog) {
            float borderStart = ubo.renderFogParams.y;
            float borderEnd   = max(ubo.renderFogParams.z, borderStart + 8.0);
            float bt = clamp((horizDist - borderStart) / (borderEnd - borderStart), 0.0, 1.0);
            borderFog = bt * bt * (3.0 - 2.0 * bt);
        }

        float fogFactor = clamp(max(atmoFog, borderFog), 0.0, 1.0);
        if (fogFactor > 0.0005) {
            vec3 skyFogCol = evalSkyFogColor(viewDir, sunDir);
            float caveBlend = 1.0 - smoothstep(0.04, 0.42, skyNorm);
            vec3 activeFogColor = mix(skyFogCol, vec3(0.00015, 0.00020, 0.00035), caveBlend);
            litColor = mix(litColor, activeFogColor, fogFactor);
            if (vBlockId == 9u || vBlockId == 14u) {
                // Distant water/glass smoothly dissolves into the horizon sky
                outAlpha = mix(outAlpha, 1.0, borderFog * 0.85);
            }
        }
    }

    vec3 mapped = acesToneMap(litColor * 1.05);
    mapped = pow(mapped, vec3(1.0 / 2.2));
    outColor = vec4(mapped, outAlpha);
}
