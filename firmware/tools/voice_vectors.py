#!/usr/bin/env python3
"""Golden vectors for the voice-notes codecs (docs/voice-notes/DESIGN.md 3.1, 12.1, P0 notes).

Writes tests/vectors/voice_adpcm_*.txt and voice_mulaw_*.txt from synthetic signals only (no
recording, no voice, nothing private). The panel's C++ encoder (piece A, core/voice_dsp) and the
Pi's Python decoder (piece F) must both match these files bit for bit; core/voice_proto.cpp's
voice_adpcm_decode is checked against them by tests/test_voice_proto.cpp.

  python -I tools/voice_vectors.py          write the files (from the repo root, or anywhere)
  python -I tools/voice_vectors.py --check  exit 1 if the files differ from what this script makes

Everything is integer arithmetic, so the output is the same on every machine and Python version.

The codec, frozen by P0:
  IMA ADPCM, 4 bits a sample, 16 kHz mono. A recording starts at pred 0, step index 0. It is cut in
  chunks of 474 samples (237 bytes); each chunk carries the state BEFORE its first sample (pred i16,
  step index u8), so it decodes alone, and the state runs on unbroken from chunk to chunk. Two
  samples a byte, LOW nibble first; an odd last sample leaves the high nibble 0.
  mu-law: G.711 (bias 0x84, clip 32635), one sample a byte, chunks of 237 samples, pred and step 0.

File format (ASCII, LF, '#' lines are comments):
  *_input.txt, *_decoded.txt: one 16-bit sample per 4 hex digits (two's complement), 32 a line.
  *_chunks.txt: one chunk a line: "idx pred step len hexbytes".
"""
import os
import sys

STEP = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73,
    80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494,
    544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499,
    2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]
ADPCM_CHUNK = 474
MULAW_CHUNK = 237


def clamp16(v):
    return -32768 if v < -32768 else 32767 if v > 32767 else v


# ---- IMA ADPCM ----

def adpcm_encode_sample(sample, pred, index):
    """One sample: returns (code, new_pred, new_index). The standard IMA encoder."""
    step = STEP[index]
    diff = sample - pred
    code = 0
    if diff < 0:
        code = 8
        diff = -diff
    vpdiff = step >> 3
    if diff >= step:
        code |= 4
        diff -= step
        vpdiff += step
    step >>= 1
    if diff >= step:
        code |= 2
        diff -= step
        vpdiff += step
    step >>= 1
    if diff >= step:
        code |= 1
        vpdiff += step
    pred = clamp16(pred - vpdiff if code & 8 else pred + vpdiff)
    index = min(88, max(0, index + INDEX[code]))
    return code, pred, index


def adpcm_decode_sample(code, pred, index):
    step = STEP[index]
    diff = step >> 3
    if code & 4:
        diff += step
    if code & 2:
        diff += step >> 1
    if code & 1:
        diff += step >> 2
    pred = clamp16(pred - diff if code & 8 else pred + diff)
    index = min(88, max(0, index + INDEX[code]))
    return pred, index


def adpcm_encode(samples):
    """Returns a list of chunks (pred, step, bytes)."""
    chunks = []
    pred, index = 0, 0
    for at in range(0, len(samples), ADPCM_CHUNK):
        part = samples[at:at + ADPCM_CHUNK]
        head = (pred, index)
        out = bytearray((len(part) + 1) // 2)
        for i, s in enumerate(part):
            code, pred, index = adpcm_encode_sample(s, pred, index)
            if i & 1:
                out[i >> 1] |= code << 4
            else:
                out[i >> 1] = code
        chunks.append((head[0], head[1], bytes(out)))
    return chunks


def adpcm_decode_chunk(pred, step, data, n):
    """Decodes n samples of one chunk on its own."""
    out = []
    index = step
    for i in range(n):
        b = data[i >> 1]
        code = (b >> 4) if i & 1 else (b & 0x0F)
        pred, index = adpcm_decode_sample(code, pred, index)
        out.append(pred)
    return out


# ---- G.711 mu-law ----

def mulaw_encode(s):
    sign = 0
    if s < 0:
        s = -s
        sign = 0x80
    if s > 32635:
        s = 32635
    s += 0x84
    exponent = 7
    mask = 0x4000
    while exponent > 0 and not (s & mask):
        exponent -= 1
        mask >>= 1
    mantissa = (s >> (exponent + 3)) & 0x0F
    return ~(sign | (exponent << 4) | mantissa) & 0xFF


def mulaw_decode(b):
    b = ~b & 0xFF
    sign = b & 0x80
    exponent = (b >> 4) & 7
    mantissa = b & 0x0F
    s = (((mantissa << 3) + 0x84) << exponent) - 0x84
    return -s if sign else s


# ---- Synthetic signals (integer only) ----

def isin(phase, amp):
    """A parabolic sine: phase 0-65535 is one turn; amp is the peak."""
    phase &= 0xFFFF
    half = phase & 0x7FFF
    y = (half * (32768 - half)) >> 13  # 0..32768
    y = (y * amp) >> 15
    return -y if phase & 0x8000 else y


class Lcg:
    def __init__(self, seed):
        self.x = seed & 0xFFFFFFFF

    def next(self):
        self.x = (self.x * 1664525 + 1013904223) & 0xFFFFFFFF
        return self.x

    def sample(self, amp):
        return (((self.next() >> 16) & 0xFFFF) - 32768) * amp >> 15


def sweep_signal(n=16000):
    """1 s at 16 kHz: a sweep 100 Hz -> 6 kHz with a rising and falling envelope, 50 ms noise bursts
    every 250 ms, and 100 ms of silence in the middle (the step index has to fall back)."""
    rng = Lcg(20261120)
    out = []
    phase = 0
    for i in range(n):
        freq = 100 + (5900 * i) // n
        phase = (phase + (freq * 65536) // 16000) & 0xFFFF
        env = 1500 + (28000 * (i if i < n // 2 else n - i)) // (n // 2)
        s = isin(phase, env)
        if (i % 4000) < 800:
            s += rng.sample(9000)
        if 7600 <= i < 9200:
            s = 0
        out.append(clamp16(s))
    return out


def edge_signal():
    """475 samples (an odd last chunk of 1 sample): full-scale steps, alternation, silence, +-1."""
    out = []
    out += [32767] * 20 + [-32768] * 20
    out += [32767 if i & 1 else -32768 for i in range(40)]
    out += [0] * 50
    out += [1 if i & 1 else -1 for i in range(40)]
    out += [(i * 1000) - 32768 for i in range(66)]
    out += [32767 - i * 977 for i in range(67)]
    out += [isin(i * 2000, 20000) for i in range(172)]
    assert len(out) == 475, len(out)
    return [clamp16(v) for v in out]


# ---- Files ----

def hex_samples(samples):
    lines = []
    for at in range(0, len(samples), 32):
        lines.append(''.join('%04x' % (s & 0xFFFF) for s in samples[at:at + 32]))
    return lines


def chunk_lines(chunks):
    return ['%d %d %d %d %s' % (i, p, st, len(d), d.hex()) for i, (p, st, d) in enumerate(chunks)]


def build():
    files = {}
    head = ['# Voice-notes golden vectors, made by tools/voice_vectors.py (synthetic signal only).',
            '# Do not edit: run the script. Formats in its docstring.']
    for name, sig in (('sweep', sweep_signal()), ('edge', edge_signal())):
        chunks = adpcm_encode(sig)
        dec = []
        left = len(sig)
        for p, st, d in chunks:
            n = min(ADPCM_CHUNK, left)
            dec += adpcm_decode_chunk(p, st, d, n)
            left -= n
        files['voice_adpcm_%s_input.txt' % name] = head + ['# %d samples, 16 kHz' % len(sig)] + hex_samples(sig)
        files['voice_adpcm_%s_chunks.txt' % name] = head + [
            '# IMA ADPCM, %d chunks of up to %d samples: idx pred step len hex (LOW nibble first)' %
            (len(chunks), ADPCM_CHUNK)] + chunk_lines(chunks)
        files['voice_adpcm_%s_decoded.txt' % name] = head + ['# %d samples, each chunk decoded alone' % len(dec)] + \
            hex_samples(dec)
    sig = sweep_signal()
    mchunks = []
    for at in range(0, len(sig), MULAW_CHUNK):
        mchunks.append((0, 0, bytes(mulaw_encode(s) for s in sig[at:at + MULAW_CHUNK])))
    mdec = [mulaw_decode(b) for _, _, d in mchunks for b in d]
    files['voice_mulaw_sweep_chunks.txt'] = head + [
        '# G.711 mu-law of voice_adpcm_sweep_input.txt, %d chunks of up to %d samples: idx 0 0 len hex' %
        (len(mchunks), MULAW_CHUNK)] + chunk_lines(mchunks)
    files['voice_mulaw_sweep_decoded.txt'] = head + ['# %d samples' % len(mdec)] + hex_samples(mdec)
    return {k: '\n'.join(v) + '\n' for k, v in files.items()}


def vectors_dir():
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tests', 'vectors')


def main(argv):
    out_dir = vectors_dir()
    files = build()
    if '--check' in argv:
        bad = 0
        for name, text in sorted(files.items()):
            path = os.path.join(out_dir, name)
            try:
                with open(path, 'rb') as f:
                    same = f.read() == text.encode('ascii')
            except OSError:
                same = False
            if not same:
                print('differs: %s' % name)
                bad += 1
        print('%d files checked, %d differ' % (len(files), bad))
        return 1 if bad else 0
    os.makedirs(out_dir, exist_ok=True)
    for name, text in sorted(files.items()):
        with open(os.path.join(out_dir, name), 'wb') as f:
            f.write(text.encode('ascii'))
        print('wrote %s' % name)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
