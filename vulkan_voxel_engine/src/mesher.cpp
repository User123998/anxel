#include "mesher.hpp"
#include "math/chunk_math.hpp"
#include "math/lighting_math.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>

namespace umbra {

namespace {

struct FaceDef {
    ivec3 normal;
    vec3  corners[4];
    ivec3 tangentU;
    ivec3 tangentV;
};

// Face indices: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
// Every face strictly obeys: c1 = c0 + tangentU, c3 = c0 + tangentV, c2 = c0 + tangentU + tangentV (CCW)
constexpr FaceDef FACES[6] = {
    {{ 1, 0, 0}, {{1,0,1}, {1,0,0}, {1,1,0}, {1,1,1}}, { 0,0,-1}, {0,1, 0}},
    {{-1, 0, 0}, {{0,0,0}, {0,0,1}, {0,1,1}, {0,1,0}}, { 0,0, 1}, {0,1, 0}},
    {{ 0, 1, 0}, {{0,1,1}, {1,1,1}, {1,1,0}, {0,1,0}}, { 1,0, 0}, {0,0,-1}},
    {{ 0,-1, 0}, {{0,0,0}, {1,0,0}, {1,0,1}, {0,0,1}}, { 1,0, 0}, {0,0, 1}},
    {{ 0, 0, 1}, {{0,0,1}, {1,0,1}, {1,1,1}, {0,1,1}}, { 1,0, 0}, {0,1, 0}},
    {{ 0, 0,-1}, {{1,0,0}, {0,0,0}, {0,1,0}, {1,1,0}}, {-1,0, 0}, {0,1, 0}},
};

constexpr int CORNER_SU[4] = { -1,  1,  1, -1 };
constexpr int CORNER_SV[4] = { -1, -1,  1,  1 };

// =============================================================================
// 3x3 Cached Chunk Neighborhood: ZERO hash-map lookups during the entire chunk mesh!
// =============================================================================
struct ChunkNeighborhood {
    const Chunk* grid[3][3] = {};
    int baseX = 0;
    int baseZ = 0;

    void init(const VoxelWorld& world, ChunkCoord center) {
        baseX = center.cx * CHUNK_SIZE;
        baseZ = center.cz * CHUNK_SIZE;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                grid[dz + 1][dx + 1] = world.getChunk({center.cx + dx, center.cz + dz});
            }
        }
    }

    inline const Chunk* resolveChunk(int lx, int lz, int& outLx, int& outLz) const noexcept {
        int gx = 1 + (lx >> math::CHUNK_SHIFT);
        int gz = 1 + (lz >> math::CHUNK_SHIFT);
        outLx  = lx & math::CHUNK_MASK;
        outLz  = lz & math::CHUNK_MASK;
        if (static_cast<unsigned>(gx) > 2u || static_cast<unsigned>(gz) > 2u) return nullptr;
        return grid[gz][gx];
    }

    inline BlockType getBlockLocal(int lx, int y, int lz) const noexcept {
        if (static_cast<unsigned>(y) >= static_cast<unsigned>(CHUNK_HEIGHT)) return BlockType::Air;
        int cx, cz;
        const Chunk* c = resolveChunk(lx, lz, cx, cz);
        return c ? c->blocks[math::chunkVoxelIndex(cx, y, cz)] : BlockType::Air;
    }

    inline void getLightLocal(int lx, int y, int lz,
                              uint8_t& outSky, uint8_t& outStatBlk) const noexcept {
        if (y >= CHUNK_HEIGHT) { outSky = 15; outStatBlk = 0; return; }
        if (y < 0)             { outSky = 0;  outStatBlk = 0; return; }
        int cx, cz;
        const Chunk* c = resolveChunk(lx, lz, cx, cz);
        if (!c) { outSky = 15; outStatBlk = 0; return; }
        uint8_t packed = c->light[math::chunkVoxelIndex(cx, y, cz)];
        outSky     = math::unpackSky(packed);
        outStatBlk = math::unpackBlock(packed);
    }
};

inline vec3 sampleVertexLightAO(const ChunkNeighborhood& nb,
                                int airLx, int airY, int airLz,
                                const ivec3& uAxis,
                                const ivec3& vAxis,
                                int su, int sv,
                                bool smoothLighting,
                                uint16_t& outQuant) noexcept {
    if (!smoothLighting) {
        uint8_t sk = 0, bl = 0;
        nb.getLightLocal(airLx, airY, airLz, sk, bl);
        outQuant = static_cast<uint16_t>((static_cast<uint16_t>(sk) << 6) |
                                         (static_cast<uint16_t>(bl) << 2) | 3u);
        return vec3(static_cast<float>(sk) * (1.0f / 15.0f),
                    static_cast<float>(bl) * (1.0f / 15.0f),
                    1.0f);
    }

    int s1x = airLx + uAxis.x * su, s1y = airY + uAxis.y * su, s1z = airLz + uAxis.z * su;
    int s2x = airLx + vAxis.x * sv, s2y = airY + vAxis.y * sv, s2z = airLz + vAxis.z * sv;
    int cnx = s1x + vAxis.x * sv,   cny = s1y + vAxis.y * sv,  cnz = s1z + vAxis.z * sv;

    bool s1 = isOpaqueForAO(nb.getBlockLocal(s1x, s1y, s1z));
    bool s2 = isOpaqueForAO(nb.getBlockLocal(s2x, s2y, s2z));
    bool cn = isOpaqueForAO(nb.getBlockLocal(cnx, cny, cnz));

    int aoLevel = math::computeVertexAO(s1, s2, cn);

    uint8_t cSky = 0, cBlk = 0;
    nb.getLightLocal(airLx, airY, airLz, cSky, cBlk);
    int skySum = static_cast<int>(cSky);
    int blkSum = static_cast<int>(cBlk);
    int count  = 1;

    auto accumulate = [&](int lx, int y, int lz, bool blocked) noexcept {
        if (!blocked) {
            uint8_t sk = 0, bl = 0;
            nb.getLightLocal(lx, y, lz, sk, bl);
            skySum += static_cast<int>(sk);
            blkSum += static_cast<int>(bl);
            count  += 1;
        }
    };

    accumulate(s1x, s1y, s1z, s1);
    accumulate(s2x, s2y, s2z, s2);
    if (!(s1 && s2)) {
        accumulate(cnx, cny, cnz, cn);
    }

    // Exact quantized corner signature for zero-artifact smooth greedy merging:
    // skyAvg [0..60] (6 bits) | blkAvg [0..60] (6 bits) | ao [0..3] (2 bits) = 14 bits
    uint16_t skyQ = static_cast<uint16_t>((skySum * 4) / count);
    uint16_t blkQ = static_cast<uint16_t>((blkSum * 4) / count);
    outQuant = static_cast<uint16_t>((skyQ << 8) | (blkQ << 2) | static_cast<uint16_t>(aoLevel));

    float inv15 = 1.0f / (static_cast<float>(count) * 15.0f);
    return vec3(
        static_cast<float>(skySum) * inv15,
        static_cast<float>(blkSum) * inv15,
        static_cast<float>(aoLevel) * (1.0f / 3.0f)
    );
}

// Measures exact water column depth below (lx, y, lz) up to 12 blocks (0 if column is not water)
inline float measureWaterColDepthBelow(const ChunkNeighborhood& nb, int lx, int y, int lz) noexcept {
    if (nb.getBlockLocal(lx, y, lz) != BlockType::Water) return 0.0f;
    int d = 1;
    for (int yy = y - 1; yy >= 0 && d < 12; --yy) {
        if (nb.getBlockLocal(lx, yy, lz) == BlockType::Water) ++d;
        else break;
    }
    return static_cast<float>(d);
}

// Measures water column depth above a submerged face air-neighbor up to 12 blocks
inline float measureWaterColDepthAbove(const ChunkNeighborhood& nb, int lx, int y, int lz) noexcept {
    if (nb.getBlockLocal(lx, y, lz) != BlockType::Water) return 0.0f;
    int d = 1;
    for (int yy = y + 1; yy < CHUNK_HEIGHT && d < 12; ++yy) {
        if (nb.getBlockLocal(lx, yy, lz) == BlockType::Water) ++d;
        else break;
    }
    return static_cast<float>(d);
}

// 4-column bilinear water depth at a horizontal vertex corner -> 100% seamless connected water!
inline float sampleSmoothWaterDepthNorm(const ChunkNeighborhood& nb,
                                        int lx, int y, int lz,
                                        const ivec3& uAxis,
                                        const ivec3& vAxis,
                                        int su, int sv) noexcept {
    float d0 = measureWaterColDepthBelow(nb, lx, y, lz);
    float d1 = measureWaterColDepthBelow(nb, lx + uAxis.x * su, y, lz + uAxis.z * su);
    float d2 = measureWaterColDepthBelow(nb, lx + vAxis.x * sv, y, lz + vAxis.z * sv);
    float d3 = measureWaterColDepthBelow(nb, lx + uAxis.x * su + vAxis.x * sv,
                                         y,  lz + uAxis.z * su + vAxis.z * sv);
    float avg = (d0 + d1 + d2 + d3) * 0.25f;
    return std::clamp(avg * (1.0f / 12.0f), 0.0f, 1.0f);
}

void emitTorchGeometry(MeshData& out, int wx, int y, int wz, uint8_t skyVal) {
    const float minX = static_cast<float>(wx) + 0.40f, maxX = static_cast<float>(wx) + 0.60f;
    const float minY = static_cast<float>(y)  + 0.00f, maxY = static_cast<float>(y)  + 0.72f;
    const float minZ = static_cast<float>(wz) + 0.40f, maxZ = static_cast<float>(wz) + 0.60f;

    vec4 lightAO(static_cast<float>(skyVal) * (1.0f / 15.0f), 1.0f, 1.0f, 0.0f);
    constexpr vec2 uv[4] = {{0,0}, {1,0}, {1,1}, {0,1}};

    for (uint32_t f = 0; f < 6; ++f) {
        const auto& fd = FACES[f];
        uint32_t base = static_cast<uint32_t>(out.vertices.size());
        uint32_t packedMat = static_cast<uint32_t>(BlockType::Torch) | (f << 8u);

        for (int c = 0; c < 4; ++c) {
            vec3 pos(
                fd.corners[c].x > 0.5f ? maxX : minX,
                fd.corners[c].y > 0.5f ? maxY : minY,
                fd.corners[c].z > 0.5f ? maxZ : minZ
            );
            out.vertices.push_back(VoxelVertex{pos, vec3(fd.normal), uv[c], lightAO, packedMat});
        }
        out.indices.push_back(base + 0);
        out.indices.push_back(base + 1);
        out.indices.push_back(base + 2);
        out.indices.push_back(base + 0);
        out.indices.push_back(base + 2);
        out.indices.push_back(base + 3);
    }
}

struct GreedyCell {
    uint64_t  mergeKey      = 0;     // Exact block + corner-light + smoothDepth + submerged + flip signature
    BlockType block         = BlockType::Air;
    vec4      lightAO[4]{};          // x=sky, y=blk, z=AO, w=smoothWaterDepthNorm [0..1]
    bool      submerged     = false; // True when adjacent face neighbor is Water
    bool      waterTopEdge  = false; // True for Water side faces whose top edge touches open air
    bool      flip          = false;
    bool      constU        = false; // q0==q1 && q3==q2 -> bit-exact horizontal U-strip merge!
    bool      constV        = false; // q0==q3 && q1==q2 -> bit-exact vertical V-strip merge!
    bool      active        = false;
};

inline void emitMergedQuad(MeshData& out,
                           uint32_t f,
                           const vec3& basePos,
                           int w,
                           int h,
                           const GreedyCell& cell) {
    const auto& fd = FACES[f];
    const bool isTranslucent = isTranslucentMesh(cell.block);
    auto& verts = isTranslucent ? out.transVertices : out.vertices;
    auto& inds  = isTranslucent ? out.transIndices  : out.indices;

    // Recess open water top surfaces naturally by 0.12 blocks below riverbanks & beaches
    vec3 origin = basePos;
    const bool isWaterTop = (cell.block == BlockType::Water && f == 2u);
    if (isWaterTop) {
        origin.y -= 0.12f;
    }

    const vec3 uStep = vec3(fd.tangentU) * static_cast<float>(w);
    const vec3 vStep = vec3(fd.tangentV) * static_cast<float>(h);

    vec3 pos[4] = {
        origin,
        origin + uStep,
        origin + uStep + vStep,
        origin + vStep
    };
    // Align exposed Water side faces cleanly with the recessed 0.88 top water surface
    if (cell.block == BlockType::Water && f != 2u && f != 3u && cell.waterTopEdge) {
        pos[2].y -= 0.12f;
        pos[3].y -= 0.12f;
    }
    const vec2 uv[4] = {
        {0.0f, 0.0f},
        {static_cast<float>(w), 0.0f},
        {static_cast<float>(w), static_cast<float>(h)},
        {0.0f, static_cast<float>(h)}
    };

    const uint32_t packedMat =
        static_cast<uint32_t>(cell.block) |
        (f << 8u) |
        (cell.submerged ? (1u << 11u) : 0u);

    const uint32_t base = static_cast<uint32_t>(verts.size());
    const vec3 norm(fd.normal);

    for (int c = 0; c < 4; ++c) {
        verts.push_back(VoxelVertex{pos[c], norm, uv[c], cell.lightAO[c], packedMat});
    }

    if (!cell.flip) {
        inds.push_back(base + 0);
        inds.push_back(base + 1);
        inds.push_back(base + 2);
        inds.push_back(base + 0);
        inds.push_back(base + 2);
        inds.push_back(base + 3);
    } else {
        inds.push_back(base + 1);
        inds.push_back(base + 2);
        inds.push_back(base + 3);
        inds.push_back(base + 1);
        inds.push_back(base + 3);
        inds.push_back(base + 0);
    }

    // For open-sky water surfaces, also emit the underside meniscus face (-Y normal, reversed winding)
    // marked with bit 11 (submerged=1) so underwater Snell's Window ONLY renders on true surface ceilings!
    if (isWaterTop) {
        const uint32_t underBase = static_cast<uint32_t>(verts.size());
        const uint32_t underMat =
            static_cast<uint32_t>(BlockType::Water) |
            (3u << 8u) | // faceDir = 3 (-Y underside meniscus)
            (1u << 11u); // bit 11 = verified open-surface Snell meniscus
        const vec3 downNorm(0.0f, -1.0f, 0.0f);
        for (int c = 0; c < 4; ++c) {
            verts.push_back(VoxelVertex{pos[c], downNorm, uv[c], cell.lightAO[c], underMat});
        }
        inds.push_back(underBase + 0);
        inds.push_back(underBase + 2);
        inds.push_back(underBase + 1);
        inds.push_back(underBase + 0);
        inds.push_back(underBase + 3);
        inds.push_back(underBase + 2);
    }
}

inline GreedyCell buildFaceCell(const ChunkNeighborhood& nb,
                                int lx, int y, int lz,
                                BlockType b,
                                uint32_t f,
                                bool smoothLighting) noexcept {
    const auto& fd = FACES[f];
    const int nlx = lx + fd.normal.x;
    const int ny  = y  + fd.normal.y;
    const int nlz = lz + fd.normal.z;

    GreedyCell cell{};
    cell.active = true;
    cell.block  = b;

    BlockType adj = nb.getBlockLocal(nlx, ny, nlz);
    const bool isWaterTop  = (b == BlockType::Water && f == 2u);
    const bool isWaterSide = (b == BlockType::Water && f != 2u && f != 3u);
    cell.submerged    = (b != BlockType::Water && adj == BlockType::Water);
    cell.waterTopEdge = isWaterSide && (nb.getBlockLocal(lx, y + 1, lz) != BlockType::Water);

    float fallbackDepthNorm = 0.0f;
    if (isWaterSide) {
        fallbackDepthNorm = std::clamp(measureWaterColDepthBelow(nb, lx, y, lz) * (1.0f / 12.0f), 0.0f, 1.0f);
    } else if (cell.submerged) {
        fallbackDepthNorm = std::clamp(measureWaterColDepthAbove(nb, nlx, ny, nlz) * (1.0f / 12.0f), 0.0f, 1.0f);
    }

    const uint8_t selfEmit = blockEmission(b);
    const float selfEmitNorm = static_cast<float>(selfEmit) * (1.0f / 15.0f);
    const uint16_t selfBlkQ  = static_cast<uint16_t>(selfEmit * 4u);

    uint32_t q[4]{};
    float luma[4]{};
    for (int c = 0; c < 4; ++c) {
        uint16_t baseQ = 0;
        vec3 baseLight = sampleVertexLightAO(
            nb, nlx, ny, nlz, fd.tangentU, fd.tangentV, CORNER_SU[c], CORNER_SV[c],
            smoothLighting, baseQ);

        if (selfEmit > 0 && selfEmitNorm > baseLight.y) {
            baseLight.y = selfEmitNorm;
            uint16_t skyQ = baseQ >> 8;
            uint16_t aoQ  = baseQ & 3u;
            baseQ = static_cast<uint16_t>((skyQ << 8) | (selfBlkQ << 2) | aoQ);
        }

        // Smooth 4-column bilinear water depth per vertex -> zero blocky depth contours!
        float depthNorm = fallbackDepthNorm;
        if (isWaterTop) {
            depthNorm = sampleSmoothWaterDepthNorm(
                nb, lx, y, lz, fd.tangentU, fd.tangentV, CORNER_SU[c], CORNER_SV[c]);
            baseLight.z = 1.0f; // Water surface never darkens with block AO creases
            baseQ = static_cast<uint16_t>((baseQ & ~3u) | 3u);
        } else if (b == BlockType::Glass) {
            baseLight.z = 1.0f;
            baseQ = static_cast<uint16_t>((baseQ & ~3u) | 3u);
        }

        cell.lightAO[c] = vec4(baseLight.x, baseLight.y, baseLight.z, depthNorm);
        uint32_t depthQ = static_cast<uint32_t>(std::clamp(static_cast<int>(depthNorm * 15.0f + 0.5f), 0, 15));
        q[c] = (static_cast<uint32_t>(baseQ) << 4u) | depthQ; // 18-bit exact corner signature
        luma[c] = baseLight.x + baseLight.y;
    }

    cell.flip = math::shouldFlipQuad(
        cell.lightAO[0].z, cell.lightAO[1].z, cell.lightAO[2].z, cell.lightAO[3].z,
        luma[0], luma[1], luma[2], luma[3]);

    // Bit-exact smooth-lighting & smooth-water greedy merge eligibility:
    // - Open flat oceans share identical q[0..3] -> merge into giant 16x16 quads!
    // - Coastal slopes vary smoothly across vertices -> crisp 100% seamless bilinear interpolation!
    cell.constU = (q[0] == q[1] && q[3] == q[2]);
    cell.constV = (q[0] == q[3] && q[1] == q[2]) && !cell.waterTopEdge;

    uint64_t flags = (cell.submerged ? 1ull : 0ull) | (cell.waterTopEdge ? 2ull : 0ull) | (cell.flip ? 4ull : 0ull);
    cell.mergeKey = (static_cast<uint64_t>(b) << 44) |
                    (static_cast<uint64_t>(q[0] & 0x3FFFFu) << 26) |
                    (static_cast<uint64_t>(q[2] & 0x3FFFFu) << 8) |
                    (flags << 1) | 1ull;
    return cell;
}

} // namespace

MeshData VoxelMesher::buildChunkMesh(const VoxelWorld& world,
                                     ChunkCoord coord,
                                     bool smoothLighting,
                                     bool greedyMeshing) {
    MeshData out;
    ChunkNeighborhood nb{};
    nb.init(world, coord);

    const Chunk* chunk = nb.grid[1][1];
    if (!chunk) return out;

    out.vertices.reserve(4096);
    out.indices.reserve(6144);
    out.transVertices.reserve(512);
    out.transIndices.reserve(768);

    const int baseX = nb.baseX;
    const int baseZ = nb.baseZ;

    // =========================================================================
    // PHASE 1: Binary Occupancy Bitmask Packing (2 x uint64_t per column = 128 Y levels)
    // Also emits Torch micro-geometry in the same single cache-linear pass!
    // =========================================================================
    uint64_t opaqueLo[CHUNK_SIZE][CHUNK_SIZE]{};
    uint64_t opaqueHi[CHUNK_SIZE][CHUNK_SIZE]{};
    uint64_t solidNonAirLo[CHUNK_SIZE][CHUNK_SIZE]{}; // Opaque + Leaves + Water (excludes Air & Torch)
    uint64_t solidNonAirHi[CHUNK_SIZE][CHUNK_SIZE]{};

    int minY = CHUNK_HEIGHT;
    int maxY = -1;

    for (int y = 0; y < CHUNK_HEIGHT; ++y) {
        const uint64_t bit = 1ull << (y & 63);
        const bool hiHalf  = (y >= 64);
        bool rowAny = false;

        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                size_t idx = math::chunkVoxelIndex(lx, y, lz);
                BlockType b = chunk->blocks[idx];
                if (b == BlockType::Air) continue;
                rowAny = true;

                if (b == BlockType::Torch) {
                    emitTorchGeometry(out, baseX + lx, y, baseZ + lz, math::unpackSky(chunk->light[idx]));
                    continue;
                }

                if (!hiHalf) {
                    solidNonAirLo[lz][lx] |= bit;
                    if (!isTransparent(b)) opaqueLo[lz][lx] |= bit;
                } else {
                    solidNonAirHi[lz][lx] |= bit;
                    if (!isTransparent(b)) opaqueHi[lz][lx] |= bit;
                }
            }
        }
        if (rowAny) {
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
        }
    }

    if (maxY < minY) return out;

    // Helper: tests whether voxel at (lx,y,lz) exposes face f (connected culling for Water & Glass!)
    auto isFaceExposed = [&](int lx, int y, int lz, BlockType b, uint32_t f) noexcept -> bool {
        const auto& fd = FACES[f];
        int ny = y + fd.normal.y;
        if (ny < 0) return false;
        BlockType neighbor = nb.getBlockLocal(lx + fd.normal.x, ny, lz + fd.normal.z);
        if (b == BlockType::Water) {
            // Never emit bottom (-Y) faces of water into underwater cavities;
            // open-sky underside meniscus is emitted cleanly by isWaterTop (f == 2u) only!
            if (f == 3u) return false;
            // Top surface (+Y): render whenever above is open air, torch, or overhanging leaves
            if (f == 2u) {
                return (neighbor == BlockType::Air ||
                        neighbor == BlockType::Torch ||
                        neighbor == BlockType::Leaves);
            }
            // Side faces (+X,-X,+Z,-Z): ONLY render against open Air (never create hollow blue boxes around torches/leaves/glass!)
            return (neighbor == BlockType::Air);
        }
        if (b == BlockType::Glass) {
            return (neighbor != BlockType::Glass) && isTransparent(neighbor);
        }
        return isTransparent(neighbor);
    };

    // =========================================================================
    // PHASE 2A: Binary-Culled + Smooth Greedy +Y (f=2) and -Y (f=3) Slices
    // Uses 64-bit bitwise culling (col & ~(col >> 1)) + std::countr_zero!
    // =========================================================================
    {
        uint64_t expPosYLo[CHUNK_SIZE][CHUNK_SIZE]{};
        uint64_t expPosYHi[CHUNK_SIZE][CHUNK_SIZE]{};
        uint64_t expNegYLo[CHUNK_SIZE][CHUNK_SIZE]{};
        uint64_t expNegYHi[CHUNK_SIZE][CHUNK_SIZE]{};

        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                uint64_t sLo = solidNonAirLo[lz][lx];
                uint64_t sHi = solidNonAirHi[lz][lx];
                uint64_t oLo = opaqueLo[lz][lx];
                uint64_t oHi = opaqueHi[lz][lx];

                // +Y exposed if block present AND block above is NOT opaque
                uint64_t aboveOpaqueLo = (oLo >> 1) | ((oHi & 1ull) << 63);
                uint64_t aboveOpaqueHi = (oHi >> 1);
                expPosYLo[lz][lx] = sLo & ~aboveOpaqueLo;
                expPosYHi[lz][lx] = sHi & ~aboveOpaqueHi;

                // -Y exposed if block present AND block below is NOT opaque (y=0 never exposed downward)
                uint64_t belowOpaqueLo = (oLo << 1) | 1ull; // bit 0 masked out (y == 0)
                uint64_t belowOpaqueHi = (oHi << 1) | (oLo >> 63);
                expNegYLo[lz][lx] = sLo & ~belowOpaqueLo;
                expNegYHi[lz][lx] = sHi & ~belowOpaqueHi;
            }
        }

        // Sweep Y slices [minY..maxY] for +Y (f=2) and -Y (f=3)
        std::array<GreedyCell, CHUNK_SIZE * CHUNK_SIZE> slice{};

        for (uint32_t f : {2u, 3u}) {
            const auto& fd = FACES[f];
            for (int y = minY; y <= maxY; ++y) {
                const uint64_t yBit = 1ull << (y & 63);
                const bool hiHalf   = (y >= 64);

                bool anyActive = false;
                for (int v = 0; v < CHUNK_SIZE; ++v) {
                    int lz = (f == 2u) ? (CHUNK_SIZE - 1 - v) : v; // Matches tangentV=(0,0,-1) for +Y, (0,0,+1) for -Y
                    for (int u = 0; u < CHUNK_SIZE; ++u) {
                        int lx = u; // Matches tangentU=(1,0,0) for both +Y and -Y
                        uint64_t mask = (f == 2u)
                            ? (hiHalf ? expPosYHi[lz][lx] : expPosYLo[lz][lx])
                            : (hiHalf ? expNegYHi[lz][lx] : expNegYLo[lz][lx]);
                        auto& slot = slice[static_cast<size_t>(v * CHUNK_SIZE + u)];
                        slot.active = false;
                        if ((mask & yBit) == 0) continue;

                        BlockType b = chunk->blocks[math::chunkVoxelIndex(lx, y, lz)];
                        if ((b == BlockType::Water || b == BlockType::Glass) && !isFaceExposed(lx, y, lz, b, f)) continue;

                        slot = buildFaceCell(nb, lx, y, lz, b, f, smoothLighting);
                        anyActive = true;
                    }
                }
                if (!anyActive) continue;

                // 2D Smooth Greedy Merge on 16x16 horizontal slice
                for (int v = 0; v < CHUNK_SIZE; ++v) {
                    for (int u = 0; u < CHUNK_SIZE; ) {
                        GreedyCell& start = slice[static_cast<size_t>(v * CHUNK_SIZE + u)];
                        if (!start.active) { ++u; continue; }

                        int w = 1, h = 1;
                        if (greedyMeshing) {
                            if (start.constU) {
                                while (u + w < CHUNK_SIZE) {
                                    const GreedyCell& next = slice[static_cast<size_t>(v * CHUNK_SIZE + u + w)];
                                    if (!next.active || !next.constU || next.mergeKey != start.mergeKey) break;
                                    ++w;
                                }
                            }
                            if (start.constV && (w == 1 || start.constU)) {
                                bool canExpandV = true;
                                while (v + h < CHUNK_SIZE && canExpandV) {
                                    for (int du = 0; du < w; ++du) {
                                        const GreedyCell& c = slice[static_cast<size_t>((v + h) * CHUNK_SIZE + u + du)];
                                        if (!c.active || !c.constV || (w > 1 && !c.constU) || c.mergeKey != start.mergeKey) {
                                            canExpandV = false;
                                            break;
                                        }
                                    }
                                    if (canExpandV) ++h;
                                }
                            }
                        }

                        for (int dv = 0; dv < h; ++dv) {
                            for (int du = 0; du < w; ++du) {
                                slice[static_cast<size_t>((v + dv) * CHUNK_SIZE + u + du)].active = false;
                            }
                        }

                        int lx0 = u;
                        int lz0 = (f == 2u) ? (CHUNK_SIZE - 1 - v) : v;
                        vec3 basePos(
                            static_cast<float>(baseX + lx0) + fd.corners[0].x,
                            static_cast<float>(y)           + fd.corners[0].y,
                            static_cast<float>(baseZ + lz0) + fd.corners[0].z
                        );
                        emitMergedQuad(out, f, basePos, w, h, start);
                        u += w;
                    }
                }
            }
        }
    }

    // =========================================================================
    // PHASE 2B: Binary-Culled + Smooth Greedy Vertical Slices (+X, -X, +Z, -Z)
    // Uses 64-bit bitwise culling per column + std::countr_zero!
    // =========================================================================
    {
        const int ySpan = maxY - minY + 1;
        thread_local std::vector<GreedyCell> vSlice;
        if (vSlice.size() < static_cast<size_t>(CHUNK_SIZE * ySpan)) {
            vSlice.resize(static_cast<size_t>(CHUNK_SIZE * ySpan));
        }

        for (uint32_t f : {0u, 1u, 4u, 5u}) {
            const auto& fd = FACES[f];

            for (int depth = 0; depth < CHUNK_SIZE; ++depth) {
                bool anyActive = false;

                for (int u = 0; u < CHUNK_SIZE; ++u) {
                    int lx = 0, lz = 0;
                    if (f == 0u)      { lx = depth; lz = CHUNK_SIZE - 1 - u; } // +X: tangentU=(0,0,-1)
                    else if (f == 1u) { lx = depth; lz = u; }                  // -X: tangentU=(0,0,+1)
                    else if (f == 4u) { lz = depth; lx = u; }                  // +Z: tangentU=(+1,0,0)
                    else              { lz = depth; lx = CHUNK_SIZE - 1 - u; } // -Z: tangentU=(-1,0,0)

                    // Clear column slots first
                    for (int vy = 0; vy < ySpan; ++vy) {
                        vSlice[static_cast<size_t>(vy * CHUNK_SIZE + u)].active = false;
                    }

                    // Fast 64-bit binary cull against neighbor column in direction fd.normal
                    uint64_t sLo = solidNonAirLo[lz][lx];
                    uint64_t sHi = solidNonAirHi[lz][lx];
                    if ((sLo | sHi) == 0) continue;

                    int nlx = lx + fd.normal.x;
                    int nlz = lz + fd.normal.z;
                    uint64_t nbOpaqueLo = 0, nbOpaqueHi = 0;

                    if (nlx >= 0 && nlx < CHUNK_SIZE && nlz >= 0 && nlz < CHUNK_SIZE) {
                        nbOpaqueLo = opaqueLo[nlz][nlx];
                        nbOpaqueHi = opaqueHi[nlz][nlx];
                    } else {
                        // Neighbor chunk border column: check only set bits of sLo / sHi!
                        uint64_t mLo = sLo;
                        while (mLo) {
                            int y = std::countr_zero(mLo);
                            mLo &= (mLo - 1);
                            if (!isTransparent(nb.getBlockLocal(nlx, y, nlz))) {
                                nbOpaqueLo |= (1ull << y);
                            }
                        }
                        uint64_t mHi = sHi;
                        while (mHi) {
                            int bIdx = std::countr_zero(mHi);
                            mHi &= (mHi - 1);
                            if (!isTransparent(nb.getBlockLocal(nlx, 64 + bIdx, nlz))) {
                                nbOpaqueHi |= (1ull << bIdx);
                            }
                        }
                    }

                    uint64_t expLo = sLo & ~nbOpaqueLo;
                    uint64_t expHi = sHi & ~nbOpaqueHi;

                    auto processMask = [&](uint64_t mask, int yBase) {
                        while (mask) {
                            int bitIdx = std::countr_zero(mask);
                            mask &= (mask - 1);
                            int y = yBase + bitIdx;
                            if (y < minY || y > maxY) continue;
                            BlockType b = chunk->blocks[math::chunkVoxelIndex(lx, y, lz)];
                            if ((b == BlockType::Water || b == BlockType::Glass) && !isFaceExposed(lx, y, lz, b, f)) continue;
                            vSlice[static_cast<size_t>((y - minY) * CHUNK_SIZE + u)] =
                                buildFaceCell(nb, lx, y, lz, b, f, smoothLighting);
                            anyActive = true;
                        }
                    };
                    processMask(expLo, 0);
                    processMask(expHi, 64);
                }

                if (!anyActive) continue;

                // 2D Smooth Greedy Merge on (16 x ySpan) vertical slice
                for (int vy = 0; vy < ySpan; ++vy) {
                    for (int u = 0; u < CHUNK_SIZE; ) {
                        GreedyCell& start = vSlice[static_cast<size_t>(vy * CHUNK_SIZE + u)];
                        if (!start.active) { ++u; continue; }

                        int w = 1, h = 1;
                        if (greedyMeshing) {
                            if (start.constU) {
                                while (u + w < CHUNK_SIZE) {
                                    const GreedyCell& next = vSlice[static_cast<size_t>(vy * CHUNK_SIZE + u + w)];
                                    if (!next.active || !next.constU || next.mergeKey != start.mergeKey) break;
                                    ++w;
                                }
                            }
                            if (start.constV && (w == 1 || start.constU)) {
                                bool canExpandV = true;
                                while (vy + h < ySpan && canExpandV) {
                                    for (int du = 0; du < w; ++du) {
                                        const GreedyCell& c = vSlice[static_cast<size_t>((vy + h) * CHUNK_SIZE + u + du)];
                                        if (!c.active || !c.constV || (w > 1 && !c.constU) || c.mergeKey != start.mergeKey) {
                                            canExpandV = false;
                                            break;
                                        }
                                    }
                                    if (canExpandV) ++h;
                                }
                            }
                        }

                        for (int dv = 0; dv < h; ++dv) {
                            for (int du = 0; du < w; ++du) {
                                vSlice[static_cast<size_t>((vy + dv) * CHUNK_SIZE + u + du)].active = false;
                            }
                        }

                        int lx0 = 0, lz0 = 0;
                        if (f == 0u)      { lx0 = depth; lz0 = CHUNK_SIZE - 1 - u; }
                        else if (f == 1u) { lx0 = depth; lz0 = u; }
                        else if (f == 4u) { lz0 = depth; lx0 = u; }
                        else              { lz0 = depth; lx0 = CHUNK_SIZE - 1 - u; }

                        vec3 basePos(
                            static_cast<float>(baseX + lx0) + fd.corners[0].x,
                            static_cast<float>(minY + vy)   + fd.corners[0].y,
                            static_cast<float>(baseZ + lz0) + fd.corners[0].z
                        );
                        emitMergedQuad(out, f, basePos, w, h, start);
                        u += w;
                    }
                }
            }
        }
    }

    return out;
}

} // namespace umbra
