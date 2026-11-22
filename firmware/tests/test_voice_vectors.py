"""Checks the voice-notes golden vectors and their generator (tools/voice_vectors.py).

  python -I -m unittest discover -s tests -p "test_voice_*.py"
"""
import importlib.util
import os
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
_spec = importlib.util.spec_from_file_location('voice_vectors', os.path.join(ROOT, 'tools', 'voice_vectors.py'))
vv = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(vv)
VEC = os.path.join(ROOT, 'tests', 'vectors')


def read_samples(name):
    out = []
    with open(os.path.join(VEC, name), 'r', encoding='ascii') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            for at in range(0, len(line), 4):
                v = int(line[at:at + 4], 16)
                out.append(v - 65536 if v >= 32768 else v)
    return out


def read_chunks(name):
    out = []
    with open(os.path.join(VEC, name), 'r', encoding='ascii') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            idx, pred, step, n, data = line.split()
            data = bytes.fromhex(data)
            assert len(data) == int(n)
            out.append((int(idx), int(pred), int(step), data))
    return out


class VoiceVectors(unittest.TestCase):
    def test_files_are_current(self):
        for name, text in vv.build().items():
            with open(os.path.join(VEC, name), 'rb') as f:
                self.assertEqual(f.read(), text.encode('ascii'), name)

    def test_files_ascii_lf(self):
        for name in os.listdir(VEC):
            with open(os.path.join(VEC, name), 'rb') as f:
                b = f.read()
            self.assertNotIn(b'\r', b, name)
            self.assertTrue(all(c < 0x80 for c in b), name)

    def test_adpcm_chunks_decode_alone(self):
        for sig in ('sweep', 'edge'):
            inp = read_samples('voice_adpcm_%s_input.txt' % sig)
            chunks = read_chunks('voice_adpcm_%s_chunks.txt' % sig)
            dec = read_samples('voice_adpcm_%s_decoded.txt' % sig)
            self.assertEqual(len(dec), len(inp))
            # Chunk headers are the running decoder state, so decoding all in one go gives the same.
            pred, index, run = 0, 0, []
            left = len(inp)
            for i, (idx, p, st, data) in enumerate(chunks):
                self.assertEqual(idx, i)
                self.assertEqual((p, st), (pred, index), 'chunk %d header' % i)
                n = min(vv.ADPCM_CHUNK, left)
                self.assertEqual(len(data), (n + 1) // 2)
                for k in range(n):
                    code = (data[k >> 1] >> 4) if k & 1 else (data[k >> 1] & 15)
                    pred, index = vv.adpcm_decode_sample(code, pred, index)
                    run.append(pred)
                left -= n
            self.assertEqual(run, dec)

    def test_odd_last_chunk_high_nibble_zero(self):
        chunks = read_chunks('voice_adpcm_edge_chunks.txt')
        self.assertEqual(len(chunks), 2)
        self.assertEqual(len(chunks[-1][3]), 1)
        self.assertEqual(chunks[-1][3][0] >> 4, 0)

    def test_adpcm_quality(self):
        inp = read_samples('voice_adpcm_sweep_input.txt')
        dec = read_samples('voice_adpcm_sweep_decoded.txt')
        sig = sum(s * s for s in inp)
        err = sum((a - b) ** 2 for a, b in zip(inp, dec))
        self.assertGreater(sig, 0)
        # 4-bit IMA on a 6 kHz sweep and noise: far better than 10:1 in power.
        self.assertLess(err * 10, sig)

    def test_mulaw(self):
        inp = read_samples('voice_adpcm_sweep_input.txt')
        chunks = read_chunks('voice_mulaw_sweep_chunks.txt')
        dec = read_samples('voice_mulaw_sweep_decoded.txt')
        flat = [b for _, p, st, d in chunks for b in d]
        self.assertTrue(all(p == 0 and st == 0 for _, p, st, _ in chunks))
        self.assertEqual(len(flat), len(inp))
        self.assertEqual([vv.mulaw_decode(b) for b in flat], dec)
        for s, d in zip(inp, dec):
            self.assertLessEqual(abs(s - d), max(64, abs(s) // 16))
        self.assertEqual(vv.mulaw_encode(0), 0xFF)
        self.assertEqual(vv.mulaw_decode(0xFF), 0)


if __name__ == '__main__':
    unittest.main()
