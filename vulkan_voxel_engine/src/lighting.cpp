#include "lighting.hpp"

#include <algorithm>
#include <cstring>

namespace umbra {

namespace {

constexpr int DX[6] = { 1, -1,  0,  0,  0,  0 };
constexpr int DY[6] = { 0,  0,  1, -1,  0,  0 };
constexpr int DZ[6] = { 0,  0,  0,  0,  1, -1 };

inline bool isLoadedVoxel(const VoxelWorld& world, int x, int y, int z) noexcept {
    if (y < 0 || y >= CHUNK_HEIGHT) return false;
    return world.hasChunk({math::chunkFromWorld(x), math::chunkFromWorld(z)});
}

inline constexpr size_t dynIdx(int lx, int ly, int lz) noexcept {
    return static_cast<size_t>((lz << 10) | (ly << 5) | lx);
}

} // namespace

void LightEngine::lightNewChunk(VoxelWorld& world, ChunkCoord coord) {
    Chunk* chunk = world.getChunk(coord);
    if (!chunk) return;

    skyAddQ_.clear();
    blkAddQ_.clear();

    const int baseX = coord.cx * CHUNK_SIZE;
    const int baseZ = coord.cz * CHUNK_SIZE;

    // 1. Vertical sky-column projection + local emissive seed pass inside the new chunk
    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            int wx = baseX + lx;
            int wz = baseZ + lz;
            int currentSky = 15;
            int waterDepthSteps = 0;

            for (int y = CHUNK_HEIGHT - 1; y >= 0; --y) {
                size_t idx = Chunk::localIndex(lx, y, lz);
                BlockType b = chunk->blocks[idx];

                uint8_t emit = blockEmission(b);
                if (emit > 0) {
                    chunk->light[idx] = setBlockLight(chunk->light[idx], emit);
                    blkAddQ_.push({wx, y, wz});
                }

                if (!isTransparent(b)) {
                    currentSky = 0;
                    waterDepthSteps = 0;
                    continue;
                }

                if (b == BlockType::Leaves) {
                    currentSky = std::max(0, currentSky - static_cast<int>(lightAttenuation(b)));
                } else if (b == BlockType::Water && currentSky > 0) {
                    // Natural optical sunlight penetration in open-sky oceans & rivers:
                    // gently attenuates 1 level every 2m and preserves deep-ocean twilight (>= 3)
                    // while sealed roofed caves (currentSky == 0) remain 100% pitch-black!
                    ++waterDepthSteps;
                    if ((waterDepthSteps & 1) == 0 && currentSky > 3) {
                        --currentSky;
                    }
                }

                if (currentSky > 0) {
                    chunk->light[idx] = setSkyLight(chunk->light[idx], static_cast<uint8_t>(currentSky));
                    skyAddQ_.push({wx, y, wz});
                }
            }
        }
    }

    // 2. Pull border light from already-loaded neighboring chunks so seams never appear
    auto seedFromBorder = [&](int nx, int nz) {
        ChunkCoord nc{math::chunkFromWorld(nx), math::chunkFromWorld(nz)};
        const Chunk* nbChunk = world.getChunk(nc);
        if (!nbChunk) return;
        int nlx = math::localFromWorld(nx);
        int nlz = math::localFromWorld(nz);
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            uint8_t packed = nbChunk->light[Chunk::localIndex(nlx, y, nlz)];
            if (getSkyLight(packed) > 1)   skyAddQ_.push({nx, y, nz});
            if (getBlockLight(packed) > 1) blkAddQ_.push({nx, y, nz});
        }
    };

    for (int i = 0; i < CHUNK_SIZE; ++i) {
        seedFromBorder(baseX - 1,          baseZ + i);
        seedFromBorder(baseX + CHUNK_SIZE, baseZ + i);
        seedFromBorder(baseX + i,          baseZ - 1);
        seedFromBorder(baseX + i,          baseZ + CHUNK_SIZE);
    }

    chunk->lit = true;
    chunk->meshDirty = true;

    propagateSkyQueue(world);
    propagateBlockQueue(world);

    // Ensure immediate neighbors refresh their shared border AO/faces
    world.markChunkDirty({coord.cx - 1, coord.cz});
    world.markChunkDirty({coord.cx + 1, coord.cz});
    world.markChunkDirty({coord.cx, coord.cz - 1});
    world.markChunkDirty({coord.cx, coord.cz + 1});
}

void LightEngine::propagateSkyQueue(VoxelWorld& world) {
    while (!skyAddQ_.empty()) {
        LightNode n = skyAddQ_.pop();

        uint8_t srcLight = world.getSky(n.x, n.y, n.z);
        if (srcLight <= 1) continue;

        for (int d = 0; d < 6; ++d) {
            int nx = n.x + DX[d];
            int ny = n.y + DY[d];
            int nz = n.z + DZ[d];
            if (!isLoadedVoxel(world, nx, ny, nz)) continue;

            BlockType nb = world.getBlock(nx, ny, nz);
            if (!isTransparent(nb)) continue;

            bool clearVertical = (nb == BlockType::Air || nb == BlockType::Glass);
            uint8_t cost = lightAttenuation(nb);
            if (d == 3 && srcLight == 15 && clearVertical) {
                cost = 0;
            } else if (d == 3 && nb == BlockType::Water && srcLight >= 4 && ((ny & 1) == 1)) {
                // Match gentle 1-per-2m vertical ocean penetration during live block edits
                cost = 0;
            }
            uint8_t nextLight = (srcLight > cost) ? static_cast<uint8_t>(srcLight - cost) : 0;

            if (nextLight > world.getSky(nx, ny, nz)) {
                world.setSky(nx, ny, nz, nextLight);
                skyAddQ_.push({nx, ny, nz});
            }
        }
    }
}

void LightEngine::propagateBlockQueue(VoxelWorld& world) {
    while (!blkAddQ_.empty()) {
        LightNode n = blkAddQ_.pop();

        uint8_t srcLight = world.getBlockLightVal(n.x, n.y, n.z);
        if (srcLight <= 1) continue;

        for (int d = 0; d < 6; ++d) {
            int nx = n.x + DX[d];
            int ny = n.y + DY[d];
            int nz = n.z + DZ[d];
            if (!isLoadedVoxel(world, nx, ny, nz)) continue;

            BlockType nb = world.getBlock(nx, ny, nz);
            if (!isTransparent(nb)) continue;

            uint8_t cost = lightAttenuation(nb);
            uint8_t nextLight = (srcLight > cost) ? static_cast<uint8_t>(srcLight - cost) : 0;

            if (nextLight > world.getBlockLightVal(nx, ny, nz)) {
                world.setBlockLightVal(nx, ny, nz, nextLight);
                blkAddQ_.push({nx, ny, nz});
            }
        }
    }
}

void LightEngine::processSkyRemoval(VoxelWorld& world) {
    while (!skyRemQ_.empty()) {
        RemovalNode n = skyRemQ_.pop();

        for (int d = 0; d < 6; ++d) {
            int nx = n.x + DX[d];
            int ny = n.y + DY[d];
            int nz = n.z + DZ[d];
            if (!isLoadedVoxel(world, nx, ny, nz)) continue;

            uint8_t neighborVal = world.getSky(nx, ny, nz);
            if (neighborVal == 0) continue;

            bool fedByThis = (neighborVal < n.prevVal) || (d == 3 && n.prevVal == 15 && neighborVal == 15);
            if (fedByThis) {
                world.setSky(nx, ny, nz, 0);
                skyRemQ_.push({nx, ny, nz, neighborVal});
            } else if (neighborVal >= n.prevVal) {
                skyAddQ_.push({nx, ny, nz});
            }
        }
    }
}

void LightEngine::processBlockRemoval(VoxelWorld& world) {
    while (!blkRemQ_.empty()) {
        RemovalNode n = blkRemQ_.pop();

        for (int d = 0; d < 6; ++d) {
            int nx = n.x + DX[d];
            int ny = n.y + DY[d];
            int nz = n.z + DZ[d];
            if (!isLoadedVoxel(world, nx, ny, nz)) continue;

            uint8_t neighborVal = world.getBlockLightVal(nx, ny, nz);
            if (neighborVal == 0) continue;

            if (neighborVal < n.prevVal) {
                world.setBlockLightVal(nx, ny, nz, 0);
                blkRemQ_.push({nx, ny, nz, neighborVal});
            } else {
                blkAddQ_.push({nx, ny, nz});
            }
        }
    }
}

void LightEngine::onBlockChanged(VoxelWorld& world, int x, int y, int z,
                                 BlockType oldBlock, BlockType newBlock) {
    if (!isLoadedVoxel(world, x, y, z)) return;

    skyRemQ_.clear();
    skyAddQ_.clear();
    blkRemQ_.clear();
    blkAddQ_.clear();

    uint8_t oldSky = world.getSky(x, y, z);
    uint8_t oldBlk = world.getBlockLightVal(x, y, z);

    if (!isTransparent(newBlock)) {
        if (oldSky > 0) {
            world.setSky(x, y, z, 0);
            skyRemQ_.push({x, y, z, oldSky});
        }
        if (oldBlk > 0) {
            world.setBlockLightVal(x, y, z, 0);
            blkRemQ_.push({x, y, z, oldBlk});
        }
    } else if (blockEmission(oldBlock) > blockEmission(newBlock)) {
        world.setBlockLightVal(x, y, z, 0);
        blkRemQ_.push({x, y, z, oldBlk});
    }

    processSkyRemoval(world);
    processBlockRemoval(world);

    uint8_t newEmit = blockEmission(newBlock);
    if (newEmit > 0) {
        world.setBlockLightVal(x, y, z, newEmit);
        blkAddQ_.push({x, y, z});
    }

    if (isTransparent(newBlock)) {
        uint8_t aboveSky = (y + 1 < CHUNK_HEIGHT) ? world.getSky(x, y + 1, z) : 15;
        if (aboveSky == 15 && (newBlock == BlockType::Air || newBlock == BlockType::Glass)) {
            world.setSky(x, y, z, 15);
            skyAddQ_.push({x, y, z});
        }
        for (int d = 0; d < 6; ++d) {
            int nx = x + DX[d];
            int ny = y + DY[d];
            int nz = z + DZ[d];
            if (!isLoadedVoxel(world, nx, ny, nz)) continue;
            if (world.getSky(nx, ny, nz) > 1)           skyAddQ_.push({nx, ny, nz});
            if (world.getBlockLightVal(nx, ny, nz) > 1) blkAddQ_.push({nx, ny, nz});
        }
    }

    propagateSkyQueue(world);
    propagateBlockQueue(world);
}

bool LightEngine::updateDynamicLight(const VoxelWorld& world, ivec3 originVoxel,
                                     uint8_t heldEmissionLevel, bool forceRefresh) {
    if (!forceRefresh &&
        heldEmissionLevel == lastDynLevel_ &&
        (heldEmissionLevel == 0 || originVoxel == lastDynOrigin_)) {
        return false;
    }

    lastDynLevel_  = heldEmissionLevel;
    lastDynOrigin_ = originVoxel;
    dynTexOrigin_  = ivec3(originVoxel.x - DYN_TEX_HALF,
                           originVoxel.y - DYN_TEX_HALF,
                           originVoxel.z - DYN_TEX_HALF);
    dynTexDirty_   = true;

    std::memset(dynVolume_.data(), 0, dynVolume_.size());

    if (heldEmissionLevel == 0) return true;
    if (originVoxel.y < 0 || originVoxel.y >= CHUNK_HEIGHT) return true;

    // Pre-cache 3x3 chunk pointer neighborhood around player -> ZERO hash lookups inside BFS!
    const int baseCx = math::chunkFromWorld(originVoxel.x);
    const int baseCz = math::chunkFromWorld(originVoxel.z);
    const Chunk* grid[3][3]{};
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            grid[dz + 1][dx + 1] = world.getChunk({baseCx + dx, baseCz + dz});
        }
    }

    auto sampleBlockFast = [&](int wx, int wy, int wz) noexcept -> BlockType {
        if (static_cast<unsigned>(wy) >= static_cast<unsigned>(CHUNK_HEIGHT)) return BlockType::Air;
        int gx = math::chunkFromWorld(wx) - baseCx + 1;
        int gz = math::chunkFromWorld(wz) - baseCz + 1;
        if (static_cast<unsigned>(gx) > 2u || static_cast<unsigned>(gz) > 2u) return BlockType::Air;
        const Chunk* c = grid[gz][gx];
        if (!c) return BlockType::Air;
        return c->blocks[math::chunkVoxelIndex(math::localFromWorld(wx), wy, math::localFromWorld(wz))];
    };

    // Crucial occlusion invariant: if player is inside a solid block, emit ZERO light!
    if (!isTransparent(sampleBlockFast(originVoxel.x, originVoxel.y, originVoxel.z))) {
        return true;
    }

    // 18-neighbor continuous floating-point Euclidean BFS inside 32x32x32 L1-cached array:
    // - Computes exact continuous occluded path length (1.0 ortho, 1.41421356 diag) instead of coarse integers!
    // - Packs 256 smooth normalized levels into R8_UNORM so hardware trilinear shading is 100% silky-smooth!
    // - Marks ZERO chunks dirty -> ZERO remeshing lag when walking with Torch/Glowstone/Magma!
    static constexpr int DIAG_X[12] = { 1,  1, -1, -1,  1,  1, -1, -1,  0,  0,  0,  0 };
    static constexpr int DIAG_Y[12] = { 0,  0,  0,  0,  1, -1,  1, -1,  1,  1, -1, -1 };
    static constexpr int DIAG_Z[12] = { 1, -1,  1, -1,  0,  0,  0,  0,  1, -1,  1, -1 };
    static constexpr float DIAG_STEP = 1.41421356f;

    const float maxRange = static_cast<float>(std::min<uint8_t>(heldEmissionLevel, 15));
    const float invRange = 1.0f / std::max(maxRange, 1.0f);

    if (dynDist_.size() != DYN_TEX_VOLUME) dynDist_.resize(DYN_TEX_VOLUME);
    std::fill(dynDist_.begin(), dynDist_.end(), 1e9f);

    dynQ_.clear();
    const size_t centerIdx = dynIdx(DYN_TEX_HALF, DYN_TEX_HALF, DYN_TEX_HALF);
    dynDist_[centerIdx]   = 0.0f;
    dynVolume_[centerIdx] = 255;
    dynQ_.push({static_cast<uint8_t>(DYN_TEX_HALF),
                static_cast<uint8_t>(DYN_TEX_HALF),
                static_cast<uint8_t>(DYN_TEX_HALF)});

    while (!dynQ_.empty()) {
        DynNode n = dynQ_.pop();
        const int lx = n.lx;
        const int ly = n.ly;
        const int lz = n.lz;

        const float srcDist = dynDist_[dynIdx(lx, ly, lz)];
        if (srcDist >= maxRange - 0.5f) continue;

        const int wx = dynTexOrigin_.x + lx;
        const int wy = dynTexOrigin_.y + ly;
        const int wz = dynTexOrigin_.z + lz;

        // 1. Orthogonal 6 neighbors (exact step = 1.0 * attenuation)
        for (int d = 0; d < 6; ++d) {
            int nlx = lx + DX[d];
            int nly = ly + DY[d];
            int nlz = lz + DZ[d];
            if (static_cast<unsigned>(nlx) >= static_cast<unsigned>(DYN_TEX_SIZE) ||
                static_cast<unsigned>(nly) >= static_cast<unsigned>(DYN_TEX_SIZE) ||
                static_cast<unsigned>(nlz) >= static_cast<unsigned>(DYN_TEX_SIZE)) {
                continue;
            }

            BlockType nb = sampleBlockFast(wx + DX[d], wy + DY[d], wz + DZ[d]);
            if (!isTransparent(nb)) continue; // 100% occluded by solid walls!

            float atten = static_cast<float>(lightAttenuation(nb));
            float nextDist = srcDist + atten;
            if (nextDist >= maxRange) continue;

            size_t nIdx = dynIdx(nlx, nly, nlz);
            if (nextDist + 0.002f < dynDist_[nIdx]) {
                dynDist_[nIdx] = nextDist;
                float norm = 1.0f - nextDist * invRange;
                int q = static_cast<int>(norm * 255.0f + 0.5f);
                dynVolume_[nIdx] = static_cast<uint8_t>(std::clamp(q, 1, 255));
                dynQ_.push({static_cast<uint8_t>(nlx),
                            static_cast<uint8_t>(nly),
                            static_cast<uint8_t>(nlz)});
            }
        }

        // 2. Open-corner 2D diagonal neighbors (exact step = sqrt(2) * attenuation)
        // Strict corner occlusion: BOTH orthogonal bridge voxels must be transparent!
        if (srcDist + DIAG_STEP < maxRange) {
            for (int d = 0; d < 12; ++d) {
                int dx = DIAG_X[d];
                int dy = DIAG_Y[d];
                int dz = DIAG_Z[d];
                int nlx = lx + dx;
                int nly = ly + dy;
                int nlz = lz + dz;
                if (static_cast<unsigned>(nlx) >= static_cast<unsigned>(DYN_TEX_SIZE) ||
                    static_cast<unsigned>(nly) >= static_cast<unsigned>(DYN_TEX_SIZE) ||
                    static_cast<unsigned>(nlz) >= static_cast<unsigned>(DYN_TEX_SIZE)) {
                    continue;
                }

                BlockType nb = sampleBlockFast(wx + dx, wy + dy, wz + dz);
                if (!isTransparent(nb)) continue;

                bool bridge1 = (dx != 0)
                    ? isTransparent(sampleBlockFast(wx + dx, wy, wz))
                    : isTransparent(sampleBlockFast(wx, wy + dy, wz));
                bool bridge2 = (dz != 0)
                    ? isTransparent(sampleBlockFast(wx, wy, wz + dz))
                    : isTransparent(sampleBlockFast(wx, wy + dy, wz));
                if (!bridge1 || !bridge2) continue;

                float atten = static_cast<float>(lightAttenuation(nb));
                float nextDist = srcDist + DIAG_STEP * atten;
                if (nextDist >= maxRange) continue;

                size_t nIdx = dynIdx(nlx, nly, nlz);
                if (nextDist + 0.002f < dynDist_[nIdx]) {
                    dynDist_[nIdx] = nextDist;
                    float norm = 1.0f - nextDist * invRange;
                    int q = static_cast<int>(norm * 255.0f + 0.5f);
                    dynVolume_[nIdx] = static_cast<uint8_t>(std::clamp(q, 1, 255));
                    dynQ_.push({static_cast<uint8_t>(nlx),
                                static_cast<uint8_t>(nly),
                                static_cast<uint8_t>(nlz)});
                }
            }
        }
    }
    return true;
}

} // namespace umbra
