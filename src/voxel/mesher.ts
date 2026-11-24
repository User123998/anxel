import * as THREE from 'three';
import {
  BlockType,
  blockEmission,
  isTranslucentMesh,
  isTransparent,
} from './types';
import { VoxelWorld } from './world';
import { sampleVertexLightAO, shouldFlipQuad } from './lighting';

interface FaceDef {
  normal: [number, number, number];
  corners: [number, number, number][];
  uAxis: [number, number, number];
  vAxis: [number, number, number];
}

// Face indices: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z (CCW outward winding)
const FACES: FaceDef[] = [
  {
    normal: [1, 0, 0],
    corners: [
      [1, 0, 1],
      [1, 0, 0],
      [1, 1, 0],
      [1, 1, 1],
    ],
    uAxis: [0, 0, -1],
    vAxis: [0, 1, 0],
  },
  {
    normal: [-1, 0, 0],
    corners: [
      [0, 0, 0],
      [0, 0, 1],
      [0, 1, 1],
      [0, 1, 0],
    ],
    uAxis: [0, 0, 1],
    vAxis: [0, 1, 0],
  },
  {
    normal: [0, 1, 0],
    corners: [
      [0, 1, 1],
      [1, 1, 1],
      [1, 1, 0],
      [0, 1, 0],
    ],
    uAxis: [1, 0, 0],
    vAxis: [0, 0, -1],
  },
  {
    normal: [0, -1, 0],
    corners: [
      [0, 0, 0],
      [1, 0, 0],
      [1, 0, 1],
      [0, 0, 1],
    ],
    uAxis: [1, 0, 0],
    vAxis: [0, 0, 1],
  },
  {
    normal: [0, 0, 1],
    corners: [
      [0, 0, 1],
      [1, 0, 1],
      [1, 1, 1],
      [0, 1, 1],
    ],
    uAxis: [1, 0, 0],
    vAxis: [0, 1, 0],
  },
  {
    normal: [0, 0, -1],
    corners: [
      [1, 0, 0],
      [0, 0, 0],
      [0, 1, 0],
      [1, 1, 0],
    ],
    uAxis: [-1, 0, 0],
    vAxis: [0, 1, 0],
  },
];

const QUAD_UV: [number, number][] = [
  [0, 0],
  [1, 0],
  [1, 1],
  [0, 1],
];

const CORNER_SU = [-1, 1, 1, -1];
const CORNER_SV = [-1, -1, 1, 1];

export interface VoxelMeshBuildResult {
  geometry: THREE.BufferGeometry;
  translucentGeometry: THREE.BufferGeometry;
  vertexCount: number;
  triangleCount: number;
  opaqueTriangles: number;
  translucentTriangles: number;
}

interface MeshAccumulator {
  positions: number[];
  normals: number[];
  uvs: number[];
  lightAO: number[];
  matFace: number[];
  waterData: number[]; // [submerged (0|1), waterDepth (0..15)]
  indices: number[];
  vCount: number;
}

function createAccumulator(): MeshAccumulator {
  return {
    positions: [],
    normals: [],
    uvs: [],
    lightAO: [],
    matFace: [],
    waterData: [],
    indices: [],
    vCount: 0,
  };
}

function finalizeGeometry(acc: MeshAccumulator): THREE.BufferGeometry {
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(acc.positions, 3));
  geometry.setAttribute('normal', new THREE.Float32BufferAttribute(acc.normals, 3));
  geometry.setAttribute('uv', new THREE.Float32BufferAttribute(acc.uvs, 2));
  geometry.setAttribute('aLightAO', new THREE.Float32BufferAttribute(acc.lightAO, 4));
  geometry.setAttribute('aMatFace', new THREE.Float32BufferAttribute(acc.matFace, 2));
  geometry.setAttribute('aWaterData', new THREE.Float32BufferAttribute(acc.waterData, 2));
  geometry.setIndex(acc.indices);
  geometry.computeBoundingSphere();
  return geometry;
}

function measureWaterDepthBelow(world: VoxelWorld, x: number, y: number, z: number): number {
  if (world.getBlock(x, y, z) !== BlockType.Water) return 0;
  let depth = 0;
  for (let dy = 0; dy < 12; dy++) {
    const yy = y - dy;
    if (yy < 0) break;
    if (world.getBlock(x, yy, z) === BlockType.Water) {
      depth++;
    } else {
      break;
    }
  }
  return depth;
}

function sampleSmoothWaterDepth(
  world: VoxelWorld,
  x: number,
  y: number,
  z: number,
  ux: number,
  uz: number,
  vx: number,
  vz: number,
  su: number,
  sv: number
): number {
  const d0 = measureWaterDepthBelow(world, x, y, z);
  const d1 = measureWaterDepthBelow(world, x + ux * su, y, z + uz * su);
  const d2 = measureWaterDepthBelow(world, x + vx * sv, y, z + vz * sv);
  const d3 = measureWaterDepthBelow(world, x + ux * su + vx * sv, y, z + uz * su + vz * sv);
  return (d0 + d1 + d2 + d3) * 0.25;
}

function measureWaterDepthAbove(world: VoxelWorld, x: number, y: number, z: number): number {
  let depth = 0;
  for (let dy = 0; dy < 12; dy++) {
    const yy = y + dy;
    if (yy >= world.height) break;
    if (world.getBlock(x, yy, z) === BlockType.Water) {
      depth++;
    } else {
      break;
    }
  }
  return depth;
}

export function buildWorldGeometry(world: VoxelWorld): VoxelMeshBuildResult {
  const opaque = createAccumulator();
  const trans = createAccumulator();

  const pushTorch = (x: number, y: number, z: number) => {
    const minX = x + 0.39;
    const maxX = x + 0.61;
    const minY = y + 0.0;
    const maxY = y + 0.74;
    const minZ = z + 0.39;
    const maxZ = z + 0.61;
    const skyNorm = world.getSky(x, y, z) / 15;

    for (let f = 0; f < 6; f++) {
      const fd = FACES[f];
      const base = opaque.vCount;
      for (let c = 0; c < 4; c++) {
        const corner = fd.corners[c];
        opaque.positions.push(
          corner[0] > 0.5 ? maxX : minX,
          corner[1] > 0.5 ? maxY : minY,
          corner[2] > 0.5 ? maxZ : minZ
        );
        opaque.normals.push(fd.normal[0], fd.normal[1], fd.normal[2]);
        opaque.uvs.push(QUAD_UV[c][0], QUAD_UV[c][1]);
        opaque.lightAO.push(skyNorm, 1.0, 1.0, 0.0);
        opaque.matFace.push(BlockType.Torch, f);
        opaque.waterData.push(0.0, 0.0);
        opaque.vCount++;
      }
      opaque.indices.push(base, base + 1, base + 2, base, base + 2, base + 3);
    }
  };

  for (let y = 0; y < world.height; y++) {
    for (let z = 0; z < world.depth; z++) {
      for (let x = 0; x < world.width; x++) {
        const b = world.getBlock(x, y, z);
        if (b === BlockType.Air) continue;

        if (b === BlockType.Torch) {
          pushTorch(x, y, z);
          continue;
        }

        const isTrans = isTranslucentMesh(b);
        const acc = isTrans ? trans : opaque;
        const selfEmit = blockEmission(b) / 15;

        for (let f = 0; f < 6; f++) {
          const fd = FACES[f];
          const nx = x + fd.normal[0];
          const ny = y + fd.normal[1];
          const nz = z + fd.normal[2];

          const neighbor = world.getBlock(nx, ny, nz);
          if (b === BlockType.Water) {
            // Never emit bottom (-Y) faces into underwater pockets; open-sky underside is emitted by isWaterTop!
            if (f === 3) continue;
            if (f === 2) {
              if (
                neighbor !== BlockType.Air &&
                neighbor !== BlockType.Torch &&
                neighbor !== BlockType.Leaves
              ) {
                continue;
              }
            } else {
              // Side faces only against open Air -> never create glowing blue boxes around torches/leaves/glass
              if (neighbor !== BlockType.Air) continue;
            }
          } else if (b === BlockType.Glass) {
            // Connected glass: cull internal faces between adjacent Glass blocks
            if (neighbor === BlockType.Glass || !isTransparent(neighbor)) {
              continue;
            }
          } else {
            if (!isTransparent(neighbor)) continue;
          }

          let submerged = 0.0;
          let fallbackWaterDepth = 0.0;
          if (b === BlockType.Water) {
            fallbackWaterDepth = Math.max(1, measureWaterDepthBelow(world, x, y, z));
          } else if (neighbor === BlockType.Water) {
            submerged = 1.0;
            fallbackWaterDepth = Math.max(1, measureWaterDepthAbove(world, nx, ny, nz));
          }

          const isWaterTop = b === BlockType.Water && f === 2;
          const topRecess = isWaterTop ? 0.12 : 0.0;
          const aboveIsAir =
            b === BlockType.Water &&
            y + 1 < world.height &&
            world.getBlock(x, y + 1, z) !== BlockType.Water;

          const base = acc.vCount;
          const aos = [0, 0, 0, 0];
          const lumas = [0, 0, 0, 0];
          const cornerDepths = [fallbackWaterDepth, fallbackWaterDepth, fallbackWaterDepth, fallbackWaterDepth];

          for (let c = 0; c < 4; c++) {
            const [sL, bL, ao, dReach] = sampleVertexLightAO(
              world,
              nx,
              ny,
              nz,
              fd.uAxis[0],
              fd.uAxis[1],
              fd.uAxis[2],
              fd.vAxis[0],
              fd.vAxis[1],
              fd.vAxis[2],
              CORNER_SU[c],
              CORNER_SV[c]
            );
            const effStatic = Math.max(bL, selfEmit);
            aos[c] = isTrans ? 1.0 : ao;
            lumas[c] = sL + effStatic + dReach;

            const corner = fd.corners[c];
            let py = y + corner[1];
            if (isWaterTop) {
              py -= topRecess;
              cornerDepths[c] = sampleSmoothWaterDepth(
                world,
                x,
                y,
                z,
                fd.uAxis[0],
                fd.uAxis[2],
                fd.vAxis[0],
                fd.vAxis[2],
                CORNER_SU[c],
                CORNER_SV[c]
              );
            } else if (b === BlockType.Water && aboveIsAir && corner[1] > 0.5) {
              py -= 0.12;
            }

            acc.positions.push(x + corner[0], py, z + corner[2]);
            acc.normals.push(fd.normal[0], fd.normal[1], fd.normal[2]);
            acc.uvs.push(QUAD_UV[c][0], QUAD_UV[c][1]);
            acc.lightAO.push(sL, effStatic, aos[c], dReach);
            acc.matFace.push(b, f);
            acc.waterData.push(submerged, cornerDepths[c]);
            acc.vCount++;
          }

          if (
            !shouldFlipQuad(
              aos[0],
              aos[1],
              aos[2],
              aos[3],
              lumas[0],
              lumas[1],
              lumas[2],
              lumas[3]
            )
          ) {
            acc.indices.push(base, base + 1, base + 2, base, base + 2, base + 3);
          } else {
            acc.indices.push(base + 1, base + 2, base + 3, base + 1, base + 3, base);
          }

          // Emit matching -Y underside meniscus face for Water surface (enables underwater Snell's Window)
          if (isWaterTop) {
            const uBase = acc.vCount;
            const underFd = FACES[3]; // -Y
            for (let c = 0; c < 4; c++) {
              const corner = underFd.corners[c];
              const sL = world.getSky(x, y, z) / 15;
              const bL = world.getBlockLightVal(x, y, z) / 15;
              const dReach = world.getDynamicLightVal(x, y, z) / 240;
              acc.positions.push(x + corner[0], y + 0.88, z + corner[2]);
              acc.normals.push(0, -1, 0);
              acc.uvs.push(QUAD_UV[c][0], QUAD_UV[c][1]);
              acc.lightAO.push(sL, bL, 1.0, dReach);
              acc.matFace.push(BlockType.Water, 3);
              acc.waterData.push(1.0, fallbackWaterDepth);
              acc.vCount++;
            }
            acc.indices.push(uBase, uBase + 1, uBase + 2, uBase, uBase + 2, uBase + 3);
          }
        }
      }
    }
  }

  const geometry = finalizeGeometry(opaque);
  const translucentGeometry = finalizeGeometry(trans);
  const opaqueTriangles = Math.floor(opaque.indices.length / 3);
  const translucentTriangles = Math.floor(trans.indices.length / 3);

  return {
    geometry,
    translucentGeometry,
    vertexCount: opaque.vCount + trans.vCount,
    triangleCount: opaqueTriangles + translucentTriangles,
    opaqueTriangles,
    translucentTriangles,
  };
}
