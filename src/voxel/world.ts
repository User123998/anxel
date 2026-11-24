import {
  BlockType,
  RaycastHit,
  TimeOfDayState,
  WORLD_X,
  WORLD_Y,
  WORLD_Z,
  getBlockLight,
  getSkyLight,
  setBlockLight,
  setSkyLight,
} from './types';

function hash2f(x: number, z: number, seed: number): number {
  let h =
    (Math.imul(x, 374761393) +
      Math.imul(z, 668265263) +
      Math.imul(seed, 1442695041)) >>>
    0;
  h = Math.imul(h ^ (h >>> 13), 1274126177) >>> 0;
  return ((h ^ (h >>> 16)) & 0xffff) / 65535;
}

function hash3f(x: number, y: number, z: number, seed: number): number {
  let h =
    (Math.imul(x, 374761393) +
      Math.imul(y, 2147483647) +
      Math.imul(z, 668265263) +
      Math.imul(seed, 1442695041)) >>>
    0;
  h = Math.imul(h ^ (h >>> 13), 1274126177) >>> 0;
  return ((h ^ (h >>> 16)) & 0xffff) / 65535;
}

function valueNoise2D(x: number, z: number, seed: number): number {
  const xi = Math.floor(x);
  const zi = Math.floor(z);
  const xf = x - xi;
  const zf = z - zi;
  const u = xf * xf * (3 - 2 * xf);
  const v = zf * zf * (3 - 2 * zf);

  const a = hash2f(xi, zi, seed);
  const b = hash2f(xi + 1, zi, seed);
  const c = hash2f(xi, zi + 1, seed);
  const d = hash2f(xi + 1, zi + 1, seed);

  return a + (b - a) * u + (c - a + (a - b - c + d) * u) * v;
}

function fbm2D(x: number, z: number, seed: number): number {
  let sum = 0;
  let amp = 0.55;
  let freq = 1;
  for (let i = 0; i < 4; i++) {
    sum += amp * valueNoise2D(x * freq, z * freq, seed + i * 97);
    freq *= 2.04;
    amp *= 0.48;
  }
  return sum;
}

function valueNoise3D(x: number, y: number, z: number, seed: number): number {
  const xi = Math.floor(x);
  const yi = Math.floor(y);
  const zi = Math.floor(z);
  const xf = x - xi;
  const yf = y - yi;
  const zf = z - zi;
  const u = xf * xf * (3 - 2 * xf);
  const v = yf * yf * (3 - 2 * yf);
  const w = zf * zf * (3 - 2 * zf);

  const s = (dx: number, dy: number, dz: number) =>
    hash3f(xi + dx, yi + dy, zi + dz, seed);

  const x00 = s(0, 0, 0) + (s(1, 0, 0) - s(0, 0, 0)) * u;
  const x10 = s(0, 1, 0) + (s(1, 1, 0) - s(0, 1, 0)) * u;
  const x01 = s(0, 0, 1) + (s(1, 0, 1) - s(0, 0, 1)) * u;
  const x11 = s(0, 1, 1) + (s(1, 1, 1) - s(0, 1, 1)) * u;
  const y0 = x00 + (x10 - x00) * v;
  const y1 = x01 + (x11 - x01) * v;
  return y0 + (y1 - y0) * w;
}

function clamp(v: number, lo: number, hi: number): number {
  return Math.max(lo, Math.min(hi, v));
}

function mix3(
  a: [number, number, number],
  b: [number, number, number],
  t: number
): [number, number, number] {
  return [
    a[0] + (b[0] - a[0]) * t,
    a[1] + (b[1] - a[1]) * t,
    a[2] + (b[2] - a[2]) * t,
  ];
}

export class VoxelWorld {
  readonly width: number;
  readonly height: number;
  readonly depth: number;
  readonly blocks: Uint8Array;
  readonly light: Uint8Array;
  readonly dynLight: Uint8Array;

  constructor(width = WORLD_X, height = WORLD_Y, depth = WORLD_Z) {
    this.width = width;
    this.height = height;
    this.depth = depth;
    const total = width * height * depth;
    this.blocks = new Uint8Array(total);
    this.light = new Uint8Array(total);
    this.dynLight = new Uint8Array(total);
  }

  inBounds(x: number, y: number, z: number): boolean {
    return (
      x >= 0 &&
      x < this.width &&
      y >= 0 &&
      y < this.height &&
      z >= 0 &&
      z < this.depth
    );
  }

  index(x: number, y: number, z: number): number {
    return (y * this.depth + z) * this.width + x;
  }

  getBlock(x: number, y: number, z: number): BlockType {
    if (!this.inBounds(x, y, z)) return BlockType.Air;
    return this.blocks[this.index(x, y, z)] as BlockType;
  }

  setBlockRaw(x: number, y: number, z: number, b: BlockType): void {
    if (!this.inBounds(x, y, z)) return;
    this.blocks[this.index(x, y, z)] = b;
  }

  getSky(x: number, y: number, z: number): number {
    if (!this.inBounds(x, y, z)) {
      return y >= this.height ? 15 : 0;
    }
    return getSkyLight(this.light[this.index(x, y, z)]);
  }

  getBlockLightVal(x: number, y: number, z: number): number {
    if (!this.inBounds(x, y, z)) return 0;
    return getBlockLight(this.light[this.index(x, y, z)]);
  }

  setSky(x: number, y: number, z: number, v: number): void {
    if (!this.inBounds(x, y, z)) return;
    const idx = this.index(x, y, z);
    this.light[idx] = setSkyLight(this.light[idx], v);
  }

  setBlockLightVal(x: number, y: number, z: number, v: number): void {
    if (!this.inBounds(x, y, z)) return;
    const idx = this.index(x, y, z);
    this.light[idx] = setBlockLight(this.light[idx], v);
  }

  getDynamicLightVal(x: number, y: number, z: number): number {
    if (!this.inBounds(x, y, z)) return 0;
    return this.dynLight[this.index(x, y, z)];
  }

  setDynamicLightVal(x: number, y: number, z: number, v: number): void {
    if (!this.inBounds(x, y, z)) return;
    this.dynLight[this.index(x, y, z)] = Math.max(0, Math.min(255, v | 0));
  }

  getEffectiveBlockLight(x: number, y: number, z: number): number {
    if (!this.inBounds(x, y, z)) return 0;
    const idx = this.index(x, y, z);
    const stat = getBlockLight(this.light[idx]);
    const dyn = Math.min(15, (this.dynLight[idx] + 8) >> 4);
    return stat > dyn ? stat : dyn;
  }

  clearLightField(): void {
    this.light.fill(0);
    this.dynLight.fill(0);
  }

  findSurfaceY(x: number, z: number): number {
    if (x < 0 || x >= this.width || z < 0 || z >= this.depth) return 10;
    for (let y = this.height - 1; y >= 0; y--) {
      const b = this.getBlock(x, y, z);
      if (
        b !== BlockType.Air &&
        b !== BlockType.Leaves &&
        b !== BlockType.Torch
      ) {
        return y;
      }
    }
    return 1;
  }

  generateProceduralWorld(seed = 1337): void {
    this.blocks.fill(BlockType.Air);
    this.light.fill(0);
    this.dynLight.fill(0);

    const seaLevel = 9;

    // 1. Highland valley + rising northern cliff massif (z >= 30)
    for (let z = 0; z < this.depth; z++) {
      for (let x = 0; x < this.width; x++) {
        const hNoise = fbm2D(x * 0.048, z * 0.048, seed);
        const ridge =
          1 - Math.abs(valueNoise2D(x * 0.035, z * 0.035, seed + 409) * 2 - 1);
        const cliffStep = 1 / (1 + Math.exp(-0.28 * (z - 31)));

        let h = Math.round(7.5 + hNoise * 8.5 + ridge * 4.5 + cliffStep * 9.5);
        h = clamp(h, 4, this.height - 6);

        for (let y = 0; y <= h; y++) {
          let b = BlockType.Stone;
          if (y <= 4) {
            b = BlockType.DeepSlate;
          } else if (y === h) {
            if (h <= seaLevel + 1) b = BlockType.Sand;
            else if (h >= 25) b = BlockType.Snow;
            else if (h >= 23) b = BlockType.Stone;
            else b = BlockType.Grass;
          } else if (y >= h - 2 && h < 23) {
            b = h <= seaLevel + 1 ? BlockType.Sand : BlockType.Dirt;
          }
          this.setBlockRaw(x, y, z, b);
        }

        for (let y = h + 1; y <= seaLevel; y++) {
          this.setBlockRaw(x, y, z, BlockType.Water);
        }
      }
    }

    // 2. Subtle subterranean caverns + dramatic signature cave tunnel plunging into darkness
    for (let z = 5; z < this.depth - 5; z++) {
      for (let y = 3; y < 22; y++) {
        for (let x = 5; x < this.width - 5; x++) {
          if (this.getBlock(x, y, z) === BlockType.Water) continue;
          const n =
            valueNoise3D(x * 0.1, y * 0.12, z * 0.1, seed + 701) * 0.72 +
            valueNoise3D(x * 0.18, y * 0.18, z * 0.18, seed + 911) * 0.28;
          if (n > 0.725 && y < 18) {
            this.setBlockRaw(x, y, z, BlockType.Air);
          }
        }
      }
    }

    // Hero Cave: wide entrance at (32, 14, 28) curving deep into the cliff massif
    for (let step = 0; step < 28; step++) {
      const t = step / 27;
      const cx = 32 + Math.sin(t * 2.5) * 4.5;
      const cy = 14.5 - t * 5.2 + Math.cos(t * 3.1) * 0.9;
      const cz = 27 + step;
      const radius = 2.9 + Math.sin(t * Math.PI) * 1.6;
      const rInt = Math.ceil(radius + 1);

      for (let dz = -rInt; dz <= rInt; dz++) {
        for (let dy = -rInt; dy <= rInt; dy++) {
          for (let dx = -rInt; dx <= rInt; dx++) {
            const vx = Math.round(cx) + dx;
            const vy = Math.round(cy) + dy;
            const vz = Math.round(cz) + dz;
            if (!this.inBounds(vx, vy, vz) || vy <= 2) continue;

            const dist = Math.sqrt(dx * dx + dy * dy * 1.2 + dz * dz);
            if (dist <= radius) {
              this.setBlockRaw(vx, vy, vz, BlockType.Air);
            } else if (
              dist <= radius + 1.35 &&
              this.getBlock(vx, vy, vz) !== BlockType.Air
            ) {
              this.setBlockRaw(vx, vy, vz, BlockType.DeepSlate);
            }
          }
        }
      }
    }

    // 3. Natural Stone Arch / Overhang on western ridge (casts real skylight shadow below)
    for (let dx = -6; dx <= 6; dx++) {
      const ax = 18 + dx;
      const az = 22;
      const archH = Math.round(21 - (dx * dx) / 7.2);
      for (let zOff = -2; zOff <= 2; zOff++) {
        for (let y = Math.max(10, archH - 2); y <= archH; y++) {
          if (this.inBounds(ax, y, az + zOff)) {
            this.setBlockRaw(
              ax,
              y,
              az + zOff,
              y === archH ? BlockType.Grass : BlockType.Stone
            );
          }
        }
      }
    }
    // Clear underside of the arch so the overhang shadow is unmistakable
    for (let dx = -4; dx <= 4; dx++) {
      const ax = 18 + dx;
      for (let zOff = -2; zOff <= 2; zOff++) {
        const ground = this.findSurfaceY(ax, 22 + zOff);
        for (let y = Math.min(ground, 11) + 1; y <= 17 - Math.floor(Math.abs(dx) * 0.7); y++) {
          if (this.inBounds(ax, y, 22 + zOff)) {
            this.setBlockRaw(ax, y, 22 + zOff, BlockType.Air);
          }
        }
      }
    }

    // 4. Deep cavern glowing magma pool + crystal accents
    for (let dz = -2; dz <= 2; dz++) {
      for (let dx = -2; dx <= 2; dx++) {
        if (dx * dx + dz * dz <= 3) {
          this.setBlockRaw(34 + dx, 6, 50 + dz, BlockType.Magma);
          this.setBlockRaw(34 + dx, 7, 50 + dz, BlockType.Air);
          this.setBlockRaw(34 + dx, 8, 50 + dz, BlockType.Air);
        }
      }
    }
    this.setBlockRaw(29, 11, 41, BlockType.Crystal);
    this.setBlockRaw(36, 10, 44, BlockType.Crystal);

    // 5. Campsite flanking torches at cave mouth + lantern under the stone arch
    const mouthY = this.findSurfaceY(32, 25);
    if (mouthY > seaLevel) {
      this.setBlockRaw(29, mouthY + 1, 26, BlockType.Timber);
      this.setBlockRaw(29, mouthY + 2, 26, BlockType.Torch);

      this.setBlockRaw(35, mouthY + 1, 26, BlockType.Timber);
      this.setBlockRaw(35, mouthY + 2, 26, BlockType.Torch);

      // Timber shelter overhang near valley path with a warm torch & Glass skylight/window panes
      const sx = 45;
      const sz = 20;
      const sy = this.findSurfaceY(sx, sz);
      if (sy > seaLevel) {
        for (let dz = -2; dz <= 2; dz++) {
          for (let dx = -2; dx <= 2; dx++) {
            // Roof with central 3x3 Glass skylight transmitting full 15 sky light
            const isSkylight = Math.abs(dx) <= 1 && Math.abs(dz) <= 1;
            this.setBlockRaw(
              sx + dx,
              sy + 4,
              sz + dz,
              isSkylight ? BlockType.Glass : BlockType.Timber
            );
            // Corner pillars
            if (Math.abs(dx) === 2 && Math.abs(dz) === 2) {
              for (let py = 1; py <= 3; py++) {
                this.setBlockRaw(sx + dx, sy + py, sz + dz, BlockType.Timber);
              }
            }
            // Side Glass window wall facing the valley vista
            if (dx === -2 && Math.abs(dz) <= 1) {
              this.setBlockRaw(sx + dx, sy + 1, sz + dz, BlockType.Timber);
              this.setBlockRaw(sx + dx, sy + 2, sz + dz, BlockType.Glass);
              this.setBlockRaw(sx + dx, sy + 3, sz + dz, BlockType.Glass);
            }
            // Back wall
            if (dz === 2 && Math.abs(dx) <= 1) {
              for (let py = 1; py <= 3; py++) {
                this.setBlockRaw(sx + dx, sy + py, sz + dz, BlockType.Stone);
              }
            }
          }
        }
        this.setBlockRaw(sx, sy + 1, sz, BlockType.Torch);
      }
    }

    // Crystal-clear foreground lake basin with gentle sandy shore sloping down to 5-block deep turquoise water
    for (let z = 12; z <= 25; z++) {
      for (let x = 23; x <= 39; x++) {
        const dx = (x - 31) / 8.0;
        const dz = (z - 18.5) / 6.2;
        const r2 = dx * dx + dz * dz;
        if (r2 < 1.0) {
          const depthCurve = Math.pow(1.0 - r2, 1.35);
          const bedY = Math.max(3, Math.round(seaLevel - 1 - depthCurve * 4.6));
          for (let y = bedY + 1; y <= seaLevel; y++) {
            this.setBlockRaw(x, y, z, BlockType.Water);
          }
          for (let y = seaLevel + 1; y <= seaLevel + 5; y++) {
            const cur = this.getBlock(x, y, z);
            if (cur === BlockType.Grass || cur === BlockType.Dirt || cur === BlockType.Sand || cur === BlockType.Stone) {
              this.setBlockRaw(x, y, z, BlockType.Air);
            }
          }
          this.setBlockRaw(x, bedY, z, bedY <= seaLevel - 4 ? BlockType.Stone : BlockType.Sand);
        }
      }
    }

    // Suspended glowstone lantern under the stone arch
    this.setBlockRaw(18, 17, 22, BlockType.Glowstone);

    // 6. Procedural pine grove casting soft foliage shade (keep hero vista corridor clear)
    for (let z = 6; z < this.depth - 6; z += 6) {
      for (let x = 6; x < this.width - 6; x += 6) {
        if (x >= 21 && x <= 42 && z >= 8 && z <= 33) continue;
        if (Math.abs(x - 18) < 8 && Math.abs(z - 22) < 6) continue;
        if (Math.abs(x - 45) < 6 && Math.abs(z - 20) < 6) continue;

        const r = hash2f(x, z, seed + 2027);
        if (r < 0.56) continue;

        const tx = clamp(x + Math.floor((r - 0.56) * 5), 4, this.width - 5);
        const tz = clamp(
          z + Math.floor(hash2f(z, x, seed + 3037) * 3),
          4,
          this.depth - 5
        );
        const ty = this.findSurfaceY(tx, tz);
        if (ty <= seaLevel + 1 || ty > 23) continue;
        if (this.getBlock(tx, ty, tz) !== BlockType.Grass) continue;

        const trunkH = 3 + (Math.floor(r * 100) % 3);
        for (let h = 1; h <= trunkH; h++) {
          this.setBlockRaw(tx, ty + h, tz, BlockType.Timber);
        }
        for (let ly = trunkH - 1; ly <= trunkH + 2; ly++) {
          const rad = ly === trunkH + 2 ? 1 : 2;
          for (let dz = -rad; dz <= rad; dz++) {
            for (let dx = -rad; dx <= rad; dx++) {
              if (Math.abs(dx) === rad && Math.abs(dz) === rad && ly > trunkH)
                continue;
              if (dx === 0 && dz === 0 && ly <= trunkH) continue;
              if (
                this.inBounds(tx + dx, ty + ly, tz + dz) &&
                this.getBlock(tx + dx, ty + ly, tz + dz) === BlockType.Air
              ) {
                this.setBlockRaw(tx + dx, ty + ly, tz + dz, BlockType.Leaves);
              }
            }
          }
        }
      }
    }
  }
}

export function raycastVoxel(
  world: VoxelWorld,
  origin: [number, number, number],
  direction: [number, number, number],
  maxDistance = 42
): RaycastHit | null {
  const len = Math.hypot(direction[0], direction[1], direction[2]);
  if (len < 1e-6) return null;
  const dx = direction[0] / len;
  const dy = direction[1] / len;
  const dz = direction[2] / len;

  let vx = Math.floor(origin[0]);
  let vy = Math.floor(origin[1]);
  let vz = Math.floor(origin[2]);

  const stepX = dx >= 0 ? 1 : -1;
  const stepY = dy >= 0 ? 1 : -1;
  const stepZ = dz >= 0 ? 1 : -1;

  const tDeltaX = Math.abs(dx) < 1e-6 ? Infinity : Math.abs(1 / dx);
  const tDeltaY = Math.abs(dy) < 1e-6 ? Infinity : Math.abs(1 / dy);
  const tDeltaZ = Math.abs(dz) < 1e-6 ? Infinity : Math.abs(1 / dz);

  let tMaxX = dx >= 0 ? (vx + 1 - origin[0]) * tDeltaX : (origin[0] - vx) * tDeltaX;
  let tMaxY = dy >= 0 ? (vy + 1 - origin[1]) * tDeltaY : (origin[1] - vy) * tDeltaY;
  let tMaxZ = dz >= 0 ? (vz + 1 - origin[2]) * tDeltaZ : (origin[2] - vz) * tDeltaZ;

  let nx = 0;
  let ny = 1;
  let nz = 0;
  let dist = 0;

  while (dist <= maxDistance) {
    if (world.inBounds(vx, vy, vz)) {
      const b = world.getBlock(vx, vy, vz);
      if (b !== BlockType.Air && b !== BlockType.Water) {
        return { bx: vx, by: vy, bz: vz, nx, ny, nz, distance: dist };
      }
    }

    if (tMaxX < tMaxY) {
      if (tMaxX < tMaxZ) {
        vx += stepX;
        dist = tMaxX;
        tMaxX += tDeltaX;
        nx = -stepX;
        ny = 0;
        nz = 0;
      } else {
        vz += stepZ;
        dist = tMaxZ;
        tMaxZ += tDeltaZ;
        nx = 0;
        ny = 0;
        nz = -stepZ;
      }
    } else {
      if (tMaxY < tMaxZ) {
        vy += stepY;
        dist = tMaxY;
        tMaxY += tDeltaY;
        nx = 0;
        ny = -stepY;
        nz = 0;
      } else {
        vz += stepZ;
        dist = tMaxZ;
        tMaxZ += tDeltaZ;
        nx = 0;
        ny = 0;
        nz = -stepZ;
      }
    }
  }

  return null;
}

interface SkyKeyframe {
  t: number;
  zenith: [number, number, number];
  horizon: [number, number, number];
  sunColor: [number, number, number];
  skyAmbient: [number, number, number];
  mieGlow: [number, number, number];
  skyIntensity: number;
  moonIntensity: number;
  warmGlow: number;
}

// Identical 10-keyframe astronomical table as vulkan_voxel_engine/src/world.cpp
const SKY_KEYS: SkyKeyframe[] = [
  // 0.000 — Sunrise (06:00): rose-gold & peach eastern glow, lavender-indigo zenith
  {
    t: 0.0,
    zenith: [0.22, 0.27, 0.52],
    horizon: [0.95, 0.56, 0.36],
    sunColor: [1.0, 0.62, 0.34],
    skyAmbient: [0.58, 0.44, 0.46],
    mieGlow: [1.0, 0.58, 0.34],
    skyIntensity: 0.56,
    moonIntensity: 0.12,
    warmGlow: 0.96,
  },
  // 0.075 — Early Morning (07:48): soft apricot-gold melting into fresh sky blue
  {
    t: 0.075,
    zenith: [0.2, 0.48, 0.82],
    horizon: [0.82, 0.84, 0.9],
    sunColor: [1.0, 0.88, 0.68],
    skyAmbient: [0.48, 0.62, 0.82],
    mieGlow: [1.0, 0.78, 0.52],
    skyIntensity: 0.86,
    moonIntensity: 0.0,
    warmGlow: 0.42,
  },
  // 0.150 — Crisp Morning (09:36): clear azure sky & dewy natural daylight
  {
    t: 0.15,
    zenith: [0.16, 0.46, 0.84],
    horizon: [0.66, 0.81, 0.94],
    sunColor: [1.0, 0.95, 0.84],
    skyAmbient: [0.45, 0.63, 0.88],
    mieGlow: [1.0, 0.88, 0.7],
    skyIntensity: 0.96,
    moonIntensity: 0.0,
    warmGlow: 0.16,
  },
  // 0.250 — High Noon (12:00): deep cerulean zenith & crisp sunlight
  {
    t: 0.25,
    zenith: [0.13, 0.41, 0.82],
    horizon: [0.6, 0.78, 0.94],
    sunColor: [1.0, 0.97, 0.9],
    skyAmbient: [0.44, 0.62, 0.88],
    mieGlow: [1.0, 0.94, 0.82],
    skyIntensity: 1.0,
    moonIntensity: 0.0,
    warmGlow: 0.1,
  },
  // 0.410 — Golden Evening (15:50): honey-amber sunlight & warm horizon haze
  {
    t: 0.41,
    zenith: [0.17, 0.37, 0.72],
    horizon: [0.88, 0.68, 0.46],
    sunColor: [1.0, 0.76, 0.42],
    skyAmbient: [0.56, 0.5, 0.56],
    mieGlow: [1.0, 0.64, 0.28],
    skyIntensity: 0.82,
    moonIntensity: 0.0,
    warmGlow: 0.74,
  },
  // 0.495 — Dramatic Sunset (17:52): crimson, tangerine & magenta-violet twilight
  {
    t: 0.495,
    zenith: [0.13, 0.15, 0.36],
    horizon: [0.92, 0.38, 0.2],
    sunColor: [1.0, 0.42, 0.16],
    skyAmbient: [0.52, 0.3, 0.36],
    mieGlow: [1.0, 0.36, 0.16],
    skyIntensity: 0.44,
    moonIntensity: 0.16,
    warmGlow: 1.0,
  },
  // 0.575 — Blue Hour Twilight (19:48): deep velvet indigo & faint ember horizon
  {
    t: 0.575,
    zenith: [0.025, 0.042, 0.12],
    horizon: [0.14, 0.12, 0.22],
    sunColor: [0.18, 0.11, 0.09],
    skyAmbient: [0.14, 0.19, 0.36],
    mieGlow: [0.48, 0.22, 0.2],
    skyIntensity: 0.095,
    moonIntensity: 0.72,
    warmGlow: 0.32,
  },
  // 0.750 — Starry Midnight (00:00): deep sapphire sky, true natural moonlit darkness
  {
    t: 0.75,
    zenith: [0.008, 0.015, 0.044],
    horizon: [0.024, 0.042, 0.09],
    sunColor: [0.0, 0.0, 0.0],
    skyAmbient: [0.11, 0.18, 0.36],
    mieGlow: [0.12, 0.18, 0.34],
    skyIntensity: 0.042,
    moonIntensity: 1.0,
    warmGlow: 0.04,
  },
  // 0.925 — Pre-Dawn First Light (04:12): cool plum & soft rose hint on horizon
  {
    t: 0.925,
    zenith: [0.032, 0.048, 0.13],
    horizon: [0.22, 0.16, 0.24],
    sunColor: [0.22, 0.15, 0.13],
    skyAmbient: [0.18, 0.2, 0.36],
    mieGlow: [0.64, 0.32, 0.28],
    skyIntensity: 0.11,
    moonIntensity: 0.62,
    warmGlow: 0.42,
  },
  // 1.000 — Wrap smoothly back to Sunrise
  {
    t: 1.0,
    zenith: [0.22, 0.27, 0.52],
    horizon: [0.95, 0.56, 0.36],
    sunColor: [1.0, 0.62, 0.34],
    skyAmbient: [0.58, 0.44, 0.46],
    mieGlow: [1.0, 0.58, 0.34],
    skyIntensity: 0.56,
    moonIntensity: 0.12,
    warmGlow: 0.96,
  },
];

export function evaluateTimeOfDay(timeOfDay: number): TimeOfDayState {
  const t = ((timeOfDay % 1) + 1) % 1;
  const angle = t * Math.PI * 2;
  const sunX = Math.cos(angle);
  const sunY = Math.sin(angle);
  const sunZ = 0.34 * Math.sin(angle + 0.45) + 0.22;
  const len = Math.hypot(sunX, sunY, sunZ);
  const sunDir: [number, number, number] = [sunX / len, sunY / len, sunZ / len];

  let idx = 0;
  while (idx + 2 < SKY_KEYS.length && t > SKY_KEYS[idx + 1].t) {
    idx++;
  }
  const a = SKY_KEYS[idx];
  const b = SKY_KEYS[idx + 1];
  const span = Math.max(b.t - a.t, 1e-5);
  const u = clamp((t - a.t) / span, 0, 1);
  const s = u * u * (3 - 2 * u);

  const skyZenith = mix3(a.zenith, b.zenith, s);
  const skyHorizon = mix3(a.horizon, b.horizon, s);
  const sunColor = mix3(a.sunColor, b.sunColor, s);
  const skyAmbient = mix3(a.skyAmbient, b.skyAmbient, s);
  const mieGlow = mix3(a.mieGlow, b.mieGlow, s);
  const skyIntensity = a.skyIntensity + (b.skyIntensity - a.skyIntensity) * s;
  const moonIntensity = a.moonIntensity + (b.moonIntensity - a.moonIntensity) * s;
  const warmGlow = a.warmGlow + (b.warmGlow - a.warmGlow) * s;

  // Format 24-hour clock (t=0 => 06:00 Sunrise, t=0.25 => 12:00 Noon, t=0.5 => 18:00 Sunset, t=0.75 => 00:00 Night)
  const totalMinutes = Math.floor(((t * 24 + 6) % 24) * 60 + 0.001);
  const hh = Math.floor(totalMinutes / 60) % 24;
  const mm = totalMinutes % 60;
  const clockText = `${String(hh).padStart(2, '0')}:${String(mm).padStart(2, '0')}`;

  let phaseName: TimeOfDayState['phaseName'] = 'Noon';
  if (t < 0.06 || t >= 0.94) phaseName = 'Sunrise';
  else if (t < 0.19) phaseName = 'Morning';
  else if (t < 0.35) phaseName = 'Noon';
  else if (t < 0.46) phaseName = 'Evening';
  else if (t < 0.56) phaseName = 'Sunset';
  else phaseName = 'Night';

  return {
    sunDir,
    sunColor,
    skyZenith,
    skyHorizon,
    skyAmbient,
    mieGlow,
    horizonFog: skyHorizon,
    caveDark: [0.008, 0.011, 0.018],
    skyIntensity,
    moonIntensity,
    warmGlow,
    phaseName,
    clockText,
  };
}
