import JSZip from 'jszip';

export interface ProjectSourceFile {
  path: string;
  name: string;
  category: 'Build & Docs' | 'Vulkan GLSL 450 Shaders' | 'C++20 Engine Sources';
  language: 'bash' | 'markdown' | 'glsl' | 'cpp';
  summary: string;
  content: string;
  lineCount: number;
}

export interface ProjectBinaryAsset {
  path: string;
  name: string;
  folder: 'block' | 'items';
  url: string;
}

const rawModules = import.meta.glob(
  '/vulkan_voxel_engine/**/*.{cpp,hpp,h,vert,frag,sh,md}',
  {
    query: '?raw',
    import: 'default',
    eager: true,
  }
) as Record<string, string>;

const pngAssetModules = import.meta.glob(
  '/vulkan_voxel_engine/assets/texture/**/*.png',
  {
    query: '?url',
    import: 'default',
    eager: true,
  }
) as Record<string, string>;

const FILE_META: Record<
  string,
  {
    order: number;
    category: ProjectSourceFile['category'];
    language: ProjectSourceFile['language'];
    summary: string;
  }
> = {
  'compile.sh': {
    order: 1,
    category: 'Build & Docs',
    language: 'bash',
    summary:
      'Cross-platform Windows (MSYS2/MinGW + LunarG SDK), Linux & macOS build script — compiles all shaders inside assets/shaders/, links C++20 binary, and copies assets/ into build/assets/.',
  },
  'README.md': {
    order: 2,
    category: 'Build & Docs',
    language: 'markdown',
    summary:
      'Architecture overview covering real PNG block/item textures, assets/shaders/ build flow, FastNoiseLite biomes/caves, 3D dynamic lighting, binary greedy meshing, and RD 50.',
  },
  'assets/shaders/voxel.frag': {
    order: 3,
    category: 'Vulkan GLSL 450 Shaders',
    language: 'glsl',
    summary:
      'Samples real PNG block textures from sampler2DArray uBlockTextures (assets/texture/block/*.png) with optical Beer-Lambert water, textured glass, and occluded dynamic light.',
  },
  'assets/shaders/voxel.vert': {
    order: 4,
    category: 'Vulkan GLSL 450 Shaders',
    language: 'glsl',
    summary:
      'Unpacks greedy-merged voxel vertices including UVs, submerged flag & waterDepth (0..15) alongside compact lightAO and FrameUBO.',
  },
  'assets/shaders/sky.frag': {
    order: 5,
    category: 'Vulkan GLSL 450 Shaders',
    language: 'glsl',
    summary:
      'Jitter-free rotation-only 360° Sun/Moon/Star celestial sphere (zero cameraPos shake when walking/flying) + underwater abyss horizon blend.',
  },
  'assets/shaders/sky.vert': {
    order: 6,
    category: 'Vulkan GLSL 450 Shaders',
    language: 'glsl',
    summary:
      'Zero-buffer fullscreen triangle sky dome rendered at depth 1.0 between opaque terrain and translucent water/glass.',
  },
  'assets/shaders/hud.vert': {
    order: 7,
    category: 'Vulkan GLSL 450 Shaders',
    language: 'glsl',
    summary:
      '2D NDC vertex shader passing UVs, tint color, and PNG item texture layer for the zero-dependency Vulkan HUD, hotbar, inventory, and menus.',
  },
  'assets/shaders/hud.frag': {
    order: 8,
    category: 'Vulkan GLSL 450 Shaders',
    language: 'glsl',
    summary:
      'Alpha-blended 2D fragment shader sampling real PNG item icons from sampler2DArray uItemTextures (assets/texture/items/*.png) alongside crisp bitmap UI.',
  },
  'src/texture_loader.hpp': {
    order: 9,
    category: 'C++20 Engine Sources',
    language: 'cpp',
    summary:
      'Zero-dependency C++20 PNG decoder (RFC 1950/1951 DEFLATE + PNG unfiltering) loading assets/texture/block/*.png and assets/texture/items/*.png into GPU texture arrays.',
  },
  'src/math/vec_mat.hpp': {
    order: 10,
    category: 'C++20 Engine Sources',
    language: 'cpp',
    summary:
      'Pure zero-GLM linear algebra, C2 smootherstepf, lerp3D, analytical mat4 inverse(), and 6-plane Gribb-Hartmann frustum culler.',
  },
  'src/math/lighting_math.hpp': {
    order: 11,
    category: 'C++20 Engine Sources',
    language: 'cpp',
    summary:
      'Compile-time zero-anchored sky/block exponential falloff LUTs, nibble packing, strict vertex AO, quad flip test & evaluateAreaDarkness zone classifier.',
  },
  'src/math/chunk_math.hpp': {
    order: 12,
    category: 'C++20 Engine Sources',
    language: 'cpp',
    summary:
      '128-tall chunk dimensions, single-cycle bit-shift chunk coordinates (>> 4, & 15), SplitMix hashing, and zero-allocation FlatQueue<T>.',
  },
  'src/math/noise/noise.h': {
    order: 13,
    category: 'C++20 Engine Sources',
    language: 'cpp',
    summary:
      'FastNoiseLite v1.0.1 single-header procedural noise library driving C2-smooth continents, biomes, rivers, and 4x4x4 trilinear 3D caves.',
  },
  'src/inventory.hpp': {
    order: 14,
    category: 'C++20 Engine Sources',
    language: 'cpp',
    summary:
      '36-slot inventory including Glass block (slot 8), stack merging, click-to-swap, and heldLightEmission() gating dynamic player light.',
  },
  'src/hud.hpp': {
    order: 15,
    category: 'C++20 Engine Sources',
    language: 'cpp',
    summary:
      'Zero-dependency Vulkan 2D HudBuilder interface with textured HudVertex (pos, uv, color, texLayer) for PNG item icons, menus, and Shift+F3 debugger.',
  },
  'src/hud.cpp': {
    order: 16,
    category: 'C++20 Engine Sources',
    language: 'cpp',
    summary:
      '5x7 bitmap font renderer, PNG item icon quads (assets/texture/items/*.png), interactive Settings UI, and Shift+F3 live telemetry graph.',
  },
  'src/lighting.cpp': {
    order: 17,
    category: 'C++20 Engine Sources',
    language: 'cpp',
    summary:
      'Cross-chunk BFS sky/block lighting (full 15 sky light through Glass) + reusable-buffer continuous floating-point 18-neighbor Euclidean dynamic light volume.',
  },
  'src/lighting.hpp': {
    order: 18,
    category: 'C++20 Engine Sources',
    language: 'cpp',
    summary:
      'LightEngine interface with reusable FlatQueue pools, reusable dynDist_ scratch buffer, and 32 KB 3D dynamic light volume.',
  },
  'src/mesher.cpp': {
    order: 19,
    category: 'C++20 Engine Sources',
    language: 'cpp',
    summary:
      'Dual-Pass Smooth Binary Greedy Mesher: opaque + translucent (recessed Water + Snell underside + packed waterDepth + connected Glass culling).',
  },
  'src/mesher.hpp': {
    order: 20,
    category: 'C++20 Engine Sources',
    language: 'cpp',
    summary:
      'Dual-pass Smooth Binary Greedy VoxelMesher interface outputting opaque and translucent chunk meshes.',
  },
  'src/vulkan_engine.cpp': {
    order: 21,
    category: 'C++20 Engine Sources',
    language: 'cpp',
    summary:
      'Vulkan 1.2 renderer loading PNG block & item VK_IMAGE_VIEW_TYPE_2D_ARRAY textures, assets/shaders/*.spv, async 3D dynamic light, and frustum culling.',
  },
  'src/vulkan_engine.hpp': {
    order: 22,
    category: 'C++20 Engine Sources',
    language: 'cpp',
    summary:
      'VulkanEngine renderer declaration with block/item PNG texture arrays, HUD descriptor set layout, dual opaque/translucent pipelines, and live telemetry.',
  },
  'src/world.cpp': {
    order: 23,
    category: 'C++20 Engine Sources',
    language: 'cpp',
    summary:
      'C2-smooth FastNoiseLite worldgen with coherent biome alignment, sandy riverbanks, slope-aware cliffs, and 4x4x4 trilinearly interpolated 3D caves.',
  },
  'src/world.hpp': {
    order: 24,
    category: 'C++20 Engine Sources',
    language: 'cpp',
    summary:
      'Infinite VoxelWorld with O(1) precomputed spiral streaming (up to RD 50), biome sampling, and 32 KB-leaner Chunk struct.',
  },
  'src/camera.hpp': {
    order: 25,
    category: 'C++20 Engine Sources',
    language: 'cpp',
    summary:
      'Pure C++20 camera with rotation-only view matrix (zero Sun/Moon shake), buoyant swimming physics, underwater eye depth detection, and voxel collision.',
  },
  'src/types.hpp': {
    order: 26,
    category: 'C++20 Engine Sources',
    language: 'cpp',
    summary:
      'Core types: BlockType::Glass (14), EngineSettings (RD 4..50), DebugMetrics (60-frame history + CPU timings), and packed optical water vertex attributes.',
  },
  'src/main.cpp': {
    order: 27,
    category: 'C++20 Engine Sources',
    language: 'cpp',
    summary:
      'GLFW game loop with Shift+F3 debugger, per-stage CPU profiling, rotation-only invSkyVP, underwater state sync, and Win32 MessageBoxA diagnostics.',
  },
};

export const CPP_PROJECT_FILES: ProjectSourceFile[] = Object.entries(rawModules)
  .map(([fullPath, content]) => {
    const relPath = fullPath.replace(/^\/vulkan_voxel_engine\//, '');
    const meta = FILE_META[relPath] ?? {
      order: 99,
      category: 'C++20 Engine Sources' as const,
      language: 'cpp' as const,
      summary: 'Engine source file.',
    };
    const parts = relPath.split('/');
    return {
      path: relPath,
      name: parts[parts.length - 1],
      category: meta.category,
      language: meta.language,
      summary: meta.summary,
      content,
      lineCount: content.split('\n').length,
      _order: meta.order,
    };
  })
  .sort((a, b) => a._order - b._order)
  .map(({ _order: _unused, ...rest }) => rest);

export const CPP_BINARY_ASSETS: ProjectBinaryAsset[] = Object.entries(
  pngAssetModules
)
  .map(([fullPath, url]) => {
    const relPath = fullPath.replace(/^\/vulkan_voxel_engine\//, '');
    const parts = relPath.split('/');
    const folder: 'block' | 'items' = relPath.includes('/items/')
      ? 'items'
      : 'block';
    return {
      path: relPath,
      name: parts[parts.length - 1],
      folder,
      url,
    };
  })
  .sort((a, b) => a.path.localeCompare(b.path));

// Ordered 18-layer block texture list matching BlockTexLayer in src/texture_loader.hpp & voxel.frag
const BLOCK_LAYER_FILES = [
  'grass_top.png',
  'grass_side.png',
  'grass_bottom.png',
  'dirt.png',
  'stone.png',
  'deepslate.png',
  'timber_top.png',
  'timber_side.png',
  'leaves.png',
  'torch_side.png',
  'torch_top.png',
  'glowstone.png',
  'water.png',
  'sand.png',
  'magma.png',
  'crystal.png',
  'snow.png',
  'glass.png',
] as const;

export const BLOCK_TEXTURE_URLS: string[] = BLOCK_LAYER_FILES.map(
  (filename) =>
    pngAssetModules[`/vulkan_voxel_engine/assets/texture/block/${filename}`] ??
    ''
);

export const ITEM_TEXTURE_URLS: Record<string, string> = Object.fromEntries(
  CPP_BINARY_ASSETS.filter((a) => a.folder === 'items').map((a) => [
    a.name.replace(/\.png$/i, ''),
    a.url,
  ])
);

export async function downloadProjectZip(): Promise<void> {
  const zip = new JSZip();
  const root = zip.folder('umbra_vulkan_voxel');
  if (!root) return;

  for (const file of CPP_PROJECT_FILES) {
    const isScript = file.path.endsWith('.sh');
    root.file(file.path, file.content, {
      unixPermissions: isScript ? '755' : '644',
    });
  }

  // Package all real PNG block & item textures as intact binary files
  await Promise.all(
    CPP_BINARY_ASSETS.map(async (asset) => {
      const resp = await fetch(asset.url);
      if (resp.ok) {
        const buf = await resp.arrayBuffer();
        root.file(asset.path, buf, { binary: true, unixPermissions: '644' });
      }
    })
  );

  const blob = await zip.generateAsync({
    type: 'blob',
    platform: 'UNIX',
  });

  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = 'umbra_vulkan_voxel_cpp20.zip';
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  window.setTimeout(() => URL.revokeObjectURL(url), 1500);
}
