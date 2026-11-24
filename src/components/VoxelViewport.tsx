import { useEffect, useRef } from 'react';
import * as THREE from 'three';
import {
  BlockType,
  CameraVistaId,
  DebugMetrics,
  LightViewMode,
  RaycastHit,
  ToolId,
  WorldProbeStats,
  blockEmission,
  blockName,
  toolToBlockType,
} from '../voxel/types';
import { VoxelWorld, evaluateTimeOfDay, raycastVoxel } from '../voxel/world';
import {
  computeFullLightfield,
  createDynamicLightState,
  onBlockChanged,
  probeWorldStats,
  updateDynamicLight,
} from '../voxel/lighting';
import { buildWorldGeometry } from '../voxel/mesher';
import {
  createBlockTextureAtlas,
  createSkyShaderMaterial,
  createVoxelShaderMaterial,
} from '../voxel/shaders';
import { BLOCK_TEXTURE_URLS } from '../data/cppProjectFiles';
import { playVoxelSound } from '../voxel/audio';
import { EngineGraphicsSettings } from './PauseModal';

export interface VoxelViewportProps {
  gameState: 'title' | 'playing' | 'paused';
  sourceOpen: boolean;
  timeOfDay: number;
  autoCycle: boolean;
  selectedTool: ToolId;
  lightViewMode: LightViewMode;
  graphicsSettings: EngineGraphicsSettings;
  muted: boolean;
  vistaSignal: { id: CameraVistaId; nonce: number };
  resetNonce: number;
  showDebugger: boolean;
  onTimeTick: (nextTime: number) => void;
  onProbeUpdate: (stats: WorldProbeStats, clockText: string, phaseName: string) => void;
  onDebugUpdate: (metrics: DebugMetrics) => void;
  onActionNotice: (msg: string) => void;
}

interface CameraPreset {
  pos: [number, number, number];
  look: [number, number, number];
}

const CAMERA_PRESETS: Record<CameraVistaId, CameraPreset> = {
  // Unobstructed open-sky vista framing sunlit cliffs, stone arch, torches & deep dark cavern
  overview: { pos: [31.5, 19.2, 13.5], look: [32.0, 14.5, 31.5] },
  // Close to the cavern entrance showing dramatic daylight -> darkness falloff + flanking torches
  caveMouth: { pos: [32.0, 15.8, 19.5], look: [32.2, 13.8, 35.0] },
  // Deep inside the dark cave tunnel looking toward the glowing magma/crystal chamber
  insideCave: { pos: [32.5, 12.6, 35.5], look: [34.2, 9.5, 48.0] },
  // Looking through the natural stone arch overhang shadow with suspended glowstone lantern
  stoneArch: { pos: [18.5, 15.5, 11.5], look: [18.2, 15.8, 25.0] },
};

// Reusable scratch vectors outside the render loop (zero per-frame GC)
const _forward = new THREE.Vector3();
const _right = new THREE.Vector3();
const _up = new THREE.Vector3(0, 1, 0);
const _move = new THREE.Vector3();
const _rayOrigin = new THREE.Vector3();
const _rayDir = new THREE.Vector3();
const _ndc = new THREE.Vector2();
const _raycaster = new THREE.Raycaster();
const _targetPos = new THREE.Vector3();
const _targetLook = new THREE.Vector3();
const _curLook = new THREE.Vector3();
const _lookDelta = new THREE.Vector3();

function detectSoftwareWebGL(): boolean {
  try {
    const canvas = document.createElement('canvas');
    const gl = canvas.getContext('webgl2') || canvas.getContext('webgl');
    if (!gl) return true;
    const ext = gl.getExtension('WEBGL_debug_renderer_info');
    const param = ext ? ext.UNMASKED_RENDERER_WEBGL : gl.RENDERER;
    const gpu = String(gl.getParameter(param) || '');
    gl.getExtension('WEBGL_lose_context')?.loseContext();
    return /SwiftShader|llvmpipe|softpipe|Software|Basic Render/i.test(gpu);
  } catch {
    return true;
  }
}

function yawPitchFromLook(
  pos: THREE.Vector3,
  look: THREE.Vector3
): { yaw: number; pitch: number } {
  _lookDelta.subVectors(look, pos).normalize();
  const yaw = Math.atan2(_lookDelta.z, _lookDelta.x);
  const pitch = Math.asin(THREE.MathUtils.clamp(_lookDelta.y, -0.98, 0.98));
  return { yaw, pitch };
}

export default function VoxelViewport(props: VoxelViewportProps) {
  const mountRef = useRef<HTMLDivElement | null>(null);
  const propsRef = useRef(props);
  propsRef.current = props;

  // Imperative bridge refs to trigger scene actions without recreating WebGLRenderer
  const actionsRef = useRef<{
    jumpToVista: (id: CameraVistaId) => void;
    resetWorld: () => void;
    wakeLoop: () => void;
  } | null>(null);

  useEffect(() => {
    actionsRef.current?.jumpToVista(props.vistaSignal.id);
  }, [props.vistaSignal.nonce, props.vistaSignal.id]);

  useEffect(() => {
    if (props.resetNonce > 0) {
      actionsRef.current?.resetWorld();
    }
  }, [props.resetNonce]);

  useEffect(() => {
    actionsRef.current?.wakeLoop();
  }, [
    props.gameState,
    props.sourceOpen,
    props.timeOfDay,
    props.lightViewMode,
    props.selectedTool,
    props.graphicsSettings,
  ]);

  useEffect(() => {
    const container = mountRef.current;
    if (!container) return;

    const isSoftwareGl = detectSoftwareWebGL();
    const renderer = new THREE.WebGLRenderer({
      antialias: !isSoftwareGl,
      powerPreference: 'high-performance',
    });
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    renderer.setPixelRatio(isSoftwareGl ? 1 : Math.min(window.devicePixelRatio, 2));
    renderer.setSize(container.clientWidth || window.innerWidth, container.clientHeight || window.innerHeight);
    container.innerHTML = '';
    container.appendChild(renderer.domElement);

    const scene = new THREE.Scene();
    const camera = new THREE.PerspectiveCamera(
      62,
      (container.clientWidth || window.innerWidth) /
        Math.max(1, container.clientHeight || window.innerHeight),
      0.15,
      260
    );

    // Build initial voxel world & full dual-channel BFS lightfield
    const world = new VoxelWorld();
    world.generateProceduralWorld(1337);
    computeFullLightfield(world);
    const dynLightState = createDynamicLightState();

    const blockAtlas = createBlockTextureAtlas(BLOCK_TEXTURE_URLS, () => {
      actionsRef.current?.wakeLoop();
    });
    const voxelMaterial = createVoxelShaderMaterial(false, blockAtlas);
    const translucentMaterial = createVoxelShaderMaterial(true, blockAtlas);
    let lastMeshMs = 0;
    let lastDynMs = 0;
    const tMesh0 = performance.now();
    let meshBuild = buildWorldGeometry(world);
    lastMeshMs = performance.now() - tMesh0;

    const voxelMesh = new THREE.Mesh(meshBuild.geometry, voxelMaterial);
    voxelMesh.frustumCulled = false;
    voxelMesh.renderOrder = 0;
    scene.add(voxelMesh);

    // Atmospheric Sky Dome (matches Vulkan shaders/sky.frag, drawn between opaque & translucent)
    const skyMaterial = createSkyShaderMaterial();
    const skyDome = new THREE.Mesh(
      new THREE.SphereGeometry(180, 24, 16),
      skyMaterial
    );
    skyDome.frustumCulled = false;
    skyDome.renderOrder = 1;
    scene.add(skyDome);

    // Translucent Water & Glass Pass (alpha-blended, depthWrite=false, drawn after sky dome)
    const translucentMesh = new THREE.Mesh(meshBuild.translucentGeometry, translucentMaterial);
    translucentMesh.frustumCulled = false;
    translucentMesh.renderOrder = 2;
    scene.add(translucentMesh);

    // Wireframe selection box highlighting targeted voxel
    const highlightEdges = new THREE.EdgesGeometry(
      new THREE.BoxGeometry(1.018, 1.018, 1.018)
    );
    const highlightMat = new THREE.LineBasicMaterial({
      color: 0xfbbf24,
      transparent: true,
      opacity: 0.9,
    });
    const highlightBox = new THREE.LineSegments(highlightEdges, highlightMat);
    highlightBox.visible = false;
    scene.add(highlightBox);

    // Camera state
    const initPreset = CAMERA_PRESETS.overview;
    camera.position.set(...initPreset.pos);
    _curLook.set(...initPreset.look);
    let { yaw, pitch } = yawPitchFromLook(camera.position, _curLook);
    let transitionActive = false;
    _targetPos.copy(camera.position);
    _targetLook.copy(_curLook);
    let tourAngle = 0;

    let currentTimeOfDay = propsRef.current.timeOfDay;
    let lastSyncedTimeProp = propsRef.current.timeOfDay;

    // Input state
    const keys = new Set<string>();
    let pointerDown = false;
    let pointerButton = 0;
    let downX = 0;
    let downY = 0;
    let lastX = 0;
    let lastY = 0;
    let dragged = false;
    let lastHoverHit: RaycastHit | null = null;
    let lastHoverTime = 0;
    let contextLost = false;

    const rebuildWorldMesh = () => {
      const t0 = performance.now();
      const nextBuild = buildWorldGeometry(world);
      lastMeshMs = performance.now() - t0;
      voxelMesh.geometry.dispose();
      translucentMesh.geometry.dispose();
      voxelMesh.geometry = nextBuild.geometry;
      translucentMesh.geometry = nextBuild.translucentGeometry;
      meshBuild = nextBuild;
    };

    const getHeldEmission = (tool: ToolId, state: 'title' | 'playing' | 'paused'): number => {
      if (state !== 'playing' || tool === 'break') return 0;
      return blockEmission(toolToBlockType(tool));
    };

    const emitProbeNow = () => {
      const tod = evaluateTimeOfDay(currentTimeOfDay);
      const heldEmit = getHeldEmission(propsRef.current.selectedTool, propsRef.current.gameState);
      const stats = probeWorldStats(
        world,
        [camera.position.x, camera.position.y, camera.position.z],
        tod.skyIntensity,
        heldEmit
      );
      propsRef.current.onProbeUpdate(stats, tod.clockText, tod.phaseName);
    };

    const performRaycastFromClient = (
      clientX: number,
      clientY: number
    ): RaycastHit | null => {
      const rect = renderer.domElement.getBoundingClientRect();
      if (rect.width <= 0 || rect.height <= 0) return null;
      _ndc.x = ((clientX - rect.left) / rect.width) * 2 - 1;
      _ndc.y = -((clientY - rect.top) / rect.height) * 2 + 1;
      _raycaster.setFromCamera(_ndc, camera);
      _rayOrigin.copy(_raycaster.ray.origin);
      _rayDir.copy(_raycaster.ray.direction);
      return raycastVoxel(
        world,
        [_rayOrigin.x, _rayOrigin.y, _rayOrigin.z],
        [_rayDir.x, _rayDir.y, _rayDir.z],
        48
      );
    };

    const applyVoxelEdit = (hit: RaycastHit, forceBreak: boolean) => {
      const tool = propsRef.current.selectedTool;
      const shouldBreak = forceBreak || tool === 'break';

      const heldEmit = getHeldEmission(tool, propsRef.current.gameState);

      if (shouldBreak) {
        if (hit.by <= 1) return;
        const oldB = world.getBlock(hit.bx, hit.by, hit.bz);
        if (oldB === BlockType.Air) return;
        const adjacentWater =
          world.getBlock(hit.bx, hit.by + 1, hit.bz) === BlockType.Water ||
          world.getBlock(hit.bx + 1, hit.by, hit.bz) === BlockType.Water ||
          world.getBlock(hit.bx - 1, hit.by, hit.bz) === BlockType.Water ||
          world.getBlock(hit.bx, hit.by, hit.bz + 1) === BlockType.Water ||
          world.getBlock(hit.bx, hit.by, hit.bz - 1) === BlockType.Water;
        const replacement = adjacentWater ? BlockType.Water : BlockType.Air;
        world.setBlockRaw(hit.bx, hit.by, hit.bz, replacement);
        onBlockChanged(world, hit.bx, hit.by, hit.bz, oldB, replacement);
        if (heldEmit > 0) {
          updateDynamicLight(
            world,
            dynLightState,
            Math.floor(camera.position.x),
            Math.floor(camera.position.y),
            Math.floor(camera.position.z),
            heldEmit,
            true
          );
        }
        rebuildWorldMesh();
        playVoxelSound('break', propsRef.current.muted);
        propsRef.current.onActionNotice(
          adjacentWater
            ? `Broke underwater block — water flowed back naturally`
            : `Broke voxel (${hit.bx}, ${hit.by}, ${hit.bz}) — relit live`
        );
      } else {
        const px = hit.bx + hit.nx;
        const py = hit.by + hit.ny;
        const pz = hit.bz + hit.nz;
        if (!world.inBounds(px, py, pz)) return;
        const oldB = world.getBlock(px, py, pz);
        if (oldB !== BlockType.Air && oldB !== BlockType.Water) return;
        const newBlock = toolToBlockType(tool);
        if (oldB === BlockType.Water && newBlock === BlockType.Torch) {
          propsRef.current.onActionNotice(
            `Dry Torch cannot displace Water — use Glowstone, Magma, or Glass underwater`
          );
          return;
        }
        world.setBlockRaw(px, py, pz, newBlock);
        onBlockChanged(world, px, py, pz, oldB, newBlock);
        if (heldEmit > 0) {
          updateDynamicLight(
            world,
            dynLightState,
            Math.floor(camera.position.x),
            Math.floor(camera.position.y),
            Math.floor(camera.position.z),
            heldEmit,
            true
          );
        }
        rebuildWorldMesh();
        const isLight = blockEmission(newBlock) > 0;
        playVoxelSound(isLight ? 'torch' : 'place', propsRef.current.muted);
        propsRef.current.onActionNotice(
          isLight
            ? `Placed light emitter (level ${blockEmission(newBlock)})`
            : `Placed solid block — shadow updated`
        );
      }
      emitProbeNow();
    };

    // Pointer & Keyboard listeners
    const onPointerDown = (e: PointerEvent) => {
      if (propsRef.current.gameState !== 'playing' || propsRef.current.sourceOpen) return;
      pointerDown = true;
      pointerButton = e.button;
      downX = e.clientX;
      downY = e.clientY;
      lastX = e.clientX;
      lastY = e.clientY;
      dragged = false;
      transitionActive = false;
    };

    const onPointerMove = (e: PointerEvent) => {
      if (propsRef.current.gameState !== 'playing' || propsRef.current.sourceOpen) return;

      if (pointerDown) {
        const dx = e.clientX - lastX;
        const dy = e.clientY - lastY;
        lastX = e.clientX;
        lastY = e.clientY;
        if (Math.hypot(e.clientX - downX, e.clientY - downY) > 5) {
          dragged = true;
        }
        if (dragged) {
          yaw -= dx * 0.0042;
          pitch = THREE.MathUtils.clamp(pitch - dy * 0.0042, -1.42, 1.42);
        }
        return;
      }

      const now = performance.now();
      if (now - lastHoverTime > 45) {
        lastHoverTime = now;
        lastHoverHit = performRaycastFromClient(e.clientX, e.clientY);
        if (lastHoverHit) {
          highlightBox.visible = true;
          highlightBox.position.set(
            lastHoverHit.bx + 0.5,
            lastHoverHit.by + 0.5,
            lastHoverHit.bz + 0.5
          );
        } else {
          highlightBox.visible = false;
        }
      }
    };

    const onPointerUp = (e: PointerEvent) => {
      if (!pointerDown) return;
      const wasDragged = dragged;
      const btn = pointerButton;
      pointerDown = false;
      if (propsRef.current.gameState !== 'playing' || propsRef.current.sourceOpen) return;

      if (!wasDragged) {
        const hit = performRaycastFromClient(e.clientX, e.clientY);
        if (hit) {
          applyVoxelEdit(hit, btn === 2);
        }
      }
    };

    const onWheel = (e: WheelEvent) => {
      if (propsRef.current.gameState !== 'playing' || propsRef.current.sourceOpen) return;
      transitionActive = false;
      _forward.set(
        Math.cos(yaw) * Math.cos(pitch),
        Math.sin(pitch),
        Math.sin(yaw) * Math.cos(pitch)
      );
      camera.position.addScaledVector(_forward, -e.deltaY * 0.012);
      camera.position.x = THREE.MathUtils.clamp(camera.position.x, 2, world.width - 2);
      camera.position.y = THREE.MathUtils.clamp(camera.position.y, 2, world.height + 10);
      camera.position.z = THREE.MathUtils.clamp(camera.position.z, 2, world.depth - 2);
    };

    const onContextMenu = (e: MouseEvent) => e.preventDefault();

    const onKeyDown = (e: KeyboardEvent) => {
      if (propsRef.current.gameState !== 'playing' || propsRef.current.sourceOpen) return;
      const code = e.code;
      if (
        code === 'KeyW' ||
        code === 'KeyA' ||
        code === 'KeyS' ||
        code === 'KeyD' ||
        code === 'KeyQ' ||
        code === 'KeyE' ||
        code === 'Space' ||
        code === 'ShiftLeft' ||
        code === 'ArrowUp' ||
        code === 'ArrowDown' ||
        code === 'ArrowLeft' ||
        code === 'ArrowRight'
      ) {
        transitionActive = false;
        keys.add(code);
      }
    };

    const onKeyUp = (e: KeyboardEvent) => {
      keys.delete(e.code);
    };

    const clearInputs = () => {
      keys.clear();
      pointerDown = false;
    };

    const onResize = () => {
      const w = container.clientWidth || window.innerWidth;
      const h = Math.max(1, container.clientHeight || window.innerHeight);
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
      renderer.setSize(w, h);
    };

    const onContextLost = (e: Event) => {
      e.preventDefault();
      contextLost = true;
    };
    const onContextRestored = () => {
      contextLost = false;
      wakeLoop();
    };

    const canvas = renderer.domElement;
    canvas.addEventListener('pointerdown', onPointerDown);
    window.addEventListener('pointermove', onPointerMove);
    window.addEventListener('pointerup', onPointerUp);
    canvas.addEventListener('wheel', onWheel, { passive: true });
    canvas.addEventListener('contextmenu', onContextMenu);
    canvas.addEventListener('webglcontextlost', onContextLost);
    canvas.addEventListener('webglcontextrestored', onContextRestored);
    window.addEventListener('keydown', onKeyDown);
    window.addEventListener('keyup', onKeyUp);
    window.addEventListener('blur', clearInputs);
    document.addEventListener('visibilitychange', clearInputs);
    window.addEventListener('resize', onResize);

    // Software-WebGL fence pacing (prevents CPU starvation in headless Chromium)
    let frameId = 0;
    let disposed = false;
    let loopActive = false;

    function afterDraw(cb: () => void) {
      const gl = renderer.getContext() as WebGL2RenderingContext;
      if (!gl || typeof gl.fenceSync !== 'function') {
        frameId = window.setTimeout(cb, 85);
        return;
      }
      const sync = gl.fenceSync(gl.SYNC_GPU_COMMANDS_COMPLETE, 0);
      gl.flush();
      const poll = () => {
        if (disposed) return;
        const state = sync && gl.getSyncParameter(sync, gl.SYNC_STATUS);
        if (state === gl.UNSIGNALED) {
          frameId = window.setTimeout(poll, 5);
        } else {
          if (sync) gl.deleteSync(sync);
          frameId = window.setTimeout(cb, 80);
        }
      };
      frameId = window.setTimeout(poll, 5);
    }

    let lastFrameMs = performance.now();
    let elapsedSec = 0;
    let lastHudSyncMs = 0;
    let lastDebugSyncMs = 0;
    const frameHistory: number[] = new Array(60).fill(16.6);
    let frameHistIdx = 0;

    function computeFacingLabel(yawRad: number): string {
      const fx = Math.cos(yawRad);
      const fz = Math.sin(yawRad);
      if (Math.abs(fx) >= Math.abs(fz)) {
        return fx >= 0 ? 'East (+X)' : 'West (-X)';
      }
      return fz >= 0 ? 'South (+Z)' : 'North (-Z)';
    }

    function updateAndRender() {
      if (disposed || contextLost) {
        loopActive = false;
        return;
      }

      const now = performance.now();
      const rawDtMs = Math.max(0.5, now - lastFrameMs);
      const dt = Math.min(rawDtMs / 1000, 0.05);
      lastFrameMs = now;
      elapsedSec += dt;

      frameHistory[frameHistIdx] = Math.min(rawDtMs, 100);
      frameHistIdx = (frameHistIdx + 1) % 60;

      const p = propsRef.current;

      // Sync external time-of-day slider/preset changes
      if (Math.abs(p.timeOfDay - lastSyncedTimeProp) > 0.0005) {
        currentTimeOfDay = p.timeOfDay;
        lastSyncedTimeProp = p.timeOfDay;
      } else if (p.autoCycle && p.gameState === 'playing' && !p.sourceOpen) {
        currentTimeOfDay = (currentTimeOfDay + dt * 0.022) % 1;
        lastSyncedTimeProp = currentTimeOfDay;
      }

      // Camera choreography
      if (p.gameState === 'title') {
        // Live World Camera Tour gliding smoothly in open sky across the sunlit cliff & dark cave mouth
        tourAngle += dt * 0.14;
        camera.position.set(
          31.5 + Math.sin(tourAngle) * 6.0,
          19.2 + Math.cos(tourAngle * 0.7) * 1.1,
          13.0 + (1 - Math.cos(tourAngle)) * 2.5
        );
        camera.lookAt(32.0, 14.8, 32.0);
        highlightBox.visible = false;
      } else if (p.gameState === 'playing' && !p.sourceOpen) {
        if (transitionActive) {
          const alpha = 1 - Math.exp(-5.5 * dt);
          camera.position.lerp(_targetPos, alpha);
          _curLook.lerp(_targetLook, alpha);
          const yp = yawPitchFromLook(camera.position, _curLook);
          yaw = yp.yaw;
          pitch = yp.pitch;
          if (camera.position.distanceTo(_targetPos) < 0.08) {
            transitionActive = false;
          }
        } else {
          // WASD + Arrow + Q/E / Space/Shift free flight
          const speed = 11.0;
          _forward.set(
            Math.cos(yaw) * Math.cos(pitch),
            Math.sin(pitch),
            Math.sin(yaw) * Math.cos(pitch)
          );
          _right.crossVectors(_forward, _up).normalize();
          _move.set(0, 0, 0);

          if (keys.has('KeyW') || keys.has('ArrowUp')) _move.add(_forward);
          if (keys.has('KeyS') || keys.has('ArrowDown')) _move.sub(_forward);
          if (keys.has('KeyD') || keys.has('ArrowRight')) _move.add(_right);
          if (keys.has('KeyA') || keys.has('ArrowLeft')) _move.sub(_right);
          if (keys.has('KeyE') || keys.has('Space')) _move.y += 1;
          if (keys.has('KeyQ') || keys.has('ShiftLeft')) _move.y -= 1;

          if (_move.lengthSq() > 0.0001) {
            _move.normalize();
            camera.position.addScaledVector(_move, speed * dt);
            camera.position.x = THREE.MathUtils.clamp(camera.position.x, 1.5, world.width - 1.5);
            camera.position.y = THREE.MathUtils.clamp(camera.position.y, 2.0, world.height + 12);
            camera.position.z = THREE.MathUtils.clamp(camera.position.z, 1.5, world.depth - 1.5);
          }
        }

        _curLook.set(
          camera.position.x + Math.cos(yaw) * Math.cos(pitch),
          camera.position.y + Math.sin(pitch),
          camera.position.z + Math.sin(yaw) * Math.cos(pitch)
        );
        camera.lookAt(_curLook);
      }

      // Update 100% voxel-occluded dynamic light strictly from the currently held light-source block
      // (Torch=14, Glowstone=15, Magma=13; Break/Stone/Timber/Glass=0 -> zero dynamic light!)
      const heldEmit = getHeldEmission(p.selectedTool, p.gameState);
      const tDyn0 = performance.now();
      const dynChanged = updateDynamicLight(
        world,
        dynLightState,
        Math.floor(camera.position.x),
        Math.floor(camera.position.y),
        Math.floor(camera.position.z),
        heldEmit,
        false
      );
      lastDynMs = performance.now() - tDyn0;
      if (dynChanged) {
        rebuildWorldMesh();
        emitProbeNow();
      }

      // Sync live FOV from Settings UI
      if (Math.abs(camera.fov - p.graphicsSettings.fov) > 0.1) {
        camera.fov = p.graphicsSettings.fov;
        camera.updateProjectionMatrix();
      }

      // Detect whether camera eye is submerged below the recessed water meniscus (y + 0.88)
      const eyeBx = Math.floor(camera.position.x);
      const eyeBy = Math.floor(camera.position.y);
      const eyeBz = Math.floor(camera.position.z);
      const eyeBlock = world.getBlock(eyeBx, eyeBy, eyeBz);
      let eyeUnderwater = false;
      if (eyeBlock === BlockType.Water) {
        const aboveEye = world.getBlock(eyeBx, eyeBy + 1, eyeBz);
        const localY = camera.position.y - eyeBy;
        eyeUnderwater = aboveEye === BlockType.Water || localY < 0.86;
      }
      let eyeWaterDepth = 0.0;
      if (eyeUnderwater) {
        let depthBlocks = 0;
        for (let wy = eyeBy; wy < world.height && depthBlocks < 64; wy++) {
          if (world.getBlock(eyeBx, wy, eyeBz) === BlockType.Water) depthBlocks++;
          else break;
        }
        if (depthBlocks > 0) {
          const topWaterY = eyeBy + depthBlocks - 1;
          const aboveTop = world.getBlock(eyeBx, topWaterY + 1, eyeBz);
          const openSurface =
            aboveTop === BlockType.Air ||
            aboveTop === BlockType.Torch ||
            aboveTop === BlockType.Leaves;
          const surfaceY = topWaterY + (openSurface ? 0.88 : 1.0);
          eyeWaterDepth = Math.max(0.0, surfaceY - camera.position.y);
        }
      }

      // Update Vulkan-mirrored uniforms from continuous 6-phase solar curve + live Settings UI
      const tod = evaluateTimeOfDay(currentTimeOfDay);
      const previewBorderEnd = Math.min(54, 24 + p.graphicsSettings.renderDistance * 2.2);
      const previewBorderStart = previewBorderEnd * 0.72;
      const syncVoxelUniforms = (mat: THREE.ShaderMaterial) => {
        mat.uniforms.uTime.value = elapsedSec;
        mat.uniforms.uSunDir.value.set(...tod.sunDir);
        mat.uniforms.uSkyIntensity.value = tod.skyIntensity;
        mat.uniforms.uSunColor.value.set(...tod.sunColor);
        mat.uniforms.uMoonIntensity.value = tod.moonIntensity;
        mat.uniforms.uSkyAmbient.value.set(...tod.skyAmbient);
        mat.uniforms.uSkyZenith.value.set(...tod.skyZenith);
        mat.uniforms.uHorizonFog.value.set(...tod.horizonFog);
        mat.uniforms.uMieGlow.value.set(...tod.mieGlow);
        mat.uniforms.uWarmGlow.value = tod.warmGlow;
        mat.uniforms.uFogStrength.value = p.graphicsSettings.fogStrength;
        mat.uniforms.uBorderFogParams.value.set(
          previewBorderStart,
          previewBorderEnd,
          p.graphicsSettings.borderFog ? 1.0 : 0.0
        );
        mat.uniforms.uQualityFlags.value.set(
          p.graphicsSettings.smoothLighting ? 1.0 : 0.0,
          p.graphicsSettings.quality === 'High' || p.graphicsSettings.quality === 'Ultra' ? 1.0 : 0.0
        );
        mat.uniforms.uCaveDark.value.set(...tod.caveDark);
        mat.uniforms.uDebugMode.value = p.lightViewMode;
        mat.uniforms.uHeldLightPosStrength.value.set(
          camera.position.x,
          camera.position.y,
          camera.position.z,
          heldEmit / 15
        );
        mat.uniforms.uCameraUnderwater.value = eyeUnderwater ? 1.0 : 0.0;
        mat.uniforms.uWaterDepth.value = eyeWaterDepth;
      };
      syncVoxelUniforms(voxelMaterial);
      syncVoxelUniforms(translucentMaterial);

      skyMaterial.uniforms.uTime.value = elapsedSec;
      skyMaterial.uniforms.uSunDir.value.set(...tod.sunDir);
      skyMaterial.uniforms.uSkyIntensity.value = tod.skyIntensity;
      skyMaterial.uniforms.uMoonIntensity.value = tod.moonIntensity;
      skyMaterial.uniforms.uSkyZenith.value.set(...tod.skyZenith);
      skyMaterial.uniforms.uSkyHorizon.value.set(...tod.skyHorizon);
      skyMaterial.uniforms.uMieGlow.value.set(...tod.mieGlow);
      skyMaterial.uniforms.uWarmGlow.value = tod.warmGlow;
      skyMaterial.uniforms.uCameraUnderwater.value = eyeUnderwater ? 1.0 : 0.0;
      skyMaterial.uniforms.uWaterDepth.value = eyeWaterDepth;

      renderer.render(scene, camera);

      // Throttled HUD telemetry sync (<= 4Hz, never 60Hz React setState!)
      if (now - lastHudSyncMs > 260) {
        lastHudSyncMs = now;
        if (p.autoCycle && p.gameState === 'playing' && !p.sourceOpen) {
          p.onTimeTick(currentTimeOfDay);
        }
        emitProbeNow();
      }

      // Throttled Shift+F3 Debugger sync (~8 Hz only when open)
      if (p.showDebugger && now - lastDebugSyncMs > 125) {
        lastDebugSyncMs = now;
        let sum = 0;
        let minV = 999;
        let maxV = 0;
        const orderedHistory: number[] = new Array(60);
        for (let i = 0; i < 60; i++) {
          const v = frameHistory[(frameHistIdx + i) % 60];
          orderedHistory[i] = v;
          sum += v;
          if (v < minV) minV = v;
          if (v > maxV) maxV = v;
        }
        const avgMs = sum / 60;
        let waterDepth = 0;
        if (eyeUnderwater) {
          let wy = eyeBy;
          while (wy < world.height && world.getBlock(eyeBx, wy, eyeBz) === BlockType.Water) {
            waterDepth++;
            wy++;
          }
        }
        const dynLvl = Math.min(15, (world.getDynamicLightVal(eyeBx, eyeBy, eyeBz) + 8) >> 4);
        const tgtBlock = lastHoverHit
          ? blockName(world.getBlock(lastHoverHit.bx, lastHoverHit.by, lastHoverHit.bz))
          : 'None';

        p.onDebugUpdate({
          fps: Math.round(1000 / Math.max(avgMs, 0.1)),
          frameMs: rawDtMs,
          avgMs,
          minMs: minV,
          maxMs: maxV,
          frameHistory: orderedHistory,
          pos: [camera.position.x, camera.position.y, camera.position.z],
          chunk: [eyeBx >> 4, eyeBz >> 4],
          facing: computeFacingLabel(yaw),
          yawDeg: ((yaw * 180) / Math.PI) % 360,
          pitchDeg: (pitch * 180) / Math.PI,
          skyLight: world.getSky(eyeBx, eyeBy, eyeBz),
          blockLight: world.getBlockLightVal(eyeBx, eyeBy, eyeBz),
          dynLight: dynLvl,
          eyeUnderwater,
          waterDepth,
          opaqueTriangles: meshBuild.opaqueTriangles,
          translucentTriangles: meshBuild.translucentTriangles,
          totalVertices: meshBuild.vertexCount,
          meshMs: lastMeshMs,
          dynMs: lastDynMs,
          targetBlock: tgtBlock,
          targetPos: lastHoverHit
            ? [lastHoverHit.bx, lastHoverHit.by, lastHoverHit.bz]
            : null,
        });
      }

      // Pause background rendering while Source Explorer or Pause Modal is open
      if (p.sourceOpen || p.gameState === 'paused') {
        loopActive = false;
        return;
      }

      if (isSoftwareGl) {
        afterDraw(updateAndRender);
      } else {
        frameId = requestAnimationFrame(updateAndRender);
      }
    }

    function wakeLoop() {
      if (disposed || contextLost) return;
      if (!loopActive) {
        loopActive = true;
        lastFrameMs = performance.now();
        updateAndRender();
      }
    }

    actionsRef.current = {
      jumpToVista: (id: CameraVistaId) => {
        const preset = CAMERA_PRESETS[id];
        _targetPos.set(...preset.pos);
        _targetLook.set(...preset.look);
        transitionActive = true;
        wakeLoop();
      },
      resetWorld: () => {
        world.generateProceduralWorld(1337);
        computeFullLightfield(world);
        dynLightState.activeVoxels.length = 0;
        dynLightState.level = 0;
        const preset = CAMERA_PRESETS.overview;
        _targetPos.set(...preset.pos);
        _targetLook.set(...preset.look);
        transitionActive = true;
        const heldEmit = getHeldEmission(propsRef.current.selectedTool, propsRef.current.gameState);
        if (heldEmit > 0) {
          updateDynamicLight(
            world,
            dynLightState,
            Math.floor(camera.position.x),
            Math.floor(camera.position.y),
            Math.floor(camera.position.z),
            heldEmit,
            true
          );
        }
        rebuildWorldMesh();
        emitProbeNow();
        propsRef.current.onActionNotice('World & lightfield reset to initial state');
        wakeLoop();
      },
      wakeLoop,
    };

    emitProbeNow();
    wakeLoop();

    return () => {
      disposed = true;
      cancelAnimationFrame(frameId);
      clearTimeout(frameId);
      canvas.removeEventListener('pointerdown', onPointerDown);
      window.removeEventListener('pointermove', onPointerMove);
      window.removeEventListener('pointerup', onPointerUp);
      canvas.removeEventListener('wheel', onWheel);
      canvas.removeEventListener('contextmenu', onContextMenu);
      canvas.removeEventListener('webglcontextlost', onContextLost);
      canvas.removeEventListener('webglcontextrestored', onContextRestored);
      window.removeEventListener('keydown', onKeyDown);
      window.removeEventListener('keyup', onKeyUp);
      window.removeEventListener('blur', clearInputs);
      document.removeEventListener('visibilitychange', clearInputs);
      window.removeEventListener('resize', onResize);

      meshBuild.geometry.dispose();
      meshBuild.translucentGeometry.dispose();
      blockAtlas.dispose();
      voxelMaterial.dispose();
      translucentMaterial.dispose();
      skyDome.geometry.dispose();
      skyMaterial.dispose();
      highlightEdges.dispose();
      highlightMat.dispose();
      renderer.dispose();
    };
  }, []);

  return (
    <div
      ref={mountRef}
      data-testid="voxel-canvas-mount"
      className="fixed inset-0 w-full h-full select-none cursor-crosshair"
    />
  );
}
