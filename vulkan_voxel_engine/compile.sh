#!/usr/bin/env bash
# ==============================================================================
# Umbra Voxel — Pure C++20 & Vulkan 1.2 Cross-Platform Build Script
# Supports: Windows (.exe via native MSYS2 / MinGW64 / UCRT64 / Git Bash or
#           Linux cross-compilation with mingw-w64), Linux, and macOS
# Zero GLM dependency — uses pure C++20 math library (src/math/)
# ==============================================================================
# Usage:
#   ./compile.sh              # Build release binary for host OS + compile GLSL -> SPIR-V
#   ./compile.sh --windows    # Build Windows .exe binary (or 'window', 'windows', '--win')
#   ./compile.sh --release    # Build with -O3 optimizations (default)
#   ./compile.sh --debug      # Build with debug symbols & Vulkan validation layers
#   ./compile.sh --run        # Build and immediately launch the voxel engine
#   ./compile.sh --clean      # Remove build artifacts before compiling
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_TYPE="release"
DO_CLEAN=0
DO_RUN=0
TARGET_PLATFORM=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --debug)
      BUILD_TYPE="debug"
      shift
      ;;
    --release)
      BUILD_TYPE="release"
      shift
      ;;
    --clean)
      DO_CLEAN=1
      shift
      ;;
    --run)
      DO_RUN=1
      shift
      ;;
    --windows|--win|window|windows|-w|--exe)
      TARGET_PLATFORM="windows"
      shift
      ;;
    --linux|linux)
      TARGET_PLATFORM="linux"
      shift
      ;;
    --macos|--darwin|macos|darwin)
      TARGET_PLATFORM="macos"
      shift
      ;;
    --target)
      if [[ $# -lt 2 ]]; then
        echo "[ERROR] --target requires an argument (windows, linux, macos)" >&2
        exit 1
      fi
      TARGET_PLATFORM="$2"
      shift 2
      ;;
    --target=*)
      TARGET_PLATFORM="${1#*=}"
      shift
      ;;
    -h|--help)
      echo "Usage: ./compile.sh [--release | --debug] [--windows] [--clean] [--run]"
      echo ""
      echo "Commands & Options:"
      echo "  ./compile.sh               Build release binary for current host platform"
      echo "  ./compile.sh --windows     Build Windows (.exe) binary (or 'window', 'windows', '--win')"
      echo "  ./compile.sh --release     Build with -O3 optimizations (default)"
      echo "  ./compile.sh --debug       Build with debug symbols & Vulkan validation layers"
      echo "  ./compile.sh --clean       Remove build artifacts before compiling"
      echo "  ./compile.sh --run         Build and immediately launch the voxel engine"
      exit 0
      ;;
    *)
      echo "[ERROR] Unknown argument: $1"
      echo "Usage: ./compile.sh [--release | --debug] [--windows] [--clean] [--run]"
      exit 1
      ;;
  esac
done

RED='\033[0;31m'
GREEN='\033[0;32m'
AMBER='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${CYAN}[UMBRA]${NC} $*"; }
ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
warn()  { echo -e "${AMBER}[WARN]${NC}  $*"; }
fail()  { echo -e "${RED}[FAIL]${NC}  $*" >&2; exit 1; }

# ------------------------------------------------------------------------------
# 0. Detect Host & Target Operating System
# ------------------------------------------------------------------------------
OS_RAW="$(uname -s)"
HOST_PLATFORM="linux"
case "$OS_RAW" in
  MINGW*|MSYS*|CYGWIN*)
    HOST_PLATFORM="windows"
    ;;
  Darwin*)
    HOST_PLATFORM="macos"
    ;;
  *)
    HOST_PLATFORM="linux"
    ;;
esac

if [[ -z "$TARGET_PLATFORM" ]]; then
  TARGET_PLATFORM="$HOST_PLATFORM"
fi

PLATFORM="$TARGET_PLATFORM"
EXE_EXT=""
if [[ "$PLATFORM" == "windows" ]]; then
  EXE_EXT=".exe"
fi
info "Target platform: $PLATFORM (host: $HOST_PLATFORM, $OS_RAW)"

if [[ "$DO_CLEAN" -eq 1 ]]; then
  info "Cleaning build directory..."
  rm -rf build
fi

OBJ_DIR="build/obj_${PLATFORM}"
mkdir -p "$OBJ_DIR" build/obj

# Auto-discover Windows LunarG Vulkan SDK if VULKAN_SDK is not set in shell
if [[ "$PLATFORM" == "windows" && -z "${VULKAN_SDK:-}" ]]; then
  for candidate in /c/VulkanSDK/* /mnt/c/VulkanSDK/*; do
    if [[ -d "$candidate/Include" && -d "$candidate/Lib" ]]; then
      VULKAN_SDK="$candidate"
      break
    fi
  done
  if [[ -n "${VULKAN_SDK:-}" ]]; then
    info "Auto-detected Windows Vulkan SDK: $VULKAN_SDK"
  fi
fi

# Normalize Windows paths (e.g. C:\VulkanSDK\...) to POSIX for MSYS/Git Bash tools
if [[ "$PLATFORM" == "windows" && -n "${VULKAN_SDK:-}" ]] && command -v cygpath >/dev/null 2>&1; then
  VULKAN_SDK="$(cygpath -u "$VULKAN_SDK")"
fi

# ------------------------------------------------------------------------------
# 1. Detect C++20 Compiler
# ------------------------------------------------------------------------------
CXX=""
if [[ "$PLATFORM" == "windows" && "$HOST_PLATFORM" != "windows" ]]; then
  # Cross-compiling for Windows from Linux / macOS
  if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
    CXX="x86_64-w64-mingw32-g++"
  elif command -v x86_64-w64-mingw32-clang++ >/dev/null 2>&1; then
    CXX="x86_64-w64-mingw32-clang++"
  elif command -v i686-w64-mingw32-g++ >/dev/null 2>&1; then
    CXX="i686-w64-mingw32-g++"
  else
    fail "No MinGW-w64 C++ cross-compiler found (e.g. x86_64-w64-mingw32-g++).
    Ubuntu/Debian: sudo apt install g++-mingw-w64-x86-64
    Arch Linux:    sudo pacman -S mingw-w64-gcc
    Fedora:        sudo dnf install mingw64-gcc-c++"
  fi
else
  # Native compilation
  if command -v g++ >/dev/null 2>&1; then
    CXX="g++"
  elif command -v clang++ >/dev/null 2>&1; then
    CXX="clang++"
  else
    fail "No C++20 compiler found (g++ or clang++).
    Windows (MSYS2 UCRT64/MINGW64): pacman -S mingw-w64-ucrt-x86_64-gcc
    Ubuntu/Debian:                  sudo apt install g++
    Arch Linux:                     sudo pacman -S gcc
    Fedora:                         sudo dnf install gcc-c++
    macOS:                          xcode-select --install"
  fi
fi
info "Using C++ compiler: $CXX ($($CXX --version | head -n 1))"

# ------------------------------------------------------------------------------
# 2. Detect GLSL -> SPIR-V Shader Compiler
# ------------------------------------------------------------------------------
SHADER_COMPILER=""
SHADER_MODE=""
if command -v glslc >/dev/null 2>&1; then
  SHADER_COMPILER="glslc"
  SHADER_MODE="glslc"
elif [[ -n "${VULKAN_SDK:-}" && -x "${VULKAN_SDK}/Bin/glslc.exe" ]]; then
  SHADER_COMPILER="${VULKAN_SDK}/Bin/glslc.exe"
  SHADER_MODE="glslc"
elif [[ -n "${VULKAN_SDK:-}" && -x "${VULKAN_SDK}/bin/glslc" ]]; then
  SHADER_COMPILER="${VULKAN_SDK}/bin/glslc"
  SHADER_MODE="glslc"
elif command -v glslangValidator >/dev/null 2>&1; then
  SHADER_COMPILER="glslangValidator"
  SHADER_MODE="glslang"
elif [[ -x "/home/wang/.local/bin/glslangValidator" ]]; then
  SHADER_COMPILER="/home/wang/.local/bin/glslangValidator"
  SHADER_MODE="glslang"
elif [[ -n "${VULKAN_SDK:-}" && -x "${VULKAN_SDK}/Bin/glslangValidator.exe" ]]; then
  SHADER_COMPILER="${VULKAN_SDK}/Bin/glslangValidator.exe"
  SHADER_MODE="glslang"
elif [[ -n "${VULKAN_SDK:-}" && -x "${VULKAN_SDK}/bin/glslangValidator" ]]; then
  SHADER_COMPILER="${VULKAN_SDK}/bin/glslangValidator"
  SHADER_MODE="glslang"
else
  fail "Neither glslc nor glslangValidator found. Install the Vulkan SDK or shaderc:
  Windows:       Install LunarG Vulkan SDK (https://vulkan.lunarg.com) or MSYS2 shaderc
  Ubuntu/Debian: sudo apt install glslc   (or glslang-tools)
  Arch Linux:    sudo pacman -S shaderc
  Fedora:        sudo dnf install glslc
  macOS:         brew install shaderc"
fi
info "Using shader compiler: $SHADER_COMPILER"

# ------------------------------------------------------------------------------
# 3. Resolve Vulkan & GLFW3 include/link flags (NO GLM needed!)
# ------------------------------------------------------------------------------
EXTRA_CXXFLAGS=()
EXTRA_LDFLAGS=()

if [[ "$PLATFORM" == "windows" ]]; then
  # --- Windows Vulkan SDK / Headers & Loader ---
  if [[ -n "${VULKAN_SDK:-}" && -d "${VULKAN_SDK}/Include" ]]; then
    EXTRA_CXXFLAGS+=("-I${VULKAN_SDK}/Include")
    if [[ -f "${VULKAN_SDK}/Lib/libvulkan-1.a" ]]; then
      EXTRA_LDFLAGS+=("-L${VULKAN_SDK}/Lib" "-lvulkan-1")
    elif [[ -f "${VULKAN_SDK}/Lib/vulkan-1.lib" ]]; then
      EXTRA_LDFLAGS+=("${VULKAN_SDK}/Lib/vulkan-1.lib")
    else
      EXTRA_LDFLAGS+=("-L${VULKAN_SDK}/Lib" "-lvulkan-1")
    fi
  elif [[ "$HOST_PLATFORM" == "windows" ]] && command -v pkg-config >/dev/null 2>&1 && pkg-config --exists vulkan; then
    read -ra VK_CFLAGS <<< "$(pkg-config --cflags vulkan)"
    read -ra VK_LIBS   <<< "$(pkg-config --libs vulkan)"
    EXTRA_CXXFLAGS+=("${VK_CFLAGS[@]}")
    EXTRA_LDFLAGS+=("${VK_LIBS[@]}")
  else
    # Cross-compiling on Linux or fallback:
    # Set up isolated Vulkan headers directory to avoid glibc /usr/include/stdint.h shadowing
    if [[ -d "/usr/include/vulkan" ]]; then
      WIN_INC_DIR="build/win_include"
      mkdir -p "$WIN_INC_DIR/vulkan"
      for f in /usr/include/vulkan/*; do
        [[ -e "$f" ]] && ln -sf "$f" "$WIN_INC_DIR/vulkan/$(basename "$f")"
      done
      if [[ -d "/usr/include/vk_video" ]]; then
        mkdir -p "$WIN_INC_DIR/vk_video"
        for f in /usr/include/vk_video/*; do
          [[ -e "$f" ]] && ln -sf "$f" "$WIN_INC_DIR/vk_video/$(basename "$f")"
        done
      fi
      EXTRA_CXXFLAGS+=("-I${WIN_INC_DIR}")
    fi

    # Vulkan linking for Windows
    if [[ -f "/mnt/c/Windows/System32/vulkan-1.dll" ]]; then
      EXTRA_LDFLAGS+=("/mnt/c/Windows/System32/vulkan-1.dll")
    elif [[ -f "/c/Windows/System32/vulkan-1.dll" ]]; then
      EXTRA_LDFLAGS+=("/c/Windows/System32/vulkan-1.dll")
    elif [[ -f "/tmp/vktest/libvulkan-1.a" ]]; then
      EXTRA_LDFLAGS+=("-L/tmp/vktest" "-lvulkan-1")
    else
      EXTRA_LDFLAGS+=("-lvulkan-1")
    fi
  fi

  # --- Windows GLFW3 (via sysroot, optional GLFW_DIR, or pkg-config) ---
  if [[ -z "${GLFW_DIR:-}" ]]; then
    for candidate in \
      "/home/wang/mingw64_sysroot/mingw64" \
      "${HOME}/mingw64_sysroot/mingw64" \
      "/home/wang/vcpkg/packages/glfw3_x64-mingw-static" \
      "${HOME}/vcpkg/packages/glfw3_x64-mingw-static" \
      "/opt/mingw64" \
      "/usr/x86_64-w64-mingw32" \
      "/c/msys64/mingw64" \
      "/mnt/c/msys64/mingw64" \
      "/c/msys64/ucrt64" \
      "/mnt/c/msys64/ucrt64"; do
      if [[ -d "$candidate/include/GLFW" ]]; then
        GLFW_DIR="$candidate"
        break
      fi
    done
  fi

  if [[ -n "${GLFW_DIR:-}" ]]; then
    GLFW_POSIX="$GLFW_DIR"
    if command -v cygpath >/dev/null 2>&1; then
      GLFW_POSIX="$(cygpath -u "$GLFW_DIR")"
    fi
    EXTRA_CXXFLAGS+=("-I${GLFW_POSIX}/include")
    if [[ -f "${GLFW_POSIX}/lib/libglfw3.a" ]]; then
      EXTRA_LDFLAGS+=("${GLFW_POSIX}/lib/libglfw3.a")
    elif [[ -f "${GLFW_POSIX}/lib-mingw-w64/libglfw3.a" ]]; then
      EXTRA_LDFLAGS+=("${GLFW_POSIX}/lib-mingw-w64/libglfw3.a")
    elif [[ -d "${GLFW_POSIX}/lib-mingw-w64" ]]; then
      EXTRA_LDFLAGS+=("-L${GLFW_POSIX}/lib-mingw-w64" "-lglfw3")
    elif [[ -d "${GLFW_POSIX}/lib" ]]; then
      EXTRA_LDFLAGS+=("-L${GLFW_POSIX}/lib" "-lglfw3")
    else
      EXTRA_LDFLAGS+=("-lglfw3")
    fi

    # Copy runtime glfw3.dll to build/ if available
    for dll_cand in "${GLFW_POSIX}/bin/glfw3.dll" "${GLFW_POSIX}/lib/glfw3.dll" "/home/wang/mingw64_sysroot/mingw64/bin/glfw3.dll"; do
      if [[ -f "$dll_cand" ]]; then
        cp -f "$dll_cand" build/
        break
      fi
    done
  elif [[ "$HOST_PLATFORM" == "windows" ]] && command -v pkg-config >/dev/null 2>&1 && pkg-config --exists glfw3; then
    read -ra GLFW_CFLAGS <<< "$(pkg-config --cflags glfw3)"
    read -ra GLFW_LIBS   <<< "$(pkg-config --libs glfw3)"
    EXTRA_CXXFLAGS+=("${GLFW_CFLAGS[@]}")
    EXTRA_LDFLAGS+=("${GLFW_LIBS[@]}")
  else
    EXTRA_LDFLAGS+=("-lglfw3")
  fi

  # Required Win32 system libraries & static C++ runtime (avoids missing MinGW DLLs)
  EXTRA_LDFLAGS+=(
    "-static-libgcc"
    "-static-libstdc++"
    "-lgdi32"
    "-luser32"
    "-lshell32"
  )
else
  # Linux & macOS configuration
  if command -v pkg-config >/dev/null 2>&1; then
    if pkg-config --exists vulkan; then
      read -ra VK_CFLAGS <<< "$(pkg-config --cflags vulkan)"
      read -ra VK_LIBS   <<< "$(pkg-config --libs vulkan)"
      EXTRA_CXXFLAGS+=("${VK_CFLAGS[@]}")
      EXTRA_LDFLAGS+=("${VK_LIBS[@]}")
    elif [[ -n "${VULKAN_SDK:-}" ]]; then
      EXTRA_CXXFLAGS+=("-I${VULKAN_SDK}/include")
      EXTRA_LDFLAGS+=("-L${VULKAN_SDK}/lib" "-lvulkan")
    else
      EXTRA_LDFLAGS+=("-lvulkan")
    fi

    if pkg-config --exists glfw3; then
      read -ra GLFW_CFLAGS <<< "$(pkg-config --cflags glfw3)"
      read -ra GLFW_LIBS   <<< "$(pkg-config --libs glfw3)"
      EXTRA_CXXFLAGS+=("${GLFW_CFLAGS[@]}")
      EXTRA_LDFLAGS+=("${GLFW_LIBS[@]}")
    else
      EXTRA_LDFLAGS+=("-lglfw")
    fi
  else
    warn "pkg-config not found; falling back to default -lvulkan -lglfw link flags."
    if [[ -n "${VULKAN_SDK:-}" ]]; then
      EXTRA_CXXFLAGS+=("-I${VULKAN_SDK}/include")
      EXTRA_LDFLAGS+=("-L${VULKAN_SDK}/lib")
    fi
    EXTRA_LDFLAGS+=("-lvulkan" "-lglfw")
  fi

  if [[ "$PLATFORM" == "macos" ]]; then
    if command -v brew >/dev/null 2>&1; then
      BREW_PREFIX="$(brew --prefix)"
      EXTRA_CXXFLAGS+=("-I${BREW_PREFIX}/include")
      EXTRA_LDFLAGS+=("-L${BREW_PREFIX}/lib" "-Wl,-rpath,${BREW_PREFIX}/lib")
    fi
    EXTRA_LDFLAGS+=("-framework" "Cocoa" "-framework" "IOKit" "-framework" "CoreVideo")
  else
    EXTRA_LDFLAGS+=("-ldl" "-lpthread")
  fi
fi

# Verify required headers exist before compiling (only Vulkan + GLFW3 — zero GLM!)
check_header() {
  local header="$1"
  local hint="$2"
  if ! echo "#include <${header}>" | "$CXX" -std=c++20 -x c++ -E "${EXTRA_CXXFLAGS[@]}" - >/dev/null 2>&1; then
    fail "Missing required C++ header <${header}>.
  Install dependencies:
  ${hint}"
  fi
}

check_header "vulkan/vulkan.h" \
  "Windows (MSYS2): pacman -S mingw-w64-ucrt-x86_64-vulkan-headers mingw-w64-ucrt-x86_64-vulkan-loader (or LunarG Vulkan SDK)
  Linux (Ubuntu/Debian): sudo apt install libvulkan-dev vulkan-validationlayers
  Arch Linux:      sudo pacman -S vulkan-devel
  Fedora:          sudo dnf install vulkan-loader-devel vulkan-headers
  macOS:           brew install vulkan-headers vulkan-loader molten-vk"

check_header "GLFW/glfw3.h" \
  "Windows (MSYS2): pacman -S mingw-w64-ucrt-x86_64-glfw  (or set GLFW_DIR=/path/to/glfw)
  Linux (Ubuntu/Debian): sudo apt install libglfw3-dev
  Arch Linux:      sudo pacman -S glfw
  Fedora:          sudo dnf install glfw-devel
  macOS:           brew install glfw"

ok "All C++20, Vulkan, and GLFW3 dependencies verified (pure C++ math — zero GLM required)."

# ------------------------------------------------------------------------------
# 4. Compile GLSL 450 Shaders inside assets/shaders/ -> SPIR-V
# ------------------------------------------------------------------------------
SHADERS_DIR="assets/shaders"
if [[ ! -d "$SHADERS_DIR" ]]; then
  fail "Shaders directory not found: $SHADERS_DIR"
fi

compile_shader() {
  local src="$1"
  local out="${src}.spv"
  info "Compiling shader: $src -> $out"
  if [[ "$SHADER_MODE" == "glslc" ]]; then
    "$SHADER_COMPILER" --target-env=vulkan1.2 "$src" -o "$out"
  else
    "$SHADER_COMPILER" -V --target-env vulkan1.2 "$src" -o "$out"
  fi
}

SHADER_COUNT=0
shopt -s nullglob
for shader_src in "${SHADERS_DIR}"/*.vert "${SHADERS_DIR}"/*.frag "${SHADERS_DIR}"/*.comp; do
  compile_shader "$shader_src"
  SHADER_COUNT=$((SHADER_COUNT + 1))
done
shopt -u nullglob

if [[ "$SHADER_COUNT" -eq 0 ]]; then
  fail "No GLSL shaders (*.vert / *.frag / *.comp) found in ${SHADERS_DIR}/"
fi
ok "Compiled ${SHADER_COUNT} SPIR-V shaders inside ${SHADERS_DIR}/."

# Generate embedded SPIR-V C++ fallback header from assets/shaders/*.spv
if command -v python3 >/dev/null 2>&1; then
  python3 -c '
import os
shaders_dir = "assets/shaders"
files = sorted(f for f in os.listdir(shaders_dir) if f.endswith(".spv"))
header = ["// Auto-generated fallback shaders\n#pragma once\n#include <string>\n#include <vector>\n\nnamespace umbra::embedded {\n\n"]
for f in files:
    p = os.path.join(shaders_dir, f)
    var = f.replace(".", "_")
    with open(p, "rb") as fp:
        b = fp.read()
    hex_data = ", ".join(f"0x{x:02x}" for x in b)
    header.append(f"static const unsigned char {var}[] = {{\n{hex_data}\n}};\n")
    header.append(f"static const size_t {var}_size = {len(b)};\n\n")

header.append("inline bool getEmbeddedShader(const std::string& name, std::vector<char>& out) {\n")
for f in files:
    var = f.replace(".", "_")
    header.append(f"    if (name.find(\"{f}\") != std::string::npos) {{\n")
    header.append(f"        out.assign(reinterpret_cast<const char*>({var}), reinterpret_cast<const char*>({var}) + {var}_size);\n")
    header.append("        return true;\n    }\n")
header.append("    return false;\n}\n\n} // namespace umbra::embedded\n")

with open("src/embedded_shaders.hpp", "w") as fp:
    fp.write("".join(header))
'
  ok "Embedded shaders generated (src/embedded_shaders.hpp)."
fi

# ------------------------------------------------------------------------------
# 5. Compile Pure C++20 Engine Sources
# ------------------------------------------------------------------------------
CXXFLAGS=(
  "-std=c++20"
  "-Wall"
  "-Wextra"
  "-Wno-unused-parameter"
  "-Isrc"
)

if [[ "$BUILD_TYPE" == "debug" ]]; then
  CXXFLAGS+=("-g" "-O0" "-DENABLE_VALIDATION_LAYERS=1")
  info "Build configuration: DEBUG (Vulkan Validation Layers ENABLED)"
else
  CXXFLAGS+=("-O3" "-fno-math-errno" "-funroll-loops" "-DNDEBUG" "-DENABLE_VALIDATION_LAYERS=0")
  info "Build configuration: RELEASE (-O3 -fno-math-errno -funroll-loops)"
fi

SOURCES=(
  "src/world.cpp"
  "src/lighting.cpp"
  "src/mesher.cpp"
  "src/hud.cpp"
  "src/vulkan_engine.cpp"
  "src/main.cpp"
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="${OBJ_DIR}/$(basename "${src%.cpp}.o")"
  OBJECTS+=("$obj")
  info "Compiling C++20 ($PLATFORM): $src"
  "$CXX" "${CXXFLAGS[@]}" "${EXTRA_CXXFLAGS[@]}" -c "$src" -o "$obj"
done

TARGET="build/umbra_voxel${EXE_EXT}"
info "Linking executable: $TARGET"
"$CXX" "${OBJECTS[@]}" -o "$TARGET" "${EXTRA_LDFLAGS[@]}"

# ------------------------------------------------------------------------------
# 6. Copy assets/ (compiled shaders + PNG block & item textures) into build/
# ------------------------------------------------------------------------------
info "Copying assets/ -> build/assets/ ..."
rm -rf build/assets
cp -R assets build/assets
ok "Assets copied to build/assets/ (shaders + block/item textures)."
ok "Build complete: ./$TARGET"

if [[ "$DO_RUN" -eq 1 ]]; then
  info "Launching Umbra Voxel Engine..."
  if [[ "$PLATFORM" == "windows" && "$HOST_PLATFORM" != "windows" ]]; then
    if [[ -f "/proc/sys/fs/binfmt_misc/WSLInterop" || -n "${WSL_DISTRO_NAME:-}" ]]; then
      exec "./$TARGET"
    elif command -v wine >/dev/null 2>&1; then
      exec wine "./$TARGET"
    else
      warn "Cannot run Windows binary directly without Wine or WSL. Binary saved at: ./$TARGET"
      exit 0
    fi
  else
    exec "./$TARGET"
  fi
fi
