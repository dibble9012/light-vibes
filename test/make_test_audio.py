#!/usr/bin/env python3
"""Write synthetic test music as raw S16LE stereo 48 kHz for `lightvibesd --analyze`.

    python3 test/make_test_audio.py build/song.raw     # 124 BPM: kick, snare, hats, bass, a vocal line, then silence
Needs numpy.
"""
import sys
import numpy as np

out = sys.argv[1] if len(sys.argv) > 1 else "song.raw"
sr = 48000; T = 16
t = np.arange(sr * T) / sr; x = np.zeros_like(t)
sp = 60 / 124
rng = np.random.default_rng(1)
for i, b in enumerate(np.arange(0, 12, sp)):
    n = (t >= b) & (t < b + 0.3); tt = t[n] - b
    x[n] += 0.55 * np.sin(2 * np.pi * (50 + 70 * np.exp(-tt * 35)) * tt) * np.exp(-tt * 10)          # kick
    if i % 2 == 1:
        n2 = (t >= b) & (t < b + 0.2); tt2 = t[n2] - b
        x[n2] += 0.25 * rng.standard_normal(n2.sum()) * np.exp(-tt2 * 20) + 0.2 * np.sin(2 * np.pi * 200 * tt2) * np.exp(-tt2 * 25)  # snare
for b in np.arange(0, 12, sp / 2):
    n = (t >= b) & (t < b + 0.04); x[n] += 0.06 * rng.standard_normal(n.sum())                            # hats
x += 0.15 * np.sin(2 * np.pi * 55 * t) * (t < 12)                                                          # bass
voc = (t > 4) & (t < 10); x += voc * 0.12 * np.sin(2 * np.pi * (330 + 20 * np.sin(2 * np.pi * 5 * t)) * t)  # vocal
s = np.clip(x * 32767 * 0.6, -32768, 32767).astype("<i2")
np.stack([s, s], 1).tofile(out)
print(f"wrote {out}: {T} s, ~{int(12 / sp)} kicks")
