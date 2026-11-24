#include "camera.hpp"
#include "hud.hpp"
#include "inventory.hpp"
#include "lighting.hpp"
#include "math/lighting_math.hpp"
#include "mesher.hpp"
#include "vulkan_engine.hpp"
#include "world.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

struct AppContext {
    umbra::VulkanEngine*    engine    = nullptr;
    umbra::VoxelWorld*      world     = nullptr;
    umbra::LightEngine*     light     = nullptr;
    umbra::Camera*          camera    = nullptr;
    umbra::PlayerInventory* inventory = nullptr;
    umbra::HudBuilder*      hud       = nullptr;
    umbra::EngineSettings*  settings  = nullptr;

    umbra::UiScreen uiScreen = umbra::UiScreen::None;

    bool   cursorCaptured = false;
    bool   mouseLeftDown  = false;
    double lastMouseX     = 0.0;
    double lastMouseY     = 0.0;
    double curMouseX      = 0.0;
    double curMouseY      = 0.0;
    bool   firstMouse     = true;

    float timeOfDay     = 0.015f; // Crystal-clear rose-gold Sunrise
    float timeSpeed     = 0.008f; // Smooth natural 360-degree day/night cycle
    bool  timePaused    = false;
    bool  dynLightDirty = true;
    bool  showDebugger  = false;  // Toggled via Shift+F3 (or F3)
    int   debugViewMode = 0;      // 0=Combined, 1=SkyLight, 2=BlockLight, 3=AO
};

void setCursorCapture(GLFWwindow* window, AppContext* ctx, bool capture) {
    ctx->cursorCaptured = capture;
    ctx->firstMouse     = true;
    glfwSetInputMode(window, GLFW_CURSOR, capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void getScaledMouse(GLFWwindow* window, AppContext* ctx, double& outX, double& outY, int& outW, int& outH) {
    int winW = 1, winH = 1;
    glfwGetWindowSize(window, &winW, &winH);
    VkExtent2D fb = ctx->engine->extent();
    outW = static_cast<int>(fb.width);
    outH = static_cast<int>(fb.height);
    outX = ctx->curMouseX * (static_cast<double>(fb.width)  / std::max(winW, 1));
    outY = ctx->curMouseY * (static_cast<double>(fb.height) / std::max(winH, 1));
}

void framebufferResizeCallback(GLFWwindow* window, int /*w*/, int /*h*/) {
    auto* ctx = static_cast<AppContext*>(glfwGetWindowUserPointer(window));
    if (ctx && ctx->engine) {
        ctx->engine->markFramebufferResized();
    }
}

void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    auto* ctx = static_cast<AppContext*>(glfwGetWindowUserPointer(window));
    if (!ctx) return;

    ctx->curMouseX = xpos;
    ctx->curMouseY = ypos;

    // Live slider dragging inside Settings UI
    if (ctx->uiScreen == umbra::UiScreen::Settings && ctx->mouseLeftDown) {
        double sx = 0.0, sy = 0.0;
        int fbW = 1, fbH = 1;
        getScaledMouse(window, ctx, sx, sy, fbW, fbH);
        bool needRemesh = false;
        ctx->hud->handleUiMouse(ctx->uiScreen, *ctx->settings, sx, sy, fbW, fbH, true, needRemesh);
        if (needRemesh) {
            ctx->world->markAllChunksDirty();
        }
        return;
    }

    if (!ctx->cursorCaptured || ctx->inventory->isOpen() || ctx->uiScreen != umbra::UiScreen::None) {
        return;
    }

    if (ctx->firstMouse) {
        ctx->lastMouseX = xpos;
        ctx->lastMouseY = ypos;
        ctx->firstMouse = false;
        return;
    }
    double dx = xpos - ctx->lastMouseX;
    double dy = ypos - ctx->lastMouseY;
    ctx->lastMouseX = xpos;
    ctx->lastMouseY = ypos;
    ctx->camera->rotate(static_cast<float>(dx), static_cast<float>(dy));
}

void scrollCallback(GLFWwindow* window, double /*xoffset*/, double yoffset) {
    auto* ctx = static_cast<AppContext*>(glfwGetWindowUserPointer(window));
    if (!ctx || !ctx->inventory || ctx->inventory->isOpen() || ctx->uiScreen != umbra::UiScreen::None) {
        return;
    }
    if (yoffset > 0.0) {
        ctx->inventory->scrollHotbar(-1);
        ctx->dynLightDirty = true;
    } else if (yoffset < 0.0) {
        ctx->inventory->scrollHotbar(+1);
        ctx->dynLightDirty = true;
    }
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/) {
    auto* ctx = static_cast<AppContext*>(glfwGetWindowUserPointer(window));
    if (!ctx) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        ctx->mouseLeftDown = (action == GLFW_PRESS);
    }
    if (action != GLFW_PRESS) return;

    // 1. Pause & Settings UI interaction
    if (ctx->uiScreen != umbra::UiScreen::None) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            double sx = 0.0, sy = 0.0;
            int fbW = 1, fbH = 1;
            getScaledMouse(window, ctx, sx, sy, fbW, fbH);
            bool needRemesh = false;
            umbra::UiAction act = ctx->hud->handleUiMouse(
                ctx->uiScreen, *ctx->settings, sx, sy, fbW, fbH, false, needRemesh);
            if (needRemesh) {
                ctx->world->markAllChunksDirty();
            }
            if (act == umbra::UiAction::Resume) {
                ctx->uiScreen = umbra::UiScreen::None;
                setCursorCapture(window, ctx, true);
            } else if (act == umbra::UiAction::OpenSettings) {
                ctx->uiScreen = umbra::UiScreen::Settings;
            } else if (act == umbra::UiAction::BackToPause) {
                ctx->uiScreen = umbra::UiScreen::Pause;
            } else if (act == umbra::UiAction::Quit) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        }
        return;
    }

    // 2. Interactive slot swapping when Inventory modal is open
    if (ctx->inventory->isOpen()) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            double sx = 0.0, sy = 0.0;
            int fbW = 1, fbH = 1;
            getScaledMouse(window, ctx, sx, sy, fbW, fbH);
            int slot = ctx->hud->hitTestSlot(*ctx->inventory, sx, sy, fbW, fbH);
            if (slot >= 0) {
                ctx->inventory->clickSlot(slot);
                ctx->dynLightDirty = true;
            }
        }
        return;
    }

    if (!ctx->cursorCaptured) {
        setCursorCapture(window, ctx, true);
        return;
    }

    // 3. Active voxel break/place
    auto hit = ctx->world->raycast(ctx->camera->position, ctx->camera->forward(), 10.0f);
    if (!hit.has_value()) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        umbra::ivec3 p = hit->blockPos;
        if (p.y > 1 && p.y < umbra::CHUNK_HEIGHT) {
            umbra::BlockType oldB = ctx->world->getBlock(p.x, p.y, p.z);
            if (oldB != umbra::BlockType::Air) {
                // If breaking a block underwater or beside water at/below sea level, refill naturally with Water
                // so digging/removing blocks in water NEVER leaves an ugly hollow air hole!
                bool adjacentWater = (p.y <= umbra::SEA_LEVEL) && (
                    ctx->world->getBlock(p.x, p.y + 1, p.z) == umbra::BlockType::Water ||
                    ctx->world->getBlock(p.x + 1, p.y, p.z) == umbra::BlockType::Water ||
                    ctx->world->getBlock(p.x - 1, p.y, p.z) == umbra::BlockType::Water ||
                    ctx->world->getBlock(p.x, p.y, p.z + 1) == umbra::BlockType::Water ||
                    ctx->world->getBlock(p.x, p.y, p.z - 1) == umbra::BlockType::Water
                );
                umbra::BlockType replacement = adjacentWater ? umbra::BlockType::Water : umbra::BlockType::Air;
                ctx->world->setBlockRaw(p.x, p.y, p.z, replacement);
                ctx->light->onBlockChanged(*ctx->world, p.x, p.y, p.z, oldB, replacement);
                ctx->inventory->addBlock(oldB, 1);
                ctx->dynLightDirty = true;
            }
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        umbra::ivec3 p = hit->blockPos + hit->faceNormal;
        if (p.y >= 1 && p.y < umbra::CHUNK_HEIGHT) {
            umbra::BlockType oldB = ctx->world->getBlock(p.x, p.y, p.z);
            if (oldB == umbra::BlockType::Air || oldB == umbra::BlockType::Water) {
                // Prevent dry torches from erasing water voxels into air cutouts (use Glowstone/Magma/Crystal underwater!)
                if (oldB == umbra::BlockType::Water && ctx->inventory->heldBlock() == umbra::BlockType::Torch) {
                    return;
                }
                umbra::BlockType placeBlock = umbra::BlockType::Air;
                if (ctx->inventory->consumeHeld(placeBlock)) {
                    ctx->world->setBlockRaw(p.x, p.y, p.z, placeBlock);
                    ctx->light->onBlockChanged(*ctx->world, p.x, p.y, p.z, oldB, placeBlock);
                    ctx->dynLightDirty = true;
                }
            }
        }
    }
}

void keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int mods) {
    auto* ctx = static_cast<AppContext*>(glfwGetWindowUserPointer(window));
    if (!ctx || action != GLFW_PRESS) return;

    // Shift+F3 (or F3) toggles the real-time Performance & World Debugger overlay
    if (key == GLFW_KEY_F3) {
        (void)mods;
        ctx->showDebugger = !ctx->showDebugger;
        return;
    }

    // ESC opens/closes Pause Menu & Settings cleanly
    if (key == GLFW_KEY_ESCAPE) {
        if (ctx->inventory->isOpen()) {
            ctx->inventory->setOpen(false);
            setCursorCapture(window, ctx, true);
        } else if (ctx->uiScreen == umbra::UiScreen::Settings) {
            ctx->uiScreen = umbra::UiScreen::Pause;
        } else if (ctx->uiScreen == umbra::UiScreen::Pause) {
            ctx->uiScreen = umbra::UiScreen::None;
            setCursorCapture(window, ctx, true);
        } else {
            ctx->uiScreen = umbra::UiScreen::Pause;
            setCursorCapture(window, ctx, false);
        }
        return;
    }

    if (ctx->uiScreen != umbra::UiScreen::None) return;

    // Toggle Inventory overlay (I or Tab)
    if (key == GLFW_KEY_I || key == GLFW_KEY_TAB) {
        ctx->inventory->toggleOpen();
        setCursorCapture(window, ctx, !ctx->inventory->isOpen());
        ctx->dynLightDirty = true;
        return;
    }

    // Hotbar slot selection 1..9 (immediately updates zero-remesh 3D held-block light!)
    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
        ctx->inventory->selectHotbar(key - GLFW_KEY_1);
        ctx->dynLightDirty = true;
        return;
    }

    if (ctx->inventory->isOpen()) return;

    switch (key) {
        case GLFW_KEY_P: ctx->timePaused = !ctx->timePaused; break;
        case GLFW_KEY_T:
            ctx->timeSpeed = (ctx->timeSpeed < 0.03f) ? 0.065f : 0.008f;
            break;
        case GLFW_KEY_N:
            if (ctx->timeOfDay < 0.07f || ctx->timeOfDay >= 0.90f) ctx->timeOfDay = 0.125f;
            else if (ctx->timeOfDay < 0.19f)                       ctx->timeOfDay = 0.250f;
            else if (ctx->timeOfDay < 0.34f)                       ctx->timeOfDay = 0.410f;
            else if (ctx->timeOfDay < 0.45f)                       ctx->timeOfDay = 0.495f;
            else if (ctx->timeOfDay < 0.60f)                       ctx->timeOfDay = 0.750f;
            else                                                   ctx->timeOfDay = 0.005f;
            break;
        case GLFW_KEY_V:
            ctx->debugViewMode = (ctx->debugViewMode + 1) % 4;
            break;
        case GLFW_KEY_G:
            ctx->camera->flyMode = !ctx->camera->flyMode;
            break;
        default:
            break;
    }
}

} // namespace

int main() {
    if (!glfwInit()) {
        std::cerr << "[FAIL] Failed to initialize GLFW.\n";
#ifdef _WIN32
        MessageBoxA(nullptr, "Failed to initialize GLFW.\nMake sure your display and graphics drivers are functional.", "Umbra Voxel Error", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(
        1440, 900,
        "Umbra Voxel — Pure C++20 Infinite Vulkan 1.2 Engine (FastNoiseLite + Binary Greedy + RD 50)",
        nullptr, nullptr
    );
    if (!window) {
        std::cerr << "[FAIL] Failed to create GLFW window.\n";
#ifdef _WIN32
        MessageBoxA(nullptr, "Failed to create GLFW window.\nMake sure your GPU supports Vulkan 1.2 and graphics drivers are up to date.", "Umbra Voxel Error", MB_OK | MB_ICONERROR);
#endif
        glfwTerminate();
        return 1;
    }

    try {
        umbra::EngineSettings  settings{};
        umbra::VoxelWorld      world(1337u);
        umbra::LightEngine     lightEngine;
        umbra::VulkanEngine    renderer;
        umbra::Camera          camera;
        umbra::PlayerInventory inventory;
        umbra::HudBuilder      hudBuilder;

        int spawnSurface = world.sampleTerrainHeight(0, -10);
        camera.position = umbra::vec3(
            0.5f,
            static_cast<float>(std::max(spawnSurface, umbra::SEA_LEVEL)) + 6.0f,
            -10.5f
        );
        camera.yaw   = 90.0f;
        camera.pitch = -9.0f;
        camera.fov   = settings.fov;

        int prewarmRadius = std::min(settings.renderDistance, 8);
        std::cout << "[UMBRA] Prewarming initial FastNoiseLite biome chunks (radius "
                  << prewarmRadius << ", streaming up to RD " << settings.renderDistance << ")...\n";
        world.prewarmAround(camera.position, lightEngine, prewarmRadius);

        std::cout << "[UMBRA] Initializing Vulkan 1.2 renderer (Binary Greedy + 3D DynLight + Border Fog + Settings UI)...\n";
        renderer.init(window);

        auto initialDirty = world.popDirtyChunks(camera.position, 512);
        std::vector<std::pair<umbra::ChunkCoord, umbra::MeshData>> initialUploads;
        initialUploads.reserve(initialDirty.size());
        size_t totalTris = 0;
        for (umbra::ChunkCoord coord : initialDirty) {
            umbra::MeshData m = umbra::VoxelMesher::buildChunkMesh(
                world, coord, settings.smoothLighting, settings.greedyMeshing);
            totalTris += m.indices.size() / 3;
            initialUploads.emplace_back(coord, std::move(m));
        }
        renderer.syncChunkMeshes({}, initialUploads);
        std::cout << "[UMBRA] Ready: " << world.chunks().size() << " chunks, "
                  << totalTris << " binary-greedy triangles. Press ESC for Pause/Settings (RD up to 50).\n";

        AppContext ctx{};
        ctx.engine    = &renderer;
        ctx.world     = &world;
        ctx.light     = &lightEngine;
        ctx.camera    = &camera;
        ctx.inventory = &inventory;
        ctx.hud       = &hudBuilder;
        ctx.settings  = &settings;

        glfwSetWindowUserPointer(window, &ctx);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
        glfwSetCursorPosCallback(window, cursorPosCallback);
        glfwSetScrollCallback(window, scrollCallback);
        glfwSetMouseButtonCallback(window, mouseButtonCallback);
        glfwSetKeyCallback(window, keyCallback);

        double prevTime   = glfwGetTime();
        double fpsAccum   = 0.0;
        int    fpsFrames  = 0;
        int    displayFps = 60;
        umbra::DebugMetrics debugMetrics{};
        std::vector<umbra::ChunkCoord> unloadedChunks;
        std::vector<umbra::HudVertex>  hudVerts;
        hudVerts.reserve(12288);

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            double now = glfwGetTime();
            double rawFrameDt = now - prevTime;
            float dt = std::min(static_cast<float>(rawFrameDt), 0.05f);
            prevTime = now;
            debugMetrics.pushFrameMs(static_cast<float>(rawFrameDt * 1000.0));

            fpsAccum += dt;
            ++fpsFrames;
            if (fpsAccum >= 0.35) {
                displayFps = static_cast<int>(std::round(static_cast<double>(fpsFrames) / fpsAccum));
                fpsAccum   = 0.0;
                fpsFrames  = 0;
            }

            camera.fov = settings.fov;
            bool gameplayActive = (!inventory.isOpen() && ctx.uiScreen == umbra::UiScreen::None);

            if (gameplayActive) {
                float fwd = 0.0f, strafe = 0.0f, up = 0.0f;
                if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) fwd    += 1.0f;
                if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) fwd    -= 1.0f;
                if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) strafe += 1.0f;
                if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) strafe -= 1.0f;
                if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)      up += 1.0f;
                if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) up -= 1.0f;

                bool jump   = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
                bool sprint = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
                camera.update(dt, world, fwd, strafe, up, jump, sprint);
            } else {
                camera.refreshWaterState(world);
            }

            if (!ctx.timePaused && ctx.uiScreen == umbra::UiScreen::None) {
                ctx.timeOfDay = std::fmod(ctx.timeOfDay + dt * ctx.timeSpeed, 1.0f);
            }

            // Stream infinite FastNoiseLite biome chunks smoothly up to Render Distance 50
            double tStream0 = glfwGetTime();
            unloadedChunks.clear();
            world.updateStreaming(camera.position, lightEngine,
                                  settings.renderDistance, settings.chunksGenPerFrame(), unloadedChunks);
            double tStream1 = glfwGetTime();

            // ZERO-REMESH occluded dynamic player light -> writes 32 KB 3D texture asynchronously!
            umbra::ivec3 camVoxel(
                umbra::fastFloor(camera.position.x),
                umbra::fastFloor(camera.position.y),
                umbra::fastFloor(camera.position.z)
            );
            uint8_t heldEmit = inventory.heldLightEmission();
            lightEngine.updateDynamicLight(world, camVoxel, heldEmit, ctx.dynLightDirty);
            ctx.dynLightDirty = false;
            if (lightEngine.isDynTexDirty()) {
                renderer.updateDynamicLightVolume(lightEngine.dynVolumeData());
                lightEngine.clearDynTexDirty();
            }
            double tDyn1 = glfwGetTime();

            // Mesh only newly streamed or block-edited chunks (ZERO remeshing while walking with torch!)
            auto dirtyChunks = world.popDirtyChunks(camera.position, settings.chunksMeshPerFrame());
            if (!unloadedChunks.empty() || !dirtyChunks.empty()) {
                std::vector<std::pair<umbra::ChunkCoord, umbra::MeshData>> uploads;
                uploads.reserve(dirtyChunks.size());
                for (umbra::ChunkCoord c : dirtyChunks) {
                    uploads.emplace_back(
                        c, umbra::VoxelMesher::buildChunkMesh(
                            world, c, settings.smoothLighting, settings.greedyMeshing));
                }
                renderer.syncChunkMeshes(unloadedChunks, uploads);
            }
            double tMesh1 = glfwGetTime();

            // Build FrameUBO with translation-free rotation-only invViewProj (zero Sun/Moon vibration!)
            umbra::TimeOfDayState tod = umbra::VoxelWorld::evaluateTimeOfDay(ctx.timeOfDay);
            float renderDistBlocks = static_cast<float>(settings.renderDistance * umbra::CHUNK_SIZE);
            float farPlane         = std::max(460.0f, renderDistBlocks * 1.45f + 180.0f);
            float borderEnd        = std::max(44.0f, renderDistBlocks - 10.0f);
            float borderStart      = std::max(24.0f, borderEnd * 0.74f);

            umbra::mat4 proj     = camera.projectionMatrix(renderer.aspectRatio(), farPlane);
            umbra::mat4 vp       = proj * camera.viewMatrix();
            umbra::mat4 invSkyVP = umbra::inverse(proj * camera.viewMatrixRotationOnly());

            umbra::ivec3 dynOrig = lightEngine.dynTexOrigin();
            uint32_t gfxFlags = (settings.smoothLighting ? 1u : 0u) |
                                (settings.waterSpecular  ? 2u : 0u) |
                                (settings.borderFog      ? 4u : 0u) |
                                (camera.eyeUnderwater    ? 8u : 0u);

            umbra::FrameUBO ubo{};
            ubo.viewProj             = vp;
            ubo.invViewProj          = invSkyVP;
            ubo.cameraPosTime        = umbra::vec4(camera.position, static_cast<float>(now));
            ubo.sunDirIntensity      = umbra::vec4(tod.sunDir, tod.skyIntensity);
            ubo.sunColorMoon         = umbra::vec4(tod.sunColor, tod.moonIntensity);
            ubo.skyZenithTimeOfDay   = umbra::vec4(tod.skyZenith, ctx.timeOfDay);
            ubo.skyHorizonCloudLit   = umbra::vec4(tod.skyHorizon, tod.warmGlow);
            ubo.skyAmbientFogDensity = umbra::vec4(tod.skyAmbient, 0.0044f);
            ubo.mieGlowDebugMode     = umbra::vec4(tod.mieGlow, static_cast<float>(ctx.debugViewMode));
            ubo.heldLightPosStrength = umbra::vec4(camera.position, static_cast<float>(heldEmit) / 15.0f);
            ubo.dynOriginFlags       = umbra::vec4(static_cast<float>(dynOrig.x),
                                                   static_cast<float>(dynOrig.y),
                                                   static_cast<float>(dynOrig.z),
                                                   static_cast<float>(gfxFlags));
            float eyeWaterDepth = camera.eyeUnderwater ? camera.waterDepthExact : 0.0f;
            ubo.renderFogParams      = umbra::vec4(eyeWaterDepth, borderStart, borderEnd, settings.fogStrength);

            // Build in-game Vulkan HUD, Inventory, Shift+F3 Debugger, Pause & Settings UI
            uint8_t skyL    = world.getSky(camVoxel.x, camVoxel.y, camVoxel.z);
            uint8_t statBlk = world.getBlockLightVal(camVoxel.x, camVoxel.y, camVoxel.z);
            uint8_t dynL    = lightEngine.sampleDynamicLevelAt(camVoxel);
            uint8_t blkL    = std::max(statBlk, dynL);
            auto areaProfile = umbra::math::evaluateAreaDarkness(skyL, blkL, tod.skyIntensity);
            auto terrainCol  = world.sampleTerrainColumn(camVoxel.x, camVoxel.z);
            const char* biomeStr = umbra::VoxelWorld::biomeDisplayName(terrainCol.biome);

            if (ctx.showDebugger) {
                debugMetrics.streamMs        = static_cast<float>((tStream1 - tStream0) * 1000.0);
                debugMetrics.dynLightMs      = static_cast<float>((tDyn1    - tStream1) * 1000.0);
                debugMetrics.meshMs          = static_cast<float>((tMesh1   - tDyn1)    * 1000.0);
                debugMetrics.opaqueTriangles = renderer.lastOpaqueTriCount();
                debugMetrics.transTriangles  = renderer.lastTransTriCount();
                debugMetrics.visibleChunks   = static_cast<uint32_t>(renderer.lastVisibleChunkCount());
                debugMetrics.loadedChunks    = static_cast<uint32_t>(renderer.loadedChunkMeshCount());
                debugMetrics.dirtyQueueCount = static_cast<uint32_t>(dirtyChunks.size());
                debugMetrics.cameraPos       = camera.position;
                debugMetrics.yaw             = std::fmod(std::fmod(camera.yaw, 360.0f) + 360.0f, 360.0f);
                debugMetrics.pitch           = camera.pitch;
                debugMetrics.chunkX          = umbra::floorDiv(camVoxel.x, umbra::CHUNK_SIZE);
                debugMetrics.chunkZ          = umbra::floorDiv(camVoxel.z, umbra::CHUNK_SIZE);
                debugMetrics.skyLight        = skyL;
                debugMetrics.blockLight      = statBlk;
                debugMetrics.dynLight        = dynL;
                debugMetrics.waterDepth      = camera.waterDepthAbove;
                debugMetrics.eyeUnderwater   = camera.eyeUnderwater;
                debugMetrics.inWater         = camera.inWater;
                debugMetrics.biomeName       = biomeStr;

                umbra::vec3 fwd = camera.forward();
                if (std::abs(fwd.x) > std::abs(fwd.z)) {
                    debugMetrics.facingName = (fwd.x >= 0.0f) ? "EAST (+X)" : "WEST (-X)";
                } else {
                    debugMetrics.facingName = (fwd.z >= 0.0f) ? "SOUTH (+Z)" : "NORTH (-Z)";
                }

                auto lookHit = world.raycast(camera.position, fwd, 10.0f);
                if (lookHit.has_value()) {
                    debugMetrics.targetValid = true;
                    debugMetrics.targetPos   = lookHit->blockPos;
                    umbra::BlockType tb = world.getBlock(lookHit->blockPos.x, lookHit->blockPos.y, lookHit->blockPos.z);
                    debugMetrics.targetName  = umbra::PlayerInventory::blockDisplayName(tb);
                } else {
                    debugMetrics.targetValid = false;
                    debugMetrics.targetName  = "NONE";
                }
            }

            int totalMins = static_cast<int>(std::fmod(ctx.timeOfDay * 24.0f + 6.0f, 24.0f) * 60.0f);
            double smx = 0.0, smy = 0.0;
            int fbW = 1, fbH = 1;
            getScaledMouse(window, &ctx, smx, smy, fbW, fbH);

            umbra::HudSnapshot hudSnap{};
            hudSnap.screenW         = fbW;
            hudSnap.screenH         = fbH;
            hudSnap.clockHours      = (totalMins / 60) % 24;
            hudSnap.clockMinutes    = totalMins % 60;
            hudSnap.phaseName       = tod.phaseName;
            hudSnap.areaLabel       = camera.eyeUnderwater ? "UNDERWATER" : areaProfile.label;
            hudSnap.biomeName       = biomeStr;
            hudSnap.skyLight        = skyL;
            hudSnap.blockLight      = blkL;
            hudSnap.heldEmission    = heldEmit;
            hudSnap.visibleChunks   = renderer.lastVisibleChunkCount();
            hudSnap.loadedChunks    = renderer.loadedChunkMeshCount();
            hudSnap.renderDistance  = settings.renderDistance;
            hudSnap.fps             = displayFps;
            hudSnap.cursorCaptured  = ctx.cursorCaptured;
            hudSnap.eyeUnderwater   = camera.eyeUnderwater;
            hudSnap.waterDepthAbove = camera.waterDepthAbove;
            hudSnap.showDebugger    = ctx.showDebugger;
            hudSnap.debugViewMode   = ctx.debugViewMode;
            hudSnap.uiScreen        = ctx.uiScreen;
            hudSnap.settings        = &settings;
            hudSnap.debug           = &debugMetrics;
            hudSnap.mouseX          = smx;
            hudSnap.mouseY          = smy;

            hudBuilder.build(inventory, hudSnap, hudVerts);
            double tRend0 = glfwGetTime();
            renderer.drawFrame(ubo, hudVerts);
            debugMetrics.renderMs = static_cast<float>((glfwGetTime() - tRend0) * 1000.0);
        }

        renderer.cleanup();
    } catch (const std::exception& ex) {
        std::cerr << "[FAIL] " << ex.what() << "\n";
#ifdef _WIN32
        MessageBoxA(nullptr, ex.what(), "Umbra Voxel - Fatal Error", MB_OK | MB_ICONERROR);
#endif
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
