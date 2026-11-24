import { useState } from 'react';
import {
  Check,
  Copy,
  Download,
  FileCode2,
  Layers,
  Terminal,
  X,
} from 'lucide-react';
import {
  CPP_BINARY_ASSETS,
  CPP_PROJECT_FILES,
  ProjectSourceFile,
} from '../data/cppProjectFiles';

interface SourceExplorerModalProps {
  onClose: () => void;
  onDownloadZip: () => void;
}

const CATEGORIES: ProjectSourceFile['category'][] = [
  'Build & Docs',
  'Vulkan GLSL 450 Shaders',
  'C++20 Engine Sources',
];

export default function SourceExplorerModal({
  onClose,
  onDownloadZip,
}: SourceExplorerModalProps) {
  const [selectedPath, setSelectedPath] = useState<string>(
    CPP_PROJECT_FILES[0]?.path ?? 'compile.sh'
  );
  const [activeTab, setActiveTab] = useState<'code' | 'architecture'>('code');
  const [copied, setCopied] = useState(false);

  const selectedFile =
    CPP_PROJECT_FILES.find((f) => f.path === selectedPath) ??
    CPP_PROJECT_FILES[0];

  const totalLines = CPP_PROJECT_FILES.reduce((acc, f) => acc + f.lineCount, 0);

  const handleCopy = async () => {
    if (!selectedFile) return;
    try {
      await navigator.clipboard.writeText(selectedFile.content);
      setCopied(true);
      window.setTimeout(() => setCopied(false), 1800);
    } catch {
      // Fallback copy unavailable in restricted contexts
    }
  };

  return (
    <div
      role="dialog"
      aria-modal="true"
      aria-label="C++20 and Vulkan Source Code Explorer"
      className="fixed inset-0 z-40 flex flex-col bg-[#0b0e14] text-slate-100"
    >
      {/* Top Bar: Clean 3-zone header */}
      <header className="flex items-center justify-between gap-6 px-6 py-4 border-b border-slate-800 shrink-0">
        <div className="flex items-baseline gap-3 min-w-0">
          <h2 className="font-display text-lg font-semibold text-slate-50 whitespace-nowrap shrink-0">
            Umbra Voxel C++20 / Vulkan 1.2
          </h2>
          <span className="hidden sm:inline text-xs font-mono text-slate-400 tabular-nums whitespace-nowrap">
            {CPP_PROJECT_FILES.length} source files · {CPP_BINARY_ASSETS.length} PNG textures · {totalLines.toLocaleString()} lines
          </span>
        </div>

        <nav className="flex items-center gap-1 p-1 rounded-lg bg-[#121722] border border-slate-800">
          <button
            type="button"
            onClick={() => setActiveTab('code')}
            className={`inline-flex items-center gap-1.5 px-3 py-1.5 rounded-md text-xs font-medium transition-colors cursor-pointer whitespace-nowrap shrink-0 ${
              activeTab === 'code'
                ? 'bg-slate-800 text-slate-50'
                : 'text-slate-400 hover:text-slate-200'
            }`}
          >
            <FileCode2 className="w-3.5 h-3.5" aria-hidden="true" />
            Source &amp; compile.sh
          </button>
          <button
            type="button"
            onClick={() => setActiveTab('architecture')}
            className={`inline-flex items-center gap-1.5 px-3 py-1.5 rounded-md text-xs font-medium transition-colors cursor-pointer whitespace-nowrap shrink-0 ${
              activeTab === 'architecture'
                ? 'bg-slate-800 text-slate-50'
                : 'text-slate-400 hover:text-slate-200'
            }`}
          >
            <Layers className="w-3.5 h-3.5" aria-hidden="true" />
            Lighting Architecture
          </button>
        </nav>

        <div className="flex items-center gap-2.5 shrink-0">
          <button
            type="button"
            onClick={onDownloadZip}
            className="inline-flex items-center gap-2 px-4 py-2 rounded-lg bg-amber-500 hover:bg-amber-400 text-slate-950 font-semibold text-xs transition-colors cursor-pointer whitespace-nowrap shrink-0"
          >
            <Download className="w-3.5 h-3.5" aria-hidden="true" />
            Download Project (.zip)
          </button>
          <button
            type="button"
            onClick={onClose}
            aria-label="Close source explorer"
            className="inline-flex items-center justify-center w-9 h-9 rounded-lg border border-slate-700 text-slate-300 hover:text-slate-50 hover:bg-slate-800 transition-colors cursor-pointer shrink-0"
          >
            <X className="w-4 h-4" aria-hidden="true" />
          </button>
        </div>
      </header>

      {/* Main Content */}
      {activeTab === 'code' ? (
        <div className="flex-1 min-h-0 flex flex-col md:flex-row">
          {/* Left File Tree */}
          <aside className="w-full md:w-72 border-b md:border-b-0 md:border-r border-slate-800 bg-[#0e121b] overflow-y-auto shrink-0 p-4 space-y-5 max-h-56 md:max-h-none">
            {CATEGORIES.map((category) => {
              const files = CPP_PROJECT_FILES.filter(
                (f) => f.category === category
              );
              return (
                <div key={category}>
                  <h3 className="text-xs font-medium text-slate-400 mb-2 px-2">
                    {category}
                  </h3>
                  <ul className="space-y-0.5">
                    {files.map((file) => {
                      const active = file.path === selectedFile?.path;
                      return (
                        <li key={file.path}>
                          <button
                            type="button"
                            onClick={() => setSelectedPath(file.path)}
                            className={`w-full flex items-center justify-between gap-2 px-2.5 py-2 rounded-lg text-left text-xs transition-colors cursor-pointer ${
                              active
                                ? 'bg-amber-500/15 text-amber-200 font-medium'
                                : 'text-slate-300 hover:bg-slate-800/70 hover:text-slate-100'
                            }`}
                          >
                            <span className="font-mono truncate">
                              {file.path}
                            </span>
                            <span className="font-mono text-[11px] text-slate-500 tabular-nums shrink-0">
                              {file.lineCount}
                            </span>
                          </button>
                        </li>
                      );
                    })}
                  </ul>
                </div>
              );
            })}
          </aside>

          {/* Right Code Viewer */}
          <main className="flex-1 min-w-0 flex flex-col bg-[#0b0e14]">
            {selectedFile && (
              <>
                <div className="flex flex-wrap items-center justify-between gap-3 px-6 py-3.5 border-b border-slate-800/90 bg-[#0f141f] shrink-0">
                  <div className="min-w-0">
                    <div className="flex items-center gap-2.5">
                      <span className="font-mono text-sm font-semibold text-slate-100">
                        {selectedFile.path}
                      </span>
                      <span className="text-xs font-mono text-slate-400 tabular-nums">
                        {selectedFile.lineCount} lines
                      </span>
                    </div>
                    <p className="text-xs text-slate-400 mt-0.5">
                      {selectedFile.summary}
                    </p>
                  </div>

                  <button
                    type="button"
                    onClick={handleCopy}
                    className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg border border-slate-700 text-xs font-medium text-slate-200 hover:bg-slate-800 transition-colors cursor-pointer whitespace-nowrap shrink-0"
                  >
                    {copied ? (
                      <>
                        <Check className="w-3.5 h-3.5 text-emerald-400" aria-hidden="true" />
                        Copied
                      </>
                    ) : (
                      <>
                        <Copy className="w-3.5 h-3.5" aria-hidden="true" />
                        Copy File
                      </>
                    )}
                  </button>
                </div>

                <div className="flex-1 overflow-auto p-5">
                  <pre className="font-mono text-xs leading-6 text-slate-200 tab-size-4">
                    {selectedFile.content.split('\n').map((line, i) => (
                      <div key={i} className="flex hover:bg-slate-800/35 px-2 -mx-2 rounded">
                        <span className="select-none w-11 pr-4 text-right text-slate-600 tabular-nums shrink-0">
                          {i + 1}
                        </span>
                        <code className="whitespace-pre">{line || ' '}</code>
                      </div>
                    ))}
                  </pre>
                </div>
              </>
            )}
          </main>
        </div>
      ) : (
        /* Architecture & PC Build Reference */
        <div className="flex-1 overflow-y-auto px-6 py-8 md:px-12">
          <div className="max-w-4xl mx-auto space-y-10">
            <section>
              <h3 className="font-display text-2xl font-semibold text-slate-50 mb-3">
                How Lighting &amp; Darkness Know Where and When to Appear
              </h3>
              <p className="text-sm text-slate-300 leading-relaxed max-w-2xl">
                Both the C++20/Vulkan 1.2 engine and the live browser simulation
                execute the exact same coupled spatial lightfield + temporal
                shader pipeline so caves stay pitch-black at noon, torches push
                back darkness naturally without leaking through stone walls, and
                open skies transition smoothly from dawn to moonlit midnight.
              </p>
            </section>

            <div className="grid grid-cols-1 md:grid-cols-2 gap-8 pt-2 border-t border-slate-800">
              <div className="space-y-3">
                <h4 className="font-display text-base font-semibold text-amber-300">
                  01. Spatial Occlusion (Where Darkness Lives)
                </h4>
                <p className="text-sm text-slate-300 leading-relaxed">
                  Every voxel packs two 4-bit channels in one byte:
                  <code className="font-mono text-amber-200"> skyLight</code> (high
                  nibble, 0–15) and
                  <code className="font-mono text-amber-200"> blockLight</code> (low
                  nibble, 0–15). Vertical sky columns drop straight from the top
                  of the world at level 15 until hitting an opaque roof, where
                  direct sky light cuts to 0. A 6-neighbor BFS flood-fill then
                  decays sky light by -1 per step into cave entrances and arches
                  until deep interiors hit true darkness (0).
                </p>
              </div>

              <div className="space-y-3">
                <h4 className="font-display text-base font-semibold text-amber-300">
                  02. Incremental Relighting (Live Edits)
                </h4>
                <p className="text-sm text-slate-300 leading-relaxed">
                  Breaking a cave roof or sealing a skylight runs a two-phase
                  <code className="font-mono text-amber-200"> Removal BFS → Add BFS</code>{' '}
                  solver (<code className="font-mono">src/lighting.cpp</code>).
                  Cleared voxels trace downstream dependent nodes back to zero
                  and re-seed from surviving boundary light paths so darkness
                  reclaims sealed spaces immediately.
                </p>
              </div>

              <div className="space-y-3">
                <h4 className="font-display text-base font-semibold text-amber-300">
                  03. Sky-Visibility-Coupled Direct Sun
                </h4>
                <p className="text-sm text-slate-300 leading-relaxed">
                  In <code className="font-mono">assets/shaders/voxel.frag</code>, direct
                  N·L sunlight is multiplied by
                  <code className="font-mono text-amber-200"> smoothstep(0.86, 0.99, skyNorm)</code>.
                  This eliminates the classic voxel shader bug where the sun
                  illuminates cave walls underground. Meanwhile, temporal
                  <code className="font-mono text-amber-200"> uSkyIntensity</code>{' '}
                  scales from 1.0 at noon down to 0.09 at midnight with a cool
                  Purkinje scotopic night tint.
                </p>
              </div>

              <div className="space-y-3">
                <h4 className="font-display text-base font-semibold text-amber-300">
                  04. Smooth Corner AO &amp; Real PNG Textures
                </h4>
                <p className="text-sm text-slate-300 leading-relaxed">
                  <code className="font-mono">src/texture_loader.hpp</code> decodes
                  all PNGs from <code className="font-mono">assets/texture/block/</code>{' '}
                  and <code className="font-mono">assets/texture/items/</code> into
                  Vulkan <code className="font-mono text-amber-200">sampler2DArray</code>{' '}
                  layers, while <code className="font-mono">src/mesher.cpp</code>{' '}
                  flips quad diagonals whenever{' '}
                  <code className="font-mono text-amber-200">ao0 + ao2 &lt;= ao1 + ao3</code>{' '}
                  to keep crevices artifact-free.
                </p>
              </div>
            </div>

            <section className="rounded-xl bg-[#0f141f] border border-slate-800 p-6 space-y-4">
              <div className="flex items-center gap-2 text-slate-100 font-display font-semibold">
                <Terminal className="w-4 h-4 text-amber-400" aria-hidden="true" />
                Compiling on Your PC
              </div>
              <pre className="font-mono text-xs text-slate-200 bg-[#080b11] rounded-lg p-4 overflow-x-auto leading-6 border border-slate-800/80">
{`# 1. Install dependencies (Ubuntu / Debian example — zero GLM required!)
sudo apt install g++ libvulkan-dev vulkan-validationlayers glslc libglfw3-dev pkg-config

# 2. Unzip downloaded archive and compile + launch
#    Compiles assets/shaders/* -> *.spv, builds binary, and copies assets/ -> build/assets/
unzip umbra_vulkan_voxel_cpp20.zip
cd umbra_vulkan_voxel
chmod +x compile.sh
./compile.sh --release --run

# Optional: build with Vulkan Validation Layers enabled
./compile.sh --debug --run`}
              </pre>
            </section>
          </div>
        </div>
      )}
    </div>
  );
}
