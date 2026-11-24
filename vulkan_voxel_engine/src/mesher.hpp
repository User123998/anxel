#pragma once

#include "types.hpp"
#include "world.hpp"
#include <vector>

namespace umbra {

struct MeshData {
    std::vector<VoxelVertex> vertices;      // Opaque pass (solid blocks, leaves, torches)
    std::vector<uint32_t>    indices;
    std::vector<VoxelVertex> transVertices; // Translucent pass (Water + Glass, alpha blended)
    std::vector<uint32_t>    transIndices;
};

class VoxelMesher {
public:
    // Smooth Binary Greedy Mesher:
    // - 64-bit bitwise face culling (skips interior solid voxels in O(1) bit ops)
    // - Smooth-lighting-preserving 2D greedy rectangle merging (70-85% fewer triangles!)
    // - Supports toggling smoothLighting & greedyMeshing live from the Settings UI
    static MeshData buildChunkMesh(const VoxelWorld& world,
                                   ChunkCoord coord,
                                   bool smoothLighting = true,
                                   bool greedyMeshing  = true);
};

} // namespace umbra
