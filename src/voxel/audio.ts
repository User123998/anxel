let audioCtx: AudioContext | null = null;

function getCtx(): AudioContext | null {
  if (typeof window === 'undefined') return null;
  if (!audioCtx) {
    const Ctor =
      window.AudioContext ||
      (window as unknown as { webkitAudioContext?: typeof AudioContext })
        .webkitAudioContext;
    if (!Ctor) return null;
    try {
      audioCtx = new Ctor();
    } catch {
      return null;
    }
  }
  if (audioCtx.state === 'suspended') {
    audioCtx.resume().catch(() => {});
  }
  return audioCtx;
}

export function playVoxelSound(kind: 'break' | 'place' | 'torch', muted: boolean): void {
  if (muted) return;
  const ctx = getCtx();
  if (!ctx) return;

  const now = ctx.currentTime;
  const osc = ctx.createOscillator();
  const gain = ctx.createGain();
  const jitter = 0.92 + Math.random() * 0.16;

  if (kind === 'break') {
    osc.type = 'triangle';
    osc.frequency.setValueAtTime(165 * jitter, now);
    osc.frequency.exponentialRampToValueAtTime(62, now + 0.09);
    gain.gain.setValueAtTime(0.11, now);
    gain.gain.exponentialRampToValueAtTime(0.001, now + 0.095);
  } else if (kind === 'torch') {
    osc.type = 'sine';
    osc.frequency.setValueAtTime(420 * jitter, now);
    osc.frequency.exponentialRampToValueAtTime(760 * jitter, now + 0.12);
    gain.gain.setValueAtTime(0.08, now);
    gain.gain.exponentialRampToValueAtTime(0.001, now + 0.13);
  } else {
    osc.type = 'sine';
    osc.frequency.setValueAtTime(220 * jitter, now);
    osc.frequency.exponentialRampToValueAtTime(140, now + 0.07);
    gain.gain.setValueAtTime(0.09, now);
    gain.gain.exponentialRampToValueAtTime(0.001, now + 0.075);
  }

  osc.connect(gain);
  gain.connect(ctx.destination);
  osc.start(now);
  osc.stop(now + 0.14);
}
