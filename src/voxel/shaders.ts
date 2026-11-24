import * as THREE from 'three';

// Mirrors vulkan_voxel_engine/assets/shaders/voxel.vert & assets/shaders/voxel.frag
export const VOXEL_VERTEX_SHADER = /* glsl */ `
attribute vec4 aLightAO;
attribute vec2 aMatFace;
attribute vec2 aWaterData;

varying vec3 vWorldPos;
varying vec3 vNormal;
varying vec2 vUv;
varying vec4 vLightAO;
varying vec2 vMatFace;
varying vec2 vWaterData;

void main() {
  vec4 worldPosition = modelMatrix * vec4(position, 1.0);
  vWorldPos = worldPosition.xyz;
  vNormal = normal;
  vUv = uv;
  vLightAO = aLightAO;
  vMatFace = aMatFace;
  vWaterData = aWaterData;
  gl_Position = projectionMatrix * viewMatrix * worldPosition;
}
`;

export const VOXEL_FRAGMENT_SHADER = /* glsl */ `
precision highp float;

uniform float uTime;
uniform vec3 uSunDir;
uniform float uSkyIntensity;
uniform vec3 uSunColor;
uniform float uMoonIntensity;
uniform vec3 uSkyAmbient;
uniform vec3 uSkyZenith;
uniform vec3 uHorizonFog;
uniform vec3 uMieGlow;
uniform float uWarmGlow;
uniform float uFogDensity;
uniform float uFogStrength;
uniform vec3 uBorderFogParams;
uniform vec2 uQualityFlags;
uniform vec3 uCaveDark;
uniform float uDebugMode;
uniform vec4 uHeldLightPosStrength;
uniform float uCameraUnderwater;
uniform float uWaterDepth;
uniform sampler2D uBlockAtlas;

varying vec3 vWorldPos;
varying vec3 vNormal;
varying vec2 vUv;
varying vec4 vLightAO;
varying vec2 vMatFace;
varying vec2 vWaterData;

// Zero-anchored exponential light curve matching vulkan_voxel_engine/assets/shaders/voxel.frag
float lightCurve(float norm, float base) {
  if (norm <= 0.0005) return 0.0;
  float raw = pow(base, (1.0 - norm) * 15.0);
  float floorVal = pow(base, 15.0);
  return clamp((raw - floorVal) / (1.0 - floorVal), 0.0, 1.0);
}

// Mirrors BlockTexLayer (18 layers in assets/texture/block/*.png)
int resolveBlockTexLayer(int id, int faceDir) {
  if (id == 1) {
    return (faceDir == 2) ? 0 : ((faceDir == 3) ? 2 : 1);
  } else if (id == 2) { return 3; }
  else if (id == 3)   { return 4; }
  else if (id == 4)   { return 5; }
  else if (id == 5)   { return (faceDir == 2 || faceDir == 3) ? 6 : 7; }
  else if (id == 6)   { return 8; }
  else if (id == 7)   { return (faceDir == 2) ? 10 : 9; }
  else if (id == 8)   { return 11; }
  else if (id == 9)   { return 12; }
  else if (id == 10)  { return 13; }
  else if (id == 11)  { return 14; }
  else if (id == 12)  { return 15; }
  else if (id == 13)  { return 16; }
  else if (id == 14)  { return 17; }
  return 4;
}

vec4 sampleAtlasTile(int layer, vec2 uv) {
  float u = (float(layer) + clamp(fract(uv.x), 0.004, 0.996)) / 18.0;
  float v = clamp(1.0 - fract(uv.y), 0.004, 0.996);
  return texture2D(uBlockAtlas, vec2(u, v));
}

vec4 sampleVoxelTexture(int id, int faceDir, vec2 uv, vec3 worldPos,
                        out float emissiveMask, out float specStrength) {
  emissiveMask = 0.0;
  specStrength = 0.0;

  int layer = resolveBlockTexLayer(id, faceDir);
  vec4 texel = sampleAtlasTile(layer, uv);

  if (id == 7) {
    float flamePix = step(0.72, texel.r) * step(0.38, texel.g);
    emissiveMask = (faceDir == 2) ? 1.0 : clamp(flamePix, 0.0, 1.0);
  } else if (id == 8) {
    float luma = dot(texel.rgb, vec3(0.299, 0.587, 0.114));
    emissiveMask = smoothstep(0.38, 0.80, luma) * 0.94;
  } else if (id == 9) {
    specStrength = 0.92;
  } else if (id == 11) {
    float pulse = 0.88 + 0.18 * sin(uTime * 2.8 + worldPos.x + worldPos.z);
    float molten = smoothstep(0.28, 0.62, texel.r - texel.b * 0.85);
    emissiveMask = mix(0.10, 0.95, molten);
    texel.rgb *= mix(1.0, pulse, molten);
  } else if (id == 12) {
    emissiveMask = 0.85;
    specStrength = 0.45;
  } else if (id == 13) {
    specStrength = 0.24;
  } else if (id == 14) {
    specStrength = 0.95;
  }

  return texel;
}

vec3 evalSkyFogColor(vec3 rayDir, vec3 sunDir) {
  vec3 zenith = pow(max(uSkyZenith, vec3(0.001)), vec3(2.05));
  vec3 horizon = pow(max(uHorizonFog, vec3(0.001)), vec3(2.05));
  vec3 nadir = mix(zenith * 0.34, horizon * 0.22, 0.40);
  vec3 mieCol = pow(max(uMieGlow, vec3(0.001)), vec3(1.85));

  float y = clamp(rayDir.y, -1.0, 1.0);
  vec3 sky = (y >= 0.0)
    ? mix(horizon, zenith, pow(y, 0.52))
    : mix(horizon, nadir, pow(-y, 0.60));

  float zenithDeep = pow(max(y, 0.0), 1.35) * uSkyIntensity * (1.0 - uWarmGlow * 0.5);
  sky = mix(sky, zenith * 0.84, zenithDeep * 0.35);

  float belt = exp(-pow(abs(y) * 3.8, 1.55));
  float sunCos = max(dot(rayDir, sunDir), 0.0);
  float antiSunCos = max(dot(rayDir, -sunDir), 0.0);
  sky += mieCol * belt * (0.24 + 0.82 * pow(sunCos, 2.4)) * uWarmGlow;
  sky += vec3(0.32, 0.16, 0.36) * belt * pow(antiSunCos, 2.0) * uWarmGlow * 0.38;
  return sky;
}

// Fast procedural sunlight caustics on submerged riverbeds & lake floors
float evalCaustics(vec2 p, float t) {
  float c1 = sin(p.x * 2.1 + t * 1.45 + sin(p.y * 1.7 - t * 1.1));
  float c2 = cos(p.y * 2.4 - t * 1.25 + cos(p.x * 1.9 + t * 0.95));
  float c3 = sin((p.x + p.y) * 1.6 - t * 1.65);
  float v = abs(c1 * c2 + 0.35 * c3);
  return smoothstep(0.68, 1.15, v);
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
  int blockId = int(vMatFace.x + 0.5);
  int faceDir = int(vMatFace.y + 0.5);
  float isSubmerged = vWaterData.x;
  float waterDepth = vWaterData.y;

  float emissiveMask = 0.0;
  float specStrength = 0.0;
  vec4 texSample = sampleVoxelTexture(blockId, faceDir, vUv, vWorldPos, emissiveMask, specStrength);
  vec3 srgbAlbedo = texSample.rgb;
  vec3 albedo = pow(max(srgbAlbedo, vec3(0.001)), vec3(2.15));

  float skyNorm = clamp(vLightAO.x, 0.0, 1.0);
  float blockNorm = clamp(vLightAO.y, 0.0, 1.0);
  float aoNorm = mix(1.0, clamp(vLightAO.z, 0.0, 1.0), uQualityFlags.x);
  float dynReach = clamp(vLightAO.w, 0.0, 1.0);

  vec3 N = normalize(vNormal);
  vec3 viewVec = vWorldPos - cameraPosition;
  float viewDist = length(viewVec);
  float horizDist = length(viewVec.xz);
  vec3 viewDir = viewVec / max(viewDist, 0.001);
  vec3 V = -viewDir;
  float faceShade = (N.y > 0.5) ? 1.0 : ((N.y < -0.5) ? 0.44 : (abs(N.x) > 0.5 ? 0.76 : 0.65));

  // Pure time-only firelight breathing (ZERO spatial wave -> no bent rings)
  float flicker = 1.0 + 0.020 * sin(uTime * 6.0) + 0.012 * sin(uTime * 13.1 + 1.1);

  // Silky-smooth continuous per-pixel occluded dynamic lighting (ZERO 1x1 block stepping!)
  // Uses dynReach purely as wall-visibility + detour gate; radial intensity is 100% continuous dEuclid
  float dynIntensity = 0.0;
  vec3 heldDynLight = vec3(0.0);
  float dynStrength = uHeldLightPosStrength.w;
  if (dynStrength > 0.001 && dynReach > 0.0005) {
    vec3 toLight = uHeldLightPosStrength.xyz - vWorldPos;
    float dEuclid = length(toLight);
    float maxRange = dynStrength * 14.5;

    float dPath = max(0.0, (dynStrength - dynReach) * 15.0);
    // Generous 2.35-block margin prevents voxel-grid stepping on open terrain while preserving wall corner falloff
    float detour = max(0.0, dPath - dEuclid - 2.35);
    float dEff = dEuclid + detour * 0.90;

    float occGate = smoothstep(0.0, 0.06, dynReach);
    float u = clamp(1.0 - dEff / maxRange, 0.0, 1.0);
    float smoothRadial = (u * u * (3.0 - 2.0 * u)) / (1.0 + 0.052 * dEff * dEff);

    vec3 L = toLight / max(dEuclid, 0.001);
    float ndl = clamp(dot(N, L) * 0.52 + 0.48, 0.25, 1.0);

    dynIntensity = smoothRadial * occGate * ndl * flicker * (0.48 + 0.52 * dynStrength);
    vec3 dynWarmNear = vec3(1.00, 0.74, 0.36);
    vec3 dynWarmFar = vec3(0.95, 0.36, 0.08);
    vec3 dynCol = mix(dynWarmFar, dynWarmNear, clamp(dynIntensity * 1.35, 0.0, 1.0));
    heldDynLight = dynCol * dynIntensity * 2.45;
  }

  int debugMode = int(uDebugMode + 0.5);
  if (debugMode == 1) {
    vec3 skyVis = mix(vec3(0.01, 0.02, 0.04), vec3(0.45, 0.82, 1.0), pow(skyNorm, 1.4));
    gl_FragColor = vec4(skyVis, 1.0);
    return;
  } else if (debugMode == 2) {
    float visBlk = clamp(max(blockNorm, pow(clamp(dynIntensity, 0.0, 1.0), 0.55)), 0.0, 1.0);
    vec3 blkVis = mix(vec3(0.01, 0.01, 0.02), vec3(1.0, 0.68, 0.22), pow(visBlk, 1.25));
    gl_FragColor = vec4(blkVis, 1.0);
    return;
  } else if (debugMode == 3) {
    gl_FragColor = vec4(vec3(mix(0.14, 1.0, aoNorm)), 1.0);
    return;
  }

  vec3 sunDir = normalize(uSunDir);
  vec3 moonDir = normalize(vec3(-sunDir.x, max(-sunDir.y, 0.20), -sunDir.z));
  float sunVisibility = smoothstep(0.84, 0.99, skyNorm);
  float sunHorizFade = smoothstep(-0.05, 0.14, sunDir.y);

  // Zero-anchored exponential lightfield attenuation
  float skyFalloff = lightCurve(skyNorm, 0.76);
  float blockFalloff = lightCurve(blockNorm, 0.74) * mix(1.0, flicker, step(0.01, blockNorm));

  vec3 warmNear = vec3(1.00, 0.72, 0.34);
  vec3 warmFar = vec3(0.95, 0.32, 0.07);
  vec3 blockColor = mix(warmFar, warmNear, clamp(blockFalloff * 1.35, 0.0, 1.0));
  vec3 localBlockLight = blockColor * blockFalloff * 2.55 * mix(0.84, 1.0, faceShade);
  vec3 totalEmissiveLight = localBlockLight + heldDynLight;

  // =========================================================================
  // TRANSLUCENT OPTICAL WATER (Smooth Connected Depth, Fresnel Sky, Snell Window)
  // =========================================================================
  if (blockId == 9) {
    float skyAccess = smoothstep(0.02, 0.28, skyNorm);
    float rippleScale = mix(0.32, 1.0, uSkyIntensity);
    // Gentle organic animated wave normal (calmer at night -> zero lines!)
    float w1 = sin(vWorldPos.x * 1.45 + vWorldPos.z * 0.95 + uTime * 2.05);
    float w2 = cos(vWorldPos.x * 2.45 - vWorldPos.z * 1.95 - uTime * 2.45);
    float w3 = sin(vWorldPos.x * 0.72 - vWorldPos.z * 1.60 + uTime * 1.45);
    vec3 rawWaveN = (N.y > 0.5)
      ? normalize(vec3(-(w1 * 0.030 + w2 * 0.018 + w3 * 0.024) * 0.55, 1.0, -(w1 * 0.024 - w2 * 0.018 - w3 * 0.022) * 0.55))
      : N;
    vec3 waveN = normalize(mix(N, rawWaveN, rippleScale));

    float cosTheta = clamp(dot(waveN, V), 0.05, 1.0);
    float fresnel = 0.02 + 0.98 * pow(1.0 - cosTheta, 5.0);

    // Underwater Snell's Window ONLY on true open-sky water ceiling (isSubmerged > 0.5)
    if (uCameraUnderwater > 0.5 && N.y < -0.5 && isSubmerged > 0.5) {
      float eyeDepth = max(uWaterDepth, 0.0);
      float depthDark = exp(-eyeDepth * 0.105);
      float surfaceClarity = exp(-viewDist * 0.135);
      vec3 surfaceTrans = exp(-viewDist * vec3(0.24, 0.11, 0.075));

      float snellWindow = smoothstep(0.56, 0.74, cosTheta);
      vec3 refrSky = evalSkyFogColor(normalize(vec3(viewDir.x * 0.75, abs(viewDir.y) + 0.14, viewDir.z * 0.75)), sunDir)
                   * skyAccess * surfaceTrans;
      vec3 tirTint = mix(vec3(0.004, 0.015, 0.034), vec3(0.032, 0.205, 0.305), uSkyIntensity)
                   * mix(0.14, 1.0, depthDark) * skyAccess;
      vec3 meniscusCol = mix(tirTint, refrSky * 0.92 + vec3(0.04, 0.14, 0.18) * uSkyIntensity * skyAccess * surfaceClarity, snellWindow);
      meniscusCol += totalEmissiveLight * 0.18;
      float alpha = mix(0.82, mix(0.92, 0.22, surfaceClarity), snellWindow);

      float uwDensity = mix(0.026, 0.068, clamp(eyeDepth / 24.0, 0.0, 1.0));
      vec3 uwDay = vec3(0.042, 0.265, 0.365);
      vec3 uwDusk = vec3(0.048, 0.125, 0.215);
      vec3 uwNight = vec3(0.005, 0.018, 0.040);
      vec3 uwBase = mix(uwNight, mix(uwDay, uwDusk, uWarmGlow * 0.62), uSkyIntensity) * mix(0.14, 1.0, depthDark);
      float uwCave = smoothstep(0.0, 0.18, skyNorm);
      vec3 uwFogCol = mix(vec3(0.001, 0.003, 0.007), uwBase, uwCave);
      float uwFog = clamp(1.0 - exp(-viewDist * uwDensity), 0.0, 0.96);
      vec3 distAbsorb = exp(-viewDist * vec3(0.032, 0.012, 0.006));
      meniscusCol *= mix(vec3(1.0), distAbsorb, 0.55);
      meniscusCol = mix(meniscusCol, uwFogCol, uwFog);
      alpha = mix(alpha, 0.96, uwFog * 0.72);

      vec3 mapped = pow(acesToneMap(meniscusCol * 1.05), vec3(1.0 / 2.2));
      gl_FragColor = vec4(mapped, alpha);
      return;
    }

    // Smoothly interpolated bilinear water depth (0..12m) modulated by real water.png
    vec3 waterTex = pow(max(sampleAtlasTile(12, vUv + vec2(uTime * 0.04, uTime * 0.03)).rgb, vec3(0.001)), vec3(2.15));
    float colDepth = clamp(waterDepth, 0.0, 12.0);
    float shallowT = smoothstep(0.0, 4.5, colDepth);
    float deepT = smoothstep(3.5, 11.5, colDepth);
    vec3 shallowCol = mix(vec3(0.09, 0.44, 0.50), waterTex * 1.15, 0.45);
    vec3 midCol = mix(vec3(0.032, 0.22, 0.38), waterTex * 0.65, 0.35);
    vec3 deepCol = vec3(0.015, 0.105, 0.225);
    vec3 waterTint = mix(mix(shallowCol, midCol, shallowT), deepCol, deepT);

    float dayScale = max(0.04, uSkyIntensity * 0.96 + uMoonIntensity * 0.12);
    vec3 waterBody = waterTint * (skyFalloff * dayScale + vec3(0.010, 0.032, 0.052) * uSkyIntensity * skyAccess);
    waterBody += totalEmissiveLight * 0.20;

    // Sky reflection gated cleanly by skyAccess (zero cave bright spots!)
    vec3 reflDir = reflect(-V, waveN);
    reflDir.y = max(reflDir.y, 0.02);
    vec3 skyRefl = evalSkyFogColor(reflDir, sunDir) * skyAccess * (0.18 + 0.82 * skyFalloff);

    vec3 Hsun = normalize(sunDir + V);
    float sunGlint = pow(max(dot(waveN, Hsun), 0.0), 115.0) * sunHorizFade * uSkyIntensity * sunVisibility;
    vec3 Hmoon = normalize(moonDir + V);
    float moonGlint = pow(max(dot(waveN, Hmoon), 0.0), 82.0) * uMoonIntensity * sunVisibility;
    vec3 glitter = uSunColor * sunGlint * 1.85 + vec3(0.42, 0.62, 0.96) * moonGlint * 0.36;

    float opticalPath = max(colDepth, 0.35) / mix(0.48, 1.0, cosTheta);
    float bodyAlpha = (N.y > 0.5) ? (1.0 - exp(-opticalPath * 0.19)) : 0.54;
    float fresnelWeight = mix(0.55, 0.86, uSkyIntensity);

    vec3 col = mix(waterBody, skyRefl, fresnel * fresnelWeight) + glitter;
    float alpha = clamp(mix(bodyAlpha, 0.92, fresnel * 0.72), 0.16, 0.93);

    // Distance fog on water
    float fogStrength = clamp(uFogStrength, 0.0, 1.0);
    float atmoFog = (fogStrength > 0.001)
      ? clamp(1.0 - exp(-pow(viewDist * uFogDensity * fogStrength, 1.42)), 0.0, 0.85)
      : 0.0;
    float borderFog = 0.0;
    if (uBorderFogParams.z > 0.5) {
      float borderStart = uBorderFogParams.x;
      float borderEnd = max(uBorderFogParams.y, borderStart + 6.0);
      float bt = clamp((horizDist - borderStart) / (borderEnd - borderStart), 0.0, 1.0);
      borderFog = bt * bt * (3.0 - 2.0 * bt);
    }
    float fogFactor = clamp(max(atmoFog, borderFog), 0.0, 1.0);
    if (fogFactor > 0.0005) {
      col = mix(col, evalSkyFogColor(viewDir, sunDir), fogFactor);
      alpha = mix(alpha, 1.0, borderFog);
    }

    vec3 mapped = pow(acesToneMap(col * 1.05), vec3(1.0 / 2.2));
    gl_FragColor = vec4(mapped, alpha);
    return;
  }

  // =========================================================================
  // TRANSLUCENT TEXTURED GLASS BLOCK (id == 14, assets/texture/block/glass.png)
  // =========================================================================
  if (blockId == 14) {
    float cosTheta = clamp(abs(dot(N, V)), 0.05, 1.0);
    float fresnel = 0.06 + 0.94 * pow(1.0 - cosTheta, 4.0);
    vec3 reflDir = reflect(-V, N);
    vec3 skyRefl = evalSkyFogColor(reflDir, sunDir) * (0.25 + 0.75 * skyFalloff);
    vec3 Hsun = normalize(sunDir + V);
    float sunSpec = pow(max(dot(N, Hsun), 0.0), 96.0) * sunHorizFade * uSkyIntensity * sunVisibility;

    vec3 glassIrrad = vec3(0.06 + 0.65 * skyFalloff * uSkyIntensity) + totalEmissiveLight * 0.55;
    vec3 texLinear = albedo * (glassIrrad + vec3(0.05) * uSkyIntensity);
    float texOpacity = texSample.a;

    vec3 col = mix(texLinear, skyRefl * 0.85, fresnel * (1.0 - texOpacity * 0.55)) + uSunColor * sunSpec * 1.45;
    float alpha = clamp(max(texOpacity, 0.16 + fresnel * 0.34), 0.16, 0.90);

    vec3 mapped = pow(acesToneMap(col * 1.05), vec3(1.0 / 2.2));
    gl_FragColor = vec4(mapped, alpha);
    return;
  }

  // =========================================================================
  // OPAQUE SOLID VOXEL SHADING (+ Underwater Caustics & Beer-Lambert Depth)
  // =========================================================================
  float sunNdotL = max(dot(N, sunDir), 0.0);
  vec3 directSun = uSunColor * sunNdotL * sunHorizFade * sunVisibility * uSkyIntensity * 1.42;

  float moonNdotL = max(dot(N, moonDir), 0.0);
  vec3 directMoon = vec3(0.18, 0.30, 0.58) * moonNdotL * sunVisibility * uMoonIntensity * 0.16;

  vec3 skyFillColor = mix(uSkyAmbient * 0.38, uSkyAmbient, N.y * 0.5 + 0.5);
  vec3 skyFill = skyFillColor * skyFalloff * uSkyIntensity * faceShade * 0.68;
  vec3 starlightOpen = vec3(0.0045, 0.0072, 0.0155) * skyFalloff * uMoonIntensity * faceShade;

  // Specular shimmer on alpine snow
  vec3 specular = vec3(0.0);
  if (uQualityFlags.y > 0.5 && specStrength > 0.01 && sunVisibility > 0.05) {
    vec3 Hsun = normalize(sunDir + V);
    float sunSpec = pow(max(dot(N, Hsun), 0.0), 42.0) * sunHorizFade * uSkyIntensity;
    specular = uSunColor * sunSpec * 0.55 * specStrength * sunVisibility;
  }

  float aoAmbient = mix(0.14, 1.0, aoNorm);
  float aoEmissive = mix(0.48, 1.0, aoNorm);
  vec3 deepCaveFloor = vec3(0.00018, 0.00024, 0.00042) * faceShade * aoAmbient;

  vec3 totalIrradiance =
    deepCaveFloor +
    (skyFill + starlightOpen + directSun + directMoon) * aoAmbient +
    totalEmissiveLight * aoEmissive;

  // Submerged riverbed/seabed optical absorption + drifting sunlight caustics
  if (isSubmerged > 0.5) {
    float depthM = max(waterDepth, 0.5);
    vec3 depthAtten = exp(-vec3(0.115, 0.044, 0.022) * depthM);
    float caustic = evalCaustics(vWorldPos.xz + vWorldPos.y * 0.2, uTime);
    float causticMask = max(N.y, 0.15) * sunHorizFade * uSkyIntensity * skyFalloff * exp(-depthM * 0.14);
    totalIrradiance = totalIrradiance * mix(vec3(1.0), depthAtten, 0.68)
                    + vec3(0.012, 0.045, 0.068) * uSkyIntensity * skyFalloff * aoAmbient
                    + vec3(0.32, 0.62, 0.74) * caustic * causticMask * 0.80;
  }

  vec3 litColor = albedo * totalIrradiance + specular;

  if (emissiveMask > 0.01) {
    litColor = mix(litColor, srgbAlbedo * (1.85 * flicker), emissiveMask);
  }

  // Depth-Dependent Volumetric Underwater Visibility Fog vs Above-Water Atmospheric + Border Fog
  if (uCameraUnderwater > 0.5) {
    float eyeDepth = max(uWaterDepth, 0.0);
    float depthDark = exp(-eyeDepth * 0.105);
    float uwDensity = mix(0.026, 0.068, clamp(eyeDepth / 24.0, 0.0, 1.0));
    float uwFog = clamp(1.0 - exp(-viewDist * uwDensity), 0.0, 0.96);

    vec3 uwDay = vec3(0.042, 0.265, 0.365);
    vec3 uwDusk = vec3(0.048, 0.125, 0.215);
    vec3 uwNight = vec3(0.005, 0.018, 0.040);
    vec3 uwBase = mix(uwNight, mix(uwDay, uwDusk, uWarmGlow * 0.62), uSkyIntensity) * mix(0.14, 1.0, depthDark);
    float uwCave = smoothstep(0.0, 0.18, skyNorm);
    vec3 uwColor = mix(vec3(0.001, 0.003, 0.007), uwBase, uwCave);

    vec3 distAbsorb = exp(-viewDist * vec3(0.032, 0.012, 0.006));
    litColor *= mix(vec3(1.0), distAbsorb, 0.55);
    litColor = mix(litColor, uwColor, uwFog);
  } else {
    float fogStrength = clamp(uFogStrength, 0.0, 1.0);
    float atmoFog = (fogStrength > 0.001)
      ? clamp(1.0 - exp(-pow(viewDist * uFogDensity * fogStrength, 1.42)), 0.0, 0.85)
      : 0.0;

    float borderFog = 0.0;
    if (uBorderFogParams.z > 0.5) {
      float borderStart = uBorderFogParams.x;
      float borderEnd = max(uBorderFogParams.y, borderStart + 6.0);
      float bt = clamp((horizDist - borderStart) / (borderEnd - borderStart), 0.0, 1.0);
      borderFog = bt * bt * (3.0 - 2.0 * bt);
    }

    float fogFactor = clamp(max(atmoFog, borderFog), 0.0, 1.0);
    if (fogFactor > 0.0005) {
      vec3 skyFog = evalSkyFogColor(viewDir, sunDir);
      float caveBlend = 1.0 - smoothstep(0.04, 0.42, skyNorm);
      vec3 activeFog = mix(skyFog, vec3(0.00015, 0.00020, 0.00035), caveBlend);
      litColor = mix(litColor, activeFog, fogFactor);
    }
  }

  vec3 mapped = acesToneMap(litColor * 1.05);
  mapped = pow(mapped, vec3(1.0 / 2.2));
  gl_FragColor = vec4(mapped, 1.0);
}
`;

// Rotation-only view matrix in sky vertex shader -> ZERO Sun/Moon/Star jitter when moving!
export const SKY_VERTEX_SHADER = /* glsl */ `
varying vec3 vLocalDir;

void main() {
  vLocalDir = normalize(position);
  mat4 rotView = mat4(mat3(viewMatrix));
  vec4 clipPos = projectionMatrix * rotView * vec4(position, 1.0);
  gl_Position = clipPos.xyww;
}
`;

export const SKY_FRAGMENT_SHADER = /* glsl */ `
precision highp float;

uniform float uTime;
uniform vec3 uSunDir;
uniform float uSkyIntensity;
uniform float uMoonIntensity;
uniform vec3 uSkyZenith;
uniform vec3 uSkyHorizon;
uniform vec3 uMieGlow;
uniform float uWarmGlow;
uniform float uCameraUnderwater;
uniform float uWaterDepth;

varying vec3 vLocalDir;

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
  vec3 rayDir = normalize(vLocalDir);
  vec3 sunDir = normalize(uSunDir);
  vec3 moonDir = -sunDir;

  // 1. Crystal-Clear Seamless 360-Degree Atmospheric Sky Sphere (ZERO clouds, ZERO horizon seam!)
  vec3 zenith = pow(max(uSkyZenith, vec3(0.001)), vec3(2.05));
  vec3 horizon = pow(max(uSkyHorizon, vec3(0.001)), vec3(2.05));
  vec3 nadir = mix(zenith * 0.34, horizon * 0.22, 0.40);
  vec3 mieCol = pow(max(uMieGlow, vec3(0.001)), vec3(1.85));

  float y = clamp(rayDir.y, -1.0, 1.0);
  bool cameraUnderwater = uCameraUnderwater > 0.5;
  float eyeDepth = cameraUnderwater ? max(uWaterDepth, 0.0) : 0.0;

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
    : mix(horizon, nadir, pow(-y, 0.60));

  float zenithDeep = pow(max(y, 0.0), 1.35) * uSkyIntensity * (1.0 - uWarmGlow * 0.5);
  sky = mix(sky, zenith * 0.84, zenithDeep * 0.35);

  float belt = exp(-pow(abs(y) * 3.8, 1.55));
  float sunCos = max(dot(rayDir, sunDir), 0.0);
  float antiSunCos = max(dot(rayDir, -sunDir), 0.0);
  sky += mieCol * belt * (0.24 + 0.82 * pow(sunCos, 2.4)) * uWarmGlow;
  sky += vec3(0.32, 0.16, 0.36) * belt * pow(antiSunCos, 2.0) * uWarmGlow * 0.38;

  // 2. Full 360-Degree Sun Orbit with Limb Darkening & Golden Mie Aureole
  float sunOrbitVis = 0.58 + 0.42 * smoothstep(-0.85, 0.20, sunDir.y);
  float sunCorona = pow(sunCos, 26.0) * 0.38 + pow(sunCos, 160.0) * 0.85;
  sky += mieCol * sunCorona * sunOrbitVis * (0.62 + 0.68 * uWarmGlow) * uwGlowClarity;

  float sunDot = dot(rayDir, sunDir);
  float sunDisc = smoothstep(0.9985, 0.9994, sunDot);
  float limb = smoothstep(0.9985, 0.9999, sunDot);
  vec3 sunBody = mix(vec3(2.4, 1.45, 0.72), vec3(2.8, 2.45, 1.95), limb);
  sky += sunBody * sunDisc * sunOrbitVis * uwDiscClarity;

  // 3. Full 360-Degree Moon Orbit with Silver Halo & Lunar Maria
  float moonDot = dot(rayDir, moonDir);
  float moonOrbitVis = 0.50 + 0.50 * smoothstep(-0.85, 0.20, moonDir.y);
  float moonHalo = pow(max(moonDot, 0.0), 40.0) * 0.16 + pow(max(moonDot, 0.0), 220.0) * 0.30;
  sky += vec3(0.32, 0.48, 0.85) * moonHalo * moonOrbitVis * (0.30 + 0.70 * uMoonIntensity) * uwGlowClarity;

  float moonDisc = smoothstep(0.9987, 0.9994, moonDot);
  if (moonDisc > 0.001 && uwDiscClarity > 0.001) {
    float maria = 0.80 + 0.20 * noise2(rayDir.xz * 48.0 + rayDir.y * 20.0);
    sky += vec3(0.94, 0.97, 1.15) * moonDisc * maria * moonOrbitVis * (0.42 + 0.98 * uMoonIntensity) * uwDiscClarity;
  }

  // 4. Crystal-Clear Night Starfield + Milky Way Stardust (extinguished smoothly underwater!)
  if (uMoonIntensity > 0.04 && uwStarClarity > 0.001) {
    float hazeAtten = 0.32 + 0.68 * smoothstep(0.0, 0.28, abs(y));
    float starMask = hazeAtten * uMoonIntensity * uwStarClarity;

    float galacticPlane = abs(dot(rayDir, normalize(vec3(0.45, 0.62, -0.64))));
    float mwBand = exp(-pow(galacticPlane * 5.2, 2.0));
    float mwDust = starDustFbm(rayDir.xz * 3.6 / (abs(y) + 0.58));
    sky += vec3(0.020, 0.030, 0.064) * mwBand * mwDust * starMask;

    vec3 starCell = floor(rayDir * 260.0);
    float sr = hash31(starCell);
    if (sr > 0.9942) {
      float twinkle = 0.64 + 0.36 * sin(uTime * (2.0 + fract(sr * 71.0) * 3.2) + sr * 628.0);
      float tintRand = fract(sr * 137.0);
      vec3 starCol = mix(vec3(1.0, 0.86, 0.68), vec3(0.76, 0.89, 1.0), tintRand);
      float bright = smoothstep(0.9942, 0.9998, sr);
      sky += starCol * (0.32 + 1.25 * bright) * twinkle * starMask;
    }
  }

  // 5. Depth-Dependent Underwater Spectral Absorption & Volumetric Abyss Veil
  if (cameraUnderwater) {
    float pathToSurface = eyeDepth / max(y, 0.14);
    float depthDark = exp(-eyeDepth * 0.105);

    vec3 uwDay = vec3(0.042, 0.265, 0.365);
    vec3 uwDusk = vec3(0.048, 0.125, 0.215);
    vec3 uwNight = vec3(0.005, 0.018, 0.040);
    vec3 uwAbyss = mix(uwNight, mix(uwDay, uwDusk, uWarmGlow * 0.62), uSkyIntensity) * mix(0.14, 1.0, depthDark);

    vec3 surfaceTrans = exp(-pathToSurface * vec3(0.24, 0.11, 0.075));
    sky *= surfaceTrans;

    float tirMask = 1.0 - smoothstep(0.50, 0.72, y);
    float volVeil = clamp(1.0 - exp(-pathToSurface * 0.165), 0.0, 0.985);
    sky = mix(sky, uwAbyss, max(tirMask, volVeil));
  }

  vec3 x = sky;
  vec3 mapped = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
  gl_FragColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}
`;

const BLOCK_FALLBACK_HEX = [
  '#5ca840', '#709444', '#805c3e', '#825e40', '#8a9098', '#464c56',
  '#98724a', '#765434', '#368638', '#ffc648', '#fff0a0', '#f8d474',
  '#2480c6', '#dece9a', '#e85c1a', '#5ee8f6', '#f0f6fc', '#cae8f8',
];

export function createBlockTextureAtlas(
  urls: string[],
  onLoaded?: () => void
): THREE.CanvasTexture {
  const tileSize = 16;
  const count = 18;
  const canvas = document.createElement('canvas');
  canvas.width = tileSize * count;
  canvas.height = tileSize;
  const ctx = canvas.getContext('2d');

  if (ctx) {
    ctx.imageSmoothingEnabled = false;
    for (let i = 0; i < count; i++) {
      ctx.fillStyle = BLOCK_FALLBACK_HEX[i] ?? '#888888';
      ctx.fillRect(i * tileSize, 0, tileSize, tileSize);
    }
  }

  const tex = new THREE.CanvasTexture(canvas);
  tex.magFilter = THREE.NearestFilter;
  tex.minFilter = THREE.NearestFilter;
  tex.generateMipmaps = false;
  tex.flipY = false;
  tex.wrapS = THREE.ClampToEdgeWrapping;
  tex.wrapT = THREE.ClampToEdgeWrapping;
  tex.needsUpdate = true;

  let loadedCount = 0;
  urls.slice(0, count).forEach((url, idx) => {
    if (!url) return;
    const img = new Image();
    img.decoding = 'async';
    img.onload = () => {
      if (ctx) {
        ctx.clearRect(idx * tileSize, 0, tileSize, tileSize);
        ctx.drawImage(img, idx * tileSize, 0, tileSize, tileSize);
        tex.needsUpdate = true;
      }
      loadedCount++;
      if (loadedCount === urls.length || loadedCount % 4 === 0) {
        onLoaded?.();
      }
    };
    img.src = url;
  });

  return tex;
}

export function createVoxelShaderMaterial(
  translucent = false,
  blockAtlas: THREE.Texture | null = null
): THREE.ShaderMaterial {
  return new THREE.ShaderMaterial({
    vertexShader: VOXEL_VERTEX_SHADER,
    fragmentShader: VOXEL_FRAGMENT_SHADER,
    uniforms: {
      uTime: { value: 0 },
      uSunDir: { value: new THREE.Vector3(0.25, 0.92, 0.28) },
      uSkyIntensity: { value: 1.0 },
      uSunColor: { value: new THREE.Vector3(1.0, 0.96, 0.88) },
      uMoonIntensity: { value: 0.0 },
      uSkyAmbient: { value: new THREE.Vector3(0.45, 0.62, 0.86) },
      uSkyZenith: { value: new THREE.Vector3(0.16, 0.44, 0.82) },
      uHorizonFog: { value: new THREE.Vector3(0.6, 0.76, 0.91) },
      uMieGlow: { value: new THREE.Vector3(1.0, 0.72, 0.42) },
      uWarmGlow: { value: 0.2 },
      uFogDensity: { value: 0.012 },
      uFogStrength: { value: 0.22 },
      uBorderFogParams: { value: new THREE.Vector3(38.0, 52.0, 1.0) },
      uQualityFlags: { value: new THREE.Vector2(1.0, 1.0) },
      uCaveDark: { value: new THREE.Vector3(0.008, 0.011, 0.018) },
      uDebugMode: { value: 0 },
      uHeldLightPosStrength: { value: new THREE.Vector4(0, 0, 0, 0) },
      uCameraUnderwater: { value: 0.0 },
      uWaterDepth: { value: 0.0 },
      uBlockAtlas: { value: blockAtlas },
    },
    side: THREE.FrontSide,
    transparent: translucent,
    depthWrite: !translucent,
  });
}

export function createSkyShaderMaterial(): THREE.ShaderMaterial {
  return new THREE.ShaderMaterial({
    vertexShader: SKY_VERTEX_SHADER,
    fragmentShader: SKY_FRAGMENT_SHADER,
    uniforms: {
      uTime: { value: 0 },
      uSunDir: { value: new THREE.Vector3(0.25, 0.92, 0.28) },
      uSkyIntensity: { value: 1.0 },
      uMoonIntensity: { value: 0.0 },
      uSkyZenith: { value: new THREE.Vector3(0.16, 0.44, 0.82) },
      uSkyHorizon: { value: new THREE.Vector3(0.62, 0.78, 0.93) },
      uMieGlow: { value: new THREE.Vector3(1.0, 0.72, 0.42) },
      uWarmGlow: { value: 0.2 },
      uCameraUnderwater: { value: 0.0 },
      uWaterDepth: { value: 0.0 },
    },
    side: THREE.BackSide,
    depthWrite: false,
    depthFunc: THREE.LessEqualDepth,
  });
}
