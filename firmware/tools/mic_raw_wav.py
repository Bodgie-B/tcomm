#!/usr/bin/env python3
"""Rebuild a stereo WAV from a panel's serial `mic raw [ms]` output (docs/voice-notes/WORKLOG.md 2026-11-21).

The panel prints, in order: MIC RAW I2S ..., MIC RAW ES7210 ..., the header
"MIC RAW rate 44100 ch 2 bits 16 frames N gain G regs 00=.. ...", data lines "<first frame> <hex>"
(64 frames a line, each frame L then R, int16 little-endian), MIC RAW END, MIC RAW STATS ...

Usage:
  python tools/mic_raw_wav.py capture.txt out.wav        # the stereo WAV, as the mic task read it
  python tools/mic_raw_wav.py capture.txt out.wav --split  # also out_L.wav and out_R.wav (mono each)
Prints the settings lines, the register dump, and per slot: peak, RMS, DC, the L/R correlation and the
share of energy in 0-500 Hz, 0.5-1, 1-2, 2-4, 4-8 and 8-22 kHz (to see where the treble goes).
Only the standard library: the band split is a plain DFT over a few 4096-sample windows.
"""
import cmath
import math
import struct
import sys
import wave


def parse(lines):
    """Returns (info dict, list of (L, R) frames, meta lines). Raises ValueError on a broken capture."""
    info, frames, meta = None, [], []
    started = ended = False
    for raw in lines:
        line = raw.strip()
        if line.startswith("MIC RAW"):
            meta.append(line)
            if line.startswith("MIC RAW rate "):
                w = line.split()
                info = {"rate": int(w[3]), "ch": int(w[5]), "bits": int(w[7]), "frames": int(w[9]), "gain": int(w[11]),
                        "regs": {}}
                for kv in w[13:]:
                    k, v = kv.split("=")
                    info["regs"][int(k, 16)] = None if v == "--" else int(v, 16)
                started = True
            elif line == "MIC RAW END":
                ended = True
            continue
        if not started or ended or not line:
            continue
        parts = line.split()
        if len(parts) != 2 or not parts[0].isdigit():
            continue  # a log line in between
        first = int(parts[0])
        if first != len(frames):
            raise ValueError("data line for frame %d, expected %d (a line was lost)" % (first, len(frames)))
        data = bytes.fromhex(parts[1])
        if len(data) % 4:
            raise ValueError("data line for frame %d is not whole frames" % first)
        frames.extend(struct.iter_unpack("<hh", data))
    if info is None:
        raise ValueError("no MIC RAW header")
    if not ended:
        raise ValueError("no MIC RAW END: the capture was cut short")
    if len(frames) != info["frames"]:
        raise ValueError("%d frames, the header says %d" % (len(frames), info["frames"]))
    return info, frames, meta


def write_wav(path, rate, frames, channels=2):
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        if channels == 2:
            w.writeframes(b"".join(struct.pack("<hh", l, r) for l, r in frames))
        else:
            w.writeframes(b"".join(struct.pack("<h", s) for s in frames))


BANDS = [(0, 500), (500, 1000), (1000, 2000), (2000, 4000), (4000, 8000), (8000, 22050)]


def band_shares(x, rate, n=4096, windows=4):
    """Share of the energy (DC removed) in each band, from up to `windows` Hann-windowed DFTs of n samples."""
    if len(x) < n:
        n = 1 << max(6, int(math.log2(max(len(x), 64))))
        n = min(n, len(x))
    if n < 64:
        return [0.0] * len(BANDS)
    step = max(1, (len(x) - n) // max(1, windows - 1))
    acc = [0.0] * len(BANDS)
    hann = [0.5 - 0.5 * math.cos(2 * math.pi * i / (n - 1)) for i in range(n)]
    tw = [cmath.exp(-2j * math.pi * k / n) for k in range(n)]
    for start in range(0, max(1, len(x) - n + 1), step)[:windows]:
        seg = x[start:start + n]
        mean = sum(seg) / n
        seg = [(v - mean) * hann[i] for i, v in enumerate(seg)]
        spec = fft(seg, tw)
        for k in range(1, n // 2):
            f = k * rate / n
            p = abs(spec[k]) ** 2
            for b, (lo, hi) in enumerate(BANDS):
                if lo <= f < hi:
                    acc[b] += p
                    break
    total = sum(acc)
    return [a / total if total > 0 else 0.0 for a in acc]


def fft(a, tw):
    """Iterative radix-2 FFT (len(a) a power of two); tw: exp(-2 pi i k / N) for the full N."""
    n = len(a)
    if n & (n - 1):
        raise ValueError("fft length must be a power of two")
    a = [complex(v) for v in a]
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    size = 2
    full = len(tw)
    while size <= n:
        half = size // 2
        stride = full // size
        for s in range(0, n, size):
            for k in range(half):
                t = tw[k * stride] * a[s + k + half]
                a[s + k + half] = a[s + k] - t
                a[s + k] = a[s + k] + t
        size *= 2
    return a


def slot_stats(x):
    n = len(x)
    if not n:
        return 0, 0.0, 0.0
    peak = max(abs(v) for v in x)
    mean = sum(x) / n
    rms = math.sqrt(sum(v * v for v in x) / n)
    return peak, rms, mean


def db(v):
    return 20 * math.log10(v / 32768.0) if v > 0 else -99.0


def correlation(a, b):
    n = len(a)
    if not n:
        return 0.0
    ma, mb = sum(a) / n, sum(b) / n
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((y - mb) ** 2 for y in b)
    if va <= 0 or vb <= 0:
        return 0.0
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / math.sqrt(va * vb)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    with open(argv[1], "r", encoding="utf-8", errors="replace") as f:
        info, frames, meta = parse(f)
    write_wav(argv[2], info["rate"], frames)
    left = [l for l, _ in frames]
    right = [r for _, r in frames]
    if "--split" in argv[3:]:
        stem = argv[2][:-4] if argv[2].lower().endswith(".wav") else argv[2]
        write_wav(stem + "_L.wav", info["rate"], left, 1)
        write_wav(stem + "_R.wav", info["rate"], right, 1)
    for m in meta:
        if not m.startswith("MIC RAW rate "):
            print(m)
    print("frames %d at %d Hz (%.2f s), gain %d dB" % (len(frames), info["rate"], len(frames) / info["rate"], info["gain"]))
    for name, x in (("L", left), ("R", right), ("(L+R)/2", [(l + r) // 2 for l, r in frames])):
        peak, rms, mean = slot_stats(x)
        shares = band_shares(x, info["rate"])
        print("%-8s peak %6.1f rms %6.1f dBFS dc %7.1f | %s" % (
            name, db(peak), db(rms), mean,
            " ".join("%d-%d Hz %4.1f%%" % (lo, hi, 100 * s) for (lo, hi), s in zip(BANDS, shares))))
    print("L/R correlation %.3f (near -1: one slot inverted; near 0 with one slot flat: only one microphone)" %
          correlation(left, right))
    print("wrote", argv[2])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
