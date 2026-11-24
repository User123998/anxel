#pragma once

// =============================================================================
// UMBRA VOXEL — Self-Contained C++20 PNG Decoder & Texture Array Loader
// Zero external dependencies (no libpng / stb_image required).
// Decodes standard RGBA/RGB/Grayscale/Indexed PNG files from:
//   - assets/texture/block/*.png  (18 layers for sampler2DArray uBlockTextures)
//   - assets/texture/items/*.png  (14 layers for sampler2DArray uItemTextures)
// =============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#elif defined(__APPLE__)
  #include <mach-o/dyld.h>
#elif defined(__linux__)
  #include <unistd.h>
#endif

namespace umbra {

// Layer indices for uBlockTextures (must match assets/shaders/voxel.frag)
enum class BlockTexLayer : uint32_t {
    GrassTop    = 0,
    GrassSide   = 1,
    GrassBottom = 2,
    Dirt        = 3,
    Stone       = 4,
    DeepSlate   = 5,
    TimberTop   = 6,
    TimberSide  = 7,
    Leaves      = 8,
    TorchSide   = 9,
    TorchTop    = 10,
    Glowstone   = 11,
    Water       = 12,
    Sand        = 13,
    Magma       = 14,
    Crystal     = 15,
    Snow        = 16,
    Glass       = 17,
    Count       = 18
};

// Layer indices for uItemTextures (matches BlockType 1..14 => layer = id - 1)
enum class ItemTexLayer : uint32_t {
    Grass     = 0,
    Dirt      = 1,
    Stone     = 2,
    DeepSlate = 3,
    Timber    = 4,
    Leaves    = 5,
    Torch     = 6,
    Glowstone = 7,
    Water     = 8,
    Sand      = 9,
    Magma     = 10,
    Crystal   = 11,
    Snow      = 12,
    Glass     = 13,
    Count     = 14
};

struct RgbaImage {
    uint32_t width  = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels; // RGBA8 tightly packed, row-major top-to-bottom
};

struct TextureArrayData {
    uint32_t width  = 16;
    uint32_t height = 16;
    uint32_t layers = 0;
    std::vector<uint8_t> pixels; // layers * width * height * 4 bytes (RGBA8)
    uint32_t loadedFromDisk = 0;
};

namespace detail {

inline std::filesystem::path executableDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::filesystem::path(buf).parent_path();
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size > 0) {
        std::string buf(size, '\0');
        if (_NSGetExecutablePath(buf.data(), &size) == 0) {
            return std::filesystem::weakly_canonical(std::filesystem::path(buf.c_str())).parent_path();
        }
    }
#elif defined(__linux__)
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return std::filesystem::path(buf).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

inline uint32_t readBE32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
}

// Minimal RFC 1951 DEFLATE BitReader & Huffman Decoder
struct BitReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t bytePos = 0;
    uint32_t bitBuf = 0;
    int bitCount = 0;

    bool ensure(int n) {
        while (bitCount < n) {
            if (bytePos >= size) return false;
            bitBuf |= uint32_t(data[bytePos++]) << bitCount;
            bitCount += 8;
        }
        return true;
    }

    uint32_t readBits(int n, bool& ok) {
        if (n == 0) return 0;
        if (!ensure(n)) { ok = false; return 0; }
        uint32_t v = bitBuf & ((1u << n) - 1u);
        bitBuf >>= n;
        bitCount -= n;
        return v;
    }

    void alignByte() {
        int drop = bitCount & 7;
        bitBuf >>= drop;
        bitCount -= drop;
    }

    uint8_t readAlignedByte(bool& ok) {
        if (bitCount >= 8) {
            uint8_t v = static_cast<uint8_t>(bitBuf & 0xFFu);
            bitBuf >>= 8;
            bitCount -= 8;
            return v;
        }
        if (bytePos >= size) { ok = false; return 0; }
        return data[bytePos++];
    }
};

struct HuffmanTree {
    static constexpr int MAX_BITS = 15;
    std::array<int, MAX_BITS + 1> count{};
    std::vector<int> symbols;

    bool build(const int* lengths, int numSymbols) {
        count.fill(0);
        int maxLen = 0;
        for (int i = 0; i < numSymbols; ++i) {
            int l = lengths[i];
            if (l < 0 || l > MAX_BITS) return false;
            if (l > 0) {
                count[l]++;
                maxLen = std::max(maxLen, l);
            }
        }
        if (maxLen == 0) {
            symbols.clear();
            return true;
        }
        std::array<int, MAX_BITS + 1> offsets{};
        int total = 0;
        for (int bits = 1; bits <= MAX_BITS; ++bits) {
            offsets[bits] = total;
            total += count[bits];
        }
        symbols.assign(static_cast<size_t>(total), 0);
        for (int sym = 0; sym < numSymbols; ++sym) {
            int l = lengths[sym];
            if (l > 0) {
                symbols[static_cast<size_t>(offsets[l]++)] = sym;
            }
        }
        return true;
    }

    int decode(BitReader& br, bool& ok) const {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len <= MAX_BITS; ++len) {
            uint32_t b = br.readBits(1, ok);
            if (!ok) return -1;
            code = (code << 1) | int(b);
            int c = count[len];
            if (code - c < first) {
                return symbols[static_cast<size_t>(index + (code - first))];
            }
            index += c;
            first = (first + c) << 1;
        }
        ok = false;
        return -1;
    }
};

inline bool inflateZlib(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& out) {
    if (srcSize < 6) return false;
    uint8_t cmf = src[0];
    uint8_t flg = src[1];
    if ((cmf & 0x0Fu) != 8u) return false; // Must be DEFLATE
    if (((uint32_t(cmf) << 8) | flg) % 31u != 0u) return false;
    size_t offset = 2;
    if (flg & 0x20u) {
        if (srcSize < 10) return false;
        offset += 4; // FDICT
    }

    BitReader br;
    br.data = src + offset;
    br.size = srcSize - offset;

    static constexpr int LEN_BASE[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    static constexpr int LEN_EXTRA[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
    };
    static constexpr int DIST_BASE[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
        8193, 12289, 16385, 24577
    };
    static constexpr int DIST_EXTRA[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
    };
    static constexpr int CL_ORDER[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };

    out.clear();
    bool ok = true;
    bool finalBlock = false;

    while (!finalBlock && ok) {
        finalBlock = (br.readBits(1, ok) != 0);
        uint32_t btype = br.readBits(2, ok);
        if (!ok) return false;

        if (btype == 0) {
            // Uncompressed stored block
            br.alignByte();
            uint16_t len  = uint16_t(br.readAlignedByte(ok)) | (uint16_t(br.readAlignedByte(ok)) << 8);
            uint16_t nlen = uint16_t(br.readAlignedByte(ok)) | (uint16_t(br.readAlignedByte(ok)) << 8);
            if (!ok || uint16_t(len ^ 0xFFFFu) != nlen) return false;
            out.reserve(out.size() + len);
            for (uint16_t i = 0; i < len; ++i) {
                uint8_t b = br.readAlignedByte(ok);
                if (!ok) return false;
                out.push_back(b);
            }
        } else if (btype == 1 || btype == 2) {
            HuffmanTree litTree;
            HuffmanTree distTree;
            if (btype == 1) {
                int litLens[288];
                for (int i = 0; i <= 143; ++i) litLens[i] = 8;
                for (int i = 144; i <= 255; ++i) litLens[i] = 9;
                for (int i = 256; i <= 279; ++i) litLens[i] = 7;
                for (int i = 280; i <= 287; ++i) litLens[i] = 8;
                int distLens[32];
                for (int i = 0; i < 32; ++i) distLens[i] = 5;
                if (!litTree.build(litLens, 288) || !distTree.build(distLens, 32)) return false;
            } else {
                int hlit  = int(br.readBits(5, ok)) + 257;
                int hdist = int(br.readBits(5, ok)) + 1;
                int hclen = int(br.readBits(4, ok)) + 4;
                if (!ok || hlit > 286 || hdist > 30) return false;

                int clLens[19]{};
                for (int i = 0; i < hclen; ++i) {
                    clLens[CL_ORDER[i]] = int(br.readBits(3, ok));
                }
                if (!ok) return false;
                HuffmanTree clTree;
                if (!clTree.build(clLens, 19)) return false;

                std::vector<int> lengths(static_cast<size_t>(hlit + hdist), 0);
                int idx = 0;
                while (idx < hlit + hdist) {
                    int sym = clTree.decode(br, ok);
                    if (!ok || sym < 0) return false;
                    if (sym < 16) {
                        lengths[static_cast<size_t>(idx++)] = sym;
                    } else if (sym == 16) {
                        if (idx == 0) return false;
                        int repeat = int(br.readBits(2, ok)) + 3;
                        int prev = lengths[static_cast<size_t>(idx - 1)];
                        if (!ok || idx + repeat > hlit + hdist) return false;
                        for (int r = 0; r < repeat; ++r) lengths[static_cast<size_t>(idx++)] = prev;
                    } else if (sym == 17) {
                        int repeat = int(br.readBits(3, ok)) + 3;
                        if (!ok || idx + repeat > hlit + hdist) return false;
                        for (int r = 0; r < repeat; ++r) lengths[static_cast<size_t>(idx++)] = 0;
                    } else if (sym == 18) {
                        int repeat = int(br.readBits(7, ok)) + 11;
                        if (!ok || idx + repeat > hlit + hdist) return false;
                        for (int r = 0; r < repeat; ++r) lengths[static_cast<size_t>(idx++)] = 0;
                    } else {
                        return false;
                    }
                }
                if (!litTree.build(lengths.data(), hlit) ||
                    !distTree.build(lengths.data() + hlit, hdist)) {
                    return false;
                }
            }

            // Decode compressed stream
            for (;;) {
                int sym = litTree.decode(br, ok);
                if (!ok || sym < 0) return false;
                if (sym < 256) {
                    out.push_back(static_cast<uint8_t>(sym));
                } else if (sym == 256) {
                    break;
                } else if (sym <= 285) {
                    int lenIdx = sym - 257;
                    int length = LEN_BASE[lenIdx] + int(br.readBits(LEN_EXTRA[lenIdx], ok));
                    int distSym = distTree.decode(br, ok);
                    if (!ok || distSym < 0 || distSym >= 30) return false;
                    int dist = DIST_BASE[distSym] + int(br.readBits(DIST_EXTRA[distSym], ok));
                    if (!ok || dist <= 0 || static_cast<size_t>(dist) > out.size()) return false;
                    for (int k = 0; k < length; ++k) {
                        uint8_t b = out[out.size() - static_cast<size_t>(dist)];
                        out.push_back(b);
                    }
                } else {
                    return false;
                }
            }
        } else {
            return false; // Invalid block type 3
        }
    }
    return ok;
}

inline uint8_t paethPredictor(int a, int b, int c) {
    int p = a + b - c;
    int pa = std::abs(p - a);
    int pb = std::abs(p - b);
    int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<uint8_t>(a);
    if (pb <= pc) return static_cast<uint8_t>(b);
    return static_cast<uint8_t>(c);
}

inline RgbaImage resizeNearest(const RgbaImage& src, uint32_t dstW, uint32_t dstH) {
    if (src.width == dstW && src.height == dstH) return src;
    RgbaImage out;
    out.width = dstW;
    out.height = dstH;
    out.pixels.resize(static_cast<size_t>(dstW) * dstH * 4u);
    for (uint32_t y = 0; y < dstH; ++y) {
        uint32_t sy = (y * src.height) / dstH;
        for (uint32_t x = 0; x < dstW; ++x) {
            uint32_t sx = (x * src.width) / dstW;
            const uint8_t* sp = &src.pixels[(static_cast<size_t>(sy) * src.width + sx) * 4u];
            uint8_t* dp = &out.pixels[(static_cast<size_t>(y) * dstW + x) * 4u];
            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
        }
    }
    return out;
}

inline RgbaImage makeFallbackTile(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    RgbaImage img;
    img.width = 16;
    img.height = 16;
    img.pixels.resize(16u * 16u * 4u);
    for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 16; ++x) {
            bool checker = ((x / 4 + y / 4) & 1u) != 0;
            float s = checker ? 1.0f : 0.82f;
            size_t i = (static_cast<size_t>(y) * 16u + x) * 4u;
            img.pixels[i + 0] = static_cast<uint8_t>(std::clamp(int(r * s), 0, 255));
            img.pixels[i + 1] = static_cast<uint8_t>(std::clamp(int(g * s), 0, 255));
            img.pixels[i + 2] = static_cast<uint8_t>(std::clamp(int(b * s), 0, 255));
            img.pixels[i + 3] = a;
        }
    }
    return img;
}

} // namespace detail

// Resolves a relative asset path like "assets/texture/block/grass_top.png"
// across working directory, build/ directory, and executable-relative paths.
inline std::filesystem::path resolveAssetPath(const std::string& relativePath) {
    namespace fs = std::filesystem;
    const fs::path rel(relativePath);
    const fs::path exeDir = detail::executableDir();

    const std::array<fs::path, 6> candidates = {
        rel,
        exeDir / rel,
        fs::path("build") / rel,
        exeDir / ".." / rel,
        fs::path("..") / rel,
        exeDir / ".." / ".." / rel
    };

    std::error_code ec;
    for (const auto& c : candidates) {
        if (fs::exists(c, ec) && fs::is_regular_file(c, ec)) {
            return c;
        }
    }
    return rel;
}

// Decodes a PNG file from disk into tightly packed RGBA8 pixels.
inline bool loadPngRGBA(const std::filesystem::path& filePath, RgbaImage& outImage) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    std::streamsize fsize = file.tellg();
    if (fsize < 33) return false;
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(fsize));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), fsize)) return false;

    static constexpr uint8_t SIG[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (std::memcmp(bytes.data(), SIG, 8) != 0) return false;

    uint32_t width = 0, height = 0;
    uint8_t bitDepth = 0, colorType = 0, interlace = 0;
    bool hasIHDR = false;
    std::vector<uint8_t> palette;
    std::vector<uint8_t> trns;
    std::vector<uint8_t> idat;

    size_t pos = 8;
    while (pos + 12 <= bytes.size()) {
        uint32_t len = detail::readBE32(bytes.data() + pos);
        const char* type = reinterpret_cast<const char*>(bytes.data() + pos + 4);
        if (pos + 12 + len > bytes.size()) return false;
        const uint8_t* chunk = bytes.data() + pos + 8;

        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (len < 13) return false;
            width     = detail::readBE32(chunk + 0);
            height    = detail::readBE32(chunk + 4);
            bitDepth  = chunk[8];
            colorType = chunk[9];
            uint8_t comp = chunk[10];
            uint8_t filt = chunk[11];
            interlace = chunk[12];
            if (width == 0 || height == 0 || width > 4096 || height > 4096) return false;
            if (comp != 0 || filt != 0 || interlace != 0) return false;
            hasIHDR = true;
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            palette.assign(chunk, chunk + len);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            trns.assign(chunk, chunk + len);
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), chunk, chunk + len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + len;
    }

    if (!hasIHDR || idat.empty()) return false;

    uint32_t channels = 0;
    switch (colorType) {
        case 0: channels = 1; break; // Grayscale
        case 2: channels = 3; break; // RGB
        case 3: channels = 1; break; // Indexed
        case 4: channels = 2; break; // Grayscale + Alpha
        case 6: channels = 4; break; // RGBA
        default: return false;
    }
    if (bitDepth != 1 && bitDepth != 2 && bitDepth != 4 && bitDepth != 8 && bitDepth != 16) {
        return false;
    }

    std::vector<uint8_t> inflated;
    if (!detail::inflateZlib(idat.data(), idat.size(), inflated)) return false;

    const size_t bitsPerPixel = static_cast<size_t>(channels) * bitDepth;
    const size_t stride = (static_cast<size_t>(width) * bitsPerPixel + 7u) / 8u;
    const size_t bpp = std::max<size_t>(1u, bitsPerPixel / 8u);
    if (inflated.size() < static_cast<size_t>(height) * (1u + stride)) return false;

    std::vector<uint8_t> raw(static_cast<size_t>(height) * stride, 0);
    for (uint32_t y = 0; y < height; ++y) {
        const size_t srcRowStart = static_cast<size_t>(y) * (1u + stride);
        const uint8_t filterType = inflated[srcRowStart];
        const uint8_t* srcRow = inflated.data() + srcRowStart + 1u;
        uint8_t* dstRow = raw.data() + static_cast<size_t>(y) * stride;
        const uint8_t* prevRow = (y > 0) ? (raw.data() + static_cast<size_t>(y - 1u) * stride) : nullptr;

        for (size_t i = 0; i < stride; ++i) {
            uint8_t x = srcRow[i];
            uint8_t a = (i >= bpp) ? dstRow[i - bpp] : 0;
            uint8_t b = prevRow ? prevRow[i] : 0;
            uint8_t c = (prevRow && i >= bpp) ? prevRow[i - bpp] : 0;
            uint8_t recon = x;
            switch (filterType) {
                case 0: recon = x; break;
                case 1: recon = static_cast<uint8_t>((x + a) & 0xFFu); break;
                case 2: recon = static_cast<uint8_t>((x + b) & 0xFFu); break;
                case 3: recon = static_cast<uint8_t>((x + ((int(a) + int(b)) >> 1)) & 0xFFu); break;
                case 4: recon = static_cast<uint8_t>((x + detail::paethPredictor(a, b, c)) & 0xFFu); break;
                default: return false;
            }
            dstRow[i] = recon;
        }
    }

    outImage.width = width;
    outImage.height = height;
    outImage.pixels.resize(static_cast<size_t>(width) * height * 4u);

    auto readSample = [&](const uint8_t* row, uint32_t pxIdx) -> uint32_t {
        if (bitDepth == 8) return row[pxIdx];
        if (bitDepth == 16) return row[pxIdx * 2u]; // High byte
        uint32_t ppb = 8u / bitDepth;
        uint8_t byteVal = row[pxIdx / ppb];
        uint32_t shift = (ppb - 1u - (pxIdx % ppb)) * bitDepth;
        return (byteVal >> shift) & ((1u << bitDepth) - 1u);
    };

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = raw.data() + static_cast<size_t>(y) * stride;
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t r = 0, g = 0, b = 0, a = 255;
            if (colorType == 6) {
                uint32_t step = (bitDepth == 16) ? 2u : 1u;
                const uint8_t* p = row + static_cast<size_t>(x) * 4u * step;
                r = p[0]; g = p[step]; b = p[2u * step]; a = p[3u * step];
            } else if (colorType == 2) {
                uint32_t step = (bitDepth == 16) ? 2u : 1u;
                const uint8_t* p = row + static_cast<size_t>(x) * 3u * step;
                r = p[0]; g = p[step]; b = p[2u * step];
                if (trns.size() >= 6 && r == trns[1] && g == trns[3] && b == trns[5]) a = 0;
            } else if (colorType == 4) {
                uint32_t step = (bitDepth == 16) ? 2u : 1u;
                const uint8_t* p = row + static_cast<size_t>(x) * 2u * step;
                r = g = b = p[0]; a = p[step];
            } else if (colorType == 0) {
                uint32_t s = readSample(row, x);
                uint32_t maxV = (bitDepth < 8) ? ((1u << bitDepth) - 1u) : 255u;
                uint8_t v = static_cast<uint8_t>((s * 255u) / maxV);
                r = g = b = v;
                if (trns.size() >= 2 && s == ((uint32_t(trns[0]) << 8) | trns[1])) a = 0;
            } else if (colorType == 3) {
                uint32_t idx = readSample(row, x);
                if (idx * 3u + 2u < palette.size()) {
                    r = palette[idx * 3u + 0u];
                    g = palette[idx * 3u + 1u];
                    b = palette[idx * 3u + 2u];
                }
                if (idx < trns.size()) a = trns[idx];
            }
            size_t dst = (static_cast<size_t>(y) * width + x) * 4u;
            outImage.pixels[dst + 0] = r;
            outImage.pixels[dst + 1] = g;
            outImage.pixels[dst + 2] = b;
            outImage.pixels[dst + 3] = a;
        }
    }
    return true;
}

struct TextureSpec {
    const char* filename;
    uint8_t fallbackR;
    uint8_t fallbackG;
    uint8_t fallbackB;
    uint8_t fallbackA;
};

inline const std::array<TextureSpec, static_cast<size_t>(BlockTexLayer::Count)>& blockTextureSpecs() {
    static const std::array<TextureSpec, static_cast<size_t>(BlockTexLayer::Count)> kSpecs = {{
        {"grass_top.png",    92, 168,  64, 255},
        {"grass_side.png",  112, 148,  68, 255},
        {"grass_bottom.png",128,  92,  62, 255},
        {"dirt.png",        130,  94,  64, 255},
        {"stone.png",       138, 144, 152, 255},
        {"deepslate.png",    70,  76,  86, 255},
        {"timber_top.png",  152, 114,  74, 255},
        {"timber_side.png", 118,  84,  52, 255},
        {"leaves.png",       54, 134,  56, 255},
        {"torch_side.png",  255, 198,  72, 255},
        {"torch_top.png",   255, 240, 160, 255},
        {"glowstone.png",   248, 212, 116, 255},
        {"water.png",        36, 128, 198, 210},
        {"sand.png",        222, 206, 154, 255},
        {"magma.png",       232,  92,  26, 255},
        {"crystal.png",      94, 232, 246, 255},
        {"snow.png",        240, 246, 252, 255},
        {"glass.png",       202, 234, 248, 120},
    }};
    return kSpecs;
}

inline const std::array<TextureSpec, static_cast<size_t>(ItemTexLayer::Count)>& itemTextureSpecs() {
    static const std::array<TextureSpec, static_cast<size_t>(ItemTexLayer::Count)> kSpecs = {{
        {"grass.png",        92, 168,  64, 255},
        {"dirt.png",        130,  94,  64, 255},
        {"stone.png",       138, 144, 152, 255},
        {"deepslate.png",    70,  76,  86, 255},
        {"timber.png",      118,  84,  52, 255},
        {"leaves.png",       54, 134,  56, 255},
        {"torch.png",       255, 198,  72, 255},
        {"glowstone.png",   248, 212, 116, 255},
        {"water.png",        36, 128, 198, 225},
        {"sand.png",        222, 206, 154, 255},
        {"magma.png",       232,  92,  26, 255},
        {"crystal.png",      94, 232, 246, 255},
        {"snow.png",        240, 246, 252, 255},
        {"glass.png",       202, 234, 248, 180},
    }};
    return kSpecs;
}

template <size_t N>
inline TextureArrayData loadTextureArrayFromDir(
    const std::string& assetSubdir,
    const std::array<TextureSpec, N>& specs) {
    std::vector<RgbaImage> imgs(N);
    uint32_t maxW = 16;
    uint32_t maxH = 16;
    uint32_t loaded = 0;

    for (size_t i = 0; i < N; ++i) {
        const std::string rel = assetSubdir + "/" + specs[i].filename;
        const auto resolved = resolveAssetPath(rel);
        if (loadPngRGBA(resolved, imgs[i])) {
            maxW = std::max(maxW, imgs[i].width);
            maxH = std::max(maxH, imgs[i].height);
            loaded++;
        } else {
            std::cerr << "[UMBRA] Warning: could not load texture '" << rel
                      << "' — using fallback tile.\n";
            imgs[i] = detail::makeFallbackTile(
                specs[i].fallbackR, specs[i].fallbackG, specs[i].fallbackB, specs[i].fallbackA);
        }
    }

    TextureArrayData arr;
    arr.width = maxW;
    arr.height = maxH;
    arr.layers = static_cast<uint32_t>(N);
    arr.loadedFromDisk = loaded;
    const size_t layerBytes = static_cast<size_t>(maxW) * maxH * 4u;
    arr.pixels.resize(layerBytes * N);

    for (size_t i = 0; i < N; ++i) {
        RgbaImage normalized = detail::resizeNearest(imgs[i], maxW, maxH);
        std::memcpy(arr.pixels.data() + i * layerBytes, normalized.pixels.data(), layerBytes);
    }

    std::cout << "[UMBRA] Loaded " << loaded << "/" << N << " PNG textures from "
              << assetSubdir << " (" << maxW << "x" << maxH << " RGBA8 array)\n";
    return arr;
}

inline TextureArrayData loadBlockTextureArray() {
    return loadTextureArrayFromDir("assets/texture/block", blockTextureSpecs());
}

inline TextureArrayData loadItemTextureArray() {
    return loadTextureArrayFromDir("assets/texture/items", itemTextureSpecs());
}

} // namespace umbra
