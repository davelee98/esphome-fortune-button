#!/usr/bin/env python3
"""Pull the pitch and amplitude contour out of the reference recording.

The recording is not committed: supply your own sad trombone as
reference.mp3 in this folder, then make the input:
    ffmpeg -i reference.mp3 -ac 1 -ar 22050 ref_trombone.wav
Then run this, then build_from_ref.py, both from this folder.
"""
import numpy as np
from scipy import signal
from scipy.io import wavfile

SR_IN = 22050
HOP_MS = 6.0
FMIN, FMAX = 70.0, 700.0


def load(path):
    sr, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    x /= max(np.abs(x).max(), 1e-9)
    return sr, x


def pitch_track(x, sr, hop_ms=HOP_MS, win_ms=40.0):
    """Autocorrelation pitch tracker. A trombone is strongly harmonic, so
    plain autocorrelation is plenty here."""
    hop = int(sr * hop_ms / 1000.0)
    win = int(sr * win_ms / 1000.0)
    lag_min = int(sr / FMAX)
    lag_max = int(sr / FMIN)
    times, freqs, rms = [], [], []
    for s in range(0, len(x) - win, hop):
        seg = x[s:s + win] * np.hanning(win)
        e = float(np.sqrt((seg ** 2).mean()))
        ac = np.correlate(seg, seg, mode="full")[win - 1:]
        ac0 = ac[0]          # grab the energy BEFORE blanking the short lags
        ac[:lag_min] = 0.0
        region = ac[:lag_max]
        f = 0.0
        if e > 0.004 and len(region) > lag_min + 2:
            lag = int(np.argmax(region))
            if lag > lag_min and ac0 > 0 and region[lag] / ac0 > 0.30:
                # parabolic interpolation for sub-sample accuracy
                if 0 < lag < len(region) - 1:
                    a, b, c = region[lag - 1], region[lag], region[lag + 1]
                    denom = (a - 2 * b + c)
                    if denom != 0:
                        lag = lag + 0.5 * (a - c) / denom
                f = sr / lag
        times.append(s / sr)
        freqs.append(f)
        rms.append(e)
    return np.array(times), np.array(freqs), np.array(rms)


if __name__ == "__main__":
    sr, x = load("ref_trombone.wav")
    t, f, e = pitch_track(x, sr)
    e_norm = e / e.max()

    print(f"duration {len(x)/sr:.2f}s   frames {len(t)}\n")
    print(" t(s)   Hz     level")
    for i in range(0, len(t), 5):
        bar = "#" * int(e_norm[i] * 30)
        print(f"{t[i]:5.2f} {f[i]:6.1f}  {e_norm[i]:4.2f} {bar}")

    np.savez("ref_contour.npz", t=t, f=f, e=e_norm)
