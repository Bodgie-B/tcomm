"""Checks tools/mic_raw_wav.py, which rebuilds a WAV from the panel's serial `mic raw` lines.

  python -I -m unittest discover -s tests -p "test_voice_*.py"
"""
import importlib.util
import math
import os
import struct
import tempfile
import unittest
import wave

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
_spec = importlib.util.spec_from_file_location('mic_raw_wav', os.path.join(ROOT, 'tools', 'mic_raw_wav.py'))
mr = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mr)


def capture_lines(frames, rate=44100, gain=24):
    """The lines board::mic_raw_dump prints for these (L, R) frames."""
    regs = ' '.join('%02X=%02X' % (r, (r * 7) & 0xFF) for r in range(0x4D))
    out = ['MIC RAW I2S port 1 std philips, tdm 0, rate 44100, mclk x256 (11289600 Hz), slot mode stereo',
           'MIC RAW ES7210 01 clk off 00, 02 mainclk C1',
           'MIC RAW rate %d ch 2 bits 16 frames %d gain %d regs %s' % (rate, len(frames), gain, regs)]
    for f in range(0, len(frames), 64):
        blk = frames[f:f + 64]
        out.append('%d %s' % (f, b''.join(struct.pack('<hh', l, r) for l, r in blk).hex()))
    out.append('MIC RAW END')
    out.append('MIC RAW STATS L peak -20.0')
    return [l + '\r\n' for l in out]


def tones(n, f_left, f_right, amp=8000, rate=44100):
    return [(int(amp * math.sin(2 * math.pi * f_left * i / rate)), int(-amp * math.sin(2 * math.pi * f_right * i / rate)))
            for i in range(n)]


class MicRawTest(unittest.TestCase):
    def test_parse_round_trip(self):
        frames = tones(4410, 300, 3000)
        lines = capture_lines(frames)
        lines.insert(5, 'E (1234) some log line\r\n')  # a log line in between is ignored
        info, got, meta = mr.parse(lines)
        self.assertEqual(got, frames)
        self.assertEqual(info['rate'], 44100)
        self.assertEqual(info['gain'], 24)
        self.assertEqual(info['regs'][0x02], (2 * 7) & 0xFF)
        self.assertEqual(len(info['regs']), 0x4D)
        self.assertTrue(any(m.startswith('MIC RAW I2S') for m in meta))

    def test_broken_captures_are_refused(self):
        frames = tones(300, 300, 3000)
        lines = capture_lines(frames)
        with self.assertRaises(ValueError):
            mr.parse([l for l in lines if not l.startswith('MIC RAW END')])
        with self.assertRaises(ValueError):
            mr.parse([l for l in lines if not l.startswith('64 ')])  # a lost data line
        with self.assertRaises(ValueError):
            mr.parse(lines[3:])  # no header

    def test_bands_find_the_treble(self):
        frames = tones(16384, 300, 3000)
        left = [l for l, _ in frames]
        right = [r for _, r in frames]
        sl = mr.band_shares(left, 44100)
        sr = mr.band_shares(right, 44100)
        self.assertGreater(sl[0], 0.95)  # 300 Hz: 0-500
        self.assertGreater(sr[3], 0.95)  # 3 kHz: 2-4 kHz
        self.assertAlmostEqual(sum(sl), 1.0, places=6)

    def test_wav_and_split(self):
        frames = tones(1000, 440, 880)
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, 'cap.txt')
            with open(src, 'w', newline='') as f:
                f.writelines(capture_lines(frames))
            out = os.path.join(d, 'cap.wav')
            self.assertEqual(mr.main(['mic_raw_wav.py', src, out, '--split']), 0)
            with wave.open(out, 'rb') as w:
                self.assertEqual((w.getnchannels(), w.getframerate(), w.getnframes()), (2, 44100, 1000))
                first = struct.unpack('<hh', w.readframes(1))
                self.assertEqual(first, frames[0])
            with wave.open(os.path.join(d, 'cap_R.wav'), 'rb') as w:
                self.assertEqual((w.getnchannels(), w.getnframes()), (1, 1000))

    def test_correlation_sees_an_inverted_slot(self):
        x = [int(5000 * math.sin(i / 7.0)) for i in range(2000)]
        self.assertLess(mr.correlation(x, [-v for v in x]), -0.99)
        self.assertGreater(mr.correlation(x, x), 0.99)
        self.assertEqual(mr.correlation(x, [0] * len(x)), 0.0)


if __name__ == '__main__':
    unittest.main()
