from host_tools import CC, SHARED_EXPORT, unload_library
"""Compile the production rhythm model and synthesizer; check timing, judging and audio."""
import ctypes
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BOARD = ROOT / 'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
RATE, PHRASES, STEPS, MODES, LEVELS = 16000, 4, 8, 3, 4
PERFECT, GOOD, MISS = 1, 2, 3


class Game(ctypes.Structure):
    _fields_ = [('mode', ctypes.c_uint8), ('level', ctypes.c_uint8), ('bpm', ctypes.c_uint16),
                ('two_keys', ctypes.c_bool * PHRASES), ('mask', ctypes.c_uint8 * PHRASES),
                ('high', ctypes.c_uint8 * PHRASES), ('judge', (ctypes.c_uint8 * STEPS) * PHRASES),
                ('offset_ms', (ctypes.c_int16 * STEPS) * PHRASES),
                ('tap_at', (ctypes.c_uint32 * STEPS) * PHRASES)] + [
        (n, ctypes.c_uint16) for n in ['notes', 'perfect', 'good', 'miss', 'extra']]


class Position(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int) for n in ['stage', 'phrase', 'bar', 'step', 'beat', 'bar_permille']]


class Tap(ctypes.Structure):
    _fields_ = [('judge', ctypes.c_int), ('phrase', ctypes.c_int), ('step', ctypes.c_int),
                ('offset_ms', ctypes.c_int), ('wrong_key', ctypes.c_bool)]


class Voice(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint32) for n in ['phase', 'step', 'delay', 'remaining']] + [
        ('amp', ctypes.c_int32), ('decay', ctypes.c_int32), ('age', ctypes.c_uint16), ('harmonic', ctypes.c_uint8)]


class Synth(ctypes.Structure):
    _fields_ = [('voices', Voice * 8)]


temp = tempfile.TemporaryDirectory()
library = Path(temp.name) / 'rhythm.dll'
subprocess.run([CC, '-std=gnu11', '-O2', '-shared', SHARED_EXPORT, '-Wall', '-Wextra', '-Werror', '-I', str(BOARD),
                str(BOARD / 'nabo_rhythm_model.c'), str(BOARD / 'nabo_rhythm_sound.c'), '-o', str(library)],
               capture_output=True, check=True)
lib = ctypes.CDLL(str(library))
G = ctypes.POINTER(Game)
lib.nabo_rhythm_start.argtypes = [G, ctypes.c_int, ctypes.c_int, ctypes.c_uint32]
lib.nabo_rhythm_start.restype = None
lib.nabo_rhythm_step_sample.argtypes = [G, ctypes.c_int]
lib.nabo_rhythm_step_sample.restype = ctypes.c_uint32
lib.nabo_rhythm_length.argtypes = [G]
lib.nabo_rhythm_length.restype = ctypes.c_uint32
lib.nabo_rhythm_events.argtypes = [G, ctypes.c_int]
lib.nabo_rhythm_events.restype = ctypes.c_uint
lib.nabo_rhythm_locate.argtypes = [G, ctypes.c_uint32, ctypes.POINTER(Position)]
lib.nabo_rhythm_locate.restype = None
lib.nabo_rhythm_tap.argtypes = [G, ctypes.c_uint32, ctypes.c_int]
lib.nabo_rhythm_tap.restype = Tap
lib.nabo_rhythm_update.argtypes = [G, ctypes.c_uint32]
lib.nabo_rhythm_percent.argtypes = [G]
lib.nabo_rhythm_grade.argtypes = [ctypes.c_int]
lib.nabo_rhythm_grade.restype = ctypes.c_char
lib.nabo_rhythm_sound_reset.argtypes = [ctypes.POINTER(Synth)]
lib.nabo_rhythm_sound_play.argtypes = [ctypes.POINTER(Synth), ctypes.c_int, ctypes.c_uint32]
lib.nabo_rhythm_sound_fill.argtypes = [ctypes.POINTER(Synth), ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t]


def start(mode, level, seed=7):
    game = Game()
    lib.nabo_rhythm_start(ctypes.byref(game), mode, level, seed)
    return game


def notes(game):
    for p in range(PHRASES):
        for s in range(STEPS):
            if game.mask[p] >> s & 1:
                at = lib.nabo_rhythm_step_sample(ctypes.byref(game), (2 + 2 * p) * STEPS + s)
                yield p, s, at, game.high[p] >> s & 1


class Tests(unittest.TestCase):
    def test_every_level_is_playable_and_timed(self):
        for mode in range(MODES):
            for level in range(LEVELS):
                game = start(mode, level)
                self.assertGreaterEqual(game.bpm, 88)
                self.assertLessEqual(game.bpm, 120)
                self.assertGreater(game.notes, 4 * 2)
                # Count-in plus four listen/response bars lasts 30 s at most.
                self.assertLess(lib.nabo_rhythm_length(ctypes.byref(game)) / RATE, 30)
                for p in range(PHRASES):
                    self.assertTrue(game.mask[p])
                    self.assertEqual(game.high[p] & ~game.mask[p], 0)
                if mode == 1:
                    self.assertTrue(all(game.two_keys))
                if mode == 0:
                    self.assertFalse(any(game.two_keys))

    def test_demo_events_match_response_notes(self):
        game = start(1, 2)
        for p, s, _, high in notes(game):
            events = lib.nabo_rhythm_events(ctypes.byref(game), (1 + 2 * p) * STEPS + s)
            self.assertTrue(events & (8 if high else 4))
            # Response bars only carry the metronome.
            self.assertFalse(lib.nabo_rhythm_events(ctypes.byref(game), (2 + 2 * p) * STEPS + s) & 12)
        self.assertEqual(lib.nabo_rhythm_events(ctypes.byref(game), 72), 32)

    def test_perfect_run_scores_s(self):
        game = start(1, 3)
        for _, _, at, high in notes(game):
            result = lib.nabo_rhythm_tap(ctypes.byref(game), at + 400, high)
            self.assertEqual(result.judge, PERFECT)
            self.assertEqual(result.offset_ms, 25)
        lib.nabo_rhythm_update(ctypes.byref(game), lib.nabo_rhythm_length(ctypes.byref(game)))
        self.assertEqual((game.perfect, game.miss, game.extra), (game.notes, 0, 0))
        self.assertEqual(lib.nabo_rhythm_percent(ctypes.byref(game)), 100)
        self.assertEqual(lib.nabo_rhythm_grade(100), b'S')

    def test_windows_wrong_key_and_misses(self):
        game = start(1, 0)
        p, s, at, high = next(notes(game))
        wrong = lib.nabo_rhythm_tap(ctypes.byref(game), at, 1 - high)
        self.assertTrue(wrong.wrong_key)
        self.assertEqual(game.extra, 1)
        good = lib.nabo_rhythm_tap(ctypes.byref(game), at - 100 * 16, high)
        self.assertEqual((good.judge, good.offset_ms), (GOOD, -100))
        # A late stray tap inside a response bar costs accuracy, not a note.
        all_notes = list(notes(game))
        stray = all_notes[-1][2] + 400 * 16
        self.assertEqual(lib.nabo_rhythm_tap(ctypes.byref(game), stray, 0).judge, 0)
        self.assertEqual(game.extra, 2)
        changed = lib.nabo_rhythm_update(ctypes.byref(game), lib.nabo_rhythm_length(ctypes.byref(game)))
        self.assertEqual(changed, game.notes - 1)
        self.assertEqual(game.judge[p][s], GOOD)
        self.assertEqual(lib.nabo_rhythm_grade(lib.nabo_rhythm_percent(ctypes.byref(game))), b'D')

    def test_single_key_levels_accept_either_key(self):
        game = start(0, 1)
        _, _, at, _ = next(notes(game))
        self.assertEqual(lib.nabo_rhythm_tap(ctypes.byref(game), at, 1).judge, PERFECT)

    def test_listen_taps_are_free(self):
        game = start(0, 0)
        listen = lib.nabo_rhythm_step_sample(ctypes.byref(game), STEPS + 2)
        self.assertEqual(lib.nabo_rhythm_tap(ctypes.byref(game), listen, 0).judge, 0)
        self.assertEqual(game.extra, 0)

    def test_locate_stages(self):
        game = start(2, 1)
        pos = Position()
        expected = [(0, -1), (1, 0), (2, 0), (1, 1), (2, 1), (1, 2), (2, 2), (1, 3), (2, 3), (3, 3)]
        for bar, (stage, phrase) in enumerate(expected):
            at = lib.nabo_rhythm_step_sample(ctypes.byref(game), bar * STEPS) + 10
            lib.nabo_rhythm_locate(ctypes.byref(game), at, ctypes.byref(pos))
            self.assertEqual((pos.stage, pos.phrase, pos.bar), (stage, phrase, bar))

    def test_synth_is_bounded_and_decays(self):
        synth = Synth()
        lib.nabo_rhythm_sound_reset(ctypes.byref(synth))
        for sound in range(13):
            lib.nabo_rhythm_sound_play(ctypes.byref(synth), sound, sound * 37)
        pcm = (ctypes.c_int16 * 320)()
        peak, energy = 0, []
        for _ in range(150):
            lib.nabo_rhythm_sound_fill(ctypes.byref(synth), pcm, 320)
            peak = max(peak, max(abs(v) for v in pcm))
            energy.append(sum(abs(v) for v in pcm))
        self.assertGreater(peak, 8000)
        self.assertEqual(energy[-1], 0)
        # Note onsets ramp in: the first sample of a new pluck is near silent.
        lib.nabo_rhythm_sound_reset(ctypes.byref(synth))
        lib.nabo_rhythm_sound_play(ctypes.byref(synth), 3, 0)
        lib.nabo_rhythm_sound_fill(ctypes.byref(synth), pcm, 320)
        self.assertLess(abs(pcm[1]), 200)
        self.assertGreater(max(abs(v) for v in pcm), 9000)


if __name__ == '__main__':
    try:
        unittest.main(verbosity=1, exit=False)
    finally:
        unload_library(lib._handle)
        temp.cleanup()
