#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace umbra::math {

// =============================================================================
// High-Performance Power-of-Two Chunk Coordinate Math & Zero-Alloc BFS Queues
// CHUNK_SIZE == 16 => bit-shifts replace expensive signed division/modulo!
// =============================================================================

inline constexpr int CHUNK_SHIFT  = 4;
inline constexpr int CHUNK_SIZE   = 1 << CHUNK_SHIFT; // 16
inline constexpr int CHUNK_MASK   = CHUNK_SIZE - 1;   // 15
inline constexpr int CHUNK_HEIGHT = 128;              // Tall alpine peaks (up to Y=115), deep oceans & vast caverns
inline constexpr int CHUNK_VOLUME = CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE; // 32768 voxels

// Single-cycle signed arithmetic right shift (guaranteed two's complement in C++20)
inline constexpr int chunkFromWorld(int w) noexcept {
    return w >> CHUNK_SHIFT;
}

// Single-cycle positive bitmask modulo [0..15] even for negative infinite coordinates
inline constexpr int localFromWorld(int w) noexcept {
    return w & CHUNK_MASK;
}

// Zero-multiply 3D -> 1D chunk voxel index: (y * 256) | (lz * 16) | lx
inline constexpr size_t chunkVoxelIndex(int lx, int y, int lz) noexcept {
    return static_cast<size_t>((y << 8) | (lz << 4) | lx);
}

// Fast 64-bit SplitMix chunk coordinate hash (low collision, single-cycle ops)
inline constexpr size_t hashChunkCoord(int cx, int cz) noexcept {
    uint64_t a = static_cast<uint64_t>(static_cast<uint32_t>(cx));
    uint64_t b = static_cast<uint64_t>(static_cast<uint32_t>(cz));
    uint64_t k = (a << 32) ^ (b * 0x9e3779b97f4a7c15ULL);
    k ^= (k >> 33);
    k *= 0xff51afd7ed558ccdULL;
    k ^= (k >> 33);
    return static_cast<size_t>(k);
}

// Cache-friendly reusable contiguous FIFO queue (replaces slow std::queue/std::deque!)
// Zero heap allocations per frame once warmed up.
template <typename T>
class FlatQueue {
public:
    void reserve(size_t n) { data_.reserve(n); }

    void clear() noexcept {
        data_.clear();
        head_ = 0;
    }

    bool empty() const noexcept { return head_ >= data_.size(); }
    size_t size() const noexcept { return data_.size() - head_; }

    void push(const T& v) { data_.push_back(v); }
    void push(T&& v)      { data_.push_back(static_cast<T&&>(v)); }

    T pop() noexcept {
        T val = data_[head_++];
        if (head_ == data_.size()) {
            data_.clear();
            head_ = 0;
        }
        return val;
    }

private:
    std::vector<T> data_;
    size_t         head_ = 0;
};

} // namespace umbra::math
