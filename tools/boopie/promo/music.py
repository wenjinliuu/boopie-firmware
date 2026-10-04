#!/usr/bin/env python3
# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0
"""Synthesises the promo's music: light and bright, 120 BPM, its sections and
swooshes on the video's cuts (web/scenes.js TIMELINE).

  python3 tools/boopie/promo/music.py --out work/music.wav
"""

from __future__ import annotations

import argparse
import wave
from pathlib import Path

import numpy as np

SR = 44100
BPM = 120
BEAT = 60 / BPM
DUR = 75.0
CUTS = [6.5, 10.5, 14, 19, 27, 33, 37, 45, 49, 55, 67]
CHIMES = [1.3, 3.4, 21.8, 42.6, 69.6]   # the ring closing, the name, Muse linked, "40+", the end
rng = np.random.default_rng(7)


def hz(n: float) -> float:
    return 440.0 * 2 ** ((n - 69) / 12)


def env_adsr(n: int, a: float, d: float, s: float, r: float) -> np.ndarray:
    t = np.arange(n) / SR
    e = np.where(t < a, t / max(a, 1e-4), s + (1 - s) * np.exp(-(t - a) / max(d, 1e-4)))
    rel = int(r * SR)
    if rel and n > rel:
        e[-rel:] *= np.linspace(1, 0, rel)
    return e


def add(buf: np.ndarray, sig: np.ndarray, at: float, gain: float = 1.0, pan: float = 0.0) -> None:
    i = int(at * SR)
    if i >= buf.shape[0]:
        return
    sig = sig[: buf.shape[0] - i]
    l, r = np.sqrt((1 - pan) / 2), np.sqrt((1 + pan) / 2)
    buf[i:i + len(sig), 0] += sig * gain * l
    buf[i:i + len(sig), 1] += sig * gain * r


def onepole(x: np.ndarray, cut: float) -> np.ndarray:
    """A one-pole low-pass, as a convolution with its (truncated) impulse response."""
    a = np.exp(-2 * np.pi * cut / SR)
    k = (1 - a) * a ** np.arange(int(SR * 5 / cut) + 8)
    return np.convolve(x, k)[: len(x)]


# ---------------------------------------------------------------- instruments
def pluck(note: float, dur: float = 0.6, bright: float = 1.0) -> np.ndarray:
    n = int(dur * SR)
    t = np.arange(n) / SR
    f = hz(note)
    s = (np.sin(2 * np.pi * f * t) + 0.45 * bright * np.sin(2 * np.pi * 2 * f * t) * np.exp(-t * 9)
         + 0.2 * bright * np.sin(2 * np.pi * 3 * f * t) * np.exp(-t * 14))
    return s * np.exp(-t * 6.5) * np.minimum(1, t * 400)


def bell(note: float, dur: float = 2.5) -> np.ndarray:
    n = int(dur * SR)
    t = np.arange(n) / SR
    f = hz(note)
    s = sum(a * np.sin(2 * np.pi * f * m * t) * np.exp(-t * d) for m, a, d in
            ((1, 1, 1.6), (2.76, 0.45, 3.2), (5.4, 0.25, 5.5), (8.93, 0.12, 8)))
    return s * np.minimum(1, t * 600)


def pad(notes, dur: float) -> np.ndarray:
    n = int(dur * SR)
    t = np.arange(n) / SR
    s = np.zeros(n)
    for k, note in enumerate(notes):
        for det in (-0.08, 0.0, 0.08):
            f = hz(note + det)
            ph = rng.random() * 6.28
            s += (np.sin(2 * np.pi * f * t + ph) + 0.3 * np.sin(2 * np.pi * 2 * f * t + ph)
                  + 0.12 * np.sin(2 * np.pi * 3 * f * t + ph))
    s *= env_adsr(n, 0.6, 1.0, 0.85, 0.8) / (len(notes) * 3)
    s *= 1 + 0.15 * np.sin(2 * np.pi * 0.25 * t)
    return s


def bass(note: float, dur: float) -> np.ndarray:
    n = int(dur * SR)
    t = np.arange(n) / SR
    f = hz(note)
    s = np.sin(2 * np.pi * f * t) + 0.25 * np.sin(2 * np.pi * 2 * f * t)
    return s * env_adsr(n, 0.005, 0.18, 0.55, 0.05)


def kick() -> np.ndarray:
    n = int(0.35 * SR)
    t = np.arange(n) / SR
    f = 48 + 90 * np.exp(-t * 32)
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 11)


def hat(open_: bool = False) -> np.ndarray:
    n = int((0.18 if open_ else 0.05) * SR)
    t = np.arange(n) / SR
    x = rng.standard_normal(n)
    x = x - np.concatenate([[0], x[:-1]])   # brighter
    return x * np.exp(-t * (18 if open_ else 70)) * 0.35


def clap() -> np.ndarray:
    n = int(0.25 * SR)
    t = np.arange(n) / SR
    x = rng.standard_normal(n)
    e = np.exp(-t * 22) + 0.6 * np.exp(-((t - 0.012) % 0.011) * 300) * (t < 0.035)
    return onepole(x, 3500) * e * 0.9


def swoosh(dur: float = 1.0, up: bool = True) -> np.ndarray:
    n = int(dur * SR)
    t = np.arange(n) / SR
    x = rng.standard_normal(n)
    # a band of noise that sweeps up (or down)
    lo = onepole(x, 300)
    hi = onepole(x, 4000)
    k = t / dur if up else 1 - t / dur
    s = lo * (1 - k) + hi * k
    e = np.sin(np.pi * np.clip(t / dur, 0, 1)) ** 2
    return s * e * 0.6


def riser(dur: float) -> np.ndarray:
    n = int(dur * SR)
    t = np.arange(n) / SR
    f = 200 * 2 ** (3 * t / dur)
    s = np.sin(2 * np.pi * np.cumsum(f) / SR) * 0.15 + swoosh(dur) * 0.7
    return s * (t / dur) ** 2


# ---------------------------------------------------------------- the song
# Fmaj7 | Am7 | Cmaj7 | G6, a bar of 2 s each.
CHORDS = [(53, [65, 69, 72, 76]), (57, [64, 69, 72, 76]), (48, [64, 67, 71, 76]), (55, [62, 67, 71, 74])]


def song() -> np.ndarray:
    buf = np.zeros((int(DUR * SR) + SR, 2))
    bar = 4 * BEAT
    nbars = int(np.ceil(DUR / bar))
    for b in range(nbars):
        t0 = b * bar
        root, chord = CHORDS[b % 4]
        full = 19 <= t0 < 67
        quiet = t0 >= 67
        add(buf, pad(chord, bar + 0.8), t0, 0.32 if not full else 0.26)
        # plucked arpeggio, 8ths (16ths once it's full)
        step = BEAT / 4 if full and not (45 <= t0 < 49) else BEAT / 2
        seq = chord + [chord[1] + 12, chord[2] + 12]
        for i in range(int(bar / step)):
            n = seq[(i * 3 + (i // 4)) % len(seq)] + (12 if (i % 8 == 7) else 0)
            g = 0.16 if step < BEAT / 2 else 0.2
            if t0 < 1.5 and i < 2:
                continue
            add(buf, pluck(n, 0.5, 0.8 if quiet else 1.0), t0 + i * step, g * (0.8 + 0.2 * (i % 2 == 0)),
                pan=0.35 * np.sin(i * 1.3))
        if 14 <= t0 < 67:   # bass, pulsing 8ths
            for i in range(8):
                add(buf, bass(root - 12 + (7 if i in (3, 7) else 0), BEAT / 2 * 0.9), t0 + i * BEAT / 2,
                    0.42 if i % 2 == 0 else 0.3)
    # drums, by the beat: in at the first cut, fuller from Muse on, claps from the skins
    for i in range(int(DUR / BEAT)):
        bt = i * BEAT
        if not 6.5 <= bt < 67:
            continue
        full, beat = bt >= 19, i % 4
        if full or beat in (0, 2):
            add(buf, kick(), bt, 0.75 if full else 0.55)
        if bt >= 37 and beat in (1, 3):
            add(buf, clap(), bt, 0.32)
        for h in range(2):
            add(buf, hat(open_=(h == 1 and beat == 3)), bt + h * BEAT / 2 + (BEAT / 4 if full else 0),
                0.22 if h else 0.14, pan=0.3)
    # the cuts: a swoosh into each, a riser into the drop
    for c in CUTS:
        add(buf, swoosh(0.7), c - 0.6, 0.2, pan=-0.2)
    add(buf, riser(2.0), 17.0, 0.5)
    add(buf, riser(1.5), 35.5, 0.4)
    for c in CHIMES:
        add(buf, bell(84, 2.5), c, 0.22, pan=0.25)
        add(buf, bell(91, 2.0), c + 0.06, 0.12, pan=-0.25)
    # the last chord, ringing out
    add(buf, pad([65, 69, 72, 76, 79], 6), 69, 0.3)
    add(buf, bell(77, 4), 69.6, 0.2)
    return buf[: int(DUR * SR)]


def reverb(x: np.ndarray, mix: float = 0.22) -> np.ndarray:
    n = int(1.8 * SR)
    t = np.arange(n) / SR
    out = np.empty_like(x)
    for ch in range(2):
        ir = rng.standard_normal(n) * np.exp(-t * 3.2)
        ir = onepole(ir, 6000)
        ir /= np.sqrt(np.sum(ir ** 2))
        wet = np.fft.irfft(np.fft.rfft(x[:, ch], len(x) + n) * np.fft.rfft(ir, len(x) + n))[: len(x)]
        out[:, ch] = x[:, ch] + wet * mix
    return out


def main() -> None:
    a = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    a.add_argument("--out", type=Path, required=True)
    args = a.parse_args()
    x = reverb(song())
    # gentle glue: soft clip and fades
    x = np.tanh(x * 1.2) / np.tanh(1.2)
    x /= np.max(np.abs(x)) / 0.89
    fade = int(1.2 * SR)
    x[-fade:] *= np.linspace(1, 0, fade)[:, None]
    x[: int(0.05 * SR)] *= np.linspace(0, 1, int(0.05 * SR))[:, None]
    pcm = (x * 32767).astype(np.int16)
    with wave.open(str(args.out), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())
    print(args.out)


if __name__ == "__main__":
    main()
