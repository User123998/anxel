# Umbra Voxel Engine — Pure C++20 & Vulkan 1.2

A standalone, **zero-GLM** C++20 / Vulkan 1.2 infinite voxel engine featuring:

- **FastNoiseLite 1.0.1 Multi-Biome Worldgen (`src/math/noise/noise.h`)**: 128-block-tall worlds with **Oceans, Coastal Beaches, Domain-Warped Rivers, Plains, Alpine Meadows, Dense Forests, Towering Mountains, Snowy Peaks**, plus **3D Spaghetti Tunnels & Vaulted Cheese Caverns** with magma veins and glowing crystal ceilings.
- **Zero-Remesh 3D Occluded Dynamic Lighting**: Held light sources (`Torch`, `Glowstone`, `Magma`, `Crystal`) run an L1-cached 18-neighbor Euclidean BFS into a **32×32×32 `R8_UNORM` Vulkan 3D texture** uploaded asynchronously in the frame command buffer. Walking with a torch triggers **zero chunk remeshing and zero `vkDeviceWaitIdle` stalls**, delivering silky-smooth radial firelight with 100% solid-wall occlusion.
- **Smooth Binary Greedy Meshing**: 64-bit bitwise occupancy culling (`std::countr_zero`) + smooth-lighting-preserving 2D greedy quad merging cuts vertex/triangle counts by **70–85%**, enabling **Render Distance up to 50 chunks** (`800`-block radius).
- **Adjustable Atmospheric Haze + Seamless Render-Distance Border Fog**: Crisp foreground/midground visibility (default 22% haze) paired with a C2-smooth horizon border fade that blends distant chunks invisibly into the exact 360° sky gradient.
- **Interactive Pause Menu & Live Video/World Settings UI (`ESC`)**: Adjust **Render Distance (4–50)**, **Graphics Quality (`LOW` / `MED` / `HIGH` / `ULTRA`)**, **Fog Density (0–100%)**, **Border Fog**, **Smooth Lighting**, **Greedy Meshing**, and **FOV (60–105°)** live in-game.
- **Real PNG Texture Pipeline (`assets/texture/block/`, `assets/texture/items/`, `src/texture_loader.hpp`)**: Zero-dependency C++20 PNG decoder loads all 18 block textures and 14 item icons directly into Vulkan `VK_IMAGE_VIEW_TYPE_2D_ARRAY` (`sampler2DArray`) textures with nearest-neighbor crisp pixel sampling. Drop in custom PNG texture packs (16×16, 32×32, 64×64) without recompiling.
- **Crystal-Clear 360° Sky & Natural Area Darkness**: Zero clouds, zero horizon seam, full 360° sun/moon orbit, and zero-anchored light curves so sealed caves are truly pitch black.

## Asset & Directory Layout
```text
vulkan_voxel_engine/
├── assets/
│   ├── shaders/            # GLSL 450 sources (*.vert, *.frag) -> compiled in-place to *.spv
│   └── texture/
│       ├── block/          # 18 block face PNG textures (grass_top, grass_side, stone, torch_side, ...)
│       └── items/          # 14 inventory & hotbar item icon PNGs (grass, dirt, torch, crystal, ...)
├── src/                    # Pure C++20 engine + zero-dependency PNG decoder (texture_loader.hpp)
└── compile.sh              # Compiles assets/shaders/*, builds executable, copies assets/ -> build/assets/
```

## Quick Build & Run

### Linux / macOS / Windows (MSYS2 MinGW64 + LunarG Vulkan SDK)
```bash
chmod +x compile.sh
./compile.sh --release --run
```
`compile.sh` compiles every shader inside `assets/shaders/` to SPIR-V (`*.spv`), links `build/umbra_voxel`, and copies the complete `assets/` folder into `build/assets/`.

## Controls
- `WASD` + Mouse — Move & Look (`Ctrl` sprint, `G` toggle Fly / Walk)
- `ESC` — Pause Menu & Live Settings UI (Render Distance up to 50, Graphics Quality, Fog, FOV)
- `I` / `Tab` — Open / Close 36-Slot Inventory (click slots to swap)
- `1–9` / Mouse Wheel — Switch hotbar item (dynamic light activates ONLY when holding a light source!)
- `Left Click` / `Right Click` — Break / Place block
- `N` — Cycle Sunrise / Morning / Noon / Evening / Sunset / Night
- `P` / `T` — Pause / Speed up day-night cycle
- `V` — Cycle Lightfield Debug Views (Combined / Sky / Block / AO)
