"""Synthesize Neon Coast's original PCM audio; no recordings, samples or downloads.

Requires Python 3.10+ and NumPy. Generated WAVs and a measured manifest are kept
under artifacts/audio-src; Unreal's build_audio_assets.py imports them separately.
"""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import wave

import numpy as np

RATE = 32000
TAU = math.tau
ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "artifacts" / "audio-src"
RNG = np.random.default_rng(721086)


def clock(seconds):
    return np.arange(round(seconds * RATE), dtype=np.float64) / RATE


def hz(note):
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


def envelope(t, length, attack=0.006, release=0.06, decay=0.0):
    env = np.minimum(1.0, t / attack) * np.minimum(1.0, (length - t) / release)
    return np.maximum(0.0, env) * np.exp(-decay * t)


def colored_noise(length, low=30.0, high=5000.0):
    """Periodic, band-limited noise so environmental loops have no arbitrary seam."""
    count = round(length * RATE)
    white = RNG.normal(0.0, 1.0, count)
    spectrum = np.fft.rfft(white)
    frequencies = np.fft.rfftfreq(count, 1.0 / RATE)
    spectrum *= (1.0 - np.exp(-(frequencies / max(1.0, low)) ** 2))
    spectrum *= np.exp(-(frequencies / high) ** 4)
    samples = np.fft.irfft(spectrum, count)
    return samples / max(1e-9, np.sqrt(np.mean(samples * samples)))


def add(buffer, event, start, gain=1.0, pan=0.0):
    positions = (round(start * RATE) + np.arange(len(event))) % len(buffer)
    if buffer.ndim == 1:
        np.add.at(buffer, positions, event * gain)
    else:
        # Equal-power pan, including wrap-around delay tails at a music-loop seam.
        angle = (pan + 1.0) * math.pi / 4
        np.add.at(buffer[:, 0], positions, event * gain * math.cos(angle))
        np.add.at(buffer[:, 1], positions, event * gain * math.sin(angle))


def kick(soft=False):
    t = clock(0.38)
    phase = TAU * (46 * t + 3.2 * (1 - np.exp(-t * 30)))
    sound = np.sin(phase) * np.exp(-t * (13 if soft else 17))
    sound += colored_noise(.38, 1800, 6500) * np.exp(-t * 140) * (.035 if soft else .075)
    return sound * envelope(t, .38, .0015, .05)


def snare(soft=False):
    t = clock(.3)
    noise = colored_noise(.3, 500, 7000)
    sound = noise * np.exp(-t * (22 if soft else 32))
    sound += np.sin(TAU * 185 * t) * np.exp(-t * 35) * .22
    return sound * envelope(t, .3, .002, .06)


def hat(length=.12):
    t = clock(length)
    return colored_noise(length, 4500, 13000) * np.exp(-t * 45) * envelope(t, length, .001, .025)


def bass(note, length=.42, soft=False):
    t = clock(length)
    phase = TAU * hz(note) * t
    sound = np.sin(phase) + np.sin(phase * 2) * .23 + np.sin(phase * 3) * (.04 if soft else .13)
    return sound * envelope(t, length, .009, .1, 3.0 if soft else 4.0)


def lead(note, length=.45, soft=False):
    t = clock(length)
    phase = TAU * hz(note) * t
    sound = np.sin(phase + np.sin(phase * 2) * (.38 if soft else .6) * np.exp(-t * 7))
    if not soft:
        sound += .16 * np.sin(phase * 2 + .2)
    return sound * envelope(t, length, .008, .14, 6 if soft else 8)


def music(station):
    length = 16.0
    buffer = np.zeros((round(length * RATE), 2), dtype=np.float64)
    soft = station == 1
    roots = [50, 46, 53, 48, 50, 46, 53, 48] if soft else [45, 41, 48, 43, 45, 41, 48, 43]
    phrase = [12, 7, 10, 14, 17, 14, 10, 7] if soft else [12, 19, 15, 22, 19, 15, 10, 14]
    for bar, root in enumerate(roots):
        start = bar * 2.0
        thirds = 3 if bar % 4 == 0 else 4
        # Original chord voicing. Quiet detuned partials make the pad wider.
        t = clock(2.2)
        chord = np.zeros_like(t)
        for offset in [12, 12 + thirds, 19, 22 if soft else 24]:
            freq = hz(root + offset)
            chord += np.sin(TAU * freq * t) * .22
            chord += np.sin(TAU * (freq * 1.0017) * t + .4) * .06
        chord *= envelope(t, 2.2, .13, .5)
        add(buffer, chord, start, .17 if soft else .18, -.12)
        add(buffer, chord, start + .011, .10, .55)
        for beat in range(4):
            when = start + beat * .5
            if not soft or beat in (0, 2):
                add(buffer, kick(soft), when, .29 if soft else .34)
            if beat in (1, 3):
                add(buffer, snare(soft), when, .09 if soft else .14, .08)
            add(buffer, bass(root if beat != 3 else root + 7, soft=soft), when + (.12 if soft else 0), .24)
        for step in range(8):
            when = start + step * .25
            add(buffer, hat(.10), when + (.02 if soft and step % 2 else 0), .019 if soft else .025, -.5 if step % 2 else .45)
            if soft and step in (1, 4):
                continue
            offset = phrase[(step + (2 if bar >= 4 else 0)) % 8]
            melody = lead(root + offset + (12 if not soft else 0), soft=soft)
            add(buffer, melody, when, .075 if soft else .085, .23)
            add(buffer, melody, when + .375, .018, -.55)
    # Sidechain-like dip creates space around the drums without any external DSP.
    t = clock(length)
    buffer *= (.84 + .16 * (1 - np.exp(-(t % .5) * 16)))[:, None]
    return buffer


def engine_loop():
    t = clock(2.0)
    phase = TAU * 48 * t + .14 * np.sin(TAU * 8 * t)
    sound = np.sin(phase) * .48 + np.sin(phase * 2) * .24 + np.sin(phase * 3) * .11
    sound += np.sin(phase * 5) * .045 + colored_noise(2, 80, 600) * .035
    return sound * (.86 + .14 * np.sin(TAU * 12 * t))


def coast_ambience():
    t = clock(16.0)
    sound = colored_noise(16, 110, 3100) * (.14 + .09 * np.sin(TAU * t / 8) ** 2)
    sound += colored_noise(16, 25, 230) * (.045 + .025 * np.sin(TAU * t / 16))
    sound += np.sin(TAU * 52 * t) * .008 + np.sin(TAU * 81 * t) * .004
    for onset, pitch in [(2.4, 1900), (7.1, 2200), (12.5, 1800)]:
        chirp_time = clock(.4)
        bird = np.sin(TAU * (pitch * chirp_time + 240 * chirp_time**2))
        bird *= np.sin(math.pi * chirp_time / .4) ** 3
        add(sound, bird, onset, .018)
    return sound


def police_siren():
    t = clock(4.0)
    phase = TAU * 650 * t - 920 * np.cos(TAU * t / 4)
    return (np.sin(phase) + .10 * np.sin(phase * 3)) * (.82 + .18 * np.cos(TAU * t / 4))


def horn():
    t = clock(.55)
    sound = np.sin(TAU * 349.23 * t) + np.sin(TAU * 440 * t) * .8
    sound += .1 * np.sin(TAU * 698.46 * t)
    return sound * envelope(t, .55, .018, .09)


def gunshot():
    t = clock(.3)
    attack = colored_noise(.3, 600, 11000) * np.exp(-t * 52)
    body = np.sin(TAU * (80 * t + 1.8 * (1 - np.exp(-t * 30)))) * np.exp(-t * 24)
    room = colored_noise(.3, 200, 2600) * np.exp(-t * 15) * .1
    return (attack * .8 + body * .45 + room) * envelope(t, .3, .0005, .06)


def reload():
    result = np.zeros(round(.9 * RATE))
    for when, gain, pitch in [(0, .6, 1300), (.25, .4, 1700), (.58, .8, 900), (.72, .35, 2100)]:
        t = clock(.12)
        click = colored_noise(.12, 600, 5500) * np.exp(-t * 70)
        click += np.sin(TAU * pitch * t) * np.exp(-t * 85) * .13
        add(result, click * envelope(t, .12, .001, .04), when, gain)
    return result


def footstep():
    t = clock(.24)
    toe = np.maximum(0.0, t - .075)
    grit = colored_noise(.24, 160, 2600) * (np.exp(-t * 45) + .45 * np.exp(-toe * 65) * (t >= .075))
    thud = np.sin(TAU * 74 * t) * np.exp(-t * 35)
    return (grit * .32 + thud * .27) * envelope(t, .24, .003, .06)


def write_wave(name, samples, looping, peak=.70):
    samples = np.asarray(samples, dtype=np.float64)
    samples -= np.mean(samples, axis=0)
    # Very short edge fades protect import/play boundaries, including station switches.
    fade = min(round(.006 * RATE), len(samples) // 10)
    taper = np.linspace(0.0, 1.0, fade, endpoint=False)
    samples[:fade] *= taper[:, None] if samples.ndim > 1 else taper
    samples[-fade:] *= taper[::-1, None] if samples.ndim > 1 else taper[::-1]
    samples *= peak / max(1e-9, np.max(np.abs(samples)))
    pcm = np.round(np.clip(samples, -.999, .999) * 32767).astype("<i2")
    path = OUTPUT / (name + ".wav")
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(1 if samples.ndim == 1 else samples.shape[1])
        handle.setsampwidth(2)
        handle.setframerate(RATE)
        handle.writeframes(pcm.tobytes())
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    return {
        "name": name,
        "file": path.name,
        "asset": "/Game/Audio/" + name,
        "looping": looping,
        "duration_seconds": len(samples) / RATE,
        "sample_rate": RATE,
        "channels": 1 if samples.ndim == 1 else samples.shape[1],
        "sample_width_bytes": 2,
        "peak": round(float(np.max(np.abs(samples))), 6),
        "rms": round(float(np.sqrt(np.mean(samples * samples))), 6),
        "sha256": digest,
        "bytes": path.stat().st_size,
    }


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    entries = [
        write_wave("SW_Radio_NightDrive", music(0), True, .72),
        write_wave("SW_Radio_Coastline", music(1), True, .72),
        write_wave("SW_EngineLoop", engine_loop(), True, .58),
        write_wave("SW_CoastAmbience", coast_ambience(), True, .48),
        write_wave("SW_PoliceSiren", police_siren(), True, .62),
        write_wave("SW_Horn", horn(), False, .65),
        write_wave("SW_Gunshot", gunshot(), False, .78),
        write_wave("SW_Reload", reload(), False, .62),
        write_wave("SW_Footstep", footstep(), False, .52),
    ]
    manifest = {"schema": 1, "provenance": "Original deterministic NumPy synthesis; no third-party recordings or music samples.", "seed": 721086, "assets": entries}
    (OUTPUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"assets": len(entries), "bytes": sum(e["bytes"] for e in entries), "directory": str(OUTPUT)}, indent=2))


if __name__ == "__main__":
    main()
