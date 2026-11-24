import { Code2, Download, Play } from 'lucide-react';

interface TitleOverlayProps {
  onStart: () => void;
  onOpenSource: () => void;
  onDownloadZip: () => void;
  fileCount: number;
}

export default function TitleOverlay({
  onStart,
  onOpenSource,
  onDownloadZip,
  fileCount,
}: TitleOverlayProps) {
  return (
    <div className="fixed inset-0 z-20 pointer-events-none flex flex-col justify-between p-8 md:p-12">
      {/* Subtle left-side scrim ensuring WCAG AA readability over bright daylight sky */}
      <div
        aria-hidden="true"
        className="fixed inset-y-0 left-0 w-full md:w-[540px] pointer-events-none"
        style={{
          background:
            'linear-gradient(90deg, rgba(8,11,18,0.88) 0%, rgba(8,11,18,0.62) 68%, rgba(8,11,18,0.0) 100%)',
        }}
      />

      {/* Top-left emblem & title lockup anchored in calm left-third negative space */}
      <div className="relative z-10 max-w-md pt-6 md:pt-12 pointer-events-auto">
        {/* Custom isometric voxel-light SVG emblem */}
        <div className="flex items-center gap-3.5 mb-5">
          <svg
            width="44"
            height="44"
            viewBox="0 0 44 44"
            fill="none"
            aria-hidden="true"
            className="shrink-0"
          >
            <polygon
              points="22,3 39,12.5 22,22 5,12.5"
              fill="#F59E0B"
              stroke="#FEF3C7"
              strokeWidth="1.2"
            />
            <polygon
              points="5,12.5 22,22 22,41 5,31.5"
              fill="#1E293B"
              stroke="#475569"
              strokeWidth="1.2"
            />
            <polygon
              points="39,12.5 39,31.5 22,41 22,22"
              fill="#0F172A"
              stroke="#334155"
              strokeWidth="1.2"
            />
            <polygon
              points="22,12 29,16 22,20 15,16"
              fill="#FFFBEB"
              opacity="0.9"
            />
          </svg>
          <div>
            <h1 className="font-display text-4xl md:text-5xl font-bold tracking-tight text-slate-50 leading-none">
              Umbra Voxel
            </h1>
            <p className="text-xs font-mono text-amber-300/90 mt-1.5 tracking-wide">
              C++20 · Vulkan 1.2 · GLSL 450 · {fileCount} files + compile.sh
            </p>
          </div>
        </div>

        <p className="text-sm md:text-base text-slate-200/90 leading-relaxed mb-7 max-w-sm">
          Physically grounded voxel lighting and darkness. Dual-channel BFS
          sky-light columns, dynamic cave occlusion, flickering torch flood-fill,
          and continuous solar transitions — ready to compile on your PC and
          playable right here.
        </p>

        <div className="flex flex-col sm:flex-row items-stretch sm:items-center gap-3">
          <button
            type="button"
            data-start="true"
            onClick={onStart}
            className="inline-flex items-center justify-center gap-2 px-5 py-2.5 rounded-lg bg-amber-500 hover:bg-amber-400 text-slate-950 font-semibold text-sm transition-colors cursor-pointer whitespace-nowrap shrink-0 focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-amber-300"
          >
            <Play className="w-4 h-4 fill-current" aria-hidden="true" />
            Enter World
          </button>

          <button
            type="button"
            onClick={onOpenSource}
            className="inline-flex items-center justify-center gap-2 px-4 py-2.5 rounded-lg bg-[#111622] hover:bg-[#192032] border border-slate-700 text-slate-100 font-medium text-sm transition-colors cursor-pointer whitespace-nowrap shrink-0 focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-amber-300"
          >
            <Code2 className="w-4 h-4 text-amber-400" aria-hidden="true" />
            C++ / Vulkan Code
          </button>
        </div>

        <div className="mt-4">
          <button
            type="button"
            onClick={onDownloadZip}
            className="inline-flex items-center gap-2 text-xs font-medium text-slate-300 hover:text-amber-300 transition-colors cursor-pointer py-1"
          >
            <Download className="w-3.5 h-3.5" aria-hidden="true" />
            Download complete C++20 + Vulkan + compile.sh archive (.zip)
          </button>
        </div>
      </div>

      {/* Bottom-edge scrim for reliable control-hint contrast across all solar phases */}
      <div
        aria-hidden="true"
        className="fixed inset-x-0 bottom-0 h-20 pointer-events-none"
        style={{
          background:
            'linear-gradient(0deg, rgba(8,11,18,0.82) 0%, rgba(8,11,18,0.0) 100%)',
        }}
      />

      {/* Single flat, borderless control hint in bottom safe margin */}
      <div className="relative z-10 text-xs text-slate-200/90 font-mono">
        WASD Fly · Drag Look · Click Place/Break · 1–6 Hotbar &amp; Held Light · N Time · Enter Start
      </div>
    </div>
  );
}
