#pragma once

#include "inventory.hpp"
#include "math/lighting_math.hpp"
#include "types.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace umbra {

enum class UiScreen : uint8_t {
    None     = 0,
    Pause    = 1,
    Settings = 2
};

enum class UiAction : uint8_t {
    None         = 0,
    Resume       = 1,
    OpenSettings = 2,
    BackToPause  = 3,
    Quit         = 4
};

// Compact 2D HUD vertex (36 bytes): NDC position + UV + RGBA tint + item texture layer (<0 = solid color)
struct HudVertex {
    vec2  posNDC;
    vec2  uv;
    vec4  color;
    float texLayer;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription desc{};
        desc.binding   = 0;
        desc.stride    = sizeof(HudVertex);
        desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return desc;
    }

    static std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 4> attrs{};
        attrs[0].binding  = 0;
        attrs[0].location = 0;
        attrs[0].format   = VK_FORMAT_R32G32_SFLOAT;
        attrs[0].offset   = offsetof(HudVertex, posNDC);

        attrs[1].binding  = 0;
        attrs[1].location = 1;
        attrs[1].format   = VK_FORMAT_R32G32_SFLOAT;
        attrs[1].offset   = offsetof(HudVertex, uv);

        attrs[2].binding  = 0;
        attrs[2].location = 2;
        attrs[2].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[2].offset   = offsetof(HudVertex, color);

        attrs[3].binding  = 0;
        attrs[3].location = 3;
        attrs[3].format   = VK_FORMAT_R32_SFLOAT;
        attrs[3].offset   = offsetof(HudVertex, texLayer);
        return attrs;
    }
};

struct HudSnapshot {
    int                   screenW        = 1440;
    int                   screenH        = 900;
    int                   clockHours     = 6;
    int                   clockMinutes   = 20;
    const char*           phaseName      = "SUNRISE";
    const char*           areaLabel      = "OPEN SKY";
    const char*           biomeName      = "PLAINS";
    uint8_t               skyLight       = 15;
    uint8_t               blockLight     = 0;
    uint8_t               heldEmission   = 14;
    size_t                visibleChunks  = 0;
    size_t                loadedChunks   = 0;
    int                   renderDistance = DEFAULT_RENDER_DIST;
    int                   fps            = 60;
    bool                  cursorCaptured = false;
    bool                  eyeUnderwater  = false;
    int                   waterDepthAbove = 0;
    bool                  showDebugger   = false;
    int                   debugViewMode  = 0;
    UiScreen              uiScreen       = UiScreen::None;
    const EngineSettings* settings       = nullptr;
    const DebugMetrics*   debug          = nullptr;
    double                mouseX         = 0.0;
    double                mouseY         = 0.0;
};

// Pure C++20 Zero-Dependency Vulkan 2D HUD, Inventory, Shift+F3 Debugger, Pause & Settings UI Builder
// Built-in 5x7 crisp bitmap font + interactive sliders, presets & toggles (no ImGui/FreeType!)
class HudBuilder {
public:
    static constexpr size_t MAX_HUD_VERTICES = 98304;

    void build(const PlayerInventory& inv, const HudSnapshot& snap, std::vector<HudVertex>& outVerts) const;

    // Returns inventory slot index [0..35] under mouse cursor when inventory is open, or -1
    int hitTestSlot(const PlayerInventory& inv, double mouseX, double mouseY, int screenW, int screenH) const;

    // Handles clicks & slider drags on Pause and Settings UI menus
    UiAction handleUiMouse(UiScreen screen,
                           EngineSettings& settings,
                           double mouseX, double mouseY,
                           int screenW, int screenH,
                           bool isDrag,
                           bool& needRemesh) const;

private:
    struct Rect {
        float x, y, w, h;
        bool contains(float px, float py) const noexcept {
            return px >= x && px <= x + w && py >= y && py <= y + h;
        }
    };

    struct Layout {
        float slotSize   = 54.0f;
        float slotGap    = 6.0f;
        float hotbarX    = 0.0f;
        float hotbarY    = 0.0f;
        float panelX     = 0.0f;
        float panelY     = 0.0f;
        float panelW     = 0.0f;
        float panelH     = 0.0f;
        float backpackX  = 0.0f;
        float backpackY  = 0.0f;

        // Pause modal buttons
        Rect pauseCard{};
        Rect btnResume{};
        Rect btnSettings{};
        Rect btnQuit{};

        // Settings modal controls
        Rect settingsCard{};
        Rect rdSlider{};
        Rect rdMinus{};
        Rect rdPlus{};
        Rect qualityBtns[4]{};
        Rect fogSlider{};
        Rect borderFogToggle{};
        Rect smoothLightToggle{};
        Rect greedyMeshToggle{};
        Rect fovSlider{};
        Rect btnSettingsBack{};
        Rect btnSettingsResume{};
    };

    Layout computeLayout(int screenW, int screenH) const noexcept;
    Rect slotRect(const Layout& lay, int slotIdx, bool inventoryOpen) const noexcept;
};

} // namespace umbra
