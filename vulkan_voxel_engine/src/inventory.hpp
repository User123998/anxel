#pragma once

#include "types.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

namespace umbra {

// =============================================================================
// Player Inventory & Hotbar System (9 Hotbar + 27 Backpack Slots = 36 Total)
// Dynamic handheld lighting is strictly driven by the held slot's blockEmission!
// =============================================================================

struct InventorySlot {
    BlockType block = BlockType::Air;
    uint16_t  count = 0;

    constexpr bool empty() const noexcept {
        return block == BlockType::Air || count == 0;
    }
};

class PlayerInventory {
public:
    static constexpr int HOTBAR_SLOTS   = 9;
    static constexpr int BACKPACK_ROWS  = 3;
    static constexpr int BACKPACK_COLS  = 9;
    static constexpr int TOTAL_SLOTS    = HOTBAR_SLOTS + BACKPACK_ROWS * BACKPACK_COLS; // 36
    static constexpr uint16_t MAX_STACK = 99;

    PlayerInventory() {
        resetStarterKit();
    }

    void resetStarterKit() {
        for (auto& s : slots_) s = InventorySlot{};
        selectedHotbar_ = 0;
        open_ = false;
        pickedSlot_ = -1;

        // Hotbar (Keys 1-9): mix of light emitters, transparent Glass & solid building blocks
        // Holding slot 1 (Torch), 2 (Glowstone), 5 (Magma), or 6 (Crystal) emits real occluded light!
        // Holding slot 3 (Stone), 4 (Timber), 7 (Grass), 8 (Glass), or 9 (Empty) emits ZERO light!
        slots_[0] = {BlockType::Torch,     64};
        slots_[1] = {BlockType::Glowstone, 32};
        slots_[2] = {BlockType::Stone,     64};
        slots_[3] = {BlockType::Timber,    64};
        slots_[4] = {BlockType::Magma,     24};
        slots_[5] = {BlockType::Crystal,   24};
        slots_[6] = {BlockType::Grass,     64};
        slots_[7] = {BlockType::Glass,     64};
        slots_[8] = {BlockType::Air,        0}; // Empty hand slot for pitch-dark stealth / exploring

        // Backpack storage (opened via I or Tab)
        slots_[9]  = {BlockType::Dirt,      64};
        slots_[10] = {BlockType::Sand,      64};
        slots_[11] = {BlockType::DeepSlate, 64};
        slots_[12] = {BlockType::Leaves,    64};
        slots_[13] = {BlockType::Torch,     64};
        slots_[14] = {BlockType::Glowstone, 32};
        slots_[15] = {BlockType::Stone,     64};
        slots_[16] = {BlockType::Timber,    64};
        slots_[17] = {BlockType::Crystal,   32};
        slots_[18] = {BlockType::Snow,      64};
        slots_[19] = {BlockType::Glass,     64};
    }

    int selectedHotbar() const noexcept { return selectedHotbar_; }

    void selectHotbar(int index) noexcept {
        if (index < 0) index = HOTBAR_SLOTS - 1;
        if (index >= HOTBAR_SLOTS) index = 0;
        selectedHotbar_ = index;
    }

    void scrollHotbar(int delta) noexcept {
        int next = (selectedHotbar_ + delta) % HOTBAR_SLOTS;
        if (next < 0) next += HOTBAR_SLOTS;
        selectedHotbar_ = next;
    }

    bool isOpen() const noexcept { return open_; }
    void setOpen(bool v) noexcept {
        open_ = v;
        if (!open_) pickedSlot_ = -1;
    }
    void toggleOpen() noexcept { setOpen(!open_); }

    int pickedSlot() const noexcept { return pickedSlot_; }

    const InventorySlot& slot(int idx) const noexcept {
        return slots_[static_cast<size_t>(std::clamp(idx, 0, TOTAL_SLOTS - 1))];
    }

    const InventorySlot& heldSlot() const noexcept {
        return slots_[static_cast<size_t>(selectedHotbar_)];
    }

    BlockType heldBlock() const noexcept {
        const auto& s = heldSlot();
        return s.empty() ? BlockType::Air : s.block;
    }

    // Core gameplay rule: dynamic player lighting ONLY comes out when holding a light source block!
    uint8_t heldLightEmission() const noexcept {
        const auto& s = heldSlot();
        if (s.empty()) return 0;
        return blockEmission(s.block);
    }

    bool isHoldingLightSource() const noexcept {
        return heldLightEmission() > 0;
    }

    // Consumes 1 block from active hotbar slot when placing in world
    bool consumeHeld(BlockType& outBlock) noexcept {
        auto& s = slots_[static_cast<size_t>(selectedHotbar_)];
        if (s.empty()) return false;
        outBlock = s.block;
        if (s.count > 0) --s.count;
        if (s.count == 0) s.block = BlockType::Air;
        return true;
    }

    // Adds a mined block into matching stack or first empty slot
    bool addBlock(BlockType b, uint16_t amount = 1) noexcept {
        if (b == BlockType::Air || b == BlockType::Water || amount == 0) return false;

        // 1. Merge into existing stack
        for (auto& s : slots_) {
            if (s.block == b && s.count < MAX_STACK) {
                uint16_t space = static_cast<uint16_t>(MAX_STACK - s.count);
                uint16_t add   = std::min(space, amount);
                s.count = static_cast<uint16_t>(s.count + add);
                amount  = static_cast<uint16_t>(amount - add);
                if (amount == 0) return true;
            }
        }
        // 2. Place into first empty slot
        for (auto& s : slots_) {
            if (s.empty()) {
                s.block = b;
                s.count = std::min(MAX_STACK, amount);
                return true;
            }
        }
        return false;
    }

    // Click-to-swap / merge slots while Inventory overlay is open
    void clickSlot(int idx) noexcept {
        if (idx < 0 || idx >= TOTAL_SLOTS) return;
        if (pickedSlot_ < 0) {
            if (!slots_[static_cast<size_t>(idx)].empty()) {
                pickedSlot_ = idx;
            }
            return;
        }
        if (pickedSlot_ == idx) {
            pickedSlot_ = -1;
            return;
        }
        auto& src = slots_[static_cast<size_t>(pickedSlot_)];
        auto& dst = slots_[static_cast<size_t>(idx)];
        if (src.block == dst.block && !src.empty()) {
            uint16_t space = static_cast<uint16_t>(MAX_STACK - dst.count);
            uint16_t moved = std::min(space, src.count);
            dst.count = static_cast<uint16_t>(dst.count + moved);
            src.count = static_cast<uint16_t>(src.count - moved);
            if (src.count == 0) src.block = BlockType::Air;
        } else {
            std::swap(src, dst);
        }
        pickedSlot_ = -1;
    }

    static const char* blockDisplayName(BlockType b) noexcept {
        switch (b) {
            case BlockType::Air:       return "EMPTY HAND";
            case BlockType::Grass:     return "GRASS";
            case BlockType::Dirt:      return "DIRT";
            case BlockType::Stone:     return "STONE";
            case BlockType::DeepSlate: return "DEEPSLATE";
            case BlockType::Timber:    return "TIMBER";
            case BlockType::Leaves:    return "LEAVES";
            case BlockType::Torch:     return "TORCH";
            case BlockType::Glowstone: return "GLOWSTONE";
            case BlockType::Water:     return "WATER";
            case BlockType::Sand:      return "SAND";
            case BlockType::Magma:     return "MAGMA";
            case BlockType::Crystal:   return "CRYSTAL";
            case BlockType::Snow:      return "SNOW";
            case BlockType::Glass:     return "GLASS";
            default:                   return "BLOCK";
        }
    }

private:
    std::array<InventorySlot, TOTAL_SLOTS> slots_{};
    int  selectedHotbar_ = 0;
    bool open_           = false;
    int  pickedSlot_     = -1;
};

} // namespace umbra
