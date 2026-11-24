#include "world.hpp"
#include "lighting.hpp"

// FastNoiseLite 1.0.1 single-header implementation (from src/math/noise/noise.h)
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Waggressive-loop-optimizations"
#endif
#endif
#define FNL_IMPL
#include "math/noise/noise.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cmath>
#include <limits>

namespace umbra {

// Encapsulates all FastNoiseLite 1.0.1 states for fast multi-biome infinite terrain & 3D caves
struct TerrainNoiseState {
    fnl_state continent{};
    fnl_state erosion{};
    fnl_state peaks{};
    fnl_state detail{};
    fnl_state riverWarp{};
    fnl_state riverChannel{};
    fnl_state temperature{};
    fnl_state moisture{};
    fnl_state caveSpaghettiA{};
    fnl_state caveSpaghettiB{};
    fnl_state caveCheese{};

    explicit TerrainNoiseState(int baseSeed) {
        // 1. Continentalness: vast oceans, coastal shelves, continents & highland interiors
        continent = fnlCreateState();
        continent.seed         = baseSeed + 101;
        continent.noise_type   = FNL_NOISE_OPENSIMPLEX2;
        continent.fractal_type = FNL_FRACTAL_FBM;
        continent.octaves      = 4;
        continent.lacunarity   = 2.05f;
        continent.gain         = 0.48f;
        continent.frequency    = 0.0021f;

        // 2. Erosion: separates flat lush plains/meadows from rugged highland crags
        erosion = fnlCreateState();
        erosion.seed         = baseSeed + 223;
        erosion.noise_type   = FNL_NOISE_OPENSIMPLEX2;
        erosion.fractal_type = FNL_FRACTAL_FBM;
        erosion.octaves      = 3;
        erosion.lacunarity   = 2.0f;
        erosion.gain         = 0.50f;
        erosion.frequency    = 0.0038f;

        // 3. Alpine Ridged Multifractal: majestic mountain ranges & dramatic cliffs
        peaks = fnlCreateState();
        peaks.seed         = baseSeed + 419;
        peaks.noise_type   = FNL_NOISE_OPENSIMPLEX2;
        peaks.fractal_type = FNL_FRACTAL_RIDGED;
        peaks.octaves      = 4;
        peaks.lacunarity   = 2.12f;
        peaks.gain         = 0.48f;
        peaks.frequency    = 0.0046f;

        // 4. Rolling micro-hills for meadows & forest floors
        detail = fnlCreateState();
        detail.seed         = baseSeed + 613;
        detail.noise_type   = FNL_NOISE_OPENSIMPLEX2;
        detail.fractal_type = FNL_FRACTAL_FBM;
        detail.octaves      = 3;
        detail.frequency    = 0.0145f;

        // 5. Domain-warped winding rivers carving naturally toward oceans
        riverWarp = fnlCreateState();
        riverWarp.seed             = baseSeed + 773;
        riverWarp.domain_warp_type = FNL_DOMAIN_WARP_OPENSIMPLEX2;
        riverWarp.fractal_type     = FNL_FRACTAL_DOMAIN_WARP_PROGRESSIVE;
        riverWarp.octaves          = 3;
        riverWarp.frequency        = 0.0032f;
        riverWarp.domain_warp_amp  = 52.0f;

        riverChannel = fnlCreateState();
        riverChannel.seed         = baseSeed + 887;
        riverChannel.noise_type   = FNL_NOISE_OPENSIMPLEX2;
        riverChannel.fractal_type = FNL_FRACTAL_FBM;
        riverChannel.octaves      = 3;
        riverChannel.frequency    = 0.0024f;

        // 6. Climate fields (temperature + moisture -> Oceans, Plains, Meadows, Forests, Peaks)
        temperature = fnlCreateState();
        temperature.seed         = baseSeed + 1009;
        temperature.noise_type   = FNL_NOISE_OPENSIMPLEX2;
        temperature.fractal_type = FNL_FRACTAL_FBM;
        temperature.octaves      = 3;
        temperature.frequency    = 0.0018f;

        moisture = fnlCreateState();
        moisture.seed         = baseSeed + 1237;
        moisture.noise_type   = FNL_NOISE_OPENSIMPLEX2;
        moisture.fractal_type = FNL_FRACTAL_FBM;
        moisture.octaves      = 3;
        moisture.frequency    = 0.0025f;

        // 7. Natural 3D Spaghetti Tunnels + Vaulted Cheese Caverns
        caveSpaghettiA = fnlCreateState();
        caveSpaghettiA.seed             = baseSeed + 2111;
        caveSpaghettiA.noise_type       = FNL_NOISE_OPENSIMPLEX2;
        caveSpaghettiA.rotation_type_3d = FNL_ROTATION_IMPROVE_XZ_PLANES;
        caveSpaghettiA.fractal_type     = FNL_FRACTAL_FBM;
        caveSpaghettiA.octaves          = 2;
        caveSpaghettiA.frequency        = 0.0175f;

        caveSpaghettiB = fnlCreateState();
        caveSpaghettiB.seed             = baseSeed + 2791;
        caveSpaghettiB.noise_type       = FNL_NOISE_OPENSIMPLEX2;
        caveSpaghettiB.rotation_type_3d = FNL_ROTATION_IMPROVE_XZ_PLANES;
        caveSpaghettiB.fractal_type     = FNL_FRACTAL_FBM;
        caveSpaghettiB.octaves          = 2;
        caveSpaghettiB.frequency        = 0.0175f;

        caveCheese = fnlCreateState();
        caveCheese.seed             = baseSeed + 3571;
        caveCheese.noise_type       = FNL_NOISE_OPENSIMPLEX2;
        caveCheese.rotation_type_3d = FNL_ROTATION_IMPROVE_XZ_PLANES;
        caveCheese.fractal_type     = FNL_FRACTAL_FBM;
        caveCheese.octaves          = 2;
        caveCheese.frequency        = 0.022f;
    }
};

namespace {

inline constexpr uint32_t fastHash2u(int x, int z, uint32_t seed) noexcept {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u +
                 static_cast<uint32_t>(z) * 668265263u +
                 seed * 1442695041u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return h ^ (h >> 16u);
}

inline constexpr uint32_t fastHash3u(int x, int y, int z, uint32_t seed) noexcept {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u +
                 static_cast<uint32_t>(y) * 2147483647u +
                 static_cast<uint32_t>(z) * 668265263u +
                 seed * 1442695041u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return h ^ (h >> 16u);
}

inline constexpr float fastHash2f(int x, int z, uint32_t seed) noexcept {
    return static_cast<float>(fastHash2u(x, z, seed) & 0xFFFFu) * (1.0f / 65535.0f);
}

inline constexpr float fastHash3f(int x, int y, int z, uint32_t seed) noexcept {
    return static_cast<float>(fastHash3u(x, y, z, seed) & 0xFFFFu) * (1.0f / 65535.0f);
}

// Smooth piecewise keyframe evaluator across cyclic timeOfDay [0..1)
struct SkyKeyframe {
    float t;
    vec3  zenith;
    vec3  horizon;
    vec3  sunColor;
    vec3  skyAmbient;
    vec3  mieGlow;
    float skyIntensity;
    float moonIntensity;
    float warmGlow;
};

constexpr SkyKeyframe SKY_KEYS[] = {
    // 0.000 — Sunrise (06:00): rose-gold & peach eastern glow, lavender-indigo zenith
    {0.000f, {0.22f, 0.27f, 0.52f}, {0.95f, 0.56f, 0.36f}, {1.00f, 0.62f, 0.34f},
             {0.58f, 0.44f, 0.46f}, {1.00f, 0.58f, 0.34f}, 0.56f, 0.12f, 0.96f},
    // 0.075 — Early Morning (07:48): soft apricot-gold melting into fresh sky blue
    {0.075f, {0.20f, 0.48f, 0.82f}, {0.82f, 0.84f, 0.90f}, {1.00f, 0.88f, 0.68f},
             {0.48f, 0.62f, 0.82f}, {1.00f, 0.78f, 0.52f}, 0.86f, 0.00f, 0.42f},
    // 0.150 — Crisp Morning (09:36): clear azure sky & dewy natural daylight
    {0.150f, {0.16f, 0.46f, 0.84f}, {0.66f, 0.81f, 0.94f}, {1.00f, 0.95f, 0.84f},
             {0.45f, 0.63f, 0.88f}, {1.00f, 0.88f, 0.70f}, 0.96f, 0.00f, 0.16f},
    // 0.250 — High Noon (12:00): deep cerulean zenith & crisp sunlight
    {0.250f, {0.13f, 0.41f, 0.82f}, {0.60f, 0.78f, 0.94f}, {1.00f, 0.97f, 0.90f},
             {0.44f, 0.62f, 0.88f}, {1.00f, 0.94f, 0.82f}, 1.00f, 0.00f, 0.10f},
    // 0.410 — Golden Evening (15:50): honey-amber sunlight & warm horizon haze
    {0.410f, {0.17f, 0.37f, 0.72f}, {0.88f, 0.68f, 0.46f}, {1.00f, 0.76f, 0.42f},
             {0.56f, 0.50f, 0.56f}, {1.00f, 0.64f, 0.28f}, 0.82f, 0.00f, 0.74f},
    // 0.495 — Dramatic Sunset (17:52): crimson, tangerine & magenta-violet twilight
    {0.495f, {0.13f, 0.15f, 0.36f}, {0.92f, 0.38f, 0.20f}, {1.00f, 0.42f, 0.16f},
             {0.52f, 0.30f, 0.36f}, {1.00f, 0.36f, 0.16f}, 0.44f, 0.16f, 1.00f},
    // 0.575 — Blue Hour Twilight (19:48): deep velvet indigo & faint ember horizon
    {0.575f, {0.025f, 0.042f, 0.120f}, {0.14f, 0.12f, 0.22f}, {0.18f, 0.11f, 0.09f},
             {0.14f, 0.19f, 0.36f}, {0.48f, 0.22f, 0.20f}, 0.095f, 0.72f, 0.32f},
    // 0.750 — Starry Midnight (00:00): deep sapphire sky, true natural moonlit darkness
    {0.750f, {0.008f, 0.015f, 0.044f}, {0.024f, 0.042f, 0.090f}, {0.00f, 0.00f, 0.00f},
             {0.11f, 0.18f, 0.36f}, {0.12f, 0.18f, 0.34f}, 0.042f, 1.00f, 0.04f},
    // 0.925 — Pre-Dawn First Light (04:12): cool plum & soft rose hint on horizon
    {0.925f, {0.032f, 0.048f, 0.130f}, {0.22f, 0.16f, 0.24f}, {0.22f, 0.15f, 0.13f},
             {0.18f, 0.20f, 0.36f}, {0.64f, 0.32f, 0.28f}, 0.110f, 0.62f, 0.42f},
    // 1.000 — Wrap smoothly back to Sunrise
    {1.000f, {0.22f, 0.27f, 0.52f}, {0.95f, 0.56f, 0.36f}, {1.00f, 0.62f, 0.34f},
             {0.58f, 0.44f, 0.46f}, {1.00f, 0.58f, 0.34f}, 0.56f, 0.12f, 0.96f},
};

} // namespace

VoxelWorld::VoxelWorld(uint32_t seed)
    : seed_(seed), noise_(std::make_unique<TerrainNoiseState>(static_cast<int>(seed))) {}

VoxelWorld::~VoxelWorld() = default;

const char* VoxelWorld::biomeDisplayName(BiomeType b) noexcept {
    switch (b) {
        case BiomeType::Ocean:    return "DEEP OCEAN";
        case BiomeType::Beach:    return "COASTAL BEACH";
        case BiomeType::River:    return "RIVER VALLEY";
        case BiomeType::Plains:   return "SUNLIT PLAINS";
        case BiomeType::Meadow:   return "ALPINE MEADOW";
        case BiomeType::Forest:   return "ANCIENT FOREST";
        case BiomeType::Mountain: return "HIGH MOUNTAINS";
        case BiomeType::SnowPeak: return "ALPINE PEAKS";
    }
    return "PLAINS";
}

TerrainSample VoxelWorld::sampleTerrainColumn(int x, int z) const {
    const float fx = static_cast<float>(x);
    const float fz = static_cast<float>(z);

    // 1. Macro-scale FastNoiseLite continentalness, erosion, peaks & rolling detail
    float cont = fnlGetNoise2D(&noise_->continent, fx, fz); // [-1..1]
    float eros = fnlGetNoise2D(&noise_->erosion,   fx, fz); // [-1..1]
    float peak = fnlGetNoise2D(&noise_->peaks,     fx, fz); // [-1..1] (ridged)
    float det  = fnlGetNoise2D(&noise_->detail,    fx, fz); // [-1..1]
    float temp = fnlGetNoise2D(&noise_->temperature, fx, fz) * 0.5f + 0.5f;
    float mois = fnlGetNoise2D(&noise_->moisture,    fx, fz) * 0.5f + 0.5f;

    // 2. Domain-warped winding river spline with gentle floodplains & smooth sandy banks
    FNLfloat rx = fx, rz = fz;
    fnlDomainWarp2D(&noise_->riverWarp, &rx, &rz);
    float rVal = std::abs(fnlGetNoise2D(&noise_->riverChannel, rx, rz));
    // Soft floodplain flattens terrain gently toward the river before the channel carves down
    float landForRiver = smootherstepf(-0.18f, 0.10f, cont);
    float riverFlood   = (1.0f - smootherstepf(0.025f, 0.145f, rVal)) * landForRiver;
    float riverTrough  = (1.0f - smootherstepf(0.012f, 0.066f, rVal)) * landForRiver;
    float riverMask    = riverTrough;

    // 3. C2-smooth continental elevation curve (zero kinks across Ocean -> Shelf -> Beach -> Plains -> Mountains -> Peaks)
    float oceanDepth = smootherstepf(-0.08f, -0.62f, cont); // 0 at coast, 1 in deep abyssal ocean
    float coastalT   = smootherstepf(-0.24f,  0.08f, cont); // 0 on seabed shelf, 1 on dry coast
    float inlandT    = smootherstepf( 0.02f,  0.76f, cont); // 0 at shore, 1 deep inland

    // Low erosion -> dramatic alpine ranges; high erosion -> wide scenic meadows & rolling plains
    float ruggedness = smootherstepf(0.12f, 0.88f, 1.0f - (eros * 0.5f + 0.5f));
    float foothill   = smootherstepf(0.14f, 0.62f, inlandT * 0.72f + ruggedness * 0.36f);
    float mtnMask    = smootherstepf(0.32f, 0.82f, inlandT * 0.68f + ruggedness * 0.48f);

    float ridgeNorm = saturate(peak * 0.5f + 0.5f);
    float peakShape = ridgeNorm * ridgeNorm * (0.65f + 0.35f * ridgeNorm); // Natural sweeping alpine ridges

    float oceanH  = static_cast<float>(SEA_LEVEL) - 5.0f - oceanDepth * 21.0f + det * (1.8f + oceanDepth * 1.8f);
    float landH   = static_cast<float>(SEA_LEVEL) + 2.2f
                  + inlandT * 10.5f
                  + foothill * (1.0f - mtnMask * 0.45f) * 12.5f
                  + det * (2.4f + ruggedness * 4.4f)
                  + peakShape * mtnMask * 52.0f;

    float baseH = lerpf(oceanH, landH, coastalT);

    // Gently lower river valley floodplains, then smoothly carve a 3-6 block deep natural river channel
    if (riverFlood > 0.001f) {
        float bankTarget = std::min(baseH, static_cast<float>(SEA_LEVEL) + 2.2f + det * 0.8f);
        baseH = lerpf(baseH, bankTarget, riverFlood * 0.72f);
    }
    if (riverTrough > 0.001f) {
        float riverBed = static_cast<float>(SEA_LEVEL) - 4.8f + det * 1.1f;
        baseH = lerpf(baseH, riverBed, riverTrough);
    }

    // Sculpt a scenic elevated highland overlook & cavern valley near spawn (0, 0)
    float distSpawn = std::sqrt(fx * fx + fz * fz);
    if (distSpawn < 56.0f) {
        float smooth = smootherstepf(56.0f, 0.0f, distSpawn);
        float spawnH = static_cast<float>(SEA_LEVEL) + 6.0f
                     + std::sin(fx * 0.075f) * 2.2f
                     + (fz > 14.0f ? (fz - 14.0f) * 0.52f : 0.0f);
        baseH = lerpf(baseH, spawnH, smooth * 0.65f);
        riverMask *= (1.0f - smooth * 0.85f);
    }

    int h = std::clamp(static_cast<int>(std::round(baseH)), 10, CHUNK_HEIGHT - 12);

    // 4. Coherent natural biome classification aligned smoothly with elevation, moisture & temperature
    int snowLine = static_cast<int>(80.0f + (temp - 0.5f) * 14.0f);
    BiomeType biome = BiomeType::Plains;
    if (riverMask > 0.36f && h <= SEA_LEVEL + 2) {
        biome = BiomeType::River;
    } else if (cont < -0.06f && h < SEA_LEVEL) {
        biome = BiomeType::Ocean;
    } else if (h <= SEA_LEVEL + 2 && (cont < 0.14f || riverFlood > 0.35f)) {
        biome = BiomeType::Beach;
    } else if (h >= snowLine) {
        biome = BiomeType::SnowPeak;
    } else if (h >= 66 || (mtnMask > 0.58f && h >= 58)) {
        biome = BiomeType::Mountain;
    } else if (mois > 0.53f && h < 66) {
        biome = BiomeType::Forest;
    } else if (h >= 52 && mois > 0.36f) {
        biome = BiomeType::Meadow;
    } else {
        biome = BiomeType::Plains;
    }

    return TerrainSample{h, biome, mois, temp, riverMask};
}

int VoxelWorld::sampleTerrainHeight(int x, int z) const {
    return sampleTerrainColumn(x, z).height;
}

Chunk& VoxelWorld::generateChunk(ChunkCoord coord) {
    auto ptr = std::make_unique<Chunk>();
    ptr->coord = coord;
    ptr->blocks.fill(BlockType::Air);
    ptr->light.fill(0);

    const int baseX = coord.cx * CHUNK_SIZE;
    const int baseZ = coord.cz * CHUNK_SIZE;

    TerrainSample colMap[CHUNK_SIZE][CHUNK_SIZE];

    // 1. Sample 16x16 terrain columns first so we can compute exact local terrain slope for natural rock cliffs
    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            colMap[lz][lx] = sampleTerrainColumn(baseX + lx, baseZ + lz);
        }
    }

    // Fill multi-biome terrain columns with slope-aware rock cliffs, sandy seabeds & alpine snow
    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            const TerrainSample& ts = colMap[lz][lx];
            int h = ts.height;

            // Fast O(1) interior slope estimation (clamped to chunk bounds)
            int hL = colMap[lz][std::max(0, lx - 1)].height;
            int hR = colMap[lz][std::min(CHUNK_SIZE - 1, lx + 1)].height;
            int hD = colMap[std::max(0, lz - 1)][lx].height;
            int hU = colMap[std::min(CHUNK_SIZE - 1, lz + 1)][lx].height;
            int slope = std::max(std::abs(hR - hL), std::abs(hU - hD));
            bool steepCliff = (slope >= 4 && h > SEA_LEVEL + 3);

            for (int y = 0; y <= h; ++y) {
                BlockType b = BlockType::Stone;
                if (y <= 14) {
                    b = BlockType::DeepSlate;
                } else if (steepCliff && y >= h - 3) {
                    b = BlockType::Stone;
                } else if (y == h) {
                    switch (ts.biome) {
                        case BiomeType::SnowPeak:
                            b = (slope >= 5) ? BlockType::Stone : BlockType::Snow;
                            break;
                        case BiomeType::Mountain:
                            b = (h >= 73 || slope >= 3) ? BlockType::Stone : BlockType::Grass;
                            break;
                        case BiomeType::Beach:
                        case BiomeType::Ocean:
                        case BiomeType::River:
                            // Deep ocean floor transitions from warm sand to dark slate/stone for rich optical depth!
                            if (h <= SEA_LEVEL - 14)      b = BlockType::DeepSlate;
                            else if (h <= SEA_LEVEL - 9)  b = BlockType::Stone;
                            else if (h <= SEA_LEVEL + 2)  b = BlockType::Sand;
                            else                          b = BlockType::Grass;
                            break;
                        case BiomeType::Meadow:
                        case BiomeType::Plains:
                        case BiomeType::Forest:
                            b = BlockType::Grass;
                            break;
                    }
                } else if (y >= h - 3) {
                    if (ts.biome == BiomeType::Ocean || ts.biome == BiomeType::Beach ||
                        ts.biome == BiomeType::River) {
                        b = (h <= SEA_LEVEL - 10) ? BlockType::Stone : BlockType::Sand;
                    } else if (ts.biome == BiomeType::Mountain && h >= 71) {
                        b = BlockType::Stone;
                    } else if (ts.biome == BiomeType::SnowPeak && y == h - 1 && slope < 5) {
                        b = BlockType::Snow;
                    } else if (ts.biome != BiomeType::SnowPeak) {
                        b = BlockType::Dirt;
                    }
                }
                ptr->blocks[Chunk::localIndex(lx, y, lz)] = b;
            }

            // Fill oceans & winding rivers up to SEA_LEVEL
            for (int y = h + 1; y <= SEA_LEVEL; ++y) {
                ptr->blocks[Chunk::localIndex(lx, y, lz)] = BlockType::Water;
            }
        }
    }

    // 2. Ultra-Fast 4x4x4 Trilinearly Interpolated 3D Spaghetti Tunnels + Vaulted Cheese Caverns!
    //    Evaluates only 5x5x20 = 500 3D FastNoiseLite points per chunk (~40x fewer 3D noise calls!)
    //    resulting in 6-8x faster chunk streaming AND smoother, more organic cave walls!
    {
        constexpr int CAVE_MIN_Y = 4;
        constexpr int CAVE_MAX_Y = 76;
        constexpr int STEP       = 4;
        constexpr int GX         = CHUNK_SIZE / STEP + 1;                  // 5
        constexpr int GZ         = CHUNK_SIZE / STEP + 1;                  // 5
        constexpr int GY         = (CAVE_MAX_Y - CAVE_MIN_Y) / STEP + 1;   // 19

        // Determine highest possible cave Y in this chunk so flat ocean chunks skip upper Y cells!
        int chunkMaxCaveY = 0;
        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                int h = colMap[lz][lx].height;
                int lim = (h <= SEA_LEVEL + 2) ? std::min(h - 5, SEA_LEVEL - 6) : std::min(h - 1, CAVE_MAX_Y - 1);
                if (lim > chunkMaxCaveY) chunkMaxCaveY = lim;
            }
        }

        if (chunkMaxCaveY >= CAVE_MIN_Y) {
            int maxGy = std::min(GY - 1, (chunkMaxCaveY - CAVE_MIN_Y) / STEP + 1);
            float gridSA[GZ][GY][GX]{};
            float gridSB[GZ][GY][GX]{};
            float gridCh[GZ][GY][GX]{};

            for (int gz = 0; gz < GZ; ++gz) {
                float wz = static_cast<float>(baseZ + gz * STEP);
                for (int gy = 0; gy <= maxGy; ++gy) {
                    int wyInt = CAVE_MIN_Y + gy * STEP;
                    float wySpag  = static_cast<float>(wyInt) * 1.25f;
                    float wyCheese = static_cast<float>(wyInt) * 1.45f;
                    for (int gx = 0; gx < GX; ++gx) {
                        float wx = static_cast<float>(baseX + gx * STEP);
                        gridSA[gz][gy][gx] = fnlGetNoise3D(&noise_->caveSpaghettiA, wx, wySpag,   wz);
                        gridSB[gz][gy][gx] = fnlGetNoise3D(&noise_->caveSpaghettiB, wx, wySpag,   wz);
                        gridCh[gz][gy][gx] = fnlGetNoise3D(&noise_->caveCheese,     wx, wyCheese, wz);
                    }
                }
            }

            constexpr float INV_STEP = 1.0f / static_cast<float>(STEP);
            for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
                int gz0 = lz >> 2;
                float tz = static_cast<float>(lz & 3) * INV_STEP;
                for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                    int gx0 = lx >> 2;
                    float tx = static_cast<float>(lx & 3) * INV_STEP;

                    const TerrainSample& ts = colMap[lz][lx];
                    int h = ts.height;
                    // Check local 3x3 minimum terrain height so coastal cliffs never carve dry cave
                    // holes horizontally into adjacent ocean/river water columns!
                    int localMinH = h;
                    for (int dz = -1; dz <= 1; ++dz) {
                        int nz = std::clamp(lz + dz, 0, CHUNK_SIZE - 1);
                        for (int dx = -1; dx <= 1; ++dx) {
                            int nx = std::clamp(lx + dx, 0, CHUNK_SIZE - 1);
                            localMinH = std::min(localMinH, colMap[nz][nx].height);
                        }
                    }
                    int maxCaveY = (localMinH <= SEA_LEVEL + 2)
                        ? std::min(localMinH - 5, SEA_LEVEL - 7)
                        : std::min(h - 1, CAVE_MAX_Y - 1);
                    if (maxCaveY < CAVE_MIN_Y) continue;

                    int wx = baseX + lx;
                    int wz = baseZ + lz;
                    int cheeseLim = std::min(maxCaveY - 2, 50);

                    for (int y = CAVE_MIN_Y; y <= maxCaveY; ++y) {
                        size_t idx = Chunk::localIndex(lx, y, lz);
                        if (ptr->blocks[idx] == BlockType::Water) continue;

                        int ry  = y - CAVE_MIN_Y;
                        int gy0 = ry >> 2;
                        float ty = static_cast<float>(ry & 3) * INV_STEP;

                        float sA = lerp3D(
                            gridSA[gz0][gy0][gx0],     gridSA[gz0][gy0][gx0+1],
                            gridSA[gz0][gy0+1][gx0],   gridSA[gz0][gy0+1][gx0+1],
                            gridSA[gz0+1][gy0][gx0],   gridSA[gz0+1][gy0][gx0+1],
                            gridSA[gz0+1][gy0+1][gx0], gridSA[gz0+1][gy0+1][gx0+1],
                            tx, ty, tz);
                        float sB = lerp3D(
                            gridSB[gz0][gy0][gx0],     gridSB[gz0][gy0][gx0+1],
                            gridSB[gz0][gy0+1][gx0],   gridSB[gz0][gy0+1][gx0+1],
                            gridSB[gz0+1][gy0][gx0],   gridSB[gz0+1][gy0][gx0+1],
                            gridSB[gz0+1][gy0+1][gx0], gridSB[gz0+1][gy0+1][gx0+1],
                            tx, ty, tz);

                        bool isSpaghetti = (sA * sA + sB * sB < 0.0115f);
                        bool isCheese    = false;
                        if (!isSpaghetti && y < cheeseLim) {
                            float ch = lerp3D(
                                gridCh[gz0][gy0][gx0],     gridCh[gz0][gy0][gx0+1],
                                gridCh[gz0][gy0+1][gx0],   gridCh[gz0][gy0+1][gx0+1],
                                gridCh[gz0+1][gy0][gx0],   gridCh[gz0+1][gy0][gx0+1],
                                gridCh[gz0+1][gy0+1][gx0], gridCh[gz0+1][gy0+1][gx0+1],
                                tx, ty, tz);
                            float depthBonus = smoothstepf(48.0f, 14.0f, static_cast<float>(y)) * 0.08f;
                            isCheese = (ch + depthBonus > 0.56f);
                        }

                        if (isSpaghetti || isCheese) {
                            ptr->blocks[idx] = (y <= 8) ? BlockType::Magma : BlockType::Air;
                        }
                    }

                    // Scatter rare bioluminescent cave crystals on cavern ceilings
                    for (int y = 10; y < std::min(h - 5, 48); ++y) {
                        size_t idx = Chunk::localIndex(lx, y, lz);
                        if (ptr->blocks[idx] != BlockType::Air) continue;
                        size_t above = Chunk::localIndex(lx, y + 1, lz);
                        if (!isTransparent(ptr->blocks[above]) &&
                            fastHash3f(wx, y, wz, seed_ + 4409u) > 0.9968f) {
                            ptr->blocks[idx] = BlockType::Crystal;
                        }
                    }
                }
            }
        }
    }

    // 3. Landmark Spawn Cave Tunnel & Campsite Torches near (0, y, 22)
    if (coord.cx >= -1 && coord.cx <= 1 && coord.cz >= 0 && coord.cz <= 3) {
        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                int wx = baseX + lx;
                int wz = baseZ + lz;
                if (wz >= 16 && wz <= 52 && std::abs(wx) <= 12) {
                    float t = static_cast<float>(wz - 16) / 36.0f;
                    float cx = std::sin(t * 2.5f) * 4.0f;
                    float cy = static_cast<float>(SEA_LEVEL + 9) - t * 14.0f;
                    float rad = 3.6f + std::sin(t * PI) * 1.6f;
                    for (int y = SEA_LEVEL - 14; y <= SEA_LEVEL + 18; ++y) {
                        if (y < 2 || y >= CHUNK_HEIGHT) continue;
                        float dx = static_cast<float>(wx) - cx;
                        float dy = (static_cast<float>(y) - cy) * 1.12f;
                        float d = std::sqrt(dx * dx + dy * dy);
                        size_t idx = Chunk::localIndex(lx, y, lz);
                        if (ptr->blocks[idx] == BlockType::Water) continue;
                        if (d <= rad) {
                            // Never punch underwater air holes into adjacent water columns
                            bool nearWater = false;
                            if (y <= SEA_LEVEL + 1) {
                                for (int dz = -1; dz <= 1 && !nearWater; ++dz) {
                                    for (int dx = -1; dx <= 1 && !nearWater; ++dx) {
                                        int nlx = std::clamp(lx + dx, 0, CHUNK_SIZE - 1);
                                        int nlz = std::clamp(lz + dz, 0, CHUNK_SIZE - 1);
                                        if (ptr->blocks[Chunk::localIndex(nlx, std::min(y, SEA_LEVEL), nlz)] == BlockType::Water) {
                                            nearWater = true;
                                        }
                                    }
                                }
                            }
                            if (!nearWater) {
                                ptr->blocks[idx] = BlockType::Air;
                            }
                        } else if (d <= rad + 1.4f && ptr->blocks[idx] != BlockType::Air) {
                            ptr->blocks[idx] = BlockType::DeepSlate;
                        }
                    }
                }
            }
        }
    }

    // 3B. Watertight seabed verification: seal any submerged Air voxel touching Water (zero floating ocean artifacts!)
    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            int maxYCheck = std::min(SEA_LEVEL, CHUNK_HEIGHT - 2);
            for (int y = 4; y <= maxYCheck; ++y) {
                size_t idx = Chunk::localIndex(lx, y, lz);
                if (ptr->blocks[idx] != BlockType::Air) continue;
                bool touchesWater =
                    (y + 1 < CHUNK_HEIGHT && ptr->blocks[Chunk::localIndex(lx, y + 1, lz)] == BlockType::Water) ||
                    (lx > 0              && ptr->blocks[Chunk::localIndex(lx - 1, y, lz)] == BlockType::Water) ||
                    (lx + 1 < CHUNK_SIZE && ptr->blocks[Chunk::localIndex(lx + 1, y, lz)] == BlockType::Water) ||
                    (lz > 0              && ptr->blocks[Chunk::localIndex(lx, y, lz - 1)] == BlockType::Water) ||
                    (lz + 1 < CHUNK_SIZE && ptr->blocks[Chunk::localIndex(lx, y, lz + 1)] == BlockType::Water);
                if (touchesWater) {
                    ptr->blocks[idx] = BlockType::Stone;
                }
            }
        }
    }

    auto placeIfInChunk = [&](int wx, int y, int wz, BlockType b) {
        if (floorDiv(wx, CHUNK_SIZE) == coord.cx && floorDiv(wz, CHUNK_SIZE) == coord.cz &&
            y >= 0 && y < CHUNK_HEIGHT) {
            ptr->blocks[Chunk::localIndex(floorMod(wx, CHUNK_SIZE), y, floorMod(wz, CHUNK_SIZE))] = b;
        }
    };
    int campL = sampleTerrainHeight(-4, 14);
    int campR = sampleTerrainHeight(4, 14);
    placeIfInChunk(-4, campL + 1, 14, BlockType::Timber);
    placeIfInChunk(-4, campL + 2, 14, BlockType::Torch);
    placeIfInChunk( 4, campR + 1, 14, BlockType::Timber);
    placeIfInChunk( 4, campR + 2, 14, BlockType::Torch);
    placeIfInChunk( 2, SEA_LEVEL - 4, 44, BlockType::Magma);
    placeIfInChunk( 1, SEA_LEVEL - 4, 44, BlockType::Magma);
    placeIfInChunk(-2, SEA_LEVEL + 2, 33, BlockType::Crystal);

    // 4. Seamless Cross-Chunk Biome-Aware Trees (Dense Forests, Alpine Pines, Sparse Meadow/Plains Oaks)
    for (int wz = baseZ - 2; wz < baseZ + CHUNK_SIZE + 2; ++wz) {
        for (int wx = baseX - 2; wx < baseX + CHUNK_SIZE + 2; ++wx) {
            // Keep spawn clearing open
            if (wx * wx + wz * wz < 144) continue;

            // Fast hash pre-filter before calling full FastNoiseLite column sample!
            float treeRoll = fastHash2f(wx, wz, seed_ + 2027u);
            if (treeRoll < 0.942f) continue;

            TerrainSample ts = sampleTerrainColumn(wx, wz);
            int surfY = ts.height;
            if (surfY <= SEA_LEVEL + 2 || surfY >= 74) continue;

            float threshold = 0.999f;
            bool isConifer = false;
            if (ts.biome == BiomeType::Forest) {
                threshold = 0.950f - ts.moisture * 0.007f; // Dense woodland
                isConifer = (ts.temp < 0.42f);
            } else if (ts.biome == BiomeType::Meadow) {
                threshold = 0.986f; // Open alpine meadow with scattered pines
                isConifer = true;
            } else if (ts.biome == BiomeType::Plains) {
                threshold = 0.992f; // Wide sunlit plains with occasional lone oaks
                isConifer = false;
            } else {
                continue;
            }
            if (treeRoll < threshold) continue;

            int trunkH = isConifer
                ? (6 + (static_cast<int>(fastHash2u(wx, wz, seed_ + 3109u)) % 4))
                : (4 + (static_cast<int>(fastHash2u(wx, wz, seed_ + 3109u)) % 3));

            // Place trunk intersecting this chunk
            if (wx >= baseX && wx < baseX + CHUNK_SIZE && wz >= baseZ && wz < baseZ + CHUNK_SIZE) {
                int lx = wx - baseX;
                int lz = wz - baseZ;
                if (ptr->blocks[Chunk::localIndex(lx, surfY, lz)] == BlockType::Grass) {
                    for (int h = 1; h <= trunkH && surfY + h < CHUNK_HEIGHT; ++h) {
                        ptr->blocks[Chunk::localIndex(lx, surfY + h, lz)] = BlockType::Timber;
                    }
                }
            }

            // Place canopy leaves intersecting this chunk
            int leafStart = isConifer ? (trunkH - 3) : (trunkH - 1);
            int leafEnd   = isConifer ? (trunkH + 2) : (trunkH + 2);
            for (int ly = leafStart; ly <= leafEnd; ++ly) {
                int wy = surfY + ly;
                if (wy < 0 || wy >= CHUNK_HEIGHT) continue;
                int rad = isConifer
                    ? ((ly >= trunkH + 1) ? 1 : (ly <= trunkH - 1 ? 2 : 1))
                    : ((ly == trunkH + 2) ? 1 : 2);
                for (int dz = -rad; dz <= rad; ++dz) {
                    for (int dx = -rad; dx <= rad; ++dx) {
                        if (std::abs(dx) == rad && std::abs(dz) == rad && ly > trunkH) continue;
                        if (dx == 0 && dz == 0 && ly <= trunkH) continue;
                        int nx = wx + dx;
                        int nz = wz + dz;
                        if (nx < baseX || nx >= baseX + CHUNK_SIZE ||
                            nz < baseZ || nz >= baseZ + CHUNK_SIZE) {
                            continue;
                        }
                        size_t idx = Chunk::localIndex(nx - baseX, wy, nz - baseZ);
                        if (ptr->blocks[idx] == BlockType::Air) {
                            ptr->blocks[idx] = BlockType::Leaves;
                        }
                    }
                }
            }
        }
    }

    ptr->generated = true;
    ptr->meshDirty = true;
    Chunk& ref = *ptr;
    chunks_.emplace(coord, std::move(ptr));
    return ref;
}

void VoxelWorld::ensureSpiralRing(int renderRadius) {
    if (spiralRadius_ == renderRadius) return;
    spiralRadius_ = renderRadius;
    spiralOffsets_.clear();

    const int loadDistSq = renderRadius * renderRadius + 2;
    spiralOffsets_.reserve(static_cast<size_t>(3.2f * renderRadius * renderRadius) + 16);
    for (int dz = -renderRadius; dz <= renderRadius; ++dz) {
        for (int dx = -renderRadius; dx <= renderRadius; ++dx) {
            if (dx * dx + dz * dz <= loadDistSq) {
                spiralOffsets_.push_back({dx, dz});
            }
        }
    }
    std::sort(spiralOffsets_.begin(), spiralOffsets_.end(),
              [](const ChunkCoord& a, const ChunkCoord& b) {
                  return (a.cx * a.cx + a.cz * a.cz) < (b.cx * b.cx + b.cz * b.cz);
              });
    spiralCursor_ = 0;
}

void VoxelWorld::prewarmAround(const vec3& spawnPos, LightEngine& lightEngine, int radius) {
    int pcx = floorDiv(static_cast<int>(std::floor(spawnPos.x)), CHUNK_SIZE);
    int pcz = floorDiv(static_cast<int>(std::floor(spawnPos.z)), CHUNK_SIZE);
    int prewarmR = std::min(radius, 8); // Prewarm inner 8-chunk ring immediately; outer rings stream live!
    ensureSpiralRing(radius);

    std::vector<ChunkCoord> created;
    for (const ChunkCoord& off : spiralOffsets_) {
        if (off.cx * off.cx + off.cz * off.cz > prewarmR * prewarmR + 2) break;
        ChunkCoord c{pcx + off.cx, pcz + off.cz};
        if (!hasChunk(c)) {
            generateChunk(c);
            created.push_back(c);
        }
    }
    for (ChunkCoord c : created) {
        lightEngine.lightNewChunk(*this, c);
    }
    lastStreamCenter_ = {pcx, pcz};
    spiralCursor_ = 0;
}

void VoxelWorld::updateStreaming(const vec3& playerPos,
                                 LightEngine& lightEngine,
                                 int renderRadius,
                                 int maxGeneratePerFrame,
                                 std::vector<ChunkCoord>& unloadedOut) {
    renderRadius = std::clamp(renderRadius, MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE);
    int pcx = floorDiv(static_cast<int>(std::floor(playerPos.x)), CHUNK_SIZE);
    int pcz = floorDiv(static_cast<int>(std::floor(playerPos.z)), CHUNK_SIZE);

    ensureSpiralRing(renderRadius);
    ChunkCoord center{pcx, pcz};
    if (!(center == lastStreamCenter_)) {
        lastStreamCenter_ = center;
        spiralCursor_ = 0;
    }

    // 1. Unload far-away chunks beyond (renderRadius + 2) to keep RD 50 memory-bounded
    const int unloadDistSq = (renderRadius + 2) * (renderRadius + 2);
    for (auto it = chunks_.begin(); it != chunks_.end(); ) {
        int dx = it->first.cx - pcx;
        int dz = it->first.cz - pcz;
        if (dx * dx + dz * dz > unloadDistSq) {
            unloadedOut.push_back(it->first);
            it = chunks_.erase(it);
        } else {
            ++it;
        }
    }

    // 2. O(1) amortized nearest-first spiral chunk streaming (never sorts 10,000 coords per frame!)
    int generatedCount = 0;
    while (spiralCursor_ < spiralOffsets_.size() && generatedCount < maxGeneratePerFrame) {
        const ChunkCoord& off = spiralOffsets_[spiralCursor_++];
        ChunkCoord c{pcx + off.cx, pcz + off.cz};
        if (!hasChunk(c)) {
            generateChunk(c);
            lightEngine.lightNewChunk(*this, c);
            ++generatedCount;
        }
    }
}

Chunk* VoxelWorld::getChunk(ChunkCoord coord) {
    auto it = chunks_.find(coord);
    return (it != chunks_.end()) ? it->second.get() : nullptr;
}

const Chunk* VoxelWorld::getChunk(ChunkCoord coord) const {
    auto it = chunks_.find(coord);
    return (it != chunks_.end()) ? it->second.get() : nullptr;
}

BlockType VoxelWorld::getBlock(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_HEIGHT) return BlockType::Air;
    const Chunk* c = getChunk({floorDiv(x, CHUNK_SIZE), floorDiv(z, CHUNK_SIZE)});
    if (!c) return BlockType::Air;
    return c->blocks[Chunk::localIndex(floorMod(x, CHUNK_SIZE), y, floorMod(z, CHUNK_SIZE))];
}

void VoxelWorld::setBlockRaw(int x, int y, int z, BlockType b) {
    if (y < 0 || y >= CHUNK_HEIGHT) return;
    Chunk* c = getChunk({floorDiv(x, CHUNK_SIZE), floorDiv(z, CHUNK_SIZE)});
    if (!c) return;
    c->blocks[Chunk::localIndex(floorMod(x, CHUNK_SIZE), y, floorMod(z, CHUNK_SIZE))] = b;
    markVoxelAndBorderChunksDirty(x, z);
}

uint8_t VoxelWorld::getLightPacked(int x, int y, int z) const {
    if (y >= CHUNK_HEIGHT) return setSkyLight(0, 15);
    if (y < 0) return 0;
    const Chunk* c = getChunk({floorDiv(x, CHUNK_SIZE), floorDiv(z, CHUNK_SIZE)});
    if (!c) return setSkyLight(0, 15);
    return c->light[Chunk::localIndex(floorMod(x, CHUNK_SIZE), y, floorMod(z, CHUNK_SIZE))];
}

uint8_t VoxelWorld::getSky(int x, int y, int z) const {
    return getSkyLight(getLightPacked(x, y, z));
}

uint8_t VoxelWorld::getBlockLightVal(int x, int y, int z) const {
    return getBlockLight(getLightPacked(x, y, z));
}

void VoxelWorld::setSky(int x, int y, int z, uint8_t v) {
    if (y < 0 || y >= CHUNK_HEIGHT) return;
    Chunk* c = getChunk({floorDiv(x, CHUNK_SIZE), floorDiv(z, CHUNK_SIZE)});
    if (!c) return;
    size_t idx = Chunk::localIndex(floorMod(x, CHUNK_SIZE), y, floorMod(z, CHUNK_SIZE));
    uint8_t next = setSkyLight(c->light[idx], v);
    if (c->light[idx] != next) {
        c->light[idx] = next;
        markVoxelAndBorderChunksDirty(x, z);
    }
}

void VoxelWorld::setBlockLightVal(int x, int y, int z, uint8_t v) {
    if (y < 0 || y >= CHUNK_HEIGHT) return;
    Chunk* c = getChunk({floorDiv(x, CHUNK_SIZE), floorDiv(z, CHUNK_SIZE)});
    if (!c) return;
    size_t idx = Chunk::localIndex(floorMod(x, CHUNK_SIZE), y, floorMod(z, CHUNK_SIZE));
    uint8_t next = setBlockLight(c->light[idx], v);
    if (c->light[idx] != next) {
        c->light[idx] = next;
        markVoxelAndBorderChunksDirty(x, z);
    }
}

void VoxelWorld::markChunkDirty(ChunkCoord coord) {
    if (Chunk* c = getChunk(coord)) {
        c->meshDirty = true;
    }
}

void VoxelWorld::markAllChunksDirty() {
    for (auto& [coord, chunk] : chunks_) {
        chunk->meshDirty = true;
    }
}

void VoxelWorld::markVoxelAndBorderChunksDirty(int x, int z) {
    int cx = floorDiv(x, CHUNK_SIZE);
    int cz = floorDiv(z, CHUNK_SIZE);
    int lx = floorMod(x, CHUNK_SIZE);
    int lz = floorMod(z, CHUNK_SIZE);

    markChunkDirty({cx, cz});
    if (lx == 0)              markChunkDirty({cx - 1, cz});
    if (lx == CHUNK_SIZE - 1) markChunkDirty({cx + 1, cz});
    if (lz == 0)              markChunkDirty({cx, cz - 1});
    if (lz == CHUNK_SIZE - 1) markChunkDirty({cx, cz + 1});
}

std::vector<ChunkCoord> VoxelWorld::popDirtyChunks(const vec3& playerPos, size_t maxCount) {
    int pcx = floorDiv(static_cast<int>(std::floor(playerPos.x)), CHUNK_SIZE);
    int pcz = floorDiv(static_cast<int>(std::floor(playerPos.z)), CHUNK_SIZE);

    struct DirtyItem {
        ChunkCoord coord;
        int distSq;
    };
    std::vector<DirtyItem> list;
    for (const auto& [coord, chunk] : chunks_) {
        if (chunk->meshDirty && chunk->lit) {
            int dx = coord.cx - pcx;
            int dz = coord.cz - pcz;
            list.push_back({coord, dx * dx + dz * dz});
        }
    }
    if (list.empty()) return {};

    size_t take = std::min(maxCount, list.size());
    if (take < list.size()) {
        std::nth_element(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(take), list.end(),
                         [](const DirtyItem& a, const DirtyItem& b) { return a.distSq < b.distSq; });
        std::sort(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(take),
                  [](const DirtyItem& a, const DirtyItem& b) { return a.distSq < b.distSq; });
    } else {
        std::sort(list.begin(), list.end(),
                  [](const DirtyItem& a, const DirtyItem& b) { return a.distSq < b.distSq; });
    }

    std::vector<ChunkCoord> out;
    out.reserve(take);
    for (size_t i = 0; i < take; ++i) {
        if (Chunk* c = getChunk(list[i].coord)) {
            c->meshDirty = false;
        }
        out.push_back(list[i].coord);
    }
    return out;
}

int VoxelWorld::findSurfaceY(int x, int z) const {
    for (int y = CHUNK_HEIGHT - 1; y >= 0; --y) {
        BlockType b = getBlock(x, y, z);
        if (b != BlockType::Air && b != BlockType::Leaves && b != BlockType::Torch) {
            return y;
        }
    }
    return sampleTerrainHeight(x, z);
}

std::optional<RaycastHit> VoxelWorld::raycast(const vec3& origin,
                                              const vec3& direction,
                                              float maxDistance) const {
    vec3 dir = normalize(direction);
    ivec3 voxel(static_cast<int>(std::floor(origin.x)),
                static_cast<int>(std::floor(origin.y)),
                static_cast<int>(std::floor(origin.z)));

    ivec3 step(dir.x >= 0.0f ? 1 : -1,
               dir.y >= 0.0f ? 1 : -1,
               dir.z >= 0.0f ? 1 : -1);

    auto safeInv = [](float v) {
        return (std::abs(v) < 1e-6f) ? std::numeric_limits<float>::infinity() : std::abs(1.0f / v);
    };

    vec3 tDelta(safeInv(dir.x), safeInv(dir.y), safeInv(dir.z));
    vec3 tMax;

    tMax.x = (dir.x >= 0.0f)
        ? (static_cast<float>(voxel.x + 1) - origin.x) * tDelta.x
        : (origin.x - static_cast<float>(voxel.x)) * tDelta.x;
    tMax.y = (dir.y >= 0.0f)
        ? (static_cast<float>(voxel.y + 1) - origin.y) * tDelta.y
        : (origin.y - static_cast<float>(voxel.y)) * tDelta.y;
    tMax.z = (dir.z >= 0.0f)
        ? (static_cast<float>(voxel.z + 1) - origin.z) * tDelta.z
        : (origin.z - static_cast<float>(voxel.z)) * tDelta.z;

    ivec3 lastNormal(0, 1, 0);
    float dist = 0.0f;

    while (dist <= maxDistance) {
        if (voxel.y >= 0 && voxel.y < CHUNK_HEIGHT) {
            BlockType b = getBlock(voxel.x, voxel.y, voxel.z);
            if (b != BlockType::Air && b != BlockType::Water) {
                return RaycastHit{voxel, lastNormal, dist};
            }
        }

        if (tMax.x < tMax.y) {
            if (tMax.x < tMax.z) {
                voxel.x += step.x;
                dist = tMax.x;
                tMax.x += tDelta.x;
                lastNormal = ivec3(-step.x, 0, 0);
            } else {
                voxel.z += step.z;
                dist = tMax.z;
                tMax.z += tDelta.z;
                lastNormal = ivec3(0, 0, -step.z);
            }
        } else {
            if (tMax.y < tMax.z) {
                voxel.y += step.y;
                dist = tMax.y;
                tMax.y += tDelta.y;
                lastNormal = ivec3(0, -step.y, 0);
            } else {
                voxel.z += step.z;
                dist = tMax.z;
                tMax.z += tDelta.z;
                lastNormal = ivec3(0, 0, -step.z);
            }
        }
    }
    return std::nullopt;
}

TimeOfDayState VoxelWorld::evaluateTimeOfDay(float timeOfDay) {
    float t = timeOfDay - std::floor(timeOfDay);
    float angle = t * TWO_PI; // Continuous 360-degree celestial orbit

    // Full 360-degree tilted orbital plane around the sky sphere
    float sunX = std::cos(angle);
    float sunY = std::sin(angle);
    float sunZ = 0.34f * std::sin(angle + 0.45f) + 0.22f;

    constexpr size_t KEY_COUNT = sizeof(SKY_KEYS) / sizeof(SKY_KEYS[0]);
    size_t idx = 0;
    while (idx + 2 < KEY_COUNT && t > SKY_KEYS[idx + 1].t) {
        ++idx;
    }
    const SkyKeyframe& a = SKY_KEYS[idx];
    const SkyKeyframe& b = SKY_KEYS[idx + 1];
    float span = std::max(b.t - a.t, 1e-5f);
    float u = std::clamp((t - a.t) / span, 0.0f, 1.0f);
    float s = u * u * (3.0f - 2.0f * u);

    TimeOfDayState out{};
    out.sunDir        = normalize(vec3(sunX, sunY, sunZ));
    out.skyZenith     = mix(a.zenith,     b.zenith,     s);
    out.skyHorizon    = mix(a.horizon,    b.horizon,    s);
    out.sunColor      = mix(a.sunColor,   b.sunColor,   s);
    out.skyAmbient    = mix(a.skyAmbient, b.skyAmbient, s);
    out.mieGlow       = mix(a.mieGlow,    b.mieGlow,    s);
    out.skyIntensity  = a.skyIntensity  + (b.skyIntensity  - a.skyIntensity)  * s;
    out.moonIntensity = a.moonIntensity + (b.moonIntensity - a.moonIntensity) * s;
    out.warmGlow      = a.warmGlow      + (b.warmGlow      - a.warmGlow)      * s;

    if (t < 0.06f || t >= 0.94f)       out.phaseName = "Sunrise";
    else if (t < 0.19f)                out.phaseName = "Morning";
    else if (t < 0.35f)                out.phaseName = "Noon";
    else if (t < 0.46f)                out.phaseName = "Evening";
    else if (t < 0.56f)                out.phaseName = "Sunset";
    else                               out.phaseName = "Night";

    return out;
}

} // namespace umbra
