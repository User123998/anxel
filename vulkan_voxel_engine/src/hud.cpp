#include "hud.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace umbra {

namespace {

// Minimal 5x7 crisp bitmap font glyphs (A-Z, 0-9, punctuation) — zero external font files!
struct Glyph5x7 {
    char    ch;
    uint8_t rows[7]; // lower 5 bits: bit 4 = leftmost pixel, bit 0 = rightmost pixel
};

constexpr Glyph5x7 FONT_5X7[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F}},
    {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x0E, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {':', {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00}},
    {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08}},
    {'[', {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}},
    {']', {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'|', {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'%', {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13}},
    {'<', {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}},
    {'>', {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
};

const Glyph5x7* findGlyph(char c) noexcept {
    char up = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const auto& g : FONT_5X7) {
        if (g.ch == up) return &g;
    }
    return &FONT_5X7[0];
}

float measureText(const char* str, float scale) noexcept {
    size_t len = 0;
    while (str[len] != '\0') ++len;
    return len > 0 ? static_cast<float>(len) * 6.0f * scale - scale : 0.0f;
}

struct QuadEmitter {
    std::vector<HudVertex>& verts;
    float invW;
    float invH;

    inline vec2 toNDC(float px, float py) const noexcept {
        return vec2(px * invW * 2.0f - 1.0f, py * invH * 2.0f - 1.0f);
    }

    inline void rect(float x, float y, float w, float h, const vec4& col) {
        if (verts.size() + 6 > HudBuilder::MAX_HUD_VERTICES) return;
        vec2 p0 = toNDC(x,     y);
        vec2 p1 = toNDC(x + w, y);
        vec2 p2 = toNDC(x + w, y + h);
        vec2 p3 = toNDC(x,     y + h);
        const vec2 z(0.0f, 0.0f);
        verts.push_back({p0, z, col, -1.0f});
        verts.push_back({p1, z, col, -1.0f});
        verts.push_back({p2, z, col, -1.0f});
        verts.push_back({p0, z, col, -1.0f});
        verts.push_back({p2, z, col, -1.0f});
        verts.push_back({p3, z, col, -1.0f});
    }

    inline void texturedRect(float x, float y, float w, float h, float texLayer,
                             const vec4& tint = vec4(1.0f, 1.0f, 1.0f, 1.0f)) {
        if (verts.size() + 6 > HudBuilder::MAX_HUD_VERTICES) return;
        vec2 p0 = toNDC(x,     y);
        vec2 p1 = toNDC(x + w, y);
        vec2 p2 = toNDC(x + w, y + h);
        vec2 p3 = toNDC(x,     y + h);
        verts.push_back({p0, vec2(0.0f, 0.0f), tint, texLayer});
        verts.push_back({p1, vec2(1.0f, 0.0f), tint, texLayer});
        verts.push_back({p2, vec2(1.0f, 1.0f), tint, texLayer});
        verts.push_back({p0, vec2(0.0f, 0.0f), tint, texLayer});
        verts.push_back({p2, vec2(1.0f, 1.0f), tint, texLayer});
        verts.push_back({p3, vec2(0.0f, 1.0f), tint, texLayer});
    }

    inline void border(float x, float y, float w, float h, float t, const vec4& col) {
        rect(x,         y,         w, t, col);
        rect(x,         y + h - t, w, t, col);
        rect(x,         y + t,     t, h - 2.0f * t, col);
        rect(x + w - t, y + t,     t, h - 2.0f * t, col);
    }

    void text(float x, float y, float scale, const char* str, const vec4& col) {
        float cx = x;
        for (const char* p = str; *p != '\0'; ++p) {
            const Glyph5x7* g = findGlyph(*p);
            for (int r = 0; r < 7; ++r) {
                uint8_t bits = g->rows[r];
                if (bits == 0) continue;
                for (int c = 0; c < 5; ++c) {
                    if (bits & (1u << (4 - c))) {
                        rect(cx + c * scale, y + r * scale, scale, scale, col);
                    }
                }
            }
            cx += 6.0f * scale;
        }
    }

    void textCentered(float rx, float ry, float rw, float rh, float scale, const char* str, const vec4& col) {
        float tw = measureText(str, scale);
        float th = 7.0f * scale;
        text(rx + (rw - tw) * 0.5f, ry + (rh - th) * 0.5f, scale, str, col);
    }

    // Renders actual PNG item icon from assets/texture/items/*.png (layer = BlockType - 1)
    void blockIcon(float x, float y, float size, BlockType b) {
        if (b == BlockType::Air) return;
        uint8_t id = static_cast<uint8_t>(b);
        if (id < 1 || id > 14) return;

        float pad = size * 0.14f;
        float ix  = x + pad;
        float iy  = y + pad;
        float is  = size - pad * 2.0f;

        if (blockEmission(b) > 0) {
            vec4 glowCol = (b == BlockType::Crystal)
                ? vec4(0.32f, 0.88f, 0.98f, 0.24f)
                : vec4(1.00f, 0.72f, 0.24f, 0.24f);
            rect(ix - 2.0f, iy - 2.0f, is + 4.0f, is + 4.0f, glowCol);
        }

        float itemLayer = static_cast<float>(id - 1u);
        texturedRect(ix, iy, is, is, itemLayer);
    }
};

} // namespace

HudBuilder::Layout HudBuilder::computeLayout(int screenW, int screenH) const noexcept {
    Layout lay{};
    float w = static_cast<float>(std::max(screenW, 640));
    float h = static_cast<float>(std::max(screenH, 480));

    lay.slotSize = std::clamp(w * 0.038f, 44.0f, 58.0f);
    lay.slotGap  = 6.0f;

    float barW = PlayerInventory::HOTBAR_SLOTS * lay.slotSize +
                 (PlayerInventory::HOTBAR_SLOTS - 1) * lay.slotGap;
    lay.hotbarX = (w - barW) * 0.5f;
    lay.hotbarY = h - lay.slotSize - 24.0f;

    lay.panelW = barW + 32.0f;
    float rowsH = PlayerInventory::BACKPACK_ROWS * lay.slotSize +
                  (PlayerInventory::BACKPACK_ROWS - 1) * lay.slotGap;
    lay.panelH = rowsH + lay.slotSize + 96.0f;
    lay.panelX = (w - lay.panelW) * 0.5f;
    lay.panelY = (h - lay.panelH) * 0.5f - 10.0f;

    lay.backpackX = lay.panelX + 16.0f;
    lay.backpackY = lay.panelY + 46.0f;

    // Pause menu geometry
    float pauseW = 390.0f;
    float pauseH = 296.0f;
    lay.pauseCard = {(w - pauseW) * 0.5f, (h - pauseH) * 0.5f, pauseW, pauseH};
    float btnX = lay.pauseCard.x + 36.0f;
    float btnW = pauseW - 72.0f;
    float btnH = 44.0f;
    lay.btnResume   = {btnX, lay.pauseCard.y + 92.0f,  btnW, btnH};
    lay.btnSettings = {btnX, lay.pauseCard.y + 150.0f, btnW, btnH};
    lay.btnQuit     = {btnX, lay.pauseCard.y + 208.0f, btnW, btnH};

    // Settings menu geometry
    float setW = std::min(680.0f, w - 48.0f);
    float setH = 516.0f;
    lay.settingsCard = {(w - setW) * 0.5f, (h - setH) * 0.5f, setW, setH};
    float innerX = lay.settingsCard.x + 28.0f;
    float innerW = setW - 56.0f;
    float ctrlX  = innerX + 240.0f;
    float ctrlW  = innerW - 240.0f;

    // Row 0: Render Distance (4..50) with [-] [slider] [+]
    float y0 = lay.settingsCard.y + 76.0f;
    lay.rdMinus  = {ctrlX, y0, 32.0f, 30.0f};
    lay.rdPlus   = {ctrlX + ctrlW - 32.0f, y0, 32.0f, 30.0f};
    lay.rdSlider = {ctrlX + 40.0f, y0 + 2.0f, ctrlW - 80.0f, 26.0f};

    // Row 1: Graphics Quality (LOW / MED / HIGH / ULTRA)
    float y1 = y0 + 56.0f;
    float qGap = 8.0f;
    float qBtnW = (ctrlW - 3.0f * qGap) * 0.25f;
    for (int i = 0; i < 4; ++i) {
        lay.qualityBtns[i] = {ctrlX + i * (qBtnW + qGap), y1, qBtnW, 32.0f};
    }

    // Row 2: Atmospheric Fog Strength (0..100%)
    float y2 = y1 + 56.0f;
    lay.fogSlider = {ctrlX, y2 + 2.0f, ctrlW, 26.0f};

    // Row 3: Render-Distance Border Fog (ON / OFF)
    float y3 = y2 + 52.0f;
    lay.borderFogToggle = {ctrlX, y3, 140.0f, 32.0f};

    // Row 4: Smooth Lighting & Crevice AO (ON / OFF)
    float y4 = y3 + 48.0f;
    lay.smoothLightToggle = {ctrlX, y4, 140.0f, 32.0f};

    // Row 5: Binary Greedy Meshing (ON / OFF)
    float y5 = y4 + 48.0f;
    lay.greedyMeshToggle = {ctrlX, y5, 140.0f, 32.0f};

    // Row 6: Field of View (60..105)
    float y6 = y5 + 50.0f;
    lay.fovSlider = {ctrlX, y6 + 2.0f, ctrlW, 26.0f};

    // Footer buttons
    float footY = lay.settingsCard.y + setH - 58.0f;
    float halfW = (innerW - 14.0f) * 0.5f;
    lay.btnSettingsBack   = {innerX, footY, halfW, 38.0f};
    lay.btnSettingsResume = {innerX + halfW + 14.0f, footY, halfW, 38.0f};

    return lay;
}

HudBuilder::Rect HudBuilder::slotRect(const Layout& lay, int slotIdx, bool inventoryOpen) const noexcept {
    if (slotIdx < PlayerInventory::HOTBAR_SLOTS) {
        float x = inventoryOpen
            ? lay.backpackX + slotIdx * (lay.slotSize + lay.slotGap)
            : lay.hotbarX   + slotIdx * (lay.slotSize + lay.slotGap);
        float y = inventoryOpen
            ? lay.panelY + lay.panelH - lay.slotSize - 18.0f
            : lay.hotbarY;
        return {x, y, lay.slotSize, lay.slotSize};
    }

    int bi  = slotIdx - PlayerInventory::HOTBAR_SLOTS;
    int row = bi / PlayerInventory::BACKPACK_COLS;
    int col = bi % PlayerInventory::BACKPACK_COLS;
    float x = lay.backpackX + col * (lay.slotSize + lay.slotGap);
    float y = lay.backpackY + row * (lay.slotSize + lay.slotGap);
    return {x, y, lay.slotSize, lay.slotSize};
}

int HudBuilder::hitTestSlot(const PlayerInventory& inv, double mouseX, double mouseY,
                            int screenW, int screenH) const {
    if (!inv.isOpen()) return -1;
    Layout lay = computeLayout(screenW, screenH);
    float mx = static_cast<float>(mouseX);
    float my = static_cast<float>(mouseY);
    for (int i = 0; i < PlayerInventory::TOTAL_SLOTS; ++i) {
        if (slotRect(lay, i, true).contains(mx, my)) {
            return i;
        }
    }
    return -1;
}

UiAction HudBuilder::handleUiMouse(UiScreen screen,
                                   EngineSettings& settings,
                                   double mouseX, double mouseY,
                                   int screenW, int screenH,
                                   bool isDrag,
                                   bool& needRemesh) const {
    if (screen == UiScreen::None) return UiAction::None;

    Layout lay = computeLayout(screenW, screenH);
    float mx = static_cast<float>(mouseX);
    float my = static_cast<float>(mouseY);

    if (screen == UiScreen::Pause) {
        if (isDrag) return UiAction::None;
        if (lay.btnResume.contains(mx, my))   return UiAction::Resume;
        if (lay.btnSettings.contains(mx, my)) return UiAction::OpenSettings;
        if (lay.btnQuit.contains(mx, my))     return UiAction::Quit;
        return UiAction::None;
    }

    // Settings screen: allow smooth dragging on all 3 sliders
    if (lay.rdSlider.contains(mx, my)) {
        float t = std::clamp((mx - lay.rdSlider.x) / std::max(lay.rdSlider.w, 1.0f), 0.0f, 1.0f);
        int rd = MIN_RENDER_DISTANCE + static_cast<int>(std::round(t * (MAX_RENDER_DISTANCE - MIN_RENDER_DISTANCE)));
        settings.renderDistance = std::clamp(rd, MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE);
        return UiAction::None;
    }
    if (lay.fogSlider.contains(mx, my)) {
        float t = std::clamp((mx - lay.fogSlider.x) / std::max(lay.fogSlider.w, 1.0f), 0.0f, 1.0f);
        settings.fogStrength = std::round(t * 100.0f) * 0.01f;
        return UiAction::None;
    }
    if (lay.fovSlider.contains(mx, my)) {
        float t = std::clamp((mx - lay.fovSlider.x) / std::max(lay.fovSlider.w, 1.0f), 0.0f, 1.0f);
        settings.fov = std::round(60.0f + t * 45.0f);
        return UiAction::None;
    }

    if (isDrag) return UiAction::None;

    if (lay.rdMinus.contains(mx, my)) {
        settings.renderDistance = std::max(MIN_RENDER_DISTANCE, settings.renderDistance - 2);
        return UiAction::None;
    }
    if (lay.rdPlus.contains(mx, my)) {
        settings.renderDistance = std::min(MAX_RENDER_DISTANCE, settings.renderDistance + 2);
        return UiAction::None;
    }

    for (int i = 0; i < 4; ++i) {
        if (lay.qualityBtns[i].contains(mx, my)) {
            bool prevSmooth = settings.smoothLighting;
            bool prevGreedy = settings.greedyMeshing;
            settings.applyQualityPreset(static_cast<GraphicsQuality>(i));
            if (prevSmooth != settings.smoothLighting || prevGreedy != settings.greedyMeshing) {
                needRemesh = true;
            }
            return UiAction::None;
        }
    }

    if (lay.borderFogToggle.contains(mx, my)) {
        settings.borderFog = !settings.borderFog;
        return UiAction::None;
    }
    if (lay.smoothLightToggle.contains(mx, my)) {
        settings.smoothLighting = !settings.smoothLighting;
        needRemesh = true;
        return UiAction::None;
    }
    if (lay.greedyMeshToggle.contains(mx, my)) {
        settings.greedyMeshing = !settings.greedyMeshing;
        needRemesh = true;
        return UiAction::None;
    }
    if (lay.btnSettingsBack.contains(mx, my)) {
        return UiAction::BackToPause;
    }
    if (lay.btnSettingsResume.contains(mx, my)) {
        return UiAction::Resume;
    }

    return UiAction::None;
}

void HudBuilder::build(const PlayerInventory& inv, const HudSnapshot& snap,
                       std::vector<HudVertex>& outVerts) const {
    outVerts.clear();
    if (snap.screenW <= 0 || snap.screenH <= 0) return;

    QuadEmitter q{
        outVerts,
        1.0f / static_cast<float>(snap.screenW),
        1.0f / static_cast<float>(snap.screenH)
    };

    const vec4 bgPanel(0.043f, 0.055f, 0.082f, 0.88f);
    const vec4 bgModal(0.038f, 0.050f, 0.078f, 0.96f);
    const vec4 bgSlot(0.075f, 0.095f, 0.140f, 0.90f);
    const vec4 bgBtn(0.095f, 0.122f, 0.180f, 0.95f);
    const vec4 bgBtnHover(0.155f, 0.195f, 0.280f, 0.98f);
    const vec4 bgBtnActive(0.240f, 0.175f, 0.070f, 0.98f);
    const vec4 borderDim(0.24f, 0.29f, 0.39f, 0.85f);
    const vec4 borderGold(0.96f, 0.72f, 0.22f, 1.00f);
    const vec4 textBright(0.95f, 0.97f, 1.00f, 1.00f);
    const vec4 textMuted(0.62f, 0.68f, 0.78f, 0.95f);
    const vec4 textAmber(0.99f, 0.78f, 0.34f, 1.00f);
    const vec4 textCyan(0.45f, 0.82f, 1.00f, 1.00f);
    const vec4 textGreen(0.36f, 0.86f, 0.56f, 1.00f);
    const vec4 textRed(0.94f, 0.42f, 0.38f, 1.00f);

    Layout lay = computeLayout(snap.screenW, snap.screenH);
    float mx = static_cast<float>(snap.mouseX);
    float my = static_cast<float>(snap.mouseY);
    bool anyModal = inv.isOpen() || snap.uiScreen != UiScreen::None;

    // 1. Center Crosshair (only during active exploration)
    if (!anyModal) {
        float cx = static_cast<float>(snap.screenW) * 0.5f;
        float cy = static_cast<float>(snap.screenH) * 0.5f;
        q.rect(cx - 9.0f, cy - 2.0f, 18.0f, 4.0f, vec4(0.0f, 0.0f, 0.0f, 0.55f));
        q.rect(cx - 2.0f, cy - 9.0f, 4.0f, 18.0f, vec4(0.0f, 0.0f, 0.0f, 0.55f));
        q.rect(cx - 8.0f, cy - 1.0f, 16.0f, 2.0f, vec4(0.98f, 0.98f, 0.98f, 0.92f));
        q.rect(cx - 1.0f, cy - 8.0f, 2.0f, 16.0f, vec4(0.98f, 0.98f, 0.98f, 0.92f));
    }

    // 2. Top-Left Atmospheric, Biome & Natural Area Darkness HUD Card
    {
        float px = 18.0f, py = 18.0f, pw = 336.0f, ph = 112.0f;
        q.rect(px, py, pw, ph, bgPanel);
        q.border(px, py, pw, ph, 1.5f, borderDim);

        char line1[64];
        std::snprintf(line1, sizeof(line1), "%02d:%02d %s",
                      snap.clockHours, snap.clockMinutes, snap.phaseName);
        q.text(px + 12.0f, py + 11.0f, 1.9f, line1, textBright);

        char rdStr[32];
        std::snprintf(rdStr, sizeof(rdStr), "RD %d", snap.renderDistance);
        q.text(px + pw - 66.0f, py + 13.0f, 1.5f, rdStr, textCyan);

        q.text(px + 12.0f, py + 34.0f, 1.4f, snap.areaLabel, textAmber);

        char stats[64];
        std::snprintf(stats, sizeof(stats), "FPS %d  CHK %zu/%zu",
                      snap.fps, snap.visibleChunks, snap.loadedChunks);
        q.text(px + 150.0f, py + 34.0f, 1.35f, stats, textMuted);

        char biomeStr[48];
        std::snprintf(biomeStr, sizeof(biomeStr), "BIOME: %s", snap.biomeName ? snap.biomeName : "PLAINS");
        q.text(px + 12.0f, py + 52.0f, 1.35f, biomeStr, textGreen);

        char skyTxt[24];
        std::snprintf(skyTxt, sizeof(skyTxt), "SKY %2u", static_cast<unsigned>(snap.skyLight));
        q.text(px + 12.0f, py + 72.0f, 1.35f, skyTxt, textCyan);
        float barX = px + 76.0f, barW = 244.0f, barH = 7.0f;
        q.rect(barX, py + 74.0f, barW, barH, vec4(0.12f, 0.16f, 0.24f, 0.95f));
        q.rect(barX, py + 74.0f, barW * (static_cast<float>(snap.skyLight) / 15.0f), barH, textCyan);

        char blkTxt[24];
        std::snprintf(blkTxt, sizeof(blkTxt), "BLK %2u", static_cast<unsigned>(snap.blockLight));
        q.text(px + 12.0f, py + 90.0f, 1.35f, blkTxt, textAmber);
        q.rect(barX, py + 92.0f, barW, barH, vec4(0.12f, 0.16f, 0.24f, 0.95f));
        q.rect(barX, py + 92.0f, barW * (static_cast<float>(snap.blockLight) / 15.0f), barH, textAmber);
    }

    // 3. Top-Right Held Light Status & Quick Key Guide
    {
        float pw = 310.0f, ph = snap.eyeUnderwater ? 68.0f : 48.0f;
        float px = static_cast<float>(snap.screenW) - pw - 18.0f;
        float py = 18.0f;
        q.rect(px, py, pw, ph, bgPanel);
        q.border(px, py, pw, ph, 1.5f,
                 snap.eyeUnderwater ? textCyan : (snap.heldEmission > 0 ? borderGold : borderDim));

        char emitStr[48];
        if (snap.heldEmission > 0) {
            std::snprintf(emitStr, sizeof(emitStr), "HELD LIGHT: LVL %u", static_cast<unsigned>(snap.heldEmission));
            q.text(px + 12.0f, py + 10.0f, 1.55f, emitStr, textAmber);
        } else {
            q.text(px + 12.0f, py + 10.0f, 1.55f, "HELD LIGHT: OFF", textMuted);
        }
        q.text(px + 12.0f, py + 29.0f, 1.20f, "ESC MENU  I INV  SHIFT+F3 DBG  N TIME", textMuted);
        if (snap.eyeUnderwater) {
            char uwStr[48];
            std::snprintf(uwStr, sizeof(uwStr), "UNDERWATER  DEPTH %dM", snap.waterDepthAbove);
            q.text(px + 12.0f, py + 47.0f, 1.35f, uwStr, textCyan);
        }
    }

    // 3B. Shift+F3 Real-Time Engine Profiler & World Debugger Overlay
    if (snap.showDebugger && snap.debug != nullptr && snap.uiScreen == UiScreen::None) {
        const DebugMetrics& d = *snap.debug;

        // Left Telemetry Card
        float dx = 18.0f, dy = 140.0f, dw = 446.0f, dh = 236.0f;
        q.rect(dx, dy, dw, dh, vec4(0.028f, 0.038f, 0.058f, 0.88f));
        q.border(dx, dy, dw, dh, 1.5f, textCyan);

        q.text(dx + 12.0f, dy + 10.0f, 1.55f, "UMBRA ENGINE DEBUGGER [SHIFT+F3]", textCyan);
        q.rect(dx + 12.0f, dy + 28.0f, dw - 24.0f, 1.0f, borderDim);

        char buf[96];
        float ly = dy + 36.0f;
        const float stepY = 21.0f;

        std::snprintf(buf, sizeof(buf), "FPS: %d   FRAME: %.2f MS  (MIN %.1f / AVG %.1f / MAX %.1f)",
                      snap.fps, d.frameMs, d.minFrameMs, d.avgFrameMs, d.maxFrameMs);
        q.text(dx + 12.0f, ly, 1.22f, buf, d.frameMs <= 16.8f ? textGreen : (d.frameMs <= 33.4f ? textAmber : textRed));
        ly += stepY;

        std::snprintf(buf, sizeof(buf), "CPU: STRM %.2f  MESH %.2f  DYN %.2f  REND %.2f MS",
                      d.streamMs, d.meshMs, d.dynLightMs, d.renderMs);
        q.text(dx + 12.0f, ly, 1.22f, buf, textBright);
        ly += stepY;

        std::snprintf(buf, sizeof(buf), "XYZ: %.1f / %.1f / %.1f   CHUNK: %d, %d",
                      d.cameraPos.x, d.cameraPos.y, d.cameraPos.z, d.chunkX, d.chunkZ);
        q.text(dx + 12.0f, ly, 1.22f, buf, textBright);
        ly += stepY;

        std::snprintf(buf, sizeof(buf), "FACING: %s  (YAW %.0f  PITCH %.0f)",
                      d.facingName ? d.facingName : "SOUTH", d.yaw, d.pitch);
        q.text(dx + 12.0f, ly, 1.22f, buf, textMuted);
        ly += stepY;

        std::snprintf(buf, sizeof(buf), "BIOME: %s   WATER: %s (D:%d)",
                      d.biomeName ? d.biomeName : "PLAINS",
                      d.eyeUnderwater ? "SUBMERGED" : (d.inWater ? "WADING" : "DRY"),
                      d.waterDepth);
        q.text(dx + 12.0f, ly, 1.22f, buf, d.eyeUnderwater ? textCyan : textGreen);
        ly += stepY;

        std::snprintf(buf, sizeof(buf), "LIGHT: SKY %2d  BLK %2d  DYN %2d   DIRTY Q: %u",
                      d.skyLight, d.blockLight, d.dynLight, d.dirtyQueueCount);
        q.text(dx + 12.0f, ly, 1.22f, buf, textAmber);
        ly += stepY;

        std::snprintf(buf, sizeof(buf), "MESH: CHK %u/%u (RD %d)  OPQ %uK  TRN %uK TRIS",
                      d.visibleChunks, d.loadedChunks, snap.renderDistance,
                      (d.opaqueTriangles + 500u) / 1000u,
                      (d.transTriangles + 500u) / 1000u);
        q.text(dx + 12.0f, ly, 1.22f, buf, textBright);
        ly += stepY;

        if (d.targetValid) {
            std::snprintf(buf, sizeof(buf), "TARGET: %s [%d, %d, %d]",
                          d.targetName ? d.targetName : "BLOCK",
                          d.targetPos.x, d.targetPos.y, d.targetPos.z);
        } else {
            std::snprintf(buf, sizeof(buf), "TARGET: NONE");
        }
        q.text(dx + 12.0f, ly, 1.22f, buf, textCyan);
        ly += stepY;

        q.text(dx + 12.0f, ly, 1.15f, "OPTICS: 3D-TRILINEAR DYN  BEER-LAMBERT WATER  ROT-ONLY SKY", textMuted);

        // Right 60-Frame Live Frametime Profiler Graph
        float gw = 310.0f, gh = 136.0f;
        float gx = static_cast<float>(snap.screenW) - gw - 18.0f;
        float gy = (snap.eyeUnderwater ? 96.0f : 76.0f);
        q.rect(gx, gy, gw, gh, vec4(0.028f, 0.038f, 0.058f, 0.88f));
        q.border(gx, gy, gw, gh, 1.5f, borderDim);
        q.text(gx + 10.0f, gy + 8.0f, 1.25f, "FRAMETIME GRAPH (0-50 MS)", textBright);

        float plotX = gx + 10.0f;
        float plotY = gy + 26.0f;
        float plotW = gw - 20.0f;
        float plotH = gh - 44.0f;
        q.rect(plotX, plotY, plotW, plotH, vec4(0.015f, 0.020f, 0.032f, 0.95f));

        // 16.6ms (60 FPS) & 33.3ms (30 FPS) reference lines
        float y60 = plotY + plotH * (1.0f - 16.67f / 50.0f);
        float y30 = plotY + plotH * (1.0f - 33.33f / 50.0f);
        q.rect(plotX, y60, plotW, 1.0f, vec4(0.22f, 0.72f, 0.42f, 0.45f));
        q.rect(plotX, y30, plotW, 1.0f, vec4(0.95f, 0.68f, 0.22f, 0.40f));

        float barW = plotW / static_cast<float>(DebugMetrics::HISTORY_LEN);
        for (int i = 0; i < DebugMetrics::HISTORY_LEN; ++i) {
            int idx = (d.historyHead + i) % DebugMetrics::HISTORY_LEN;
            float ms = d.frameHistoryMs[static_cast<size_t>(idx)];
            if (ms <= 0.01f) continue;
            float norm = std::clamp(ms / 50.0f, 0.02f, 1.0f);
            float bh = norm * plotH;
            vec4 col = (ms <= 16.8f) ? textGreen : ((ms <= 33.4f) ? textAmber : textRed);
            q.rect(plotX + i * barW, plotY + plotH - bh, std::max(barW - 1.0f, 1.5f), bh, col);
        }
        q.text(plotX, gy + gh - 14.0f, 1.1f, "GREEN:60+FPS  AMBER:30+FPS  RED:<30FPS", textMuted);
    }

    // 4. Helper for Inventory/Hotbar Slots
    auto drawSlot = [&](int slotIdx, bool inModal) {
        Rect r = slotRect(lay, slotIdx, inModal);
        const InventorySlot& s = inv.slot(slotIdx);

        bool isSelected = (!inModal && slotIdx == inv.selectedHotbar());
        bool isPicked   = (inModal && slotIdx == inv.pickedSlot());
        bool isHovered  = (inModal && r.contains(mx, my));

        vec4 fill = bgSlot;
        if (isSelected) fill = vec4(0.18f, 0.14f, 0.08f, 0.95f);
        else if (isPicked) fill = vec4(0.24f, 0.18f, 0.08f, 0.95f);
        else if (isHovered) fill = vec4(0.14f, 0.18f, 0.25f, 0.95f);

        q.rect(r.x, r.y, r.w, r.h, fill);
        q.border(r.x, r.y, r.w, r.h, (isSelected || isPicked) ? 2.5f : 1.5f,
                 (isSelected || isPicked) ? borderGold : (isHovered ? textBright : borderDim));

        if (slotIdx < PlayerInventory::HOTBAR_SLOTS) {
            char kbuf[2] = {static_cast<char>('1' + slotIdx), '\0'};
            q.text(r.x + 4.0f, r.y + 4.0f, 1.2f, kbuf, isSelected ? textAmber : textMuted);
        }

        if (!s.empty()) {
            q.blockIcon(r.x, r.y, r.w, s.block);
            char cbuf[8];
            std::snprintf(cbuf, sizeof(cbuf), "%u", static_cast<unsigned>(s.count));
            float tw = (cbuf[1] != '\0') ? 14.0f : 7.0f;
            q.text(r.x + r.w - tw - 4.0f, r.y + r.h - 13.0f, 1.4f, cbuf, textBright);

            if (blockEmission(s.block) > 0) {
                q.rect(r.x + r.w - 9.0f, r.y + 5.0f, 4.0f, 4.0f, textAmber);
            }
        }
    };

    // 5. Compact Bottom Hotbar (shown when Inventory modal is closed)
    if (!inv.isOpen()) {
        float barW = PlayerInventory::HOTBAR_SLOTS * lay.slotSize +
                     (PlayerInventory::HOTBAR_SLOTS - 1) * lay.slotGap;
        q.rect(lay.hotbarX - 8.0f, lay.hotbarY - 8.0f, barW + 16.0f, lay.slotSize + 16.0f, bgPanel);
        q.border(lay.hotbarX - 8.0f, lay.hotbarY - 8.0f, barW + 16.0f, lay.slotSize + 16.0f, 1.5f, borderDim);

        for (int i = 0; i < PlayerInventory::HOTBAR_SLOTS; ++i) {
            drawSlot(i, false);
        }

        if (snap.uiScreen == UiScreen::None) {
            const char* itemName = PlayerInventory::blockDisplayName(inv.heldBlock());
            char labelBuf[64];
            if (snap.heldEmission > 0) {
                std::snprintf(labelBuf, sizeof(labelBuf), "%s  [LIGHT %u]", itemName, static_cast<unsigned>(snap.heldEmission));
            } else {
                std::snprintf(labelBuf, sizeof(labelBuf), "%s", itemName);
            }
            float textW = measureText(labelBuf, 1.6f);
            float lx = (static_cast<float>(snap.screenW) - textW) * 0.5f;
            float ly = lay.hotbarY - 28.0f;
            q.rect(lx - 8.0f, ly - 4.0f, textW + 16.0f, 19.0f, vec4(0.04f, 0.05f, 0.08f, 0.78f));
            q.text(lx, ly, 1.6f, labelBuf, snap.heldEmission > 0 ? textAmber : textBright);
        }
    }

    // 6. Full Inventory Overlay
    if (inv.isOpen() && snap.uiScreen == UiScreen::None) {
        q.rect(0.0f, 0.0f, static_cast<float>(snap.screenW), static_cast<float>(snap.screenH),
               vec4(0.01f, 0.015f, 0.025f, 0.62f));

        q.rect(lay.panelX, lay.panelY, lay.panelW, lay.panelH, bgModal);
        q.border(lay.panelX, lay.panelY, lay.panelW, lay.panelH, 2.0f, borderGold);

        q.text(lay.panelX + 16.0f, lay.panelY + 14.0f, 2.0f, "INVENTORY", textBright);
        q.text(lay.panelX + 160.0f, lay.panelY + 18.0f, 1.4f,
               "CLICK SLOTS TO SWAP   ESC OR I TO CLOSE", textMuted);

        for (int i = PlayerInventory::HOTBAR_SLOTS; i < PlayerInventory::TOTAL_SLOTS; ++i) {
            drawSlot(i, true);
        }

        float divY = lay.panelY + lay.panelH - lay.slotSize - 32.0f;
        q.rect(lay.panelX + 16.0f, divY, lay.panelW - 32.0f, 1.5f, borderDim);
        q.text(lay.panelX + 16.0f, divY - 14.0f, 1.3f, "HOTBAR  [HOLD TORCH/GLOWSTONE/MAGMA/CRYSTAL FOR DYNAMIC LIGHT]", textAmber);

        for (int i = 0; i < PlayerInventory::HOTBAR_SLOTS; ++i) {
            drawSlot(i, true);
        }
    }

    // Helper for crisp clickable buttons & sliders in Pause / Settings
    auto drawButton = [&](const Rect& r, const char* label, bool primary, bool danger = false) {
        bool hov = r.contains(mx, my);
        vec4 fill = primary
            ? (hov ? vec4(0.32f, 0.23f, 0.09f, 0.98f) : bgBtnActive)
            : (danger ? (hov ? vec4(0.32f, 0.11f, 0.12f, 0.98f) : vec4(0.20f, 0.08f, 0.09f, 0.95f))
                      : (hov ? bgBtnHover : bgBtn));
        vec4 bdr = primary ? borderGold : (danger ? textRed : (hov ? textBright : borderDim));
        q.rect(r.x, r.y, r.w, r.h, fill);
        q.border(r.x, r.y, r.w, r.h, hov || primary ? 2.0f : 1.5f, bdr);
        q.textCentered(r.x, r.y, r.w, r.h, 1.7f, label, primary ? textAmber : (danger ? textRed : textBright));
    };

    auto drawSlider = [&](const Rect& r, float norm01, const vec4& fillCol) {
        bool hov = r.contains(mx, my);
        float trackY = r.y + (r.h - 8.0f) * 0.5f;
        q.rect(r.x, trackY, r.w, 8.0f, vec4(0.11f, 0.14f, 0.20f, 0.98f));
        q.border(r.x, trackY, r.w, 8.0f, 1.0f, hov ? textBright : borderDim);
        float fillW = std::clamp(norm01, 0.0f, 1.0f) * r.w;
        q.rect(r.x, trackY, fillW, 8.0f, fillCol);
        float knobX = r.x + fillW - 6.0f;
        q.rect(knobX, r.y + 3.0f, 12.0f, r.h - 6.0f, hov ? textBright : borderGold);
        q.border(knobX, r.y + 3.0f, 12.0f, r.h - 6.0f, 1.5f, vec4(0.04f, 0.05f, 0.08f, 0.95f));
    };

    auto drawToggle = [&](const Rect& r, bool value) {
        bool hov = r.contains(mx, my);
        vec4 fill = value ? vec4(0.10f, 0.28f, 0.20f, 0.96f) : vec4(0.12f, 0.14f, 0.20f, 0.95f);
        vec4 bdr  = value ? textGreen : (hov ? textBright : borderDim);
        q.rect(r.x, r.y, r.w, r.h, fill);
        q.border(r.x, r.y, r.w, r.h, hov || value ? 2.0f : 1.5f, bdr);
        q.textCentered(r.x, r.y, r.w, r.h, 1.6f, value ? "ON" : "OFF", value ? textGreen : textMuted);
    };

    // 7. Pause Menu Overlay
    if (snap.uiScreen == UiScreen::Pause) {
        q.rect(0.0f, 0.0f, static_cast<float>(snap.screenW), static_cast<float>(snap.screenH),
               vec4(0.01f, 0.015f, 0.025f, 0.66f));

        const Rect& c = lay.pauseCard;
        q.rect(c.x, c.y, c.w, c.h, bgModal);
        q.border(c.x, c.y, c.w, c.h, 2.0f, borderGold);

        q.textCentered(c.x, c.y + 22.0f, c.w, 24.0f, 2.5f, "GAME PAUSED", textBright);
        q.textCentered(c.x, c.y + 52.0f, c.w, 16.0f, 1.35f, "UMBRA VULKAN VOXEL ENGINE", textAmber);

        drawButton(lay.btnResume,   "RESUME GAME",     true);
        drawButton(lay.btnSettings, "SETTINGS",        false);
        drawButton(lay.btnQuit,     "QUIT TO DESKTOP", false, true);

        q.textCentered(c.x, c.y + c.h - 26.0f, c.w, 16.0f, 1.25f, "PRESS ESC TO RESUME", textMuted);
    }

    // 8. Interactive Settings UI Overlay (Render Distance 4..50, Quality, Fog, Border Fog, Meshing, FOV)
    if (snap.uiScreen == UiScreen::Settings && snap.settings != nullptr) {
        const EngineSettings& s = *snap.settings;

        q.rect(0.0f, 0.0f, static_cast<float>(snap.screenW), static_cast<float>(snap.screenH),
               vec4(0.01f, 0.015f, 0.025f, 0.72f));

        const Rect& c = lay.settingsCard;
        q.rect(c.x, c.y, c.w, c.h, bgModal);
        q.border(c.x, c.y, c.w, c.h, 2.0f, borderGold);

        float lx = c.x + 28.0f;
        q.text(lx, c.y + 20.0f, 2.2f, "VIDEO & WORLD SETTINGS", textBright);
        q.text(lx, c.y + 46.0f, 1.3f, "CHANGES APPLY LIVE   BINARY GREEDY MESHING + 3D DYNAMIC LIGHT", textMuted);
        q.rect(lx, c.y + 62.0f, c.w - 56.0f, 1.5f, borderDim);

        // Row 0: Render Distance (4..50 chunks)
        char rdLabel[48];
        std::snprintf(rdLabel, sizeof(rdLabel), "RENDER DIST: %d CHK", s.renderDistance);
        q.text(lx, lay.rdSlider.y + 6.0f, 1.5f, rdLabel, textBright);
        drawButton(lay.rdMinus, "-", false);
        drawButton(lay.rdPlus,  "+", false);
        float rdNorm = static_cast<float>(s.renderDistance - MIN_RENDER_DISTANCE) /
                       static_cast<float>(MAX_RENDER_DISTANCE - MIN_RENDER_DISTANCE);
        drawSlider(lay.rdSlider, rdNorm, textCyan);

        // Row 1: Graphics Quality preset
        q.text(lx, lay.qualityBtns[0].y + 9.0f, 1.5f, "GRAPHICS QUALITY", textBright);
        const char* qNames[4] = {"LOW", "MED", "HIGH", "ULTRA"};
        for (int i = 0; i < 4; ++i) {
            bool active = (static_cast<int>(s.quality) == i);
            drawButton(lay.qualityBtns[i], qNames[i], active);
        }

        // Row 2: Fog Density (0..100%)
        char fogLabel[48];
        std::snprintf(fogLabel, sizeof(fogLabel), "FOG DENSITY: %d%%",
                      static_cast<int>(std::round(s.fogStrength * 100.0f)));
        q.text(lx, lay.fogSlider.y + 6.0f, 1.5f, fogLabel, textBright);
        drawSlider(lay.fogSlider, s.fogStrength, textAmber);

        // Row 3: Render-Distance Border Fog
        q.text(lx, lay.borderFogToggle.y + 9.0f, 1.5f, "RENDER BORDER FOG", textBright);
        drawToggle(lay.borderFogToggle, s.borderFog);
        q.text(lay.borderFogToggle.x + 154.0f, lay.borderFogToggle.y + 10.0f, 1.25f,
               "SEAMLESS SKY EDGE FADE", textMuted);

        // Row 4: Smooth Lighting & AO
        q.text(lx, lay.smoothLightToggle.y + 9.0f, 1.5f, "SMOOTH LIGHTING", textBright);
        drawToggle(lay.smoothLightToggle, s.smoothLighting);
        q.text(lay.smoothLightToggle.x + 154.0f, lay.smoothLightToggle.y + 10.0f, 1.25f,
               "VERTEX INTERP + CREVICE AO", textMuted);

        // Row 5: Binary Greedy Meshing
        q.text(lx, lay.greedyMeshToggle.y + 9.0f, 1.5f, "GREEDY MESHING", textBright);
        drawToggle(lay.greedyMeshToggle, s.greedyMeshing);
        q.text(lay.greedyMeshToggle.x + 154.0f, lay.greedyMeshToggle.y + 10.0f, 1.25f,
               "64-BIT QUAD MERGE", textMuted);

        // Row 6: FOV
        char fovLabel[48];
        std::snprintf(fovLabel, sizeof(fovLabel), "FIELD OF VIEW: %d",
                      static_cast<int>(std::round(s.fov)));
        q.text(lx, lay.fovSlider.y + 6.0f, 1.5f, fovLabel, textBright);
        float fovNorm = (s.fov - 60.0f) / 45.0f;
        drawSlider(lay.fovSlider, fovNorm, textCyan);

        // Footer
        drawButton(lay.btnSettingsBack,   "< BACK",  false);
        drawButton(lay.btnSettingsResume, "RESUME",  true);
    }
}

} // namespace umbra
