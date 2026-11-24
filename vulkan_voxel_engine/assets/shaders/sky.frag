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

layout(location = 0) in vec2 vNDC;
layout(location = 0) out vec4 outColor;

float hash21(vec2 p) {
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y);
}

float hash31(vec3 p) {
    p = fract(p * vec3(443.8975, 397.2973, 491.1871));
    p += dot(p.zxy, p.yxz + 19.19);
    return fract(p.x * p.y * p.z);
}

float noise2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float starDustFbm(vec2 p) {
    float v = 0.0;
    float a = 0.50;
    mat2 rot = mat2(0.80, -0.60, 0.60, 0.80);
    for (int i = 0; i < 3; ++i) {
        v += a * noise2(p);
        p = rot * p * 2.1 + vec2(2.7, 1.9);
        a *= 0.48;
    }
    return v;
}

void main() {
    // Translation-free rotation-only invViewProj (precomputed on CPU):
    // Evaluated at z=0.5 with zero cameraPos subtraction -> 100% rock-steady Sun, Moon & Stars while moving!
    vec4 rayClip = ubo.invViewProj * vec4(vNDC, 0.5, 1.0);
    vec3 rayDir = normalize(rayClip.xyz / rayClip.w);

    vec3 sunDir     = normalize(ubo.sunDirIntensity.xyz);
    vec3 moonDir    = -sunDir;
    float skyInt    = ubo.sunDirIntensity.w;
    float moonVis   = ubo.sunColorMoon.w;
    float warmGlow  = ubo.skyHorizonCloudLit.w;

    // 1. Crystal-Clear Seamless 360-Degree Atmospheric Sky Sphere (ZERO clouds, ZERO horizon seam!)
    vec3 zenith  = pow(max(ubo.skyZenithTimeOfDay.rgb, vec3(0.001)), vec3(2.05));
    vec3 horizon = pow(max(ubo.skyHorizonCloudLit.rgb, vec3(0.001)), vec3(2.05));
    vec3 nadir   = mix(zenith * 0.34, horizon * 0.22, 0.40);
    vec3 mieCol  = pow(max(ubo.mieGlowDebugMode.rgb, vec3(0.001)), vec3(1.85));

    float y = clamp(rayDir.y, -1.0, 1.0);
    int gfxFlags = int(ubo.dynOriginFlags.w + 0.5);
    bool cameraUnderwater = (gfxFlags & 8) != 0;
    float eyeDepth = cameraUnderwater ? max(ubo.renderFogParams.x, 0.0) : 0.0;

    // Physical underwater optical clarity falls off exponentially with depth:
    // - Sharp stars vanish within ~2-4m below surface
    // - Crisp Sun/Moon discs diffuse and vanish by ~8-10m
    // - Broad overhead skylight glow softens smoothly into deep abyss beyond 15-20m
    float uwStarClarity = cameraUnderwater
        ? exp(-eyeDepth * 0.85) * smoothstep(0.55, 0.85, y)
        : 1.0;
    float uwDiscClarity = cameraUnderwater
        ? exp(-eyeDepth * 0.42) * smoothstep(0.45, 0.78, y)
        : 1.0;
    float uwGlowClarity = cameraUnderwater
        ? exp(-eyeDepth * 0.14) * smoothstep(0.18, 0.68, y)
        : 1.0;

    vec3 sky = (y >= 0.0)
        ? mix(horizon, zenith, pow(y, 0.52))
        : mix(horizon, nadir,  pow(-y, 0.60));

    // Pure Rayleigh zenith deepening during crisp clear daytime
    float zenithDeep = pow(max(y, 0.0), 1.35) * skyInt * (1.0 - warmGlow * 0.5);
    sky = mix(sky, zenith * 0.84, zenithDeep * 0.35);

    // Symmetric twilight scattering belt + Belt of Venus anti-solar rose-violet glow
    float belt = exp(-pow(abs(y) * 3.8, 1.55));
    float sunCos = max(dot(rayDir, sunDir), 0.0);
    float antiSunCos = max(dot(rayDir, -sunDir), 0.0);
    sky += mieCol * belt * (0.24 + 0.82 * pow(sunCos, 2.4)) * warmGlow;
    sky += vec3(0.32, 0.16, 0.36) * belt * pow(antiSunCos, 2.0) * warmGlow * 0.38;

    // 2. Full 360-Degree Sun Orbit with Atmospheric Limb Darkening & Golden Mie Aureole
    float sunOrbitVis = 0.58 + 0.42 * smoothstep(-0.85, 0.20, sunDir.y);
    float sunCorona = pow(sunCos, 26.0) * 0.38 + pow(sunCos, 160.0) * 0.85;
    sky += mieCol * sunCorona * sunOrbitVis * (0.62 + 0.68 * warmGlow) * uwGlowClarity;

    float sunDot = dot(rayDir, sunDir);
    float sunDisc = smoothstep(0.9985, 0.9994, sunDot);
    float limb = smoothstep(0.9985, 0.9999, sunDot);
    vec3 sunBody = mix(vec3(2.4, 1.45, 0.72), vec3(2.8, 2.45, 1.95), limb);
    sky += sunBody * sunDisc * sunOrbitVis * uwDiscClarity;

    // 3. Full 360-Degree Moon Orbit with Silver Halo & Lunar Maria
    float moonDot = dot(rayDir, moonDir);
    float moonOrbitVis = 0.50 + 0.50 * smoothstep(-0.85, 0.20, moonDir.y);
    float moonHalo = pow(max(moonDot, 0.0), 40.0) * 0.16 + pow(max(moonDot, 0.0), 220.0) * 0.30;
    sky += vec3(0.32, 0.48, 0.85) * moonHalo * moonOrbitVis * (0.30 + 0.70 * moonVis) * uwGlowClarity;

    float moonDisc = smoothstep(0.9987, 0.9994, moonDot);
    if (moonDisc > 0.001 && uwDiscClarity > 0.001) {
        float maria = 0.80 + 0.20 * noise2(rayDir.xz * 48.0 + rayDir.y * 20.0);
        sky += vec3(0.94, 0.97, 1.15) * moonDisc * maria * moonOrbitVis * (0.42 + 0.98 * moonVis) * uwDiscClarity;
    }

    // 4. Crystal-Clear Night Starfield + Delicate Milky Way Stardust (extinguished smoothly underwater!)
    if (moonVis > 0.04 && uwStarClarity > 0.001) {
        float hazeAtten = 0.32 + 0.68 * smoothstep(0.0, 0.28, abs(y));
        float starMask = hazeAtten * moonVis * uwStarClarity;

        float galacticPlane = abs(dot(rayDir, normalize(vec3(0.45, 0.62, -0.64))));
        float mwBand = exp(-pow(galacticPlane * 5.2, 2.0));
        float mwDust = starDustFbm(rayDir.xz * 3.6 / (abs(y) + 0.58));
        sky += vec3(0.020, 0.030, 0.064) * mwBand * mwDust * starMask;

        vec3 starCell = floor(rayDir * 260.0);
        float sr = hash31(starCell);
        if (sr > 0.9942) {
            float twinkle = 0.64 + 0.36 * sin(ubo.cameraPosTime.w * (2.0 + fract(sr * 71.0) * 3.2) + sr * 628.0);
            float tintRand = fract(sr * 137.0);
            vec3 starCol = mix(vec3(1.0, 0.86, 0.68), vec3(0.76, 0.89, 1.0), tintRand);
            float bright = smoothstep(0.9942, 0.9998, sr);
            sky += starCol * (0.32 + 1.25 * bright) * twinkle * starMask;
        }
    }

    // 5. Depth-Dependent Underwater Spectral Absorption & Volumetric Abyss Veil (matches voxel.frag!)
    if (cameraUnderwater) {
        float pathToSurface = eyeDepth / max(y, 0.14);
        float depthDark = exp(-eyeDepth * 0.105);

        vec3 uwDay   = vec3(0.042, 0.265, 0.365);
        vec3 uwDusk  = vec3(0.048, 0.125, 0.215);
        vec3 uwNight = vec3(0.005, 0.018, 0.040);
        vec3 uwAbyss = mix(uwNight, mix(uwDay, uwDusk, warmGlow * 0.62), skyInt) * mix(0.14, 1.0, depthDark);

        // Beer-Lambert spectral absorption through the water column above the camera
        vec3 surfaceTrans = exp(-pathToSurface * vec3(0.24, 0.11, 0.075));
        sky *= surfaceTrans;

        // Outside Snell's Window critical angle (~48.6 deg, cos ~ 0.66), rays reflect into the abyss;
        // inside Snell's Window, volumetric scattering progressively veils the overhead sky with depth
        float tirMask = 1.0 - smoothstep(0.50, 0.72, y);
        float volVeil = clamp(1.0 - exp(-pathToSurface * 0.165), 0.0, 0.985);
        sky = mix(sky, uwAbyss, max(tirMask, volVeil));
    }

    vec3 x = sky;
    vec3 mapped = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    outColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}
