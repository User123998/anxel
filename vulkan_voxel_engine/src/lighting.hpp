#pragma once

#include "math/chunk_math.hpp"
#include "world.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace umbra {

// High-Performance Zero-Allocation Cross-Chunk Dual-Channel + Zero-Remesh 3D Dynamic Light Engine:
// - Uses reusable contiguous math::FlatQueue<T> (zero per-frame heap allocations!)
// - Bridges static sky/block light seamlessly across infinite chunk borders
// - Computes voxel-occluded held-block dynamic light into a tiny L1-cached 32x32x32 byte volume
//   uploaded asynchronously to a hardware-filtered Vulkan 3D texture -> ZERO chunk remeshing when walking!
class LightEngine {
public:
    LightEngine() {
        skyAddQ_.reserve(4096);
        blkAddQ_.reserve(2048);
        skyRemQ_.reserve(2048);
        blkRemQ_.reserve(1024);
        dynQ_.reserve(4096);
        dynDist_.assign(DYN_TEX_VOLUME, 1e9f);
        dynVolume_.fill(0);
    }

    void lightNewChunk(VoxelWorld& world, ChunkCoord coord);

    void onBlockChanged(VoxelWorld& world, int x, int y, int z,
                        BlockType oldBlock, BlockType newBlock);

    // Updates 32x32x32 voxel-occluded dynamic light volume ONLY when holding a light source block!
    // Never dirties chunk meshes -> zero CPU remesh lag and zero GPU stalls when walking!
    bool updateDynamicLight(const VoxelWorld& world, ivec3 originVoxel, uint8_t heldEmissionLevel,
                            bool forceRefresh = false);

    const uint8_t* dynVolumeData() const noexcept { return dynVolume_.data(); }
    ivec3 dynTexOrigin() const noexcept           { return dynTexOrigin_; }
    bool isDynTexDirty() const noexcept           { return dynTexDirty_; }
    void clearDynTexDirty() noexcept              { dynTexDirty_ = false; }

    // Samples effective 0..15 dynamic light level at a world voxel (for HUD telemetry readout)
    uint8_t sampleDynamicLevelAt(ivec3 worldPos) const noexcept {
        int lx = worldPos.x - dynTexOrigin_.x;
        int ly = worldPos.y - dynTexOrigin_.y;
        int lz = worldPos.z - dynTexOrigin_.z;
        if (static_cast<unsigned>(lx) >= static_cast<unsigned>(DYN_TEX_SIZE) ||
            static_cast<unsigned>(ly) >= static_cast<unsigned>(DYN_TEX_SIZE) ||
            static_cast<unsigned>(lz) >= static_cast<unsigned>(DYN_TEX_SIZE)) {
            return 0;
        }
        if (lastDynLevel_ == 0) return 0;
        uint8_t raw = dynVolume_[static_cast<size_t>((lz << 10) | (ly << 5) | lx)];
        uint8_t lvl = static_cast<uint8_t>((static_cast<int>(raw) * static_cast<int>(lastDynLevel_) + 127) / 255);
        return (lvl > 15) ? 15 : lvl;
    }

private:
    struct LightNode {
        int x, y, z;
    };
    struct DynNode {
        uint8_t lx, ly, lz;
    };
    struct RemovalNode {
        int x, y, z;
        uint8_t prevVal;
    };

    void propagateSkyQueue(VoxelWorld& world);
    void propagateBlockQueue(VoxelWorld& world);
    void processSkyRemoval(VoxelWorld& world);
    void processBlockRemoval(VoxelWorld& world);

    math::FlatQueue<LightNode>   skyAddQ_;
    math::FlatQueue<LightNode>   blkAddQ_;
    math::FlatQueue<RemovalNode> skyRemQ_;
    math::FlatQueue<RemovalNode> blkRemQ_;
    math::FlatQueue<DynNode>     dynQ_;

    // 32x32x32 R8_UNORM 3D GPU texture backing buffer (indexed as (lz << 10) | (ly << 5) | lx)
    std::array<uint8_t, DYN_TEX_VOLUME> dynVolume_{};
    std::vector<float>                  dynDist_; // Reusable continuous Euclidean path-distance scratch buffer
    ivec3   dynTexOrigin_{0, 0, 0};
    bool    dynTexDirty_   = true;
    uint8_t lastDynLevel_  = 0;
    ivec3   lastDynOrigin_{0, -9999, 0};
};

} // namespace umbra
