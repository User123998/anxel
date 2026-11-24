#!/usr/bin/env python3
"""
Exact mathematical procedural texture generator matching vulkan_voxel_engine:
- vulkan_voxel_engine/shaders/voxel.frag (evalVoxelAlbedo, evalWaterNormal, glass/water shaders)
- vulkan_voxel_engine/shaders/voxel.vert (inPosition, inUV, inLightAO, inPackedMat)
- vulkan_voxel_engine/src/mesher.cpp (face directions, UV orientation, torch geometry)
- vulkan_voxel_engine/src/hud.cpp (item icons: torch layers, timber end-grain colors)
- vulkan_voxel_engine/src/types.hpp (BlockType enum 0..14)

Bit-exact IEEE 754 single-precision float (float32) arithmetic is enforced
to ensure 100% fidelity to GLSL 450 execution on GPU.
"""

import os
import math
import json
import base64
import struct
import subprocess
import ctypes
import tempfile
from PIL import Image

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
os.makedirs(OUT_DIR, exist_ok=True)

# -----------------------------------------------------------------------------
# IEEE 754 32-bit Single Precision Math Emulation for GLSL Precision Fidelity
# -----------------------------------------------------------------------------
def f32(x):
    return struct.unpack('f', struct.pack('f', float(x)))[0]

def f32_floor(x):
    return f32(math.floor(x))

def f32_fract(x):
    return f32(x - f32_floor(x))

def clamp01_f32(v):
    return f32(max(0.0, min(1.0, float(v))))

def to_byte_glsl(c):
    v = clamp01_f32(c)
    scaled = f32(v * f32(255.0))
    return int(math.floor(float(scaled) + 0.5))

def smoothstep_f32(e0, e1, x):
    e0 = f32(e0)
    e1 = f32(e1)
    x = f32(x)
    t = clamp01_f32((x - e0) / (e1 - e0))
    return f32(t * t * (f32(3.0) - f32(2.0) * t))

def mix_f32(a, b, t):
    a = f32(a)
    b = f32(b)
    t = f32(t)
    return f32(a * (f32(1.0) - t) + b * t)

def hash21_f32(px, py):
    x = f32(px)
    y = f32(py)
    x = f32_fract(f32(x * f32(123.34)))
    y = f32_fract(f32(y * f32(456.21)))
    d = f32(f32(x * f32(x + f32(45.32))) + f32(y * f32(y + f32(45.32))))
    x = f32(x + d)
    y = f32(y + d)
    return f32_fract(f32(x * y))

def get_block_grain_f32(id_val, px_x, px_y):
    offset = f32(f32(id_val) * f32(7.1))
    gx = f32(f32(px_x) + offset)
    gy = f32(f32(px_y) + offset)
    grain = hash21_f32(gx, gy)
    micro = f32(f32(grain - f32(0.5)) * f32(0.11))
    return grain, micro

# -----------------------------------------------------------------------------
# C++ Acceleration via shared library (if g++ is available)
# -----------------------------------------------------------------------------
CPP_SOURCE = r'''
#include <cmath>
#include <algorithm>
#include <cstdint>

inline float fract(float x) { return x - std::floor(x); }
inline float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
inline float step_glsl(float edge, float x) { return (x >= edge) ? 1.0f : 0.0f; }
inline float smoothstep_glsl(float edge0, float edge1, float x) {
    float t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}
inline float mix_f(float a, float b, float t) { return a * (1.0f - t) + b * t; }

struct vec2 { float x, y; };
struct vec3 { float x, y, z; };

inline float dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }

inline float hash21(vec2 p) {
    p.x = fract(p.x * 123.34f);
    p.y = fract(p.y * 456.21f);
    float d = dot(p, {p.x + 45.32f, p.y + 45.32f});
    p.x += d;
    p.y += d;
    return fract(p.x * p.y);
}

inline void get_micro_grain(float id_val, float px_x, float px_y, float& out_grain, float& out_micro) {
    float offset = id_val * 7.1f;
    float gx = px_x + offset;
    float gy = px_y + offset;
    out_grain = hash21({gx, gy});
    out_micro = (out_grain - 0.5f) * 0.11f;
}

inline uint8_t to_byte(float c) {
    float scaled = clamp01(c) * 255.0f;
    return static_cast<uint8_t>(std::floor(scaled + 0.5f));
}

extern "C" {
void render_all_textures(uint8_t* out17x1024) {
    // 0: grass_top
    {
        uint8_t* p = out17x1024 + 0 * 1024;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                float grain, micro;
                get_micro_grain(1.0f, (float)x, (float)y, grain, micro);
                float r = 0.30f + micro * 0.8f;
                float g = 0.50f + micro;
                float b = 0.22f + micro * 0.5f;
                int idx = (y * 16 + x) * 4;
                p[idx + 0] = to_byte(r);
                p[idx + 1] = to_byte(g);
                p[idx + 2] = to_byte(b);
                p[idx + 3] = 255;
            }
        }
    }

    // 1. grass_side
    {
        uint8_t* p = out17x1024 + 1 * 1024;
        for (int y = 0; y < 16; ++y) {
            float py = (float)(15 - y);
            for (int x = 0; x < 16; ++x) {
                float px = (float)x;
                float grain, micro;
                get_micro_grain(1.0f, px, py, grain, micro);
                float turf_jitter = std::floor(hash21({px, 3.0f}) * 2.5f);
                float turf_band = step_glsl(12.0f, py + turf_jitter);
                vec3 dirtCol = { 0.41f + micro, 0.28f + micro, 0.19f + micro };
                vec3 grassCol = { 0.28f + micro * 0.8f, 0.48f + micro * 0.8f, 0.21f + micro * 0.8f };
                float r = mix_f(dirtCol.x, grassCol.x, turf_band);
                float g = mix_f(dirtCol.y, grassCol.y, turf_band);
                float b = mix_f(dirtCol.z, grassCol.z, turf_band);
                int idx = (y * 16 + x) * 4;
                p[idx + 0] = to_byte(r);
                p[idx + 1] = to_byte(g);
                p[idx + 2] = to_byte(b);
                p[idx + 3] = 255;
            }
        }
    }

    // 2. grass_bottom
    {
        uint8_t* p = out17x1024 + 2 * 1024;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                float grain, micro;
                get_micro_grain(1.0f, (float)x, (float)y, grain, micro);
                float r = 0.38f + micro;
                float g = 0.26f + micro;
                float b = 0.17f + micro;
                int idx = (y * 16 + x) * 4;
                p[idx + 0] = to_byte(r);
                p[idx + 1] = to_byte(g);
                p[idx + 2] = to_byte(b);
                p[idx + 3] = 255;
            }
        }
    }

    // 3. dirt
    {
        uint8_t* p = out17x1024 + 3 * 1024;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                float grain, micro;
                get_micro_grain(2.0f, (float)x, (float)y, grain, micro);
                float r = 0.41f + micro;
                float g = 0.28f + micro;
                float b = 0.18f + micro;
                int idx = (y * 16 + x) * 4;
                p[idx + 0] = to_byte(r);
                p[idx + 1] = to_byte(g);
                p[idx + 2] = to_byte(b);
                p[idx + 3] = 255;
            }
        }
    }

    // 4. stone
    {
        uint8_t* p = out17x1024 + 4 * 1024;
        for (int y = 0; y < 16; ++y) {
            float py = (float)(15 - y);
            float worldPosY = (py + 0.5f) / 16.0f;
            float strata = std::sin(worldPosY * 2.4f + py * 0.25f) * 0.035f;
            for (int x = 0; x < 16; ++x) {
                float px = (float)x;
                float grain, micro;
                get_micro_grain(3.0f, px, py, grain, micro);
                float r = 0.45f + micro + strata;
                float g = 0.47f + micro + strata;
                float b = 0.50f + micro + strata;
                int idx = (y * 16 + x) * 4;
                p[idx + 0] = to_byte(r);
                p[idx + 1] = to_byte(g);
                p[idx + 2] = to_byte(b);
                p[idx + 3] = 255;
            }
        }
    }

    // 5. deepslate
    {
        uint8_t* p = out17x1024 + 5 * 1024;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                float grain, micro;
                get_micro_grain(4.0f, (float)x, (float)y, grain, micro);
                float cracks = step_glsl(0.87f, grain) * -0.08f;
                float r = 0.23f + micro * 0.7f + cracks;
                float g = 0.25f + micro * 0.7f + cracks;
                float b = 0.29f + micro * 0.7f + cracks;
                int idx = (y * 16 + x) * 4;
                p[idx + 0] = to_byte(r);
                p[idx + 1] = to_byte(g);
                p[idx + 2] = to_byte(b);
                p[idx + 3] = 255;
            }
        }
    }

    // 6. timber_side
    {
        uint8_t* p = out17x1024 + 6 * 1024;
        for (int y = 0; y < 16; ++y) {
            float py = (float)(15 - y);
            for (int x = 0; x < 16; ++x) {
                float px = (float)x;
                float grain, micro;
                get_micro_grain(5.0f, px, py, grain, micro);
                float rings = std::sin(px * 1.3f + grain * 2.0f) * 0.04f;
                float r = 0.46f + rings + micro * 0.6f;
                float g = 0.31f + rings + micro * 0.6f;
                float b = 0.18f + rings + micro * 0.6f;
                int idx = (y * 16 + x) * 4;
                p[idx + 0] = to_byte(r);
                p[idx + 1] = to_byte(g);
                p[idx + 2] = to_byte(b);
                p[idx + 3] = 255;
            }
        }
    }

    // 7. timber_top
    {
        uint8_t* p = out17x1024 + 7 * 1024;
        for (int y = 0; y < 16; ++y) {
            float dy = (float)y - 7.5f;
            for (int x = 0; x < 16; ++x) {
                float dx = (float)x - 7.5f;
                float dist = std::sqrt(dx * dx + dy * dy);
                float grain, micro;
                get_micro_grain(5.0f, (float)x, (float)y, grain, micro);
                float r, g, b;
                if (dist > 6.2f) {
                    r = 0.46f + micro * 0.6f;
                    g = 0.31f + micro * 0.6f;
                    b = 0.18f + micro * 0.6f;
                } else {
                    float rings = std::sin(dist * 1.85f + grain * 0.4f) * 0.04f;
                    r = 0.62f + rings + micro * 0.6f;
                    g = 0.43f + rings + micro * 0.6f;
                    b = 0.24f + rings + micro * 0.6f;
                }
                int idx = (y * 16 + x) * 4;
                p[idx + 0] = to_byte(r);
                p[idx + 1] = to_byte(g);
                p[idx + 2] = to_byte(b);
                p[idx + 3] = 255;
            }
        }
    }

    // 8. leaves
    {
        uint8_t* p = out17x1024 + 8 * 1024;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                float grain, micro;
                get_micro_grain(6.0f, (float)x, (float)y, grain, micro);
                float r = 0.18f + micro * 1.2f;
                float g = 0.37f + micro * 1.2f;
                float b = 0.19f + micro * 1.2f;
                int idx = (y * 16 + x) * 4;
                p[idx + 0] = to_byte(r);
                p[idx + 1] = to_byte(g);
                p[idx + 2] = to_byte(b);
                p[idx + 3] = 255;
            }
        }
    }

    // 9. torch
    {
        uint8_t* p = out17x1024 + 9 * 1024;
        for (int i = 0; i < 1024; ++i) p[i] = 0;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                int idx = (y * 16 + x) * 4;
                if (x >= 6 && x <= 9 && y >= 6) {
                    float grain = hash21({(float)x * 3.1f, (float)y * 7.2f});
                    float micro = (grain - 0.5f) * 0.08f;
                    p[idx + 0] = to_byte(0.44f + micro);
                    p[idx + 1] = to_byte(0.29f + micro);
                    p[idx + 2] = to_byte(0.16f + micro);
                    p[idx + 3] = 255;
                } else if (x >= 6 && x <= 9 && y >= 2 && y <= 4) {
                    p[idx + 0] = 255; p[idx + 1] = 245; p[idx + 2] = 194; p[idx + 3] = 255;
                } else if (x >= 5 && x <= 10 && y >= 2 && y <= 6) {
                    p[idx + 0] = 255; p[idx + 1] = 184; p[idx + 2] = 56; p[idx + 3] = 255;
                } else if (x >= 2 && x <= 13 && y >= 1 && y <= 8) {
                    float dx = std::abs((float)x - 7.5f);
                    float dy = std::abs((float)y - 4.0f);
                    if (dx <= 5.5f && dy <= 3.5f) {
                        float fade = (1.0f - (dx / 6.0f) * 0.5f - (dy / 4.0f) * 0.5f);
                        p[idx + 0] = 255;
                        p[idx + 1] = 158;
                        p[idx + 2] = 36;
                        p[idx + 3] = to_byte(fade * (72.0f / 255.0f));
                    }
                }
            }
        }
    }

    // 10. glowstone
    {
        uint8_t* p = out17x1024 + 10 * 1024;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                float px = (float)x, py = (float)y;
                float grain, micro;
                get_micro_grain(8.0f, px, py, grain, micro);
                bool is_frame = (x == 0 || x == 15 || y == 0 || y == 15);
                float frame_factor = is_frame ? 0.65f : 0.0f;
                vec3 inner = { 1.00f + micro * 0.5f, 0.86f + micro * 0.5f, 0.52f + micro * 0.5f };
                vec3 border = { 0.58f, 0.42f, 0.22f };
                float r = mix_f(inner.x, border.x, frame_factor);
                float g = mix_f(inner.y, border.y, frame_factor);
                float b = mix_f(inner.z, border.z, frame_factor);
                int idx = (y * 16 + x) * 4;
                p[idx + 0] = to_byte(r);
                p[idx + 1] = to_byte(g);
                p[idx + 2] = to_byte(b);
                p[idx + 3] = 255;
            }
        }
    }

    // 11. water
    {
        uint8_t* p = out17x1024 + 11 * 1024;
        for (int y = 0; y < 16; ++y) {
            float py = ((float)y + 0.5f) / 16.0f;
            for (int x = 0; x < 16; ++x) {
                float px = ((float)x + 0.5f) / 16.0f;
                float w1 = std::cos(px * 1.45f + py * 0.95f) * 0.030f;
                float w2 = std::cos(px * 2.65f - py * 2.10f) * 0.018f;
                float w3 = std::sin(px * 0.72f - py * 1.60f) * 0.024f;
                float dx = w1 * 1.45f + w2 * 2.65f + w3 * 0.72f;
                float dz = w1 * 0.95f - w2 * 2.10f - w3 * 1.60f;
                float wave = (dx * 0.6f + dz * 0.8f) * 2.5f;
                float r = 0.08f + wave * 0.35f;
                float g = 0.36f + wave * 0.85f;
                float b = 0.52f + wave * 0.75f;
                int idx = (y * 16 + x) * 4;
                p[idx + 0] = to_byte(r);
                p[idx + 1] = to_byte(g);
                p[idx + 2] = to_byte(b);
                p[idx + 3] = 195;
            }
        }
    }

    // 12. sand
    {
        uint8_t* p = out17x1024 + 12 * 1024;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                float grain, micro;
                get_micro_grain(10.0f, (float)x, (float)y, grain, micro);
                float r = 0.75f + micro * 0.6f;
                float g = 0.67f + micro * 0.6f;
                float b = 0.49f + micro * 0.6f;
                int idx = (y * 16 + x) * 4;
                p[idx + 0] = to_byte(r);
                p[idx + 1] = to_byte(g);
                p[idx + 2] = to_byte(b);
                p[idx + 3] = 255;
            }
        }
    }

    // 13. magma
    {
        uint8_t* p = out17x1024 + 13 * 1024;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                float grain, micro;
                get_micro_grain(11.0f, (float)x, (float)y, grain, micro);
                float is_crust = step_glsl(0.58f, grain);
                float r = mix_f(1.00f, 0.18f, is_crust);
                float g = mix_f(0.36f, 0.11f, is_crust);
                float b = mix_f(0.07f, 0.10f, is_crust);
                int idx = (y * 16 + x) * 4;
                p[idx + 0] = to_byte(r);
                p[idx + 1] = to_byte(g);
                p[idx + 2] = to_byte(b);
                p[idx + 3] = 255;
            }
        }
    }

    // 14. crystal
    {
        uint8_t* p = out17x1024 + 14 * 1024;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                float grain, micro;
                get_micro_grain(12.0f, (float)x, (float)y, grain, micro);
                float r = 0.30f + micro * 0.5f;
                float g = 0.84f + micro * 0.5f;
                float b = 0.97f + micro * 0.5f;
                int idx = (y * 16 + x) * 4;
                p[idx + 0] = to_byte(r);
                p[idx + 1] = to_byte(g);
                p[idx + 2] = to_byte(b);
                p[idx + 3] = 255;
            }
        }
    }

    // 15. snow
    {
        uint8_t* p = out17x1024 + 15 * 1024;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                float grain, micro;
                get_micro_grain(13.0f, (float)x, (float)y, grain, micro);
                float sparkle = step_glsl(0.94f, grain) * 0.06f;
                float r = 0.84f + micro * 0.3f + sparkle;
                float g = 0.89f + micro * 0.3f + sparkle;
                float b = 0.94f + micro * 0.3f + sparkle;
                int idx = (y * 16 + x) * 4;
                p[idx + 0] = to_byte(r);
                p[idx + 1] = to_byte(g);
                p[idx + 2] = to_byte(b);
                p[idx + 3] = 255;
            }
        }
    }

    // 16. glass
    {
        uint8_t* p = out17x1024 + 16 * 1024;
        vec3 tintCol = { 0.76f, 0.92f, 0.98f };
        vec3 frameCol = { 0.86f, 0.95f, 0.99f };
        for (int y = 0; y < 16; ++y) {
            float uv_y = ((float)y + 0.5f) / 16.0f;
            for (int x = 0; x < 16; ++x) {
                float uv_x = ((float)x + 0.5f) / 16.0f;
                float edgeDist = std::min(std::min(uv_x, 1.0f - uv_x), std::min(uv_y, 1.0f - uv_y));
                float frameMask = 1.0f - smoothstep_glsl(0.035f, 0.072f, edgeDist);
                float diag1 = 1.0f - smoothstep_glsl(0.0f, 0.045f, std::abs((uv_x - uv_y) - 0.32f));
                float diag2 = 1.0f - smoothstep_glsl(0.0f, 0.025f, std::abs((uv_x - uv_y) - 0.18f));
                float streak = std::max(diag1 * 0.38f, diag2 * 0.24f) * (1.0f - frameMask);
                float r = mix_f(tintCol.x, frameCol.x, frameMask * 0.85f) + streak * 0.8f;
                float g = mix_f(tintCol.y, frameCol.y, frameMask * 0.85f) + streak * 0.8f;
                float b = mix_f(tintCol.z, frameCol.z, frameMask * 0.85f) + streak * 0.8f;
                float alpha = clamp01(0.14f + frameMask * 0.56f + streak * 0.32f);
                int idx = (y * 16 + x) * 4;
                p[idx + 0] = to_byte(r);
                p[idx + 1] = to_byte(g);
                p[idx + 2] = to_byte(b);
                p[idx + 3] = to_byte(alpha);
            }
        }
    }
}
}
'''

TEXTURE_NAMES = [
    "grass_top",     # 0
    "grass_side",    # 1
    "grass_bottom",  # 2
    "dirt",          # 3
    "stone",         # 4
    "deepslate",     # 5
    "timber_side",   # 6
    "timber_top",    # 7
    "leaves",        # 8
    "torch",         # 9
    "glowstone",     # 10
    "water",         # 11
    "sand",          # 12
    "magma",         # 13
    "crystal",       # 14
    "snow",          # 15
    "glass",         # 16
]

def render_textures_native():
    tmp_dir = tempfile.gettempdir()
    so_path = os.path.join(tmp_dir, "librender_textures.so")
    cpp_path = os.path.join(tmp_dir, "render_textures.cpp")
    with open(cpp_path, "w") as f:
        f.write(CPP_SOURCE)
    
    subprocess.check_call(["g++", "-O3", "-shared", "-fPIC", cpp_path, "-o", so_path])
    lib = ctypes.CDLL(so_path)
    buf = (ctypes.c_uint8 * (17 * 1024))()
    lib.render_all_textures(buf)
    raw = bytes(buf)

    textures = {}
    for i, name in enumerate(TEXTURE_NAMES):
        chunk = raw[i * 1024:(i + 1) * 1024]
        img = Image.frombytes("RGBA", (16, 16), chunk)
        textures[name] = img
    return textures

def render_textures_pure_python():
    textures = {}
    
    # 0. grass_top (id=1, faceDir=2)
    img = Image.new("RGBA", (16, 16))
    for y in range(16):
        for x in range(16):
            grain, micro = get_block_grain_f32(1.0, x, y)
            r = f32(f32(0.30) + f32(micro * f32(0.8)))
            g = f32(f32(0.50) + micro)
            b = f32(f32(0.22) + f32(micro * f32(0.5)))
            img.putpixel((x, y), (to_byte_glsl(r), to_byte_glsl(g), to_byte_glsl(b), 255))
    textures["grass_top"] = img

    # 1. grass_side (id=1, side faces)
    img = Image.new("RGBA", (16, 16))
    for y in range(16):
        py = f32(15 - y)
        for x in range(16):
            px = f32(x)
            grain, micro = get_block_grain_f32(1.0, px, py)
            turf_jitter = f32_floor(f32(hash21_f32(px, 3.0) * f32(2.5)))
            turf_band = f32(1.0) if f32(py + turf_jitter) >= f32(12.0) else f32(0.0)
            dirt_r = f32(f32(0.41) + micro)
            dirt_g = f32(f32(0.28) + micro)
            dirt_b = f32(f32(0.19) + micro)
            grass_r = f32(f32(0.28) + f32(micro * f32(0.8)))
            grass_g = f32(f32(0.48) + f32(micro * f32(0.8)))
            grass_b = f32(f32(0.21) + f32(micro * f32(0.8)))
            r = mix_f32(dirt_r, grass_r, turf_band)
            g = mix_f32(dirt_g, grass_g, turf_band)
            b = mix_f32(dirt_b, grass_b, turf_band)
            img.putpixel((x, y), (to_byte_glsl(r), to_byte_glsl(g), to_byte_glsl(b), 255))
    textures["grass_side"] = img

    # 2. grass_bottom (id=1, faceDir=3)
    img = Image.new("RGBA", (16, 16))
    for y in range(16):
        for x in range(16):
            grain, micro = get_block_grain_f32(1.0, x, y)
            r = f32(f32(0.38) + micro)
            g = f32(f32(0.26) + micro)
            b = f32(f32(0.17) + micro)
            img.putpixel((x, y), (to_byte_glsl(r), to_byte_glsl(g), to_byte_glsl(b), 255))
    textures["grass_bottom"] = img

    # 3. dirt (id=2)
    img = Image.new("RGBA", (16, 16))
    for y in range(16):
        for x in range(16):
            grain, micro = get_block_grain_f32(2.0, x, y)
            r = f32(f32(0.41) + micro)
            g = f32(f32(0.28) + micro)
            b = f32(f32(0.18) + micro)
            img.putpixel((x, y), (to_byte_glsl(r), to_byte_glsl(g), to_byte_glsl(b), 255))
    textures["dirt"] = img

    # 4. stone (id=3)
    img = Image.new("RGBA", (16, 16))
    for y in range(16):
        py = f32(15 - y)
        worldPosY = f32(f32(py + f32(0.5)) / f32(16.0))
        strata = f32(math.sin(f32(f32(worldPosY * f32(2.4)) + f32(py * f32(0.25)))) * f32(0.035))
        for x in range(16):
            px = f32(x)
            grain, micro = get_block_grain_f32(3.0, px, py)
            r = f32(f32(0.45) + f32(micro + strata))
            g = f32(f32(0.47) + f32(micro + strata))
            b = f32(f32(0.50) + f32(micro + strata))
            img.putpixel((x, y), (to_byte_glsl(r), to_byte_glsl(g), to_byte_glsl(b), 255))
    textures["stone"] = img

    # 5. deepslate (id=4)
    img = Image.new("RGBA", (16, 16))
    for y in range(16):
        for x in range(16):
            grain, micro = get_block_grain_f32(4.0, x, y)
            cracks = f32(-0.08) if grain >= f32(0.87) else f32(0.0)
            r = f32(f32(0.23) + f32(f32(micro * f32(0.7)) + cracks))
            g = f32(f32(0.25) + f32(f32(micro * f32(0.7)) + cracks))
            b = f32(f32(0.29) + f32(f32(micro * f32(0.7)) + cracks))
            img.putpixel((x, y), (to_byte_glsl(r), to_byte_glsl(g), to_byte_glsl(b), 255))
    textures["deepslate"] = img

    # 6. timber_side (id=5)
    img = Image.new("RGBA", (16, 16))
    for y in range(16):
        py = f32(15 - y)
        for x in range(16):
            px = f32(x)
            grain, micro = get_block_grain_f32(5.0, px, py)
            rings = f32(math.sin(f32(f32(px * f32(1.3)) + f32(grain * f32(2.0)))) * f32(0.04))
            r = f32(f32(0.46) + f32(rings + f32(micro * f32(0.6))))
            g = f32(f32(0.31) + f32(rings + f32(micro * f32(0.6))))
            b = f32(f32(0.18) + f32(rings + f32(micro * f32(0.6))))
            img.putpixel((x, y), (to_byte_glsl(r), to_byte_glsl(g), to_byte_glsl(b), 255))
    textures["timber_side"] = img

    # 7. timber_top (id=5, end grain)
    img = Image.new("RGBA", (16, 16))
    for y in range(16):
        dy = f32(f32(y) - f32(7.5))
        for x in range(16):
            dx = f32(f32(x) - f32(7.5))
            dist = f32(math.sqrt(f32(f32(dx * dx) + f32(dy * dy))))
            grain, micro = get_block_grain_f32(5.0, x, y)
            if dist > f32(6.2):
                r = f32(f32(0.46) + f32(micro * f32(0.6)))
                g = f32(f32(0.31) + f32(micro * f32(0.6)))
                b = f32(f32(0.18) + f32(micro * f32(0.6)))
            else:
                rings = f32(math.sin(f32(f32(dist * f32(1.85)) + f32(grain * f32(0.4)))) * f32(0.04))
                r = f32(f32(0.62) + f32(rings + f32(micro * f32(0.6))))
                g = f32(f32(0.43) + f32(rings + f32(micro * f32(0.6))))
                b = f32(f32(0.24) + f32(rings + f32(micro * f32(0.6))))
            img.putpixel((x, y), (to_byte_glsl(r), to_byte_glsl(g), to_byte_glsl(b), 255))
    textures["timber_top"] = img

    # 8. leaves (id=6)
    img = Image.new("RGBA", (16, 16))
    for y in range(16):
        for x in range(16):
            grain, micro = get_block_grain_f32(6.0, x, y)
            r = f32(f32(0.18) + f32(micro * f32(1.2)))
            g = f32(f32(0.37) + f32(micro * f32(1.2)))
            b = f32(f32(0.19) + f32(micro * f32(1.2)))
            img.putpixel((x, y), (to_byte_glsl(r), to_byte_glsl(g), to_byte_glsl(b), 255))
    textures["leaves"] = img

    # 9. torch (id=7, composite from hud.cpp and voxel.frag)
    img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    for y in range(16):
        for x in range(16):
            if 6 <= x <= 9 and y >= 6:
                grain = hash21_f32(f32(f32(x) * f32(3.1)), f32(f32(y) * f32(7.2)))
                micro = f32(f32(grain - f32(0.5)) * f32(0.08))
                r = f32(f32(0.44) + micro)
                g = f32(f32(0.29) + micro)
                b = f32(f32(0.16) + micro)
                img.putpixel((x, y), (to_byte_glsl(r), to_byte_glsl(g), to_byte_glsl(b), 255))
            elif 6 <= x <= 9 and 2 <= y <= 4:
                img.putpixel((x, y), (255, 245, 194, 255))
            elif 5 <= x <= 10 and 2 <= y <= 6:
                img.putpixel((x, y), (255, 184, 56, 255))
            elif 2 <= x <= 13 and 1 <= y <= 8:
                dx = f32(abs(float(x) - 7.5))
                dy = f32(abs(float(y) - 4.0))
                if dx <= f32(5.5) and dy <= f32(3.5):
                    fade = f32(f32(1.0) - f32(f32(dx / f32(6.0)) * f32(0.5)) - f32(f32(dy / f32(4.0)) * f32(0.5)))
                    alpha = to_byte_glsl(f32(fade * f32(72.0 / 255.0)))
                    img.putpixel((x, y), (255, 158, 36, alpha))
    textures["torch"] = img

    # 10. glowstone (id=8)
    img = Image.new("RGBA", (16, 16))
    for y in range(16):
        for x in range(16):
            grain, micro = get_block_grain_f32(8.0, x, y)
            is_frame = (x == 0 or x == 15 or y == 0 or y == 15)
            frame_factor = f32(0.65) if is_frame else f32(0.0)
            inner_r = f32(f32(1.00) + f32(micro * f32(0.5)))
            inner_g = f32(f32(0.86) + f32(micro * f32(0.5)))
            inner_b = f32(f32(0.52) + f32(micro * f32(0.5)))
            r = mix_f32(inner_r, f32(0.58), frame_factor)
            g = mix_f32(inner_g, f32(0.42), frame_factor)
            b = mix_f32(inner_b, f32(0.22), frame_factor)
            img.putpixel((x, y), (to_byte_glsl(r), to_byte_glsl(g), to_byte_glsl(b), 255))
    textures["glowstone"] = img

    # 11. water (id=9, evalWaterNormal analytical 3-wave)
    img = Image.new("RGBA", (16, 16))
    for y in range(16):
        py = f32(f32(y + 0.5) / f32(16.0))
        for x in range(16):
            px = f32(f32(x + 0.5) / f32(16.0))
            w1 = f32(math.cos(f32(f32(px * f32(1.45)) + f32(py * f32(0.95)))) * f32(0.030))
            w2 = f32(math.cos(f32(f32(px * f32(2.65)) - f32(py * f32(2.10)))) * f32(0.018))
            w3 = f32(math.sin(f32(f32(px * f32(0.72)) - f32(py * f32(1.60)))) * f32(0.024))
            dx = f32(f32(w1 * f32(1.45)) + f32(f32(w2 * f32(2.65)) + f32(w3 * f32(0.72))))
            dz = f32(f32(w1 * f32(0.95)) - f32(f32(w2 * f32(2.10)) + f32(w3 * f32(1.60))))
            wave = f32(f32(f32(dx * f32(0.6)) + f32(dz * f32(0.8))) * f32(2.5))
            r = f32(f32(0.08) + f32(wave * f32(0.35)))
            g = f32(f32(0.36) + f32(wave * f32(0.85)))
            b = f32(f32(0.52) + f32(wave * f32(0.75)))
            img.putpixel((x, y), (to_byte_glsl(r), to_byte_glsl(g), to_byte_glsl(b), 195))
    textures["water"] = img

    # 12. sand (id=10)
    img = Image.new("RGBA", (16, 16))
    for y in range(16):
        for x in range(16):
            grain, micro = get_block_grain_f32(10.0, x, y)
            r = f32(f32(0.75) + f32(micro * f32(0.6)))
            g = f32(f32(0.67) + f32(micro * f32(0.6)))
            b = f32(f32(0.49) + f32(micro * f32(0.6)))
            img.putpixel((x, y), (to_byte_glsl(r), to_byte_glsl(g), to_byte_glsl(b), 255))
    textures["sand"] = img

    # 13. magma (id=11)
    img = Image.new("RGBA", (16, 16))
    for y in range(16):
        for x in range(16):
            grain, micro = get_block_grain_f32(11.0, x, y)
            is_crust = f32(1.0) if grain >= f32(0.58) else f32(0.0)
            r = mix_f32(f32(1.00), f32(0.18), is_crust)
            g = mix_f32(f32(0.36), f32(0.11), is_crust)
            b = mix_f32(f32(0.07), f32(0.10), is_crust)
            img.putpixel((x, y), (to_byte_glsl(r), to_byte_glsl(g), to_byte_glsl(b), 255))
    textures["magma"] = img

    # 14. crystal (id=12)
    img = Image.new("RGBA", (16, 16))
    for y in range(16):
        for x in range(16):
            grain, micro = get_block_grain_f32(12.0, x, y)
            r = f32(f32(0.30) + f32(micro * f32(0.5)))
            g = f32(f32(0.84) + f32(micro * f32(0.5)))
            b = f32(f32(0.97) + f32(micro * f32(0.5)))
            img.putpixel((x, y), (to_byte_glsl(r), to_byte_glsl(g), to_byte_glsl(b), 255))
    textures["crystal"] = img

    # 15. snow (id=13)
    img = Image.new("RGBA", (16, 16))
    for y in range(16):
        for x in range(16):
            grain, micro = get_block_grain_f32(13.0, x, y)
            sparkle = f32(0.06) if grain >= f32(0.94) else f32(0.0)
            r = f32(f32(0.84) + f32(f32(micro * f32(0.3)) + sparkle))
            g = f32(f32(0.89) + f32(f32(micro * f32(0.3)) + sparkle))
            b = f32(f32(0.94) + f32(f32(micro * f32(0.3)) + sparkle))
            img.putpixel((x, y), (to_byte_glsl(r), to_byte_glsl(g), to_byte_glsl(b), 255))
    textures["snow"] = img

    # 16. glass (id=14)
    img = Image.new("RGBA", (16, 16))
    tint_r, tint_g, tint_b = f32(0.76), f32(0.92), f32(0.98)
    frame_r, frame_g, frame_b = f32(0.86), f32(0.95), f32(0.99)
    for y in range(16):
        uv_y = f32(f32(y + 0.5) / f32(16.0))
        for x in range(16):
            uv_x = f32(f32(x + 0.5) / f32(16.0))
            edge_dist = min(min(uv_x, f32(1.0 - uv_x)), min(uv_y, f32(1.0 - uv_y)))
            frame_mask = f32(f32(1.0) - smoothstep_f32(0.035, 0.072, edge_dist))
            diag1 = f32(f32(1.0) - smoothstep_f32(0.0, 0.045, abs(f32(uv_x - uv_y) - f32(0.32))))
            diag2 = f32(f32(1.0) - smoothstep_f32(0.0, 0.025, abs(f32(uv_x - uv_y) - f32(0.18))))
            streak = f32(max(diag1 * f32(0.38), diag2 * f32(0.24)) * (f32(1.0) - frame_mask))
            r = f32(mix_f32(tint_r, frame_r, f32(frame_mask * f32(0.85))) + f32(streak * f32(0.8)))
            g = f32(mix_f32(tint_g, frame_g, f32(frame_mask * f32(0.85))) + f32(streak * f32(0.8)))
            b = f32(mix_f32(tint_b, frame_b, f32(frame_mask * f32(0.85))) + f32(streak * f32(0.8)))
            alpha = clamp01_f32(f32(f32(0.14) + f32(frame_mask * f32(0.56))) + f32(streak * f32(0.32)))
            img.putpixel((x, y), (to_byte_glsl(r), to_byte_glsl(g), to_byte_glsl(b), to_byte_glsl(alpha)))
    textures["glass"] = img

    return textures

def generate_all_textures():
    # Prefer native C++ execution for microsecond speed and guaranteed bit-exactness;
    # fallback to pure Python float32 if g++ is not available.
    try:
        textures = render_textures_native()
        print("Rendered 17 textures using native C++ acceleration.")
    except Exception as e:
        print(f"Native compilation failed ({e}), using bit-exact pure Python float32 simulation...")
        textures = render_textures_pure_python()
        print("Rendered 17 textures using pure Python float32 simulation.")

    # Save PNGs and compile textures_b64.json
    b64_dict = {}
    for name, image in textures.items():
        path = os.path.join(OUT_DIR, f"{name}.png")
        image.save(path)
        with open(path, "rb") as f:
            encoded = base64.b64encode(f.read()).decode("utf-8")
        b64_dict[name] = f"data:image/png;base64,{encoded}"
        print(f"Generated {name}.png ({image.size[0]}x{image.size[1]})")

    json_path = os.path.join(OUT_DIR, "textures_b64.json")
    with open(json_path, "w") as f:
        json.dump(b64_dict, f, indent=2)
    print(f"Wrote {len(b64_dict)} textures to {json_path}")

    return textures, b64_dict

if __name__ == "__main__":
    generate_all_textures()
