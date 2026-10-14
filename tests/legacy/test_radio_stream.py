"""Exercise production ICY stripping and continuous 16 kHz PCM resampling.

The host compiles firmware/nabo_radio_stream.c directly. Tests use synthetic
audio only; no network, serial port, credentials, or downloaded MP3 is needed.
"""

import ctypes
import math
import os
from pathlib import Path
import random
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RATES = (8000, 16000, 24000, 44100, 48000)


class IcyState(ctypes.Structure):
    _fields_ = [("interval", ctypes.c_size_t), ("audio_left", ctypes.c_size_t),
                ("metadata_left", ctypes.c_size_t), ("length_next", ctypes.c_bool)]


class ResamplerState(ctypes.Structure):
    _fields_ = [("step", ctypes.c_uint32), ("position", ctypes.c_uint32),
                ("previous", ctypes.c_int16)]


def fields(state):
    return tuple(getattr(state, name) for name, _ in state._fields_)


def expected_resampled(samples, rate):
    """Evaluate the piecewise-linear signal on the Q16 16 kHz output clock.

The interpolator carries the preceding source sample across frame boundaries;
its initial preceding sample is zero, yielding one source-sample delay.
"""
    step = (rate * 65536) // 16000
    count = math.ceil((len(samples) * 65536) / step)
    result = []
    for clock in range(count):
        position = clock * step
        index, fraction = divmod(position, 65536)
        previous = samples[index - 1] if index else 0
        result.append(previous + (samples[index] - previous) * fraction // 65536)
    return result


class RadioStreamTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CC") or shutil.which("gcc")
        if not compiler:
            candidate = Path("gcc")
            if candidate.is_file():
                compiler = str(candidate)
        if not compiler:
            raise RuntimeError("host gcc is required for actual-C radio stream tests")
        cls.temp = tempfile.TemporaryDirectory(prefix="nabo-radio-stream-")
        cls.addClassCleanup(cls.temp.cleanup)
        library = Path(cls.temp.name) / ("stream.dll" if os.name == "nt" else "stream.so")
        command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-ftrapv",
                   "-shared", "-I", str(ROOT / "firmware"),
                   str(ROOT / "firmware/nabo_radio_stream.c"), "-o", str(library)]
        command.insert(1, "-Wl,--export-all-symbols" if os.name == "nt" else "-fPIC")
        subprocess.run(command, check=True, capture_output=True)
        cls.lib = ctypes.CDLL(str(library))
        if os.name == "nt":
            import _ctypes
            cls.addClassCleanup(_ctypes.FreeLibrary, cls.lib._handle)
        byte_ptr = ctypes.POINTER(ctypes.c_uint8)
        sample_ptr = ctypes.POINTER(ctypes.c_int16)
        cls.lib.nabo_icy_init.argtypes = [ctypes.POINTER(IcyState), ctypes.c_size_t]
        cls.lib.nabo_icy_feed.argtypes = [ctypes.POINTER(IcyState), byte_ptr, ctypes.c_size_t,
                                        byte_ptr, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        cls.lib.nabo_icy_feed.restype = ctypes.c_bool
        cls.lib.nabo_radio_resampler_init.argtypes = [ctypes.POINTER(ResamplerState), ctypes.c_uint]
        cls.lib.nabo_radio_resampler_init.restype = ctypes.c_bool
        cls.lib.nabo_radio_resample.argtypes = [ctypes.POINTER(ResamplerState), sample_ptr,
                                               ctypes.c_size_t, sample_ptr, ctypes.c_size_t]
        cls.lib.nabo_radio_resample.restype = ctypes.c_size_t

    def icy(self, wire, interval, lengths):
        state = IcyState()
        self.lib.nabo_icy_init(ctypes.byref(state), interval)
        output = bytearray()
        at = 0
        for length in lengths:
            chunk = wire[at:at + length]
            if not chunk:
                break
            at += len(chunk)
            incoming = (ctypes.c_uint8 * len(chunk)).from_buffer_copy(chunk)
            guarded = (ctypes.c_uint8 * (len(chunk) + 2))(*([0xa5] * (len(chunk) + 2)))
            destination = ctypes.cast(ctypes.byref(guarded, 1), ctypes.POINTER(ctypes.c_uint8))
            written = ctypes.c_size_t(12345)
            self.assertTrue(self.lib.nabo_icy_feed(ctypes.byref(state), incoming, len(chunk),
                                                   destination, len(chunk), ctypes.byref(written)))
            self.assertLessEqual(written.value, len(chunk))
            self.assertEqual(guarded[0], 0xa5)
            self.assertEqual(guarded[-1], 0xa5)
            output.extend(guarded[1:1 + written.value])
        self.assertEqual(at, len(wire), "test did not feed the entire stream")
        return bytes(output), state

    def resample(self, samples, rate, chunk_size):
        state = ResamplerState()
        self.assertTrue(self.lib.nabo_radio_resampler_init(ctypes.byref(state), rate))
        result = []
        for at in range(0, len(samples), chunk_size):
            chunk = samples[at:at + chunk_size]
            incoming = (ctypes.c_int16 * len(chunk))(*chunk)
            capacity = len(chunk) * 2 + 2
            guarded = (ctypes.c_int16 * (capacity + 2))(*([22345] * (capacity + 2)))
            destination = ctypes.cast(ctypes.byref(guarded, 2), ctypes.POINTER(ctypes.c_int16))
            count = self.lib.nabo_radio_resample(ctypes.byref(state), incoming, len(chunk),
                                                  destination, capacity)
            self.assertLessEqual(count, capacity)
            self.assertEqual(guarded[0], 22345)
            self.assertEqual(list(guarded[1 + count:]), [22345] * (capacity + 1 - count))
            result.extend(guarded[1:1 + count])
        return result, state

    def test_icy_disabled_is_exact_binary_passthrough(self):
        wire = bytes(range(256)) * 3
        for stride in (1, 7, 64, len(wire)):
            with self.subTest(stride=stride):
                result, _ = self.icy(wire, 0, [stride] * len(wire))
                self.assertEqual(result, wire)

    def test_icy_strips_metadata_at_every_network_split(self):
        metadata = b"StreamTitle='a';"
        self.assertEqual(len(metadata), 16)
        wire = b"ABCD" + b"\x01" + metadata + b"EFGH" + b"\x00" + b"IJKL"
        for split in range(1, len(wire)):
            with self.subTest(split=split):
                result, state = self.icy(wire, 4, [split, len(wire) - split])
                self.assertEqual(result, b"ABCDEFGHIJKL")
                self.assertEqual(state.metadata_left, 0)

    def test_icy_handles_single_byte_chunks_and_zero_metadata(self):
        wire = b"abc\x00def\x00ghi\x00jkl"
        result, state = self.icy(wire, 3, [1] * len(wire))
        self.assertEqual(result, b"abcdefghijkl")
        self.assertEqual(state.audio_left, 0)

    def test_icy_maximum_metadata_length_and_binary_content(self):
        metadata = bytes(range(255)) * 16
        wire = b"12" + b"\xff" + metadata + b"34\x00" + b"56"
        for stride in (1, 17, 512, len(wire)):
            with self.subTest(stride=stride):
                result, _ = self.icy(wire, 2, [stride] * len(wire))
                self.assertEqual(result, b"123456")

    def test_icy_multiple_random_chunk_boundaries(self):
        rng = random.Random(7093)
        audio = [bytes(rng.randrange(256) for _ in range(31)) for _ in range(30)]
        wire = bytearray()
        for block in audio:
            metadata_units = rng.randrange(6)
            wire.extend(block)
            wire.append(metadata_units)
            wire.extend(bytes(rng.randrange(256) for _ in range(metadata_units * 16)))
        for trial in range(20):
            with self.subTest(trial=trial):
                lengths = [rng.randrange(1, 71) for _ in range(len(wire))]
                result, _ = self.icy(bytes(wire), 31, lengths)
                self.assertEqual(result, b"".join(audio))

    def test_icy_partial_metadata_is_retained_across_calls(self):
        state = IcyState()
        self.lib.nabo_icy_init(ctypes.byref(state), 2)
        first = (ctypes.c_uint8 * 7).from_buffer_copy(b"AB\x01meta")
        out = (ctypes.c_uint8 * 7)()
        written = ctypes.c_size_t()
        self.assertTrue(self.lib.nabo_icy_feed(ctypes.byref(state), first, 7, out, 7,
                                               ctypes.byref(written)))
        self.assertEqual(bytes(out[:written.value]), b"AB")
        self.assertEqual(state.metadata_left, 12)
        second = (ctypes.c_uint8 * 14).from_buffer_copy(b"data-restxxxCD")
        second_out = (ctypes.c_uint8 * 14)()
        self.assertTrue(self.lib.nabo_icy_feed(ctypes.byref(state), second, 14, second_out, 14,
                                               ctypes.byref(written)))
        self.assertEqual(bytes(second_out[:written.value]), b"CD")

    def test_icy_rejects_short_capacity_without_mutating_state(self):
        state = IcyState()
        self.lib.nabo_icy_init(ctypes.byref(state), 2)
        incoming = (ctypes.c_uint8 * 3)(65, 66, 0)
        output = (ctypes.c_uint8 * 3)(9, 9, 9)
        before = fields(state)
        written = ctypes.c_size_t(777)
        self.assertFalse(self.lib.nabo_icy_feed(ctypes.byref(state), incoming, 3, output, 2,
                                                ctypes.byref(written)))
        self.assertEqual(fields(state), before)
        self.assertEqual(list(output), [9, 9, 9])
        self.assertEqual(written.value, 777)

    def test_icy_null_arguments_and_empty_calls(self):
        state = IcyState()
        self.lib.nabo_icy_init(ctypes.byref(state), 3)
        byte = (ctypes.c_uint8 * 1)(1)
        written = ctypes.c_size_t(42)
        for args in ((None, byte, 1, byte, 1, ctypes.byref(written)),
                     (ctypes.byref(state), None, 1, byte, 1, ctypes.byref(written)),
                     (ctypes.byref(state), byte, 1, None, 1, ctypes.byref(written)),
                     (ctypes.byref(state), byte, 1, byte, 1, None)):
            self.assertFalse(self.lib.nabo_icy_feed(*args))
        before = fields(state)
        self.assertTrue(self.lib.nabo_icy_feed(ctypes.byref(state), None, 0, None, 0,
                                               ctypes.byref(written)))
        self.assertEqual(written.value, 0)
        self.assertEqual(fields(state), before)

    def test_16k_pcm_has_continuous_one_sample_delay(self):
        samples = [100, -200, 300, -400, 500, -600, 700]
        expected = [0] + samples[:-1]
        for chunk in (1, 2, 3, len(samples)):
            with self.subTest(chunk=chunk):
                result, state = self.resample(samples, 16000, chunk)
                self.assertEqual(result, expected)
                self.assertEqual(state.previous, samples[-1])

    def test_8k_upsampling_interpolates_exact_linear_ramp(self):
        result, _ = self.resample([100, 200, 300, 400], 8000, 2)
        self.assertEqual(result, [0, 50, 100, 150, 200, 250, 300, 350])

    def test_all_rates_match_linear_signal_across_every_chunk_size(self):
        samples = [i * 23 - 13000 for i in range(1001)]
        for rate in RATES:
            expected = expected_resampled(samples, rate)
            for chunk in (1, 2, 7, 37, 576, 1001):
                with self.subTest(rate=rate, chunk=chunk):
                    result, _ = self.resample(samples, rate, chunk)
                    self.assertEqual(result, expected)

    def test_cross_frame_phase_is_continuous_over_many_frames(self):
        samples = [int(29000 * math.sin(i * 0.103)) for i in range(30000)]
        for rate in RATES:
            with self.subTest(rate=rate):
                expected = expected_resampled(samples, rate)
                actual, state = self.resample(samples, rate, 1152)
                self.assertEqual(actual, expected)
                self.assertLess(state.position, state.step)

    def test_full_scale_polarity_changes_do_not_overflow(self):
        samples = [-32768, 32767, -32768, 32767] * 288
        for rate in RATES:
            with self.subTest(rate=rate):
                actual, _ = self.resample(samples, rate, 17)
                self.assertEqual(actual, expected_resampled(samples, rate))
                self.assertTrue(all(-32768 <= sample <= 32767 for sample in actual))

    def test_constant_dc_remains_constant_after_initial_boundary(self):
        for rate in RATES:
            for sample in (-32768, -1, 0, 1, 32767):
                with self.subTest(rate=rate, sample=sample):
                    actual, _ = self.resample([sample] * 1152, rate, 5)
                    self.assertTrue(all(value == sample for value in actual[3:]))

    def test_maximum_frame_uses_bounded_capacity_and_preserves_guards(self):
        samples = [32767 if i % 2 else -32768 for i in range(1152)]
        for rate in RATES:
            with self.subTest(rate=rate):
                actual, _ = self.resample(samples, rate, 1152)
                self.assertLessEqual(len(actual), 1152 * 2)
                self.assertEqual(actual, expected_resampled(samples, rate))

    def test_reject_short_output_or_oversized_frame_without_mutation(self):
        state = ResamplerState()
        self.assertTrue(self.lib.nabo_radio_resampler_init(ctypes.byref(state), 8000))
        samples = (ctypes.c_int16 * 1153)(*([100] * 1153))
        output = (ctypes.c_int16 * 2310)(*([12345] * 2310))
        before = fields(state)
        for n, capacity in ((10, 21), (1153, 2310), (0, 2310)):
            with self.subTest(n=n, capacity=capacity):
                self.assertEqual(self.lib.nabo_radio_resample(ctypes.byref(state), samples, n,
                                                              output, capacity), 0)
                self.assertEqual(fields(state), before)
                self.assertEqual(list(output), [12345] * 2310)

    def test_invalid_rates_and_null_resampler_inputs(self):
        state = ResamplerState(123, 45, -67)
        before = fields(state)
        for rate in (0, 1, 7999, 48001, 2 ** 32 - 1):
            with self.subTest(rate=rate):
                self.assertFalse(self.lib.nabo_radio_resampler_init(ctypes.byref(state), rate))
                self.assertEqual(fields(state), before)
        self.assertFalse(self.lib.nabo_radio_resampler_init(None, 16000))
        samples = (ctypes.c_int16 * 1)(1)
        output = (ctypes.c_int16 * 4)()
        self.assertTrue(self.lib.nabo_radio_resampler_init(ctypes.byref(state), 16000))
        for args in ((None, samples, 1, output, 4),
                     (ctypes.byref(state), None, 1, output, 4),
                     (ctypes.byref(state), samples, 1, None, 4),
                     (ctypes.byref(ResamplerState()), samples, 1, output, 4)):
            self.assertEqual(self.lib.nabo_radio_resample(*args), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
