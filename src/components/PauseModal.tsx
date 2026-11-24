import { useState } from 'react';
import { ArrowLeft, Code2, Home, Play, RotateCcw, Sliders } from 'lucide-react';
import { WorldProbeStats } from '../voxel/types';

export type GraphicsQualityPreset = 'Low' | 'Medium' | 'High' | 'Ultra';

export interface EngineGraphicsSettings {
  renderDistance: number; // 4..50 chunks
  quality: GraphicsQualityPreset;
  fogStrength: number; // 0..1 (default 0.22 for crisp visibility)
  borderFog: boolean;
  smoothLighting: boolean;
  greedyMeshing: boolean;
  fov: number; // 60..105 deg
}

export const DEFAULT_GRAPHICS_SETTINGS: EngineGraphicsSettings = {
  renderDistance: 12,
  quality: 'High',
  fogStrength: 0.22,
  borderFog: true,
  smoothLighting: true,
  greedyMeshing: true,
  fov: 70,
};

interface PauseModalProps {
  clockText: string;
  phaseName: string;
  probeStats: WorldProbeStats;
  settings: EngineGraphicsSettings;
  onUpdateSettings: (next: EngineGraphicsSettings) => void;
  onResume: () => void;
  onResetWorld: () => void;
  onOpenSource: () => void;
  onReturnTitle: () => void;
}

const QUALITY_PRESETS: GraphicsQualityPreset[] = ['Low', 'Medium', 'High', 'Ultra'];

export default function PauseModal({
  clockText,
  phaseName,
  probeStats,
  settings,
  onUpdateSettings,
  onResume,
  onResetWorld,
  onOpenSource,
  onReturnTitle,
}: PauseModalProps) {
  const [view, setView] = useState<'pause' | 'settings'>('pause');

  const applyPreset = (quality: GraphicsQualityPreset) => {
    onUpdateSettings({
      ...settings,
      quality,
      smoothLighting: quality !== 'Low',
      greedyMeshing: true,
    });
  };

  return (
    <div
      role="dialog"
      aria-modal="true"
      aria-label={view === 'pause' ? 'Simulation Paused' : 'Video and World Settings'}
      className="fixed inset-0 z-30 flex items-center justify-center p-6"
      style={{ backgroundColor: 'rgba(8, 11, 18, 0.78)' }}
    >
      {view === 'pause' ? (
        <div className="w-full max-w-md rounded-xl bg-[#101521] border border-slate-700/90 p-7 space-y-6">
          <div>
            <h2 className="font-display text-2xl font-bold text-slate-50">
              Simulation Paused
            </h2>
            <p className="text-sm text-slate-400 mt-1">
              Umbra Voxel Engine · FastNoiseLite + Binary Greedy
            </p>
          </div>

          <div className="grid grid-cols-3 gap-3 py-3 border-y border-slate-800">
            <div>
              <div className="text-xs text-slate-400">Solar Time</div>
              <div className="font-mono text-base font-semibold text-slate-100 tabular-nums mt-0.5">
                {clockText}
              </div>
              <div className="text-xs text-amber-300">{phaseName}</div>
            </div>
            <div>
              <div className="text-xs text-slate-400">Render Dist</div>
              <div className="font-mono text-base font-semibold text-sky-300 tabular-nums mt-0.5">
                {settings.renderDistance} chk
              </div>
              <div className="text-xs text-slate-500">{settings.quality}</div>
            </div>
            <div>
              <div className="text-xs text-slate-400">Dark Cells</div>
              <div className="font-mono text-base font-semibold text-slate-100 tabular-nums mt-0.5">
                {probeStats.darkVoxelCount.toLocaleString()}
              </div>
              <div className="text-xs text-slate-500">Sky 0 · Blk 0</div>
            </div>
          </div>

          <div className="flex flex-col gap-2.5">
            <button
              type="button"
              onClick={onResume}
              className="w-full inline-flex items-center justify-center gap-2 px-4 py-2.5 rounded-lg bg-amber-500 hover:bg-amber-400 text-slate-950 font-semibold text-sm transition-colors cursor-pointer"
            >
              <Play className="w-4 h-4 fill-current" aria-hidden="true" />
              Resume World
            </button>

            <button
              type="button"
              data-testid="open-settings-btn"
              onClick={() => setView('settings')}
              className="w-full inline-flex items-center justify-center gap-2 px-4 py-2.5 rounded-lg bg-[#171d2c] hover:bg-slate-800 border border-slate-700 text-slate-100 font-medium text-sm transition-colors cursor-pointer"
            >
              <Sliders className="w-4 h-4 text-sky-400" aria-hidden="true" />
              Video &amp; World Settings
            </button>

            <button
              type="button"
              onClick={onOpenSource}
              className="w-full inline-flex items-center justify-center gap-2 px-4 py-2.5 rounded-lg bg-[#171d2c] hover:bg-slate-800 border border-slate-700 text-slate-100 font-medium text-sm transition-colors cursor-pointer"
            >
              <Code2 className="w-4 h-4 text-amber-400" aria-hidden="true" />
              C++ / Vulkan Source &amp; compile.sh
            </button>

            <div className="grid grid-cols-2 gap-2.5">
              <button
                type="button"
                onClick={onResetWorld}
                className="inline-flex items-center justify-center gap-1.5 px-3 py-2 rounded-lg bg-[#131824] hover:bg-slate-800 border border-slate-800 text-slate-300 text-xs font-medium transition-colors cursor-pointer"
              >
                <RotateCcw className="w-3.5 h-3.5" aria-hidden="true" />
                Reset World
              </button>
              <button
                type="button"
                onClick={onReturnTitle}
                className="inline-flex items-center justify-center gap-1.5 px-3 py-2 rounded-lg bg-[#131824] hover:bg-slate-800 border border-slate-800 text-slate-300 text-xs font-medium transition-colors cursor-pointer"
              >
                <Home className="w-3.5 h-3.5" aria-hidden="true" />
                Title Screen
              </button>
            </div>
          </div>
        </div>
      ) : (
        <div
          data-testid="settings-panel"
          className="w-full max-w-xl rounded-xl bg-[#101521] border border-slate-700/90 p-7 space-y-5"
        >
          <div className="flex items-start justify-between gap-4 border-b border-slate-800 pb-4">
            <div>
              <h2 className="font-display text-2xl font-bold text-slate-50">
                Video &amp; World Settings
              </h2>
              <p className="text-xs text-slate-400 mt-1">
                Mirrors the C++ Vulkan Settings UI · Binary Greedy Meshing + Seamless Border Fog
              </p>
            </div>
            <button
              type="button"
              onClick={() => onUpdateSettings(DEFAULT_GRAPHICS_SETTINGS)}
              className="text-xs text-slate-400 hover:text-amber-300 transition-colors cursor-pointer"
            >
              Reset Defaults
            </button>
          </div>

          <div className="space-y-4">
            {/* Render Distance (4..50 chunks) */}
            <div>
              <div className="flex items-center justify-between text-xs mb-1.5">
                <label htmlFor="rd-slider" className="font-medium text-slate-200">
                  Render Distance (Max 50 Chunks)
                </label>
                <span className="font-mono text-sky-300 tabular-nums">
                  {settings.renderDistance} chunks ({settings.renderDistance * 16} blocks)
                </span>
              </div>
              <input
                id="rd-slider"
                type="range"
                min={4}
                max={50}
                step={1}
                value={settings.renderDistance}
                onChange={(e) =>
                  onUpdateSettings({
                    ...settings,
                    renderDistance: Number(e.target.value),
                  })
                }
                className="w-full accent-sky-400 cursor-pointer"
              />
            </div>

            {/* Graphics Quality Preset */}
            <div>
              <div className="flex items-center justify-between text-xs mb-1.5">
                <span className="font-medium text-slate-200">Graphics Quality</span>
                <span className="font-mono text-amber-300">{settings.quality}</span>
              </div>
              <div className="grid grid-cols-4 gap-2">
                {QUALITY_PRESETS.map((q) => {
                  const active = settings.quality === q;
                  return (
                    <button
                      key={q}
                      type="button"
                      onClick={() => applyPreset(q)}
                      className={`py-2 rounded-lg text-xs font-semibold border transition-colors cursor-pointer ${
                        active
                          ? 'bg-amber-500/20 border-amber-400 text-amber-200'
                          : 'bg-[#161c2b] border-slate-700/80 text-slate-300 hover:border-slate-500'
                      }`}
                    >
                      {q}
                    </button>
                  );
                })}
              </div>
            </div>

            {/* Atmospheric Fog Density */}
            <div>
              <div className="flex items-center justify-between text-xs mb-1.5">
                <label htmlFor="fog-slider" className="font-medium text-slate-200">
                  Atmospheric Fog Density
                </label>
                <span className="font-mono text-amber-300 tabular-nums">
                  {Math.round(settings.fogStrength * 100)}%
                </span>
              </div>
              <input
                id="fog-slider"
                type="range"
                min={0}
                max={100}
                step={1}
                value={Math.round(settings.fogStrength * 100)}
                onChange={(e) =>
                  onUpdateSettings({
                    ...settings,
                    fogStrength: Number(e.target.value) / 100,
                  })
                }
                className="w-full accent-amber-400 cursor-pointer"
              />
            </div>

            {/* Toggles */}
            <div className="grid grid-cols-3 gap-2.5 pt-1">
              <button
                type="button"
                onClick={() =>
                  onUpdateSettings({ ...settings, borderFog: !settings.borderFog })
                }
                className={`p-2.5 rounded-lg border text-left transition-colors cursor-pointer ${
                  settings.borderFog
                    ? 'bg-emerald-500/15 border-emerald-500/60 text-emerald-200'
                    : 'bg-[#161c2b] border-slate-700 text-slate-400'
                }`}
              >
                <div className="text-[11px] font-semibold">Border Fog</div>
                <div className="font-mono text-xs mt-0.5">
                  {settings.borderFog ? 'ON' : 'OFF'}
                </div>
              </button>

              <button
                type="button"
                onClick={() =>
                  onUpdateSettings({
                    ...settings,
                    smoothLighting: !settings.smoothLighting,
                  })
                }
                className={`p-2.5 rounded-lg border text-left transition-colors cursor-pointer ${
                  settings.smoothLighting
                    ? 'bg-emerald-500/15 border-emerald-500/60 text-emerald-200'
                    : 'bg-[#161c2b] border-slate-700 text-slate-400'
                }`}
              >
                <div className="text-[11px] font-semibold">Smooth Light</div>
                <div className="font-mono text-xs mt-0.5">
                  {settings.smoothLighting ? 'ON' : 'OFF'}
                </div>
              </button>

              <button
                type="button"
                onClick={() =>
                  onUpdateSettings({
                    ...settings,
                    greedyMeshing: !settings.greedyMeshing,
                  })
                }
                className={`p-2.5 rounded-lg border text-left transition-colors cursor-pointer ${
                  settings.greedyMeshing
                    ? 'bg-emerald-500/15 border-emerald-500/60 text-emerald-200'
                    : 'bg-[#161c2b] border-slate-700 text-slate-400'
                }`}
              >
                <div className="text-[11px] font-semibold">Greedy Mesh</div>
                <div className="font-mono text-xs mt-0.5">
                  {settings.greedyMeshing ? 'ON' : 'OFF'}
                </div>
              </button>
            </div>

            {/* Field of View */}
            <div>
              <div className="flex items-center justify-between text-xs mb-1.5">
                <label htmlFor="fov-slider" className="font-medium text-slate-200">
                  Field of View (FOV)
                </label>
                <span className="font-mono text-sky-300 tabular-nums">
                  {settings.fov}°
                </span>
              </div>
              <input
                id="fov-slider"
                type="range"
                min={60}
                max={105}
                step={1}
                value={settings.fov}
                onChange={(e) =>
                  onUpdateSettings({
                    ...settings,
                    fov: Number(e.target.value),
                  })
                }
                className="w-full accent-sky-400 cursor-pointer"
              />
            </div>
          </div>

          <div className="grid grid-cols-2 gap-3 pt-2 border-t border-slate-800">
            <button
              type="button"
              onClick={() => setView('pause')}
              className="inline-flex items-center justify-center gap-1.5 px-4 py-2.5 rounded-lg bg-[#171d2c] hover:bg-slate-800 border border-slate-700 text-slate-200 text-sm font-medium transition-colors cursor-pointer"
            >
              <ArrowLeft className="w-4 h-4" aria-hidden="true" />
              Back
            </button>
            <button
              type="button"
              onClick={onResume}
              className="inline-flex items-center justify-center gap-2 px-4 py-2.5 rounded-lg bg-amber-500 hover:bg-amber-400 text-slate-950 font-semibold text-sm transition-colors cursor-pointer"
            >
              <Play className="w-4 h-4 fill-current" aria-hidden="true" />
              Apply &amp; Resume
            </button>
          </div>
        </div>
      )}
    </div>
  );
}
