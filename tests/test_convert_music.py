#!/usr/bin/env python3
"""ROM-free regression for tools/convert_music.py.

Generates a synthetic PCM16 WAV, converts it to the raw S16BE stream consumed by
sat_disc_stream_music_start, and checks the bytes, level and generated header.
No game ROM or captured audio is used.
"""
import math
import os
from array import array
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "convert_music.py"


def read_be(path: Path):
    a = array("h")
    a.frombytes(path.read_bytes())
    if os.sys.byteorder != "big":
        a.byteswap()  # file is big-endian -> native values
    return a


def write_wav(path: Path, samples, rate: int, channels: int):
    a = array("h", samples)
    if os.sys.byteorder != "little":
        a.byteswap()  # native -> little-endian PCM bytes
    with wave.open(str(path), "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(a.tobytes())


class ConvertMusicTest(unittest.TestCase):
    def run_convert(self, wav: Path, out: Path, hdr: Path, *extra):
        subprocess.run(
            [sys.executable, str(SCRIPT), "--input", str(wav),
             "--output-audio", str(out), "--output-header", str(hdr),
             "--name", "TEST", *extra],
            check=True, capture_output=True, text=True,
        )

    def test_identity_rate_and_header(self):
        rate, channels = 8000, 1
        n = 4000
        samples = [int(20000 * math.sin(2 * math.pi * 440 * i / rate)) for i in range(n)]
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            wav, out, hdr = d / "in.wav", d / "out.bin", d / "out.h"
            write_wav(wav, samples, rate, channels)
            self.run_convert(wav, out, hdr, "--sample-rate", str(rate),
                             "--fade-ms", "0")
            got = read_be(out)
            self.assertEqual(len(got), n)
            for i in range(n):
                self.assertLessEqual(abs(got[i] - samples[i]), 1)
            text = hdr.read_text()
            self.assertIn("#define KOF_TEST_SAMPLE_RATE 8000u", text)
            self.assertIn(f"#define KOF_TEST_FRAMES {n}u", text)
            self.assertIn(f"#define KOF_TEST_CHANNELS {channels}u", text)
            self.assertIn(f"#define KOF_TEST_BYTES {n * channels * 2}u", text)

    def test_slice_and_resample_stereo(self):
        rate, channels = 8000, 2
        n = 8000
        # left channel: ramp; right channel: negative ramp
        samples = []
        for i in range(n):
            v = (i % 2000) - 1000
            samples.append(v)          # left
            samples.append(-v)         # right
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            wav, out, hdr = d / "in.wav", d / "out.bin", d / "out.h"
            write_wav(wav, samples, rate, channels)
            # take 250 ms (2000 frames) starting at 250 ms, resample 8000 -> 4000
            self.run_convert(wav, out, hdr, "--sample-rate", "4000",
                             "--start-ms", "250", "--duration-ms", "250",
                             "--fade-ms", "0")
            got = read_be(out)
            self.assertEqual(len(got) % channels, 0)
            frames = len(got) // channels
            self.assertAlmostEqual(frames, 1000, delta=4)
            self.assertIn("#define KOF_TEST_SAMPLE_RATE 4000u", hdr.read_text())
            # channels stay interleaved: left/right remain opposite in the body
            body = got[8:-8]
            ok = all(body[i] == -body[i + 1] for i in range(0, len(body) - 1, 2))
            self.assertTrue(ok)


if __name__ == "__main__":
    unittest.main()