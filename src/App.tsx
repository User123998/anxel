import { useCallback, useEffect, useRef, useState } from 'react';
import VoxelViewport from './components/VoxelViewport';
import TitleOverlay from './components/TitleOverlay';
import GameHud from './components/GameHud';
import SourceExplorerModal from './components/SourceExplorerModal';
import PauseModal, {
  DEFAULT_GRAPHICS_SETTINGS,
  EngineGraphicsSettings,
} from './components/PauseModal';
import {
  CameraVistaId,
  DebugMetrics,
  LightViewMode,
  ToolId,
  WorldProbeStats,
} from './voxel/types';
import {
  CPP_PROJECT_FILES,
  downloadProjectZip,
} from './data/cppProjectFiles';

type GameState = 'title' | 'playing' | 'paused';

function shouldAutostart(): boolean {
  if (typeof window === 'undefined') return false;
  return new URLSearchParams(window.location.search).get('autostart') === '1';
}

export default function App() {
  const [gameState, setGameState] = useState<GameState>(() =>
    shouldAutostart() ? 'playing' : 'title'
  );
  const [sourceOpen, setSourceOpen] = useState(false);
  const [timeOfDay, setTimeOfDay] = useState(0.015); // Rose-gold Sunrise rising over cliffs & dark cave
  const [autoCycle, setAutoCycle] = useState(false);
  const [selectedTool, setSelectedTool] = useState<ToolId>('torch');
  const [lightViewMode, setLightViewMode] = useState<LightViewMode>(0);
  const [graphicsSettings, setGraphicsSettings] = useState<EngineGraphicsSettings>(
    DEFAULT_GRAPHICS_SETTINGS
  );
  const [muted, setMuted] = useState(false);
  const [dockOpen, setDockOpen] = useState(true);
  const [showDebugger, setShowDebugger] = useState(false);
  const [debugMetrics, setDebugMetrics] = useState<DebugMetrics | null>(null);
  const [vistaSignal, setVistaSignal] = useState<{
    id: CameraVistaId;
    nonce: number;
  }>({ id: 'overview', nonce: 0 });
  const [resetNonce, setResetNonce] = useState(0);

  const [clockText, setClockText] = useState('06:21');
  const [phaseName, setPhaseName] = useState('Sunrise');
  const [probeStats, setProbeStats] = useState<WorldProbeStats>({
    skyLight: 15,
    blockLight: 0,
    emitterCount: 16,
    darkVoxelCount: 0,
    sunlitVoxelCount: 0,
    areaLabel: 'GOLDEN HOUR',
    heldEmission: 14,
    eyeUnderwater: false,
    waterDepth: 0,
  });
  const [actionNotice, setActionNotice] = useState<string | null>(null);
  const noticeTimerRef = useRef<number | null>(null);

  // Mirror state machine to document.body.dataset.state
  useEffect(() => {
    document.body.dataset.state = gameState;
  }, [gameState]);

  const showNotice = useCallback((msg: string) => {
    setActionNotice(msg);
    if (noticeTimerRef.current !== null) {
      window.clearTimeout(noticeTimerRef.current);
    }
    noticeTimerRef.current = window.setTimeout(() => {
      setActionNotice(null);
    }, 2600);
  }, []);

  const handleStart = useCallback(() => {
    if (document.activeElement instanceof HTMLElement) {
      document.activeElement.blur();
    }
    document.body.dataset.state = 'playing';
    setSourceOpen(false);
    setGameState('playing');
    setVistaSignal((prev) => ({ id: 'overview', nonce: prev.nonce + 1 }));
  }, []);

  const handleDownloadZip = useCallback(() => {
    downloadProjectZip()
      .then(() => showNotice('Downloaded umbra_vulkan_voxel_cpp20.zip'))
      .catch(() => showNotice('ZIP export failed'));
  }, [showNotice]);

  const handleProbeUpdate = useCallback(
    (stats: WorldProbeStats, clock: string, phase: string) => {
      setProbeStats(stats);
      setClockText(clock);
      setPhaseName(phase);
    },
    []
  );

  const handleJumpVista = useCallback(
    (id: CameraVistaId) => {
      setVistaSignal((prev) => ({ id, nonce: prev.nonce + 1 }));
      const labels: Record<CameraVistaId, string> = {
        overview: 'Camera: Highland Overview',
        caveMouth: 'Camera: Cave Mouth (Daylight -> Darkness)',
        insideCave: 'Camera: Deep Cavern (Pitch Darkness + Magma)',
        stoneArch: 'Camera: Natural Stone Arch Overhang',
      };
      showNotice(labels[id]);
    },
    [showNotice]
  );

  // Global keyboard shortcuts
  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement) {
        return;
      }

      if (sourceOpen) {
        if (e.code === 'Escape') {
          e.preventDefault();
          setSourceOpen(false);
        }
        return;
      }

      if (gameState === 'title') {
        if (e.code === 'Enter' || e.code === 'Space') {
          e.preventDefault();
          handleStart();
        }
        return;
      }

      if (e.code === 'Escape' || e.code === 'KeyP') {
        e.preventDefault();
        setGameState((prev) => (prev === 'playing' ? 'paused' : 'playing'));
        return;
      }

      if (gameState !== 'playing') return;

      switch (e.code) {
        case 'Digit1': setSelectedTool('break'); break;
        case 'Digit2': setSelectedTool('torch'); break;
        case 'Digit3': setSelectedTool('glowstone'); break;
        case 'Digit4': setSelectedTool('stone'); break;
        case 'Digit5': setSelectedTool('timber'); break;
        case 'Digit6': setSelectedTool('magma'); break;
        case 'Digit7': setSelectedTool('glass'); break;
        case 'F3':
          e.preventDefault();
          setShowDebugger((prev) => !prev);
          break;
        case 'KeyH':
          setDockOpen((prev) => !prev);
          break;
        case 'KeyM':
          setMuted((prev) => !prev);
          break;
        case 'KeyV':
          setLightViewMode((prev) => ((prev + 1) % 4) as LightViewMode);
          break;
        case 'KeyN':
          setTimeOfDay((prev) => {
            if (prev < 0.07 || prev >= 0.9) return 0.125;
            if (prev < 0.19) return 0.25;
            if (prev < 0.34) return 0.41;
            if (prev < 0.45) return 0.495;
            if (prev < 0.6) return 0.75;
            return 0.005;
          });
          break;
        default:
          break;
      }
    };

    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [gameState, sourceOpen, handleStart, showNotice]);

  return (
    <div className="relative w-full h-full overflow-hidden bg-[#0b0e14]">
      <VoxelViewport
        gameState={gameState}
        sourceOpen={sourceOpen}
        timeOfDay={timeOfDay}
        autoCycle={autoCycle}
        selectedTool={selectedTool}
        lightViewMode={lightViewMode}
        graphicsSettings={graphicsSettings}
        muted={muted}
        vistaSignal={vistaSignal}
        resetNonce={resetNonce}
        showDebugger={showDebugger}
        onTimeTick={setTimeOfDay}
        onProbeUpdate={handleProbeUpdate}
        onDebugUpdate={setDebugMetrics}
        onActionNotice={showNotice}
      />

      {gameState === 'title' && !sourceOpen && (
        <TitleOverlay
          onStart={handleStart}
          onOpenSource={() => setSourceOpen(true)}
          onDownloadZip={handleDownloadZip}
          fileCount={CPP_PROJECT_FILES.length}
        />
      )}

      {gameState === 'playing' && !sourceOpen && (
        <GameHud
          clockText={clockText}
          phaseName={phaseName}
          probeStats={probeStats}
          timeOfDay={timeOfDay}
          autoCycle={autoCycle}
          selectedTool={selectedTool}
          lightViewMode={lightViewMode}
          muted={muted}
          dockOpen={dockOpen}
          showDebugger={showDebugger}
          debugMetrics={debugMetrics}
          actionNotice={actionNotice}
          onTimeChange={setTimeOfDay}
          onToggleAutoCycle={() => setAutoCycle((prev) => !prev)}
          onSelectTool={setSelectedTool}
          onSelectLightMode={setLightViewMode}
          onJumpVista={handleJumpVista}
          onToggleMute={() => setMuted((prev) => !prev)}
          onToggleDock={() => setDockOpen((prev) => !prev)}
          onToggleDebugger={() => setShowDebugger((prev) => !prev)}
          onOpenSource={() => setSourceOpen(true)}
          onDownloadZip={handleDownloadZip}
          onPause={() => setGameState('paused')}
        />
      )}

      {gameState === 'paused' && !sourceOpen && (
        <PauseModal
          clockText={clockText}
          phaseName={phaseName}
          probeStats={probeStats}
          settings={graphicsSettings}
          onUpdateSettings={setGraphicsSettings}
          onResume={() => setGameState('playing')}
          onResetWorld={() => {
            setResetNonce((n) => n + 1);
            setGameState('playing');
          }}
          onOpenSource={() => setSourceOpen(true)}
          onReturnTitle={() => setGameState('title')}
        />
      )}

      {sourceOpen && (
        <SourceExplorerModal
          onClose={() => setSourceOpen(false)}
          onDownloadZip={handleDownloadZip}
        />
      )}
    </div>
  );
}
