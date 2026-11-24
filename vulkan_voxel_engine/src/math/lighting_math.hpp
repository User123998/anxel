#pragma once

#include <array>
#include <cstdint>

namespace umbra::math {

// =============================================================================
// Photometric Voxel Lighting & Area-Darkness Math (Zero-Anchored Curves)
// Guarantees level 0 == EXACT 0.0000 (no phantom glow in pitch-black caves!)
// =============================================================================

inline constexpr float evalZeroAnchoredFalloff(int level, float base) noexcept {
    if (level <= 0) return 0.0f;
    if (level >= 15) return 1.0f;
    float p = 1.0f;
    int steps = 15 - level;
    for (int i = 0; i < steps; ++i) p *= base;
    float floorVal = 1.0f;
    for (int i = 0; i < 15; ++i) floorVal *= base;
    return (p - floorVal) / (1.0f - floorVal);
}

inline constexpr std::array<float, 16> buildFalloffLut(float base) noexcept {
    std::array<float, 16> lut{};
    for (int i = 0; i < 16; ++i) {
        lut[static_cast<size_t>(i)] = evalZeroAnchoredFalloff(i, base);
    }
    return lut;
}

// Compile-time LUTs mirroring shaders/voxel.frag (zero runtime pow() cost on CPU!)
inline constexpr std::array<float, 16> SKY_FALLOFF_LUT   = buildFalloffLut(0.76f);
inline constexpr std::array<float, 16> BLOCK_FALLOFF_LUT = buildFalloffLut(0.74f);
inline constexpr std::array<float, 4>  AO_CURVE_LUT      = {0.14f, 0.45f, 0.74f, 1.00f};

static_assert(SKY_FALLOFF_LUT[0] == 0.0f && SKY_FALLOFF_LUT[15] == 1.0f,
              "Sky light falloff must be zero-anchored");
static_assert(BLOCK_FALLOFF_LUT[0] == 0.0f && BLOCK_FALLOFF_LUT[15] == 1.0f,
              "Block light falloff must be zero-anchored");

// Fast 4-bit nibble packing for per-voxel sky/block channels
inline constexpr uint8_t unpackSky(uint8_t packed) noexcept {
    return static_cast<uint8_t>((packed >> 4u) & 0x0Fu);
}

inline constexpr uint8_t unpackBlock(uint8_t packed) noexcept {
    return static_cast<uint8_t>(packed & 0x0Fu);
}

inline constexpr uint8_t packSky(uint8_t packed, uint8_t sky) noexcept {
    return static_cast<uint8_t>((packed & 0x0Fu) | ((sky & 0x0Fu) << 4u));
}

inline constexpr uint8_t packBlock(uint8_t packed, uint8_t blk) noexcept {
    return static_cast<uint8_t>((packed & 0xF0u) | (blk & 0x0Fu));
}

// Fixed-point 1/16-block Euclidean BFS scale for smooth occluded dynamic lighting (stored in uint8_t 0..240)
inline constexpr uint8_t DYN_LIGHT_SCALE = 16; // 1.000 block orthogonal
inline constexpr uint8_t DYN_ORTHO_STEP  = 16; // 1.000 block orthogonal step
inline constexpr uint8_t DYN_DIAG_STEP   = 23; // 1.4375 ≈ sqrt(2) open-corner 2D diagonal step
inline constexpr float   DYN_LIGHT_MAX_F = 240.0f; // 15 * 16

// Strict corner ambient occlusion (returns 0..3)
inline constexpr int computeVertexAO(bool side1, bool side2, bool corner) noexcept {
    return (side1 && side2) ? 0 : (3 - (static_cast<int>(side1) + static_cast<int>(side2) + static_cast<int>(corner)));
}

// Diagonal quad flip test eliminating BOTH AO anisotropy AND diagonal light-gradient bent lines
inline constexpr bool shouldFlipQuad(float ao0, float ao1, float ao2, float ao3,
                                     float l0 = 0.0f, float l1 = 0.0f,
                                     float l2 = 0.0f, float l3 = 0.0f) noexcept {
    float aoDiff = (ao0 + ao2) - (ao1 + ao3);
    if (aoDiff < -0.001f) return true;
    if (aoDiff >  0.001f) return false;
    float d02 = (l0 > l2) ? (l0 - l2) : (l2 - l0);
    float d13 = (l1 > l3) ? (l1 - l3) : (l3 - l1);
    return d02 > d13;
}

struct AreaLightProfile {
    const char* label;
    float       perceivedLight; // 0.0 = pitch black sealed cave, 1.0 = full noon daylight
    float       darknessFactor; // 1.0 - perceivedLight
};

// Classifies the player's current physical lighting zone based on sky access,
// local/held emitter light, and solar elevation.
inline constexpr AreaLightProfile evaluateAreaDarkness(uint8_t skyLevel,
                                                       uint8_t effectiveBlockLevel,
                                                       float skyIntensity) noexcept {
    float skyE = SKY_FALLOFF_LUT[skyLevel & 0x0Fu] * skyIntensity;
    float blkE = BLOCK_FALLOFF_LUT[effectiveBlockLevel & 0x0Fu];
    float total = (skyE > blkE) ? skyE : blkE;
    if (total > 1.0f) total = 1.0f;

    const char* zone = "SUNLIT";
    if (skyLevel == 0) {
        if (effectiveBlockLevel == 0)       zone = "PITCH DARK";
        else if (effectiveBlockLevel <= 5)  zone = "DIM CAVE";
        else                                zone = "LIT CAVERN";
    } else if (skyLevel <= 6) {
        if (effectiveBlockLevel >= 8)       zone = "LIT CAVERN";
        else                                zone = "CAVE SHADOW";
    } else if (skyLevel < 13) {
        zone = (skyIntensity < 0.12f) ? "DARK SHADE" : "OVERHANG";
    } else {
        if (skyIntensity < 0.08f)           zone = "STARLIT NIGHT";
        else if (skyIntensity < 0.35f)      zone = "TWILIGHT";
        else if (skyIntensity < 0.72f)      zone = "GOLDEN HOUR";
        else                                zone = "OPEN SKY";
    }

    return AreaLightProfile{zone, total, 1.0f - total};
}

} // namespace umbra::math
