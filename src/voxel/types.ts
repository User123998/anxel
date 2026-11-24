export const WORLD_X = 64;
export const WORLD_Y = 36;
export const WORLD_Z = 64;

export enum BlockType {
  Air = 0,
  Grass = 1,
  Dirt = 2,
  Stone = 3,
  DeepSlate = 4,
  Timber = 5,
  Leaves = 6,
  Torch = 7,
  Glowstone = 8,
  Water = 9,
  Sand = 10,
  Magma = 11,
  Crystal = 12,
  Snow = 13,
  Glass = 14,
}

export type ToolId = 'break' | 'torch' | 'glowstone' | 'stone' | 'timber' | 'magma' | 'glass';

export type LightViewMode = 0 | 1 | 2 | 3; // 0=Combined, 1=Sky, 2=Block, 3=AO

export type CameraVistaId = 'overview' | 'caveMouth' | 'insideCave' | 'stoneArch';

export interface RaycastHit {
  bx: number;
  by: number;
  bz: number;
  nx: number;
  ny: number;
  nz: number;
  distance: number;
}

export interface TimeOfDayState {
  sunDir: [number, number, number];
  sunColor: [number, number, number];
  skyZenith: [number, number, number];
  skyHorizon: [number, number, number];
  skyAmbient: [number, number, number];
  mieGlow: [number, number, number];
  horizonFog: [number, number, number];
  caveDark: [number, number, number];
  skyIntensity: number;
  moonIntensity: number;
  warmGlow: number;
  phaseName: 'Sunrise' | 'Morning' | 'Noon' | 'Evening' | 'Sunset' | 'Night';
  clockText: string;
}

export interface WorldProbeStats {
  skyLight: number;
  blockLight: number;
  emitterCount: number;
  darkVoxelCount: number;
  sunlitVoxelCount: number;
  areaLabel: string;
  heldEmission: number;
  eyeUnderwater: boolean;
  waterDepth: number;
}

export interface DebugMetrics {
  fps: number;
  frameMs: number;
  avgMs: number;
  minMs: number;
  maxMs: number;
  frameHistory: number[];
  pos: [number, number, number];
  chunk: [number, number];
  facing: string;
  yawDeg: number;
  pitchDeg: number;
  skyLight: number;
  blockLight: number;
  dynLight: number;
  eyeUnderwater: boolean;
  waterDepth: number;
  opaqueTriangles: number;
  translucentTriangles: number;
  totalVertices: number;
  meshMs: number;
  dynMs: number;
  targetBlock: string;
  targetPos: [number, number, number] | null;
}

export function isTransparent(b: BlockType): boolean {
  return (
    b === BlockType.Air ||
    b === BlockType.Leaves ||
    b === BlockType.Torch ||
    b === BlockType.Water ||
    b === BlockType.Glass
  );
}

export function isTranslucentMesh(b: BlockType): boolean {
  return b === BlockType.Water || b === BlockType.Glass;
}

export function isOpaqueForAO(b: BlockType): boolean {
  return (
    b !== BlockType.Air &&
    b !== BlockType.Torch &&
    b !== BlockType.Water &&
    b !== BlockType.Glass
  );
}

export function lightAttenuation(b: BlockType): number {
  switch (b) {
    case BlockType.Air:
    case BlockType.Torch:
    case BlockType.Glass:
    case BlockType.Water:
      return 1;
    case BlockType.Leaves:
      return 2;
    default:
      return 15;
  }
}

export function blockEmission(b: BlockType): number {
  switch (b) {
    case BlockType.Glowstone:
      return 15;
    case BlockType.Torch:
      return 14;
    case BlockType.Magma:
      return 13;
    case BlockType.Crystal:
      return 10;
    default:
      return 0;
  }
}

export function blockName(b: BlockType): string {
  switch (b) {
    case BlockType.Air: return 'Air';
    case BlockType.Grass: return 'Grass';
    case BlockType.Dirt: return 'Dirt';
    case BlockType.Stone: return 'Stone';
    case BlockType.DeepSlate: return 'DeepSlate';
    case BlockType.Timber: return 'Timber';
    case BlockType.Leaves: return 'Leaves';
    case BlockType.Torch: return 'Torch';
    case BlockType.Glowstone: return 'Glowstone';
    case BlockType.Water: return 'Water';
    case BlockType.Sand: return 'Sand';
    case BlockType.Magma: return 'Magma';
    case BlockType.Crystal: return 'Crystal';
    case BlockType.Snow: return 'Snow';
    case BlockType.Glass: return 'Glass';
    default: return 'Unknown';
  }
}

export function getSkyLight(packed: number): number {
  return (packed >> 4) & 0x0f;
}

export function getBlockLight(packed: number): number {
  return packed & 0x0f;
}

export function setSkyLight(packed: number, sky: number): number {
  return (packed & 0x0f) | ((sky & 0x0f) << 4);
}

export function setBlockLight(packed: number, blk: number): number {
  return (packed & 0xf0) | (blk & 0x0f);
}

export function toolToBlockType(tool: ToolId): BlockType {
  switch (tool) {
    case 'torch':
      return BlockType.Torch;
    case 'glowstone':
      return BlockType.Glowstone;
    case 'stone':
      return BlockType.Stone;
    case 'timber':
      return BlockType.Timber;
    case 'magma':
      return BlockType.Magma;
    case 'glass':
      return BlockType.Glass;
    default:
      return BlockType.Air;
  }
}
