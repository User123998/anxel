#pragma once

#include "math/chunk_math.hpp"
#include "math/lighting_math.hpp"
#include "math/vec_mat.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vulkan/vulkan.h>

namespace umbra {

// Infinite chunk-streamed voxel world dimensions (sourced from math/chunk_math.hpp)
inline constexpr int CHUNK_SIZE          = math::CHUNK_SIZE;
inline constexpr int CHUNK_HEIGHT        = math::CHUNK_HEIGHT; // 128 blocks tall
inline constexpr int SEA_LEVEL           = 42;                 // Deep oceans below, tall mountains up to Y=115
inline constexpr int MIN_RENDER_DISTANCE = 4;
inline constexpr int MAX_RENDER_DISTANCE = 50;                 // Up to 50 chunks (800-block radius!)
inline constexpr int DEFAULT_RENDER_DIST = 12;                 // Crisp default with binary greedy meshing

// Hardware-filtered 3D dynamic light volume dimensions (32x32x32 = 32 KB R8_UNORM GPU texture!)
// Zero chunk remeshing when walking with a held light source -> silky 120-240+ FPS!
inline constexpr int DYN_TEX_SIZE   = 32;
inline constexpr int DYN_TEX_HALF   = DYN_TEX_SIZE / 2;
inline constexpr int DYN_TEX_VOLUME = DYN_TEX_SIZE * DYN_TEX_SIZE * DYN_TEX_SIZE;

// Single-cycle power-of-two chunk coordinate mapping (via arithmetic shift & bitmask)
inline constexpr int floorDiv(int a, int /*b*/) noexcept {
    return math::chunkFromWorld(a);
}

inline constexpr int floorMod(int a, int /*b*/) noexcept {
    return math::localFromWorld(a);
}

struct ChunkCoord {
    int cx = 0;
    int cz = 0;

    bool operator==(const ChunkCoord& o) const noexcept {
        return cx == o.cx && cz == o.cz;
    }
};

struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const noexcept {
        return math::hashChunkCoord(c.cx, c.cz);
    }
};

enum class GraphicsQuality : int {
    Low    = 0,
    Medium = 1,
    High   = 2,
    Ultra  = 3
};

struct EngineSettings {
    int             renderDistance = DEFAULT_RENDER_DIST; // 4..50 chunks
    GraphicsQuality quality        = GraphicsQuality::High;
    float           fogStrength    = 0.22f;               // 0.0..1.0 (crisp clear air by default!)
    bool            borderFog      = true;                // Seamless chunk-edge horizon fade
    bool            smoothLighting = true;                // Smooth vertex lighting & crevice AO
    bool            greedyMeshing  = true;                // Binary greedy quad merging (70-85% fewer tris!)
    bool            waterSpecular  = true;                // Sun/Moon water & snow shimmer
    float           fov            = 70.0f;               // 60..105 degrees

    void applyQualityPreset(GraphicsQuality q) noexcept {
        quality = q;
        switch (q) {
            case GraphicsQuality::Low:
                smoothLighting = false;
                greedyMeshing  = true;
                waterSpecular  = false;
                break;
            case GraphicsQuality::Medium:
                smoothLighting = true;
                greedyMeshing  = true;
                waterSpecular  = false;
                break;
            case GraphicsQuality::High:
                smoothLighting = true;
                greedyMeshing  = true;
                waterSpecular  = true;
                break;
            case GraphicsQuality::Ultra:
                smoothLighting = true;
                greedyMeshing  = true;
                waterSpecular  = true;
                break;
        }
    }

    int chunksGenPerFrame() const noexcept {
        switch (quality) {
            case GraphicsQuality::Low:    return 3;
            case GraphicsQuality::Medium: return 5;
            case GraphicsQuality::High:   return 8;
            case GraphicsQuality::Ultra:  return 14;
        }
        return 8;
    }

    size_t chunksMeshPerFrame() const noexcept {
        switch (quality) {
            case GraphicsQuality::Low:    return 4;
            case GraphicsQuality::Medium: return 6;
            case GraphicsQuality::High:   return 10;
            case GraphicsQuality::Ultra:  return 18;
        }
        return 10;
    }

    static const char* qualityLabel(GraphicsQuality q) noexcept {
        switch (q) {
            case GraphicsQuality::Low:    return "LOW";
            case GraphicsQuality::Medium: return "MEDIUM";
            case GraphicsQuality::High:   return "HIGH";
            case GraphicsQuality::Ultra:  return "ULTRA";
        }
        return "HIGH";
    }
};

enum class BlockType : uint8_t {
    Air       = 0,
    Grass     = 1,
    Dirt      = 2,
    Stone     = 3,
    DeepSlate = 4,
    Timber    = 5,
    Leaves    = 6,
    Torch     = 7,
    Glowstone = 8,
    Water     = 9,
    Sand      = 10,
    Magma     = 11,
    Crystal   = 12,
    Snow      = 13,
    Glass     = 14
};

inline constexpr bool isTransparent(BlockType b) noexcept {
    return b == BlockType::Air || b == BlockType::Leaves ||
           b == BlockType::Torch || b == BlockType::Water ||
           b == BlockType::Glass;
}

inline constexpr bool isTranslucentMesh(BlockType b) noexcept {
    return b == BlockType::Water || b == BlockType::Glass;
}

inline constexpr bool isOpaqueForAO(BlockType b) noexcept {
    return b != BlockType::Air && b != BlockType::Torch &&
           b != BlockType::Water && b != BlockType::Glass;
}

inline constexpr bool isSolidCollider(BlockType b) noexcept {
    return b != BlockType::Air && b != BlockType::Torch && b != BlockType::Water;
}

inline constexpr uint8_t lightAttenuation(BlockType b) noexcept {
    switch (b) {
        case BlockType::Air:    return 1;
        case BlockType::Torch:  return 1;
        case BlockType::Glass:  return 1;
        case BlockType::Water:  return 1;
        case BlockType::Leaves: return 2;
        default:                return 15;
    }
}

inline constexpr uint8_t blockEmission(BlockType b) noexcept {
    switch (b) {
        case BlockType::Glowstone: return 15;
        case BlockType::Torch:     return 14;
        case BlockType::Magma:     return 13;
        case BlockType::Crystal:   return 10;
        default:                   return 0;
    }
}

inline constexpr uint8_t getSkyLight(uint8_t packed) noexcept {
    return math::unpackSky(packed);
}
inline constexpr uint8_t getBlockLight(uint8_t packed) noexcept {
    return math::unpackBlock(packed);
}
inline constexpr uint8_t setSkyLight(uint8_t packed, uint8_t sky) noexcept {
    return math::packSky(packed, sky);
}
inline constexpr uint8_t setBlockLight(uint8_t packed, uint8_t blk) noexcept {
    return math::packBlock(packed, blk);
}

// Compact 52-byte voxel vertex (dynamic light in 3D GPU texture, smooth water depth in lightAO.w!)
struct VoxelVertex {
    vec3     position;
    vec3     normal;
    vec2     uv;
    vec4     lightAO;   // x = skyLight [0..1], y = static blockLight [0..1], z = AO [0..1], w = smoothWaterDepth [0..1]
    uint32_t packedMat; // bits 0..7 = blockId, bits 8..10 = faceDir, bit 11 = submerged/waterMeniscus

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription desc{};
        desc.binding   = 0;
        desc.stride    = sizeof(VoxelVertex);
        desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return desc;
    }

    static std::array<VkVertexInputAttributeDescription, 5> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 5> attrs{};

        attrs[0].binding  = 0;
        attrs[0].location = 0;
        attrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset   = offsetof(VoxelVertex, position);

        attrs[1].binding  = 0;
        attrs[1].location = 1;
        attrs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[1].offset   = offsetof(VoxelVertex, normal);

        attrs[2].binding  = 0;
        attrs[2].location = 2;
        attrs[2].format   = VK_FORMAT_R32G32_SFLOAT;
        attrs[2].offset   = offsetof(VoxelVertex, uv);

        attrs[3].binding  = 0;
        attrs[3].location = 3;
        attrs[3].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[3].offset   = offsetof(VoxelVertex, lightAO);

        attrs[4].binding  = 0;
        attrs[4].location = 4;
        attrs[4].format   = VK_FORMAT_R32_UINT;
        attrs[4].offset   = offsetof(VoxelVertex, packedMat);

        return attrs;
    }
};

// Matches std140 layout of FrameUBO in shaders/voxel.vert, voxel.frag & sky.frag
struct alignas(16) FrameUBO {
    mat4 viewProj{};
    mat4 invViewProj{};               // Precomputed on CPU -> zero per-pixel inverse() in sky.frag!
    vec4 cameraPosTime{0.0f};         // xyz = camera world pos, w = elapsed seconds
    vec4 sunDirIntensity{0.0f, 1.0f, 0.0f, 1.0f};
    vec4 sunColorMoon{1.0f, 0.96f, 0.88f, 0.0f};
    vec4 skyZenithTimeOfDay{0.16f, 0.44f, 0.82f, 0.25f};
    vec4 skyHorizonCloudLit{0.62f, 0.78f, 0.93f, 0.0f};
    vec4 skyAmbientFogDensity{0.44f, 0.62f, 0.88f, 0.0020f};
    vec4 mieGlowDebugMode{1.0f, 0.72f, 0.42f, 0.0f};
    vec4 heldLightPosStrength{0.0f, 0.0f, 0.0f, 0.0f};
    vec4 dynOriginFlags{0.0f, 0.0f, 0.0f, 7.0f};        // xyz = 3D dynTex corner, w = packed graphics flags
    vec4 renderFogParams{0.0f, 134.0f, 186.0f, 0.22f};   // x = eyeWaterDepth (meters, 0 above water), y = borderStart, z = borderEnd, w = fogStrength
};

static_assert(sizeof(FrameUBO) == 128 + 10 * 16, "FrameUBO must match GLSL std140 layout");

struct RaycastHit {
    ivec3 blockPos{0};
    ivec3 faceNormal{0};
    float distance = 0.0f;
};

struct TimeOfDayState {
    vec3  sunDir{0.0f, 1.0f, 0.0f};
    vec3  sunColor{1.0f, 0.95f, 0.86f};
    vec3  skyZenith{0.16f, 0.44f, 0.82f};
    vec3  skyHorizon{0.62f, 0.78f, 0.93f};
    vec3  skyAmbient{0.44f, 0.62f, 0.88f};
    vec3  mieGlow{1.0f, 0.70f, 0.36f};
    float skyIntensity  = 1.0f;
    float moonIntensity = 0.0f;
    float warmGlow      = 0.0f;
    const char* phaseName = "Noon";
};

struct DebugMetrics {
    static constexpr int HISTORY_LEN = 60;

    std::array<float, HISTORY_LEN> frameHistoryMs{};
    int      historyHead       = 0;
    float    frameMs           = 16.6f;
    float    avgFrameMs        = 16.6f;
    float    minFrameMs        = 16.6f;
    float    maxFrameMs        = 16.6f;
    float    streamMs          = 0.0f;
    float    meshMs            = 0.0f;
    float    dynLightMs        = 0.0f;
    float    renderMs          = 0.0f;
    uint32_t opaqueTriangles   = 0;
    uint32_t transTriangles    = 0;
    uint32_t visibleChunks     = 0;
    uint32_t loadedChunks      = 0;
    uint32_t dirtyQueueCount   = 0;
    vec3     cameraPos{0.0f};
    float    yaw               = 0.0f;
    float    pitch             = 0.0f;
    int      chunkX            = 0;
    int      chunkZ            = 0;
    int      skyLight          = 15;
    int      blockLight        = 0;
    int      dynLight          = 0;
    int      waterDepth        = 0;
    bool     eyeUnderwater     = false;
    bool     inWater           = false;
    bool     targetValid       = false;
    ivec3    targetPos{0};
    const char* targetName     = "NONE";
    const char* biomeName      = "PLAINS";
    const char* facingName     = "SOUTH (+Z)";

    void pushFrameMs(float ms) noexcept {
        frameMs = ms;
        frameHistoryMs[static_cast<size_t>(historyHead)] = ms;
        historyHead = (historyHead + 1) % HISTORY_LEN;

        float sum = 0.0f;
        float lo  = 9999.0f;
        float hi  = 0.0f;
        int count = 0;
        for (float v : frameHistoryMs) {
            if (v <= 0.001f) continue;
            sum += v;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            ++count;
        }
        if (count > 0) {
            avgFrameMs = sum / static_cast<float>(count);
            minFrameMs = lo;
            maxFrameMs = hi;
        }
    }
};

} // namespace umbra
