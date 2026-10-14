from host_tools import CC, CXX, SHARED_EXPORT, unload_library
"""Verify that the smaller C3 radio scratch buffer preserves PCM across frames."""
import ctypes
import math
import subprocess
import tempfile
import _ctypes
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BOARD = ROOT/'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
class Resampler(ctypes.Structure):
    _fields_ = [('step', ctypes.c_uint32), ('position', ctypes.c_uint32), ('previous', ctypes.c_int16)]
with tempfile.TemporaryDirectory() as temp:
    dll = Path(temp)/'stream.dll'
    subprocess.run([CC, '-std=c11', '-shared', '-O2',
                    SHARED_EXPORT, '-Wall', '-Wextra', '-Werror', '-I', str(BOARD),
                    str(BOARD/'nabo_radio_stream.c'), '-o', str(dll)], check=True)
    lib = ctypes.CDLL(str(dll))
    lib.nabo_radio_resampler_init.argtypes = [ctypes.POINTER(Resampler), ctypes.c_uint]
    lib.nabo_radio_resample.argtypes = [ctypes.POINTER(Resampler), ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t,
                                      ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t]
    lib.nabo_radio_resample.restype = ctypes.c_size_t
    for rate in [8000, 11025, 16000, 22050, 32000, 44100, 48000]:
        whole, sliced = Resampler(), Resampler()
        lib.nabo_radio_resampler_init(ctypes.byref(whole), rate)
        lib.nabo_radio_resampler_init(ctypes.byref(sliced), rate)
        for frame in range(16):
            samples = [round(14000*math.sin((frame*1152+i)*2*math.pi*440/rate)) for i in range(1152)]
            data = (ctypes.c_int16*1152)(*samples)
            expected = (ctypes.c_int16*2306)()
            count = lib.nabo_radio_resample(ctypes.byref(whole), data, 1152, expected, 2306)
            actual = []
            for offset in range(0, 1152, 160):
                piece = samples[offset:offset+160]
                source = (ctypes.c_int16*len(piece))(*piece)
                dest = (ctypes.c_int16*322)()
                n = lib.nabo_radio_resample(ctypes.byref(sliced), source, len(piece), dest, 322)
                actual.extend(dest[:n])
            assert actual == list(expected[:count]), (rate, frame)
    unload_library(lib._handle)
print('PASS: exact PCM at 7 rates across 112 consecutive frames using 644-byte scratch')
