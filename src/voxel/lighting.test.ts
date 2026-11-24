import test from 'node:test';
import assert from 'node:assert/strict';
import { BlockType, blockEmission } from './types';
import { VoxelWorld, evaluateTimeOfDay, raycastVoxel } from './world';
import {
  computeFullLightfield,
  createDynamicLightState,
  evaluateAreaDarkness,
  evaluateUnderwaterOptics,
  onBlockChanged,
  sampleVertexLightAO,
  shouldFlipQuad,
  updateDynamicLight,
  zeroAnchoredFalloff,
} from './lighting';

test('sky light illuminates open air to 15 and drops to true darkness (0) inside sealed cave', () => {
  const w = new VoxelWorld(16, 16, 16);
  // Build a completely sealed 5x5x5 stone room centered at (8, 5, 8)
  for (let y = 3; y <= 7; y++) {
    for (let z = 6; z <= 10; z++) {
      for (let x = 6; x <= 10; x++) {
        const isWall =
          x === 6 || x === 10 || y === 3 || y === 7 || z === 6 || z === 10;
        if (isWall) w.setBlockRaw(x, y, z, BlockType.Stone);
      }
    }
  }

  computeFullLightfield(w);

  // Open sky above roof must be 15
  assert.equal(w.getSky(8, 12, 8), 15);
  // Sealed room interior must be pitch black (sky=0, block=0)
  assert.equal(w.getSky(8, 5, 8), 0);
  assert.equal(w.getBlockLightVal(8, 5, 8), 0);
});

test('breaking a cave roof skylight floods sunlight inside, and sealing it restores darkness', () => {
  const w = new VoxelWorld(16, 16, 16);
  for (let y = 3; y <= 7; y++) {
    for (let z = 6; z <= 10; z++) {
      for (let x = 6; x <= 10; x++) {
        const isWall =
          x === 6 || x === 10 || y === 3 || y === 7 || z === 6 || z === 10;
        if (isWall) w.setBlockRaw(x, y, z, BlockType.Stone);
      }
    }
  }
  computeFullLightfield(w);
  assert.equal(w.getSky(8, 5, 8), 0);

  // Punch a hole in the roof at (8, 7, 8)
  w.setBlockRaw(8, 7, 8, BlockType.Air);
  onBlockChanged(w, 8, 7, 8, BlockType.Stone, BlockType.Air);

  // Direct shaft below opening receives full skylight 15, adjacent cell decays by 1
  assert.equal(w.getSky(8, 5, 8), 15);
  assert.equal(w.getSky(9, 5, 8), 14);

  // Seal the hole again -> darkness immediately reclaims the interior
  w.setBlockRaw(8, 7, 8, BlockType.Stone);
  onBlockChanged(w, 8, 7, 8, BlockType.Air, BlockType.Stone);

  assert.equal(w.getSky(8, 5, 8), 0);
  assert.equal(w.getSky(9, 5, 8), 0);
});

test('torch block-light floods outward by -1 per step, respects opaque walls, and clears cleanly on removal', () => {
  const w = new VoxelWorld(20, 12, 12);
  // Vertical opaque wall at x = 10 separating torch at x = 8 from dark side at x = 11
  for (let y = 0; y < 12; y++) {
    for (let z = 0; z < 12; z++) {
      w.setBlockRaw(10, y, z, BlockType.Stone);
    }
  }
  computeFullLightfield(w);

  // Place torch at (8, 5, 5)
  w.setBlockRaw(8, 5, 5, BlockType.Torch);
  onBlockChanged(w, 8, 5, 5, BlockType.Air, BlockType.Torch);

  assert.equal(w.getBlockLightVal(8, 5, 5), 14);
  assert.equal(w.getBlockLightVal(9, 5, 5), 13);
  assert.equal(w.getBlockLightVal(6, 5, 5), 12);
  // Must NOT bleed through solid stone wall
  assert.equal(w.getBlockLightVal(11, 5, 5), 0);

  // Break torch -> darkness returns
  w.setBlockRaw(8, 5, 5, BlockType.Air);
  onBlockChanged(w, 8, 5, 5, BlockType.Torch, BlockType.Air);

  assert.equal(w.getBlockLightVal(8, 5, 5), 0);
  assert.equal(w.getBlockLightVal(9, 5, 5), 0);
  assert.equal(w.getBlockLightVal(6, 5, 5), 0);
});

test('smooth vertex AO darkens crevices and triggers quad diagonal flip correctly', () => {
  const w = new VoxelWorld(8, 8, 8);
  // Floor at y=2, two adjacent walls forming a corner at (4,3,3) and (3,3,4)
  w.setBlockRaw(3, 2, 3, BlockType.Stone);
  w.setBlockRaw(4, 3, 3, BlockType.Stone);
  w.setBlockRaw(3, 3, 4, BlockType.Stone);
  computeFullLightfield(w);

  // Sample corner (+1, +1) on top face air cell (3,3,3) along U=(1,0,0), V=(0,0,1)
  const [, , aoCorner] = sampleVertexLightAO(w, 3, 3, 3, 1, 0, 0, 0, 0, 1, 1, 1);
  // Both side1 and side2 are solid -> aoLevel must be 0
  assert.equal(aoCorner, 0);

  // Open corner (-1, -1) has no blockers -> aoLevel must be 1.0 (3/3)
  const [, , aoOpen] = sampleVertexLightAO(w, 3, 3, 3, 1, 0, 0, 0, 0, 1, -1, -1);
  assert.equal(aoOpen, 1);

  assert.equal(shouldFlipQuad(0, 1, 0, 1), true);
  assert.equal(shouldFlipQuad(1, 0, 1, 0), false);
});

test('temporal solar curve transitions smoothly across Sunrise, Morning, Noon, Evening, Sunset, and Night', () => {
  const sunrise = evaluateTimeOfDay(0.005);
  const morning = evaluateTimeOfDay(0.125);
  const noon = evaluateTimeOfDay(0.25);
  const evening = evaluateTimeOfDay(0.41);
  const sunset = evaluateTimeOfDay(0.495);
  const night = evaluateTimeOfDay(0.75);

  assert.equal(sunrise.phaseName, 'Sunrise');
  assert.equal(morning.phaseName, 'Morning');
  assert.equal(noon.phaseName, 'Noon');
  assert.equal(evening.phaseName, 'Evening');
  assert.equal(sunset.phaseName, 'Sunset');
  assert.equal(night.phaseName, 'Night');

  assert.ok(noon.skyIntensity > 0.95);
  assert.ok(morning.skyIntensity > 0.85);
  assert.ok(night.skyIntensity < 0.06); // True natural night darkness
  assert.ok(night.moonIntensity > 0.95);
  assert.ok(sunrise.warmGlow > 0.85);
  assert.ok(evening.warmGlow > 0.65);
  assert.ok(sunset.warmGlow > 0.95);
  assert.ok(sunrise.skyHorizon[0] > sunrise.skyHorizon[2]);
  assert.ok(sunset.skyHorizon[0] > sunset.skyHorizon[2]);
  assert.ok(noon.skyZenith[2] > noon.skyZenith[0]);
});

test('DDA raycast hits solid block and returns outward face normal', () => {
  const w = new VoxelWorld(12, 12, 12);
  w.setBlockRaw(5, 5, 5, BlockType.Stone);

  const hit = raycastVoxel(w, [2.5, 5.5, 5.5], [1, 0, 0], 10);
  assert.ok(hit !== null);
  assert.equal(hit.bx, 5);
  assert.equal(hit.by, 5);
  assert.equal(hit.bz, 5);
  assert.equal(hit.nx, -1);
  assert.equal(hit.ny, 0);
  assert.equal(hit.nz, 0);
});

test('zero-anchored falloff guarantees exact 0.0000 at level 0 and natural area-darkness classification', () => {
  assert.equal(zeroAnchoredFalloff(0, 0.76), 0);
  assert.equal(zeroAnchoredFalloff(0, 0.74), 0);
  assert.equal(zeroAnchoredFalloff(15, 0.76), 1);
  assert.equal(zeroAnchoredFalloff(15, 0.74), 1);

  for (let lvl = 1; lvl <= 15; lvl++) {
    assert.ok(zeroAnchoredFalloff(lvl, 0.76) > zeroAnchoredFalloff(lvl - 1, 0.76));
    assert.ok(zeroAnchoredFalloff(lvl, 0.74) > zeroAnchoredFalloff(lvl - 1, 0.74));
  }

  const sealedDark = evaluateAreaDarkness(0, 0, 1.0);
  assert.equal(sealedDark.label, 'PITCH DARK');
  assert.equal(sealedDark.perceivedLight, 0);
  assert.equal(sealedDark.darknessFactor, 1);

  const litCave = evaluateAreaDarkness(0, 14, 0.042);
  assert.equal(litCave.label, 'LIT CAVERN');
  assert.ok(litCave.perceivedLight > 0.7);

  const openNight = evaluateAreaDarkness(15, 0, 0.042);
  assert.equal(openNight.label, 'STARLIT NIGHT');
  assert.ok(openNight.darknessFactor > 0.94);

  const openNoon = evaluateAreaDarkness(15, 0, 1.0);
  assert.equal(openNoon.label, 'OPEN SKY');
  assert.equal(openNoon.perceivedLight, 1);
});

test('held-block dynamic BFS emits ONLY for light-source blocks (Torch/Glowstone/Magma) and never bleeds through walls', () => {
  const w = new VoxelWorld(16, 16, 16);
  // Sealed 5x5x5 stone room spanning x:[6..10], y:[3..7], z:[6..10]
  for (let y = 3; y <= 7; y++) {
    for (let z = 6; z <= 10; z++) {
      for (let x = 6; x <= 10; x++) {
        const isWall =
          x === 6 || x === 10 || y === 3 || y === 7 || z === 6 || z === 10;
        if (isWall) w.setBlockRaw(x, y, z, BlockType.Stone);
      }
    }
  }
  computeFullLightfield(w);
  const dyn = createDynamicLightState();

  // Holding Stone (emission 0) emits ZERO dynamic light
  updateDynamicLight(w, dyn, 8, 5, 8, blockEmission(BlockType.Stone), false);
  assert.equal(w.getEffectiveBlockLight(8, 5, 8), 0);

  // Switch to Torch (emission 14) at center of sealed room (8, 5, 8)
  const changedTorch = updateDynamicLight(w, dyn, 8, 5, 8, blockEmission(BlockType.Torch), false);
  assert.equal(changedTorch, true);
  assert.equal(w.getEffectiveBlockLight(8, 5, 8), 14);
  assert.equal(w.getEffectiveBlockLight(9, 5, 8), 13);
  // 2D open-corner diagonal step uses Euclidean weight (~1.44 blocks) instead of Manhattan (2.0 blocks)
  assert.equal(w.getDynamicLightVal(9, 5, 9), 14 * 16 - 23);
  assert.equal(w.getEffectiveBlockLight(7, 4, 7), 12);

  // Exterior cells right outside every wall must receive ZERO dynamic light (no bleed through blocks!)
  assert.equal(w.getEffectiveBlockLight(11, 5, 8), 0);
  assert.equal(w.getEffectiveBlockLight(5, 5, 8), 0);
  assert.equal(w.getEffectiveBlockLight(8, 5, 11), 0);
  assert.equal(w.getEffectiveBlockLight(8, 5, 5), 0);
  assert.equal(w.getEffectiveBlockLight(8, 8, 8), 0);
  assert.equal(w.getEffectiveBlockLight(8, 2, 8), 0);
  // Exterior diagonal corner outside sealed room must ALSO be strictly 0 (zero corner leak!)
  assert.equal(w.getDynamicLightVal(11, 5, 11), 0);

  // Smooth vertex sampling on exterior wall face (x=11 air cell) must see exact 0 static & 0 dynReach
  const [, extBlkLight, , extDynReach] = sampleVertexLightAO(w, 11, 5, 8, 0, 1, 0, 0, 0, 1, 1, 1);
  assert.equal(extBlkLight, 0);
  assert.equal(extDynReach, 0);

  // Switch to Glowstone (emission 15) -> updates live at the same position!
  const changedGlow = updateDynamicLight(w, dyn, 8, 5, 8, blockEmission(BlockType.Glowstone), false);
  assert.equal(changedGlow, true);
  assert.equal(w.getEffectiveBlockLight(8, 5, 8), 15);
  assert.equal(w.getEffectiveBlockLight(11, 5, 8), 0);

  // Open a doorway at (10, 5, 8) -> held light spills smoothly outside around the opening
  w.setBlockRaw(10, 5, 8, BlockType.Air);
  updateDynamicLight(w, dyn, 8, 5, 8, blockEmission(BlockType.Torch), true);
  assert.equal(w.getEffectiveBlockLight(10, 5, 8), 12);
  assert.equal(w.getEffectiveBlockLight(11, 5, 8), 11);

  // Standing inside a solid stone block (6, 5, 8) must suppress emission completely
  updateDynamicLight(w, dyn, 6, 5, 8, blockEmission(BlockType.Torch), false);
  assert.equal(w.getEffectiveBlockLight(7, 5, 8), 0);
  assert.equal(w.getEffectiveBlockLight(5, 5, 8), 0);

  // Switching back to Timber / Break (emission 0) immediately restores complete darkness inside
  updateDynamicLight(w, dyn, 8, 5, 8, blockEmission(BlockType.Timber), false);
  assert.equal(w.getEffectiveBlockLight(8, 5, 8), 0);
  assert.equal(w.getEffectiveBlockLight(11, 5, 8), 0);

  // Light-gradient tie-breaker in shouldFlipQuad eliminates diagonal Mach-band creases on flat walls
  assert.equal(shouldFlipQuad(1, 1, 1, 1, 1.0, 0.5, 0.0, 0.5), true);
  assert.equal(shouldFlipQuad(1, 1, 1, 1, 0.5, 1.0, 0.5, 0.0), false);
});

test('Glass block transmits full 15 sky light straight down and lets block/dynamic light pass with attenuation 1', () => {
  // Part A: Sealed room with a Glass skylight pane at (8, 7, 8)
  const w = new VoxelWorld(16, 16, 16);
  for (let y = 3; y <= 7; y++) {
    for (let z = 6; z <= 10; z++) {
      for (let x = 6; x <= 10; x++) {
        const isWall =
          x === 6 || x === 10 || y === 3 || y === 7 || z === 6 || z === 10;
        if (isWall) w.setBlockRaw(x, y, z, BlockType.Stone);
      }
    }
  }
  w.setBlockRaw(8, 7, 8, BlockType.Glass);
  computeFullLightfield(w);

  // Sunlight passes straight down through Glass with ZERO loss (remains 15)
  assert.equal(w.getSky(8, 7, 8), 15);
  assert.equal(w.getSky(8, 6, 8), 15);
  assert.equal(w.getSky(8, 4, 8), 15);
  assert.equal(w.getSky(9, 5, 8), 14);

  // Part B: Full-height solid stone walls at x=6 (sealed) and x=10 (with Glass window at (10,5,8))
  const w2 = new VoxelWorld(16, 16, 16);
  for (let y = 0; y < 16; y++) {
    for (let z = 0; z < 16; z++) {
      w2.setBlockRaw(6, y, z, BlockType.Stone);
      w2.setBlockRaw(10, y, z, BlockType.Stone);
    }
  }
  w2.setBlockRaw(10, 5, 8, BlockType.Glass);
  computeFullLightfield(w2);

  // Dynamic held torch light passes cleanly through Glass window (10, 5, 8) into (11, 5, 8)
  const dyn = createDynamicLightState();
  updateDynamicLight(w2, dyn, 8, 5, 8, blockEmission(BlockType.Torch), true);
  assert.equal(w2.getEffectiveBlockLight(10, 5, 8), 12);
  assert.equal(w2.getEffectiveBlockLight(11, 5, 8), 11);
  // Strictly 0 behind the sealed full-height solid stone wall at x=6
  assert.equal(w2.getEffectiveBlockLight(5, 5, 8), 0);

  // Continuous Euclidean falloff is strictly monotonic along a straight line
  assert.ok(w2.getDynamicLightVal(8, 5, 8) > w2.getDynamicLightVal(9, 5, 8));
  assert.ok(w2.getDynamicLightVal(9, 5, 8) > w2.getDynamicLightVal(10, 5, 8));
  assert.ok(w2.getDynamicLightVal(10, 5, 8) > w2.getDynamicLightVal(11, 5, 8));
});

test('deep open-sky oceans retain readable skylight while sealed roofed underwater caves stay exact 0', () => {
  const w = new VoxelWorld(16, 28, 16);
  // Fill ocean basin from y=1..22 across entire 16x16 world, stone seabed at y=0
  for (let z = 0; z < 16; z++) {
    for (let x = 0; x < 16; x++) {
      w.setBlockRaw(x, 0, z, BlockType.Stone);
      for (let y = 1; y <= 22; y++) {
        w.setBlockRaw(x, y, z, BlockType.Water);
      }
    }
  }
  // Build a completely sealed submerged stone box at x:[2..6], y:[2..6], z:[2..6] filled with Water inside
  for (let y = 2; y <= 6; y++) {
    for (let z = 2; z <= 6; z++) {
      for (let x = 2; x <= 6; x++) {
        const isWall = x === 2 || x === 6 || y === 2 || y === 6 || z === 2 || z === 6;
        if (isWall) w.setBlockRaw(x, y, z, BlockType.Stone);
      }
    }
  }

  computeFullLightfield(w);

  // Open-sky ocean surface (y=22) is bright, mid-depth (y=12) is clearly lit, abyss (y=1, 22m deep!) stays >= 3 (NEVER pitch black!)
  assert.equal(w.getSky(12, 22, 12), 15);
  assert.ok(w.getSky(12, 12, 12) >= 9, `Expected mid-ocean sky >= 9, got ${w.getSky(12, 12, 12)}`);
  assert.ok(w.getSky(12, 1, 12) >= 3, `Expected 22m deep ocean floor sky >= 3, got ${w.getSky(12, 1, 12)}`);

  // Sealed flooded cave interior (4, 4, 4) has ZERO open-sky access -> must stay exact 0!
  assert.equal(w.getSky(4, 4, 4), 0);

  // Placing a solid block underwater at (12, 20, 12) and breaking it back to Water preserves natural skylight
  w.setBlockRaw(12, 20, 12, BlockType.Stone);
  onBlockChanged(w, 12, 20, 12, BlockType.Water, BlockType.Stone);
  assert.ok(w.getSky(13, 20, 12) >= 12, 'Neighboring ocean water stays brightly lit');
  w.setBlockRaw(12, 20, 12, BlockType.Water);
  onBlockChanged(w, 12, 20, 12, BlockType.Stone, BlockType.Water);
  assert.ok(w.getSky(12, 20, 12) >= 12, 'Refilled water recovers skylight cleanly');
});

test('underwater sky clarity, stars, Moon/Sun discs, and fog visibility depend monotonically on underwater depth', () => {
  const shallow = evaluateUnderwaterOptics(0.5, 1.0);
  const mid = evaluateUnderwaterOptics(6.0, 1.0);
  const deep19 = evaluateUnderwaterOptics(19.0, 1.0);
  const grazing = evaluateUnderwaterOptics(1.0, 0.2);

  // Just below the surface (0.5m) looking straight up: sky, stars, and disc remain clearly visible
  assert.ok(shallow.starClarity > 0.6, `Expected shallow stars > 0.6, got ${shallow.starClarity}`);
  assert.ok(shallow.discClarity > 0.75, `Expected shallow disc > 0.75, got ${shallow.discClarity}`);
  assert.ok(shallow.volVeil < 0.12, `Expected shallow veil < 0.12, got ${shallow.volVeil}`);
  assert.ok(shallow.snellAlpha < 0.30, `Expected shallow Snell alpha < 0.30, got ${shallow.snellAlpha}`);

  // At 6m mid-water: stars have essentially vanished, disc is heavily diffused, sky is veiled in aqua
  assert.ok(mid.starClarity < 0.01, `Expected 6m stars < 0.01, got ${mid.starClarity}`);
  assert.ok(mid.discClarity < 0.10, `Expected 6m disc < 0.10, got ${mid.discClarity}`);
  assert.ok(mid.volVeil > 0.55 && mid.volVeil < 0.75);
  assert.ok(mid.fogDensity > shallow.fogDensity);

  // At 19m deep ocean (matching user report): stars & sharp Moon disc are completely extinguished,
  // overhead sky is >95% veiled in deep abyss, red light is almost fully absorbed, and fog is dense
  assert.ok(deep19.starClarity < 1e-6, `Expected 19m stars extinguished, got ${deep19.starClarity}`);
  assert.ok(deep19.discClarity < 0.001, `Expected 19m Moon/Sun disc extinguished, got ${deep19.discClarity}`);
  assert.ok(deep19.volVeil > 0.95, `Expected 19m volumetric veil > 0.95, got ${deep19.volVeil}`);
  assert.ok(deep19.snellAlpha > 0.85, `Expected 19m Snell ceiling mostly opaque (>0.85), got ${deep19.snellAlpha}`);
  assert.ok(deep19.depthDark < 0.16, `Expected 19m abyssal darkening < 0.16, got ${deep19.depthDark}`);
  assert.ok(deep19.fogDensity > 0.055, `Expected 19m denser underwater fog, got ${deep19.fogDensity}`);
  // Spectral Beer-Lambert ordering: red absorbs fastest, blue penetrates deepest
  assert.ok(deep19.surfaceTransRGB[0] < deep19.surfaceTransRGB[1]);
  assert.ok(deep19.surfaceTransRGB[1] < deep19.surfaceTransRGB[2]);

  // Outside Snell's Window critical angle (grazing ray y=0.2): Total Internal Reflection hides sky completely
  assert.equal(grazing.volVeil, 1.0);
  assert.equal(grazing.starClarity, 0.0);
});



