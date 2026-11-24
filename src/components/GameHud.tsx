import {
  Activity,
  ChevronDown,
  ChevronUp,
  Code2,
  Download,
  Flame,
  Pause,
  Pickaxe,
  Play,
  Volume2,
  VolumeX,
} from 'lucide-react';
import {
  CameraVistaId,
  DebugMetrics,
  LightViewMode,
  ToolId,
  WorldProbeStats,
} from '../voxel/types';
import { ITEM_TEXTURE_URLS } from '../data/cppProjectFiles';

interface GameHudProps {
  clockText: string;
  phaseName: string;
  probeStats: WorldProbeStats;
  timeOfDay: number;
  autoCycle: boolean;
  selectedTool: ToolId;
  lightViewMode: LightViewMode;
  muted: boolean;
  dockOpen: boolean;
  showDebugger: boolean;
  debugMetrics: DebugMetrics | null;
  actionNotice: string | null;
  onTimeChange: (t: number) => void;
  onToggleAutoCycle: () => void;
  onSelectTool: (tool: ToolId) => void;
  onSelectLightMode: (mode: LightViewMode) => void;
  onJumpVista: (vista: CameraVistaId) => void;
  onToggleMute: () => void;
  onToggleDock: () => void;
  onToggleDebugger: () => void;
  onOpenSource: () => void;
  onDownloadZip: () => void;
  onPause: () => void;
}

const TOOLS: { id: ToolId; key: string; label: string; itemTex?: string; note: string }[] = [
  { id: 'break', key: '1', label: 'Break', note: 'Excavate' },
  { id: 'torch', key: '2', label: 'Torch', itemTex: 'torch', note: 'Light 14' },
  { id: 'glowstone', key: '3', label: 'Glowstone', itemTex: 'glowstone', note: 'Light 15' },
  { id: 'stone', key: '4', label: 'Stone', itemTex: 'stone', note: 'Opaque' },
  { id: 'timber', key: '5', label: 'Timber', itemTex: 'timber', note: 'Opaque' },
  { id: 'magma', key: '6', label: 'Magma', itemTex: 'magma', note: 'Light 13' },
  { id: 'glass', key: '7', label: 'Glass', itemTex: 'glass', note: 'Sky 15' },
];

const TIME_PRESETS: { label: string; value: number }[] = [
  { label: 'Sunrise', value: 0.005 },
  { label: 'Morning', value: 0.125 },
  { label: 'Noon', value: 0.25 },
  { label: 'Evening', value: 0.41 },
  { label: 'Sunset', value: 0.495 },
  { label: 'Night', value: 0.75 },
];

const LIGHT_MODES: { mode: LightViewMode; label: string }[] = [
  { mode: 0, label: 'Combined' },
  { mode: 1, label: 'Sky Light' },
  { mode: 2, label: 'Block Light' },
  { mode: 3, label: 'Corner AO' },
];

const VISTAS: { id: CameraVistaId; label: string }[] = [
  { id: 'overview', label: 'Overview' },
  { id: 'caveMouth', label: 'Cave Mouth' },
  { id: 'insideCave', label: 'Inside Cave' },
  { id: 'stoneArch', label: 'Stone Arch' },
];

export default function GameHud({
  clockText,
  phaseName,
  probeStats,
  timeOfDay,
  autoCycle,
  selectedTool,
  lightViewMode,
  muted,
  dockOpen,
  showDebugger,
  debugMetrics,
  actionNotice,
  onTimeChange,
  onToggleAutoCycle,
  onSelectTool,
  onSelectLightMode,
  onJumpVista,
  onToggleMute,
  onToggleDock,
  onToggleDebugger,
  onOpenSource,
  onDownloadZip,
  onPause,
}: GameHudProps) {
  const activeTool = TOOLS.find((t) => t.id === selectedTool) ?? TOOLS[0];
  const heldEmitting = probeStats.heldEmission > 0;

  return (
    <div className="fixed inset-0 z-10 pointer-events-none flex flex-col justify-between p-4 md:p-5">
      {/* Top Row: Compact perimeter corner clusters (no backdrop-blur over WebGL) */}
      <div className="flex items-start justify-between gap-4">
        {/* Top-Left: Live Lightfield, Area Darkness Zone & Solar Readout */}
        <div
          className="pointer-events-auto rounded-lg px-3.5 py-2.5 border border-slate-700/80"
          style={{ backgroundColor: 'rgba(11, 14, 20, 0.88)' }}
        >
          <div className="flex items-baseline gap-2.5">
            <span
              data-testid="hud-clock"
              className="font-mono text-lg font-semibold text-slate-50 tabular-nums"
            >
              {clockText}
            </span>
            <span className="text-xs font-medium text-amber-300">
              {phaseName}
            </span>
            <span aria-hidden="true" className="text-slate-600 text-xs">·</span>
            <span
              data-testid="hud-area-zone"
              className="font-mono text-[11px] font-semibold tracking-wide text-slate-200"
            >
              {probeStats.areaLabel}
            </span>
          </div>

          <div className="mt-1 flex flex-wrap items-center gap-x-2.5 gap-y-0.5 text-xs text-slate-300 font-mono tabular-nums">
            <span>Sky {probeStats.skyLight}/15</span>
            <span aria-hidden="true" className="text-slate-600">·</span>
            <span>Block {probeStats.blockLight}/15</span>
            <span aria-hidden="true" className="text-slate-600">·</span>
            <span>Emitters {probeStats.emitterCount}</span>
            {probeStats.eyeUnderwater && (
              <>
                <span aria-hidden="true" className="text-slate-600">·</span>
                <span className="text-cyan-300 font-semibold">
                  SUBMERGED {probeStats.waterDepth}m
                </span>
              </>
            )}
          </div>
        </div>

        {/* Center transient feedback line when editing voxels */}
        {actionNotice && (
          <div
            role="status"
            className="hidden md:block rounded-lg px-3.5 py-1.5 text-xs font-mono text-amber-200 border border-amber-500/30"
            style={{ backgroundColor: 'rgba(11, 14, 20, 0.9)' }}
          >
            {actionNotice}
          </div>
        )}

        {/* Top-Right: Held Light Status, Source Explorer, ZIP Download & Utility Icons */}
        <div className="pointer-events-auto flex items-center gap-2">
          <div
            data-testid="hud-held-light"
            title="Dynamic light emits only when holding a light source block (Torch, Glowstone, Magma)"
            className={`hidden sm:inline-flex items-center gap-1.5 px-2.5 py-2 rounded-lg text-xs font-mono border whitespace-nowrap shrink-0 ${
              heldEmitting
                ? 'border-amber-400/70 text-amber-200'
                : 'border-slate-700/80 text-slate-400'
            }`}
            style={{
              backgroundColor: heldEmitting
                ? 'rgba(120, 53, 15, 0.72)'
                : 'rgba(11, 14, 20, 0.9)',
            }}
          >
            <Flame
              className={`w-3.5 h-3.5 ${heldEmitting ? 'text-amber-300' : 'text-slate-500'}`}
              aria-hidden="true"
            />
            {heldEmitting
              ? `${activeTool.label} · Lvl ${probeStats.heldEmission}`
              : 'Held Light: Off'}
          </div>

          <button
            type="button"
            tabIndex={-1}
            data-testid="hud-debug-toggle"
            onClick={(e) => {
              e.currentTarget.blur();
              onToggleDebugger();
            }}
            title="Toggle Performance & World Debugger [Shift+F3]"
            className={`inline-flex items-center gap-1.5 px-2.5 py-2 rounded-lg text-xs font-mono border transition-colors cursor-pointer whitespace-nowrap shrink-0 ${
              showDebugger
                ? 'border-emerald-400/80 text-emerald-200'
                : 'border-slate-700/80 text-slate-300 hover:border-slate-500'
            }`}
            style={{
              backgroundColor: showDebugger
                ? 'rgba(6, 78, 59, 0.78)'
                : 'rgba(11, 14, 20, 0.9)',
            }}
          >
            <Activity className="w-3.5 h-3.5 text-emerald-400" aria-hidden="true" />
            <span>F3</span>
          </button>

          <button
            type="button"
            onClick={(e) => {
              e.currentTarget.blur();
              onOpenSource();
            }}
            className="inline-flex items-center gap-1.5 px-3 py-2 rounded-lg text-xs font-medium text-slate-100 border border-slate-700/80 hover:border-amber-400/70 transition-colors cursor-pointer whitespace-nowrap shrink-0"
            style={{ backgroundColor: 'rgba(11, 14, 20, 0.9)' }}
          >
            <Code2 className="w-3.5 h-3.5 text-amber-400" aria-hidden="true" />
            C++ / Vulkan Source
          </button>

          <button
            type="button"
            onClick={(e) => {
              e.currentTarget.blur();
              onDownloadZip();
            }}
            aria-label="Download C++20 Vulkan Project ZIP"
            className="inline-flex items-center gap-1.5 px-3 py-2 rounded-lg text-xs font-semibold text-slate-950 bg-amber-500 hover:bg-amber-400 transition-colors cursor-pointer whitespace-nowrap shrink-0"
          >
            <Download className="w-3.5 h-3.5" aria-hidden="true" />
            <span className="hidden sm:inline">compile.sh ZIP</span>
          </button>

          <button
            type="button"
            tabIndex={-1}
            onClick={(e) => {
              e.currentTarget.blur();
              onToggleMute();
            }}
            aria-label={muted ? 'Unmute audio' : 'Mute audio'}
            title="Toggle Audio [M]"
            className="inline-flex items-center justify-center w-9 h-9 rounded-lg border border-slate-700/80 text-slate-300 hover:text-slate-100 transition-colors cursor-pointer shrink-0"
            style={{ backgroundColor: 'rgba(11, 14, 20, 0.9)' }}
          >
            {muted ? (
              <VolumeX className="w-4 h-4" aria-hidden="true" />
            ) : (
              <Volume2 className="w-4 h-4" aria-hidden="true" />
            )}
          </button>

          <button
            type="button"
            tabIndex={-1}
            onClick={(e) => {
              e.currentTarget.blur();
              onPause();
            }}
            aria-label="Pause simulation"
            title="Pause [P]"
            className="inline-flex items-center justify-center w-9 h-9 rounded-lg border border-slate-700/80 text-slate-300 hover:text-slate-100 transition-colors cursor-pointer shrink-0"
            style={{ backgroundColor: 'rgba(11, 14, 20, 0.9)' }}
          >
            <Pause className="w-4 h-4" aria-hidden="true" />
          </button>
        </div>
      </div>

      {/* Shift+F3 Performance & World Debugger Overlay */}
      {showDebugger && debugMetrics && (
        <div
          data-testid="shift-f3-debugger"
          className="flex items-start justify-between gap-4 my-auto pointer-events-none"
        >
          <div
            className="rounded-lg border border-emerald-500/40 px-3.5 py-2.5 font-mono text-[11px] leading-relaxed text-slate-200 tabular-nums max-w-[360px]"
            style={{ backgroundColor: 'rgba(8, 12, 18, 0.90)' }}
          >
            <div className="text-emerald-300 font-semibold tracking-wide mb-1">
              SHIFT+F3 ENGINE DEBUGGER
            </div>
            <div>
              FPS <span className="text-emerald-300 font-semibold">{debugMetrics.fps}</span>{' '}
              | Frame {debugMetrics.frameMs.toFixed(1)}ms (avg {debugMetrics.avgMs.toFixed(1)} / min{' '}
              {debugMetrics.minMs.toFixed(1)} / max {debugMetrics.maxMs.toFixed(1)})
            </div>
            <div>
              CPU: MESH {debugMetrics.meshMs.toFixed(2)}ms · DYN {debugMetrics.dynMs.toFixed(2)}ms
            </div>
            <div className="mt-1 text-slate-300">
              XYZ: {debugMetrics.pos[0].toFixed(1)}, {debugMetrics.pos[1].toFixed(1)},{' '}
              {debugMetrics.pos[2].toFixed(1)} | Chunk ({debugMetrics.chunk[0]}, {debugMetrics.chunk[1]})
            </div>
            <div className="text-slate-300">
              Facing: {debugMetrics.facing} ({debugMetrics.yawDeg.toFixed(0)}° / {debugMetrics.pitchDeg.toFixed(0)}°)
            </div>
            <div className="text-slate-300">
              Light: Sky {debugMetrics.skyLight}/15 · Blk {debugMetrics.blockLight}/15 · Dyn{' '}
              {debugMetrics.dynLight}/15
            </div>
            <div className="text-slate-300">
              Water: {debugMetrics.eyeUnderwater ? `SUBMERGED (${debugMetrics.waterDepth}m)` : 'Dry'} | Target:{' '}
              {debugMetrics.targetBlock}
              {debugMetrics.targetPos
                ? ` (${debugMetrics.targetPos[0]}, ${debugMetrics.targetPos[1]}, ${debugMetrics.targetPos[2]})`
                : ''}
            </div>
            <div className="text-cyan-300 mt-1">
              Tris: Opaque {debugMetrics.opaqueTriangles.toLocaleString()} · Trans{' '}
              {debugMetrics.translucentTriangles.toLocaleString()} · Verts{' '}
              {debugMetrics.totalVertices.toLocaleString()}
            </div>
          </div>

          <div
            className="hidden sm:block rounded-lg border border-emerald-500/40 px-3 py-2 font-mono text-[11px] text-slate-300 w-[240px]"
            style={{ backgroundColor: 'rgba(8, 12, 18, 0.90)' }}
          >
            <div className="flex items-center justify-between text-[10px] text-slate-400 mb-1.5">
              <span>FRAMETIME (60F)</span>
              <span>16.6ms / 33.3ms</span>
            </div>
            <div className="relative h-16 flex items-end gap-[2px] border-b border-slate-700/80 pb-0.5">
              <div
                aria-hidden="true"
                className="absolute inset-x-0 border-t border-dashed border-emerald-500/40"
                style={{ bottom: '33%' }}
              />
              <div
                aria-hidden="true"
                className="absolute inset-x-0 border-t border-dashed border-amber-500/40"
                style={{ bottom: '66%' }}
              />
              {debugMetrics.frameHistory.map((ms, i) => {
                const pct = Math.max(6, Math.min(100, (ms / 50) * 100));
                const color =
                  ms <= 16.7 ? '#34d399' : ms <= 33.3 ? '#fbbf24' : '#f87171';
                return (
                  <div
                    key={i}
                    className="flex-1 rounded-t-[1px]"
                    style={{ height: `${pct}%`, backgroundColor: color }}
                  />
                );
              })}
            </div>
          </div>
        </div>
      )}

      {/* Bottom Centered Collapsible Dock (Zero Center Occlusion) */}
      <div className="flex flex-col items-center w-full">
        <button
          type="button"
          onClick={(e) => {
            e.currentTarget.blur();
            onToggleDock();
          }}
          className="pointer-events-auto inline-flex items-center gap-1.5 px-3 py-1 rounded-t-lg text-xs font-medium text-slate-300 hover:text-slate-100 border border-b-0 border-slate-700/80 cursor-pointer whitespace-nowrap"
          style={{ backgroundColor: 'rgba(11, 14, 20, 0.9)' }}
        >
          {dockOpen ? (
            <>
              <ChevronDown className="w-3.5 h-3.5" aria-hidden="true" />
              Hide Controls
            </>
          ) : (
            <>
              <ChevronUp className="w-3.5 h-3.5" aria-hidden="true" />
              Show Controls
            </>
          )}
        </button>

        {dockOpen && (
          <div
            className="pointer-events-auto w-full max-w-[1080px] rounded-xl border border-slate-700/80 px-3.5 py-2.5 flex flex-col gap-2"
            style={{ backgroundColor: 'rgba(11, 14, 20, 0.92)' }}
          >
            {/* Row 1: Voxel Tool Hotbar + Camera Vistas */}
            <div className="flex flex-wrap lg:flex-nowrap items-center justify-between gap-2.5">
              <div className="flex items-center gap-1.5 overflow-x-auto">
                {TOOLS.map((tool) => {
                  const active = selectedTool === tool.id;
                  return (
                    <button
                      key={tool.id}
                      type="button"
                      onClick={(e) => {
                        e.currentTarget.blur();
                        onSelectTool(tool.id);
                      }}
                      className={`inline-flex items-center gap-1.5 px-2.5 py-1.5 rounded-lg text-xs font-medium transition-colors cursor-pointer whitespace-nowrap shrink-0 border ${
                        active
                          ? 'bg-amber-500/20 border-amber-400 text-amber-100'
                          : 'bg-[#131824] border-slate-800 text-slate-300 hover:text-slate-100 hover:border-slate-600'
                      }`}
                    >
                      {tool.itemTex && ITEM_TEXTURE_URLS[tool.itemTex] ? (
                        <img
                          src={ITEM_TEXTURE_URLS[tool.itemTex]}
                          alt=""
                          aria-hidden="true"
                          className="w-4 h-4 shrink-0 [image-rendering:pixelated]"
                        />
                      ) : (
                        <Pickaxe className="w-3.5 h-3.5 text-slate-300 shrink-0" aria-hidden="true" />
                      )}
                      <span>{tool.label}</span>
                      <span className="font-mono text-[11px] text-slate-400">
                        {tool.key}
                      </span>
                    </button>
                  );
                })}
              </div>

              <div aria-hidden="true" className="hidden lg:block h-4 w-px bg-slate-800 shrink-0" />

              {/* Quick Camera Vistas */}
              <div className="flex items-center gap-1 shrink-0">
                {VISTAS.map((v) => (
                  <button
                    key={v.id}
                    type="button"
                    onClick={(e) => {
                      e.currentTarget.blur();
                      onJumpVista(v.id);
                    }}
                    className="px-2 py-1.5 rounded-md text-xs text-slate-300 hover:text-amber-200 hover:bg-slate-800/80 transition-colors cursor-pointer whitespace-nowrap shrink-0"
                  >
                    {v.label}
                  </button>
                ))}
              </div>
            </div>

            {/* Row 2: 6-Phase Solar Time Scrubber + Shader Channel Inspector */}
            <div className="flex flex-wrap lg:flex-nowrap items-center justify-between gap-2.5 pt-2 border-t border-slate-800/90">
              <div className="flex items-center gap-2 flex-1 min-w-[260px]">
                <button
                  type="button"
                  onClick={(e) => {
                    e.currentTarget.blur();
                    onToggleAutoCycle();
                  }}
                  aria-label={autoCycle ? 'Pause solar cycle' : 'Play solar cycle'}
                  className="inline-flex items-center justify-center w-7 h-7 rounded-md bg-[#161c2a] hover:bg-slate-800 border border-slate-700 text-slate-200 cursor-pointer shrink-0"
                >
                  {autoCycle ? (
                    <Pause className="w-3.5 h-3.5" aria-hidden="true" />
                  ) : (
                    <Play className="w-3.5 h-3.5" aria-hidden="true" />
                  )}
                </button>

                <label htmlFor="time-scrubber" className="sr-only">
                  Time of day
                </label>
                <input
                  id="time-scrubber"
                  type="range"
                  min={0}
                  max={0.999}
                  step={0.005}
                  value={timeOfDay}
                  onChange={(e) => onTimeChange(parseFloat(e.target.value))}
                  className="umbra-slider flex-1 min-w-[84px]"
                />

                <div className="flex items-center gap-0.5 shrink-0">
                  {TIME_PRESETS.map((preset) => {
                    const active = phaseName === preset.label;
                    return (
                      <button
                        key={preset.label}
                        type="button"
                        onClick={(e) => {
                          e.currentTarget.blur();
                          onTimeChange(preset.value);
                        }}
                        className={`px-1.5 py-1 rounded text-xs transition-colors cursor-pointer whitespace-nowrap shrink-0 ${
                          active
                            ? 'bg-amber-500/20 text-amber-200 font-medium'
                            : 'text-slate-300 hover:text-amber-200 hover:bg-slate-800'
                        }`}
                      >
                        {preset.label}
                      </button>
                    );
                  })}
                </div>
              </div>

              {/* Shader Light-Channel Inspector */}
              <div
                role="group"
                aria-label="Shader light channel inspector"
                className="flex items-center gap-1 p-0.5 rounded-lg bg-[#0d111a] border border-slate-800"
              >
                {LIGHT_MODES.map((item) => {
                  const active = lightViewMode === item.mode;
                  return (
                    <button
                      key={item.label}
                      type="button"
                      onClick={(e) => {
                        e.currentTarget.blur();
                        onSelectLightMode(item.mode);
                      }}
                      className={`px-2.5 py-1 rounded-md text-xs font-medium transition-colors cursor-pointer whitespace-nowrap shrink-0 ${
                        active
                          ? 'bg-slate-700 text-slate-50'
                          : 'text-slate-400 hover:text-slate-200'
                      }`}
                    >
                      {item.label}
                    </button>
                  );
                })}
              </div>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
