#pragma once

#include "types.hpp"
#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace umbra {

class LightEngine;
struct TerrainNoiseState;

struct Chunk {
    static constexpr size_t VOLUME =
        static_cast<size_t>(CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE);

    ChunkCoord coord{};
    std::array<BlockType, VOLUME> blocks{};
    std::array<uint8_t,   VOLUME> light{}; // High nibble = skyLight (0..15), Low nibble = static blockLight (0..15)
    bool generated = false;
    bool lit       = false;
    bool meshDirty = true;

    static constexpr size_t localIndex(int lx, int y, int lz) noexcept {
        return math::chunkVoxelIndex(lx, y, lz);
    }
};

enum class BiomeType : uint8_t {
    Ocean    = 0,
    Beach    = 1,
    River    = 2,
    Plains   = 3,
    Meadow   = 4,
    Forest   = 5,
    Mountain = 6,
    SnowPeak = 7
};

struct TerrainSample {
    int       height    = SEA_LEVEL;
    BiomeType biome     = BiomeType::Plains;
    float     moisture  = 0.5f;
    float     temp      = 0.5f;
    float     riverMask = 0.0f;
};

class VoxelWorld {
public:
    explicit VoxelWorld(uint32_t seed = 1337u);
    ~VoxelWorld();

    VoxelWorld(const VoxelWorld&) = delete;
    VoxelWorld& operator=(const VoxelWorld&) = delete;

    uint32_t seed() const noexcept { return seed_; }

    // Streams infinite chunks in around playerPos (up to maxGeneratePerFrame new chunks)
    // using a precomputed spiral ring (O(1) amortized even at Render Distance 50!).
    void updateStreaming(const vec3& playerPos,
                         LightEngine& lightEngine,
                         int renderRadius,
                         int maxGeneratePerFrame,
                         std::vector<ChunkCoord>& unloadedOut);

    // Pre-generates initial spawn neighborhood cleanly at startup
    void prewarmAround(const vec3& spawnPos, LightEngine& lightEngine, int radius);

    Chunk* getChunk(ChunkCoord coord);
    const Chunk* getChunk(ChunkCoord coord) const;
    bool hasChunk(ChunkCoord coord) const { return chunks_.find(coord) != chunks_.end(); }

    const std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash>& chunks() const {
        return chunks_;
    }

    BlockType getBlock(int x, int y, int z) const;
    void setBlockRaw(int x, int y, int z, BlockType b);

    uint8_t getLightPacked(int x, int y, int z) const;
    uint8_t getSky(int x, int y, int z) const;
    uint8_t getBlockLightVal(int x, int y, int z) const;
    void setSky(int x, int y, int z, uint8_t v);
    void setBlockLightVal(int x, int y, int z, uint8_t v);

    void markChunkDirty(ChunkCoord coord);
    void markAllChunksDirty();
    void markVoxelAndBorderChunksDirty(int x, int z);

    // Collects up to maxCount chunks needing GPU remesh (prioritizing nearest to player)
    std::vector<ChunkCoord> popDirtyChunks(const vec3& playerPos, size_t maxCount);

    std::optional<RaycastHit> raycast(const vec3& origin,
                                      const vec3& direction,
                                      float maxDistance = 10.0f) const;

    // Continuous 6-phase 360-degree astronomical atmosphere:
    // 0.00 = Sunrise, 0.125 = Morning, 0.25 = Noon, 0.41 = Evening, 0.495 = Sunset, 0.75 = Night
    static TimeOfDayState evaluateTimeOfDay(float timeOfDay);

    int findSurfaceY(int x, int z) const;
    int sampleTerrainHeight(int x, int z) const;
    TerrainSample sampleTerrainColumn(int x, int z) const;
    static const char* biomeDisplayName(BiomeType b) noexcept;

private:
    Chunk& generateChunk(ChunkCoord coord);
    void ensureSpiralRing(int renderRadius);

    uint32_t seed_ = 1337u;
    std::unique_ptr<TerrainNoiseState> noise_;
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> chunks_;

    int spiralRadius_ = -1;
    std::vector<ChunkCoord> spiralOffsets_;
    size_t spiralCursor_ = 0;
    ChunkCoord lastStreamCenter_{999999, 999999};
};

} // namespace umbra
