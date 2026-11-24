import {
  BlockType,
  WorldProbeStats,
  blockEmission,
  isOpaqueForAO,
  isTransparent,
  lightAttenuation,
} from './types';
import { VoxelWorld } from './world';

const DX = [1, -1, 0, 0, 0, 0];
const DY = [0, 0, 1, -1, 0, 0];
const DZ = [0, 0, 0, 0, 1, -1];

interface LightNode {
  x: number;
  y: number;
  z: number;
}

interface RemovalNode {
  x: number;
  y: number;
  z: number;
  prevVal: number;
}

function propagateSkyQueue(world: VoxelWorld, addQ: LightNode[]): void {
  let head = 0;
  while (head < addQ.length) {
    const n = addQ[head++];
    const srcLight = world.getSky(n.x, n.y, n.z);
    if (srcLight <= 1) continue;

    for (let d = 0; d < 6; d++) {
      const nx = n.x + DX[d];
      const ny = n.y + DY[d];
      const nz = n.z + DZ[d];
      if (!world.inBounds(nx, ny, nz)) continue;

      const nb = world.getBlock(nx, ny, nz);
      if (!isTransparent(nb)) continue;

      let cost = lightAttenuation(nb);
      if (
        d === 3 &&
        srcLight === 15 &&
        (nb === BlockType.Air || nb === BlockType.Glass)
      ) {
        cost = 0;
      } else if (
        d === 3 &&
        nb === BlockType.Water &&
        srcLight >= 4 &&
        (ny & 1) === 1
      ) {
        cost = 0;
      }
      const nextLight = srcLight > cost ? srcLight - cost : 0;

      if (nextLight > world.getSky(nx, ny, nz)) {
        world.setSky(nx, ny, nz, nextLight);
        addQ.push({ x: nx, y: ny, z: nz });
      }
    }
  }
}

function propagateBlockQueue(world: VoxelWorld, addQ: LightNode[]): void {
  let head = 0;
  while (head < addQ.length) {
    const n = addQ[head++];
    const srcLight = world.getBlockLightVal(n.x, n.y, n.z);
    if (srcLight <= 1) continue;

    for (let d = 0; d < 6; d++) {
      const nx = n.x + DX[d];
      const ny = n.y + DY[d];
      const nz = n.z + DZ[d];
      if (!world.inBounds(nx, ny, nz)) continue;

      const nb = world.getBlock(nx, ny, nz);
      if (!isTransparent(nb)) continue;

      const cost = lightAttenuation(nb);
      const nextLight = srcLight > cost ? srcLight - cost : 0;

      if (nextLight > world.getBlockLightVal(nx, ny, nz)) {
        world.setBlockLightVal(nx, ny, nz, nextLight);
        addQ.push({ x: nx, y: ny, z: nz });
      }
    }
  }
}

function processSkyRemoval(
  world: VoxelWorld,
  remQ: RemovalNode[],
  addQ: LightNode[]
): void {
  let head = 0;
  while (head < remQ.length) {
    const n = remQ[head++];

    for (let d = 0; d < 6; d++) {
      const nx = n.x + DX[d];
      const ny = n.y + DY[d];
      const nz = n.z + DZ[d];
      if (!world.inBounds(nx, ny, nz)) continue;

      const neighborVal = world.getSky(nx, ny, nz);
      if (neighborVal === 0) continue;

      const fedByThis =
        neighborVal < n.prevVal ||
        (d === 3 && n.prevVal === 15 && neighborVal === 15);
      if (fedByThis) {
        world.setSky(nx, ny, nz, 0);
        remQ.push({ x: nx, y: ny, z: nz, prevVal: neighborVal });
      } else if (neighborVal >= n.prevVal) {
        addQ.push({ x: nx, y: ny, z: nz });
      }
    }
  }
}

function processBlockRemoval(
  world: VoxelWorld,
  remQ: RemovalNode[],
  addQ: LightNode[]
): void {
  let head = 0;
  while (head < remQ.length) {
    const n = remQ[head++];

    for (let d = 0; d < 6; d++) {
      const nx = n.x + DX[d];
      const ny = n.y + DY[d];
      const nz = n.z + DZ[d];
      if (!world.inBounds(nx, ny, nz)) continue;

      const neighborVal = world.getBlockLightVal(nx, ny, nz);
      if (neighborVal === 0) continue;

      if (neighborVal < n.prevVal) {
        world.setBlockLightVal(nx, ny, nz, 0);
        remQ.push({ x: nx, y: ny, z: nz, prevVal: neighborVal });
      } else {
        addQ.push({ x: nx, y: ny, z: nz });
      }
    }
  }
}

export function computeFullLightfield(world: VoxelWorld): void {
  world.clearLightField();

  const skyQueue: LightNode[] = [];
  const blockQueue: LightNode[] = [];

  for (let z = 0; z < world.depth; z++) {
    for (let x = 0; x < world.width; x++) {
      let currentSky = 15;
      let waterDepthSteps = 0;
      for (let y = world.height - 1; y >= 0; y--) {
        const b = world.getBlock(x, y, z);

        const emit = blockEmission(b);
        if (emit > 0) {
          world.setBlockLightVal(x, y, z, emit);
          blockQueue.push({ x, y, z });
        }

        if (!isTransparent(b)) {
          currentSky = 0;
          waterDepthSteps = 0;
          continue;
        }

        if (b === BlockType.Leaves) {
          currentSky = Math.max(0, currentSky - lightAttenuation(b));
        } else if (b === BlockType.Water && currentSky > 0) {
          // Gentle open-sky ocean sunlight penetration (floor 3 under open sky; roofed caves stay 0!)
          waterDepthSteps++;
          if ((waterDepthSteps & 1) === 0 && currentSky > 3) {
            currentSky--;
          }
        }

        if (currentSky > 0) {
          world.setSky(x, y, z, currentSky);
          skyQueue.push({ x, y, z });
        }
      }
    }
  }

  propagateSkyQueue(world, skyQueue);
  propagateBlockQueue(world, blockQueue);
}

export function onBlockChanged(
  world: VoxelWorld,
  x: number,
  y: number,
  z: number,
  oldBlock: BlockType,
  newBlock: BlockType
): void {
  if (!world.inBounds(x, y, z)) return;

  const skyRemQ: RemovalNode[] = [];
  const skyAddQ: LightNode[] = [];
  const blkRemQ: RemovalNode[] = [];
  const blkAddQ: LightNode[] = [];

  const oldSky = world.getSky(x, y, z);
  const oldBlk = world.getBlockLightVal(x, y, z);

  if (!isTransparent(newBlock)) {
    if (oldSky > 0) {
      world.setSky(x, y, z, 0);
      skyRemQ.push({ x, y, z, prevVal: oldSky });
    }
    if (oldBlk > 0) {
      world.setBlockLightVal(x, y, z, 0);
      blkRemQ.push({ x, y, z, prevVal: oldBlk });
    }
  } else if (blockEmission(oldBlock) > blockEmission(newBlock)) {
    world.setBlockLightVal(x, y, z, 0);
    blkRemQ.push({ x, y, z, prevVal: oldBlk });
  }

  processSkyRemoval(world, skyRemQ, skyAddQ);
  processBlockRemoval(world, blkRemQ, blkAddQ);

  const newEmit = blockEmission(newBlock);
  if (newEmit > 0) {
    world.setBlockLightVal(x, y, z, newEmit);
    blkAddQ.push({ x, y, z });
  }

  if (isTransparent(newBlock)) {
    const aboveSky = y + 1 < world.height ? world.getSky(x, y + 1, z) : 15;
    if (
      aboveSky === 15 &&
      (newBlock === BlockType.Air || newBlock === BlockType.Glass)
    ) {
      world.setSky(x, y, z, 15);
      skyAddQ.push({ x, y, z });
    }
    for (let d = 0; d < 6; d++) {
      const nx = x + DX[d];
      const ny = y + DY[d];
      const nz = z + DZ[d];
      if (!world.inBounds(nx, ny, nz)) continue;
      if (world.getSky(nx, ny, nz) > 1) skyAddQ.push({ x: nx, y: ny, z: nz });
      if (world.getBlockLightVal(nx, ny, nz) > 1)
        blkAddQ.push({ x: nx, y: ny, z: nz });
    }
  }

  propagateSkyQueue(world, skyAddQ);
  propagateBlockQueue(world, blkAddQ);
}

export interface AreaLightProfile {
  label: string;
  perceivedLight: number;
  darknessFactor: number;
}

/**
 * Zero-anchored exponential falloff curve mirroring vulkan_voxel_engine/src/math/lighting_math.hpp.
 * Guarantees exact 0.0000 at level 0 (no phantom glow in pitch-black caves) and 1.0000 at level 15.
 */
export function zeroAnchoredFalloff(level: number, base: number): number {
  if (level <= 0) return 0;
  if (level >= 15) return 1;
  const raw = Math.pow(base, 15 - level);
  const floorVal = Math.pow(base, 15);
  return (raw - floorVal) / (1 - floorVal);
}

/**
 * Classifies the player's current physical lighting/darkness zone based on sky access,
 * local/held emitter light, and solar elevation — mirrors src/math/lighting_math.hpp.
 */
export function evaluateAreaDarkness(
  skyLevel: number,
  effectiveBlockLevel: number,
  skyIntensity: number
): AreaLightProfile {
  const skyE = zeroAnchoredFalloff(skyLevel, 0.76) * skyIntensity;
  const blkE = zeroAnchoredFalloff(effectiveBlockLevel, 0.74);
  const total = Math.min(1, Math.max(skyE, blkE));

  let label = 'OPEN SKY';
  if (skyLevel === 0) {
    if (effectiveBlockLevel === 0) label = 'PITCH DARK';
    else if (effectiveBlockLevel <= 5) label = 'DIM CAVE';
    else label = 'LIT CAVERN';
  } else if (skyLevel <= 6) {
    label = effectiveBlockLevel >= 8 ? 'LIT CAVERN' : 'CAVE SHADOW';
  } else if (skyLevel < 13) {
    label = skyIntensity < 0.12 ? 'DARK SHADE' : 'OVERHANG';
  } else {
    if (skyIntensity < 0.08) label = 'STARLIT NIGHT';
    else if (skyIntensity < 0.35) label = 'TWILIGHT';
    else if (skyIntensity < 0.72) label = 'GOLDEN HOUR';
    else label = 'OPEN SKY';
  }

  return {
    label,
    perceivedLight: total,
    darknessFactor: 1 - total,
  };
}

export interface UnderwaterOptics {
  depthDark: number;
  starClarity: number;
  discClarity: number;
  glowClarity: number;
  volVeil: number;
  snellAlpha: number;
  fogDensity: number;
  surfaceTransRGB: [number, number, number];
}

function smoothstep(edge0: number, edge1: number, x: number): number {
  const t = Math.max(0, Math.min(1, (x - edge0) / (edge1 - edge0)));
  return t * t * (3 - 2 * t);
}

/**
 * Pure mirror of the depth-dependent underwater optics in sky.frag & voxel.frag.
 * Evaluates star/disc/glow extinction, Beer-Lambert transmittance, volumetric veil,
 * Snell's Window ceiling opacity, and volumetric fog density at a given continuous depth.
 */
export function evaluateUnderwaterOptics(
  eyeDepthMeters: number,
  rayY = 1.0
): UnderwaterOptics {
  const eyeDepth = Math.max(0, eyeDepthMeters);
  const y = Math.max(-1, Math.min(1, rayY));
  const pathToSurface = eyeDepth / Math.max(y, 0.14);
  const depthDark = Math.exp(-eyeDepth * 0.105);

  const starClarity = Math.exp(-eyeDepth * 0.85) * smoothstep(0.55, 0.85, y);
  const discClarity = Math.exp(-eyeDepth * 0.42) * smoothstep(0.45, 0.78, y);
  const glowClarity = Math.exp(-eyeDepth * 0.14) * smoothstep(0.18, 0.68, y);

  const tirMask = 1 - smoothstep(0.50, 0.72, y);
  const volVeil = Math.max(
    tirMask,
    Math.min(0.985, Math.max(0, 1 - Math.exp(-pathToSurface * 0.165)))
  );

  const surfaceClarity = Math.exp(-pathToSurface * 0.135);
  const snellMask = smoothstep(0.56, 0.74, Math.max(0, y));
  const snellAlpha = 0.82 + (0.92 + (0.22 - 0.92) * surfaceClarity - 0.82) * snellMask;

  const fogDensity = 0.026 + (0.068 - 0.026) * Math.min(1, eyeDepth / 24.0);
  const surfaceTransRGB: [number, number, number] = [
    Math.exp(-pathToSurface * 0.24),
    Math.exp(-pathToSurface * 0.11),
    Math.exp(-pathToSurface * 0.075),
  ];

  return {
    depthDark,
    starClarity,
    discClarity,
    glowClarity,
    volVeil,
    snellAlpha,
    fogDensity,
    surfaceTransRGB,
  };
}

export interface DynamicLightState {
  activeVoxels: number[];
  distField: Float32Array | null;
  level: number;
  ox: number;
  oy: number;
  oz: number;
}

export function createDynamicLightState(): DynamicLightState {
  return {
    activeVoxels: [],
    distField: null,
    level: 0,
    ox: -999999,
    oy: -999999,
    oz: -999999,
  };
}

const DYN_LIGHT_SCALE = 16;
const DYN_LIGHT_MAX_F = 240;
const DYN_SQRT2 = Math.SQRT2;
const DYN_INF = 1e6;

const DIAG_X = [1, 1, -1, -1, 1, 1, -1, -1, 0, 0, 0, 0];
const DIAG_Y = [0, 0, 0, 0, 1, -1, 1, -1, 1, 1, -1, -1];
const DIAG_Z = [1, -1, 1, -1, 0, 0, 0, 0, 1, -1, 1, -1];

/**
 * 100% voxel-occluded 18-neighbor continuous floating-point Euclidean BFS for handheld light-source blocks.
 * Emits ONLY when heldEmissionLevel > 0 (Torch, Glowstone, Magma, Crystal);
 * holding Stone, Timber, Glass, Break, or empty hand (level === 0) clears dynamic light immediately.
 * Strictly traverses transparent blocks (including Glass & Water) and requires BOTH orthogonal bridge voxels
 * to be transparent for diagonal steps (zero bleed through solid walls or diagonal corners).
 */
export function updateDynamicLight(
  world: VoxelWorld,
  state: DynamicLightState,
  ox: number,
  oy: number,
  oz: number,
  heldEmissionLevel: number,
  forceRefresh = false
): boolean {
  const clampedLevel = Math.max(0, Math.min(15, heldEmissionLevel | 0));
  if (
    !forceRefresh &&
    state.level === clampedLevel &&
    (clampedLevel === 0 || (state.ox === ox && state.oy === oy && state.oz === oz))
  ) {
    return false;
  }

  const totalVoxels = world.blocks.length;
  if (!state.distField || state.distField.length !== totalVoxels) {
    state.distField = new Float32Array(totalVoxels);
    state.distField.fill(DYN_INF);
  }
  const distField = state.distField;

  // 1. Clear only previously lit dynamic voxels in O(lit)
  for (let i = 0; i < state.activeVoxels.length; i++) {
    const idx = state.activeVoxels[i];
    world.dynLight[idx] = 0;
    distField[idx] = DYN_INF;
  }
  state.activeVoxels.length = 0;

  state.level = clampedLevel;
  state.ox = ox;
  state.oy = oy;
  state.oz = oz;

  if (clampedLevel === 0 || !world.inBounds(ox, oy, oz)) {
    return true;
  }

  // Never emit light through walls if camera is inside an opaque voxel
  if (!isTransparent(world.getBlock(ox, oy, oz))) {
    return true;
  }

  const maxRange = clampedLevel;
  const startIdx = world.index(ox, oy, oz);
  distField[startIdx] = 0;
  world.dynLight[startIdx] = clampedLevel * DYN_LIGHT_SCALE;
  state.activeVoxels.push(startIdx);

  const qx: number[] = [ox];
  const qy: number[] = [oy];
  const qz: number[] = [oz];
  let head = 0;

  while (head < qx.length) {
    const x = qx[head];
    const y = qy[head];
    const z = qz[head];
    head++;

    const srcDist = distField[world.index(x, y, z)];
    if (srcDist >= maxRange - 0.5) continue;

    // Orthogonal 6 neighbors (exact floating-point step = 1.0 * attenuation)
    for (let d = 0; d < 6; d++) {
      const nx = x + DX[d];
      const ny = y + DY[d];
      const nz = z + DZ[d];
      if (!world.inBounds(nx, ny, nz)) continue;

      const nb = world.getBlock(nx, ny, nz);
      if (!isTransparent(nb)) continue;

      const nextDist = srcDist + lightAttenuation(nb);
      if (nextDist >= maxRange) continue;

      const nIdx = world.index(nx, ny, nz);
      if (nextDist + 0.001 < distField[nIdx]) {
        if (distField[nIdx] >= DYN_INF * 0.5) {
          state.activeVoxels.push(nIdx);
        }
        distField[nIdx] = nextDist;
        world.dynLight[nIdx] = Math.min(
          240,
          Math.max(1, Math.round((maxRange - nextDist) * DYN_LIGHT_SCALE))
        );
        qx.push(nx);
        qy.push(ny);
        qz.push(nz);
      }
    }

    // Open-corner 2D diagonal neighbors (exact floating-point step = sqrt(2) * attenuation)
    if (srcDist + DYN_SQRT2 < maxRange) {
      for (let d = 0; d < 12; d++) {
        const dx = DIAG_X[d];
        const dy = DIAG_Y[d];
        const dz = DIAG_Z[d];
        const nx = x + dx;
        const ny = y + dy;
        const nz = z + dz;
        if (!world.inBounds(nx, ny, nz)) continue;

        const nb = world.getBlock(nx, ny, nz);
        if (!isTransparent(nb)) continue;

        // Strict corner occlusion: both orthogonal bridge voxels must be transparent
        const bridge1 =
          dx !== 0
            ? isTransparent(world.getBlock(x + dx, y, z))
            : isTransparent(world.getBlock(x, y + dy, z));
        const bridge2 =
          dz !== 0
            ? isTransparent(world.getBlock(x, y, z + dz))
            : isTransparent(world.getBlock(x, y + dy, z));
        if (!bridge1 || !bridge2) continue;

        const nextDist = srcDist + DYN_SQRT2 * lightAttenuation(nb);
        if (nextDist >= maxRange) continue;

        const nIdx = world.index(nx, ny, nz);
        if (nextDist + 0.001 < distField[nIdx]) {
          if (distField[nIdx] >= DYN_INF * 0.5) {
            state.activeVoxels.push(nIdx);
          }
          distField[nIdx] = nextDist;
          world.dynLight[nIdx] = Math.min(
            240,
            Math.max(1, Math.round((maxRange - nextDist) * DYN_LIGHT_SCALE))
          );
          qx.push(nx);
          qy.push(ny);
          qz.push(nz);
        }
      }
    }
  }

  return true;
}

export function sampleVertexLightAO(
  world: VoxelWorld,
  airX: number,
  airY: number,
  airZ: number,
  ux: number,
  uy: number,
  uz: number,
  vx: number,
  vy: number,
  vz: number,
  su: number,
  sv: number
): [number, number, number, number] {
  const s1x = airX + ux * su;
  const s1y = airY + uy * su;
  const s1z = airZ + uz * su;

  const s2x = airX + vx * sv;
  const s2y = airY + vy * sv;
  const s2z = airZ + vz * sv;

  const cx = airX + ux * su + vx * sv;
  const cy = airY + uy * su + vy * sv;
  const cz = airZ + uz * su + vz * sv;

  const s1 = isOpaqueForAO(world.getBlock(s1x, s1y, s1z));
  const s2 = isOpaqueForAO(world.getBlock(s2x, s2y, s2z));
  const cn = isOpaqueForAO(world.getBlock(cx, cy, cz));

  const aoLevel = s1 && s2 ? 0 : 3 - (Number(s1) + Number(s2) + Number(cn));

  let skySum = world.getSky(airX, airY, airZ);
  let blkSum = world.getBlockLightVal(airX, airY, airZ);
  let dynSum = world.getDynamicLightVal(airX, airY, airZ);
  let count = 1;

  if (!s1) {
    skySum += world.getSky(s1x, s1y, s1z);
    blkSum += world.getBlockLightVal(s1x, s1y, s1z);
    dynSum += world.getDynamicLightVal(s1x, s1y, s1z);
    count++;
  }
  if (!s2) {
    skySum += world.getSky(s2x, s2y, s2z);
    blkSum += world.getBlockLightVal(s2x, s2y, s2z);
    dynSum += world.getDynamicLightVal(s2x, s2y, s2z);
    count++;
  }
  if (!(s1 && s2) && !cn) {
    skySum += world.getSky(cx, cy, cz);
    blkSum += world.getBlockLightVal(cx, cy, cz);
    dynSum += world.getDynamicLightVal(cx, cy, cz);
    count++;
  }

  return [
    skySum / (count * 15),
    blkSum / (count * 15),
    aoLevel / 3,
    dynSum / (count * DYN_LIGHT_MAX_F),
  ];
}

export function shouldFlipQuad(
  ao0: number,
  ao1: number,
  ao2: number,
  ao3: number,
  l0 = 0,
  l1 = 0,
  l2 = 0,
  l3 = 0
): boolean {
  const aoDiff = ao0 + ao2 - (ao1 + ao3);
  if (aoDiff < -0.001) return true;
  if (aoDiff > 0.001) return false;
  return Math.abs(l0 - l2) > Math.abs(l1 - l3);
}

export function probeWorldStats(
  world: VoxelWorld,
  probePos: [number, number, number],
  skyIntensity = 1.0,
  heldEmission = 0
): WorldProbeStats {
  const px = Math.floor(probePos[0]);
  const py = Math.floor(probePos[1]);
  const pz = Math.floor(probePos[2]);

  let emitterCount = 0;
  let darkVoxelCount = 0;
  let sunlitVoxelCount = 0;

  const len = world.blocks.length;
  for (let i = 0; i < len; i++) {
    const b = world.blocks[i] as BlockType;
    if (blockEmission(b) > 0) emitterCount++;
    if (b === BlockType.Air) {
      const sky = (world.light[i] >> 4) & 0x0f;
      const dynLvl = Math.min(15, (world.dynLight[i] + 8) >> 4);
      const blk = Math.max(world.light[i] & 0x0f, dynLvl);
      if (sky === 0 && blk === 0) darkVoxelCount++;
      else if (sky >= 14) sunlitVoxelCount++;
    }
  }

  const skyLight = world.getSky(px, py, pz);
  const blockLight = world.getEffectiveBlockLight(px, py, pz);
  const area = evaluateAreaDarkness(skyLight, blockLight, skyIntensity);
  const eyeUnderwater = world.getBlock(px, py, pz) === BlockType.Water;
  let waterDepth = 0;
  if (eyeUnderwater) {
    let wy = py;
    while (wy < world.height && world.getBlock(px, wy, pz) === BlockType.Water) {
      waterDepth++;
      wy++;
    }
  }

  return {
    skyLight,
    blockLight,
    emitterCount,
    darkVoxelCount,
    sunlitVoxelCount,
    areaLabel: eyeUnderwater ? 'UNDERWATER' : area.label,
    heldEmission,
    eyeUnderwater,
    waterDepth,
  };
}
