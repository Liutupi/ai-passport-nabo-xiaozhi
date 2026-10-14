"""Compile the actual Helix MP3 decoder and Passport frame adapter on the host.

The tiny embedded 72 ms MP3 fixture comes from the existing diagnostic sample
(China Voice live stream, 2026-10-13); no broadcast audio file, network, SDK
checkout, credentials, or serial access is required. It contains three frames,
including a bit-reservoir underflow followed by two nonzero PCM frames.
"""

import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
FIXTURE_HEX = (
    "fffb5464060011cd30e169e912c413401afc0061018744c375a7b043c0480028"
    "38008800022a39d1eec2906232cf17138f520526b5992730305dd35be49aefea"
    "db772e929ac4d3afadff45a332fc4eafea45300036dbbe000e00ff07dd7ecde7"
    "7d0ff154d60111424001b25d012227f1d1a5c39b094bcafeaa33976397dadebd"
    "439962435ac7b87239c054b46765e9faee7fff6fb556ec87f08f4011e6bd4b00"
    "300005549c0a821f436bd1a19f4d9e5dc8022a944481b27780591c962990d46e"
    "fffb5464078001ed30de69e82a6012801b0f00220086f85975a60cd24053002c"
    "fc1088e2d5ad926653899a4d1ba9341881cef6ab3a6c348126456a55afa37ad2"
    "e4fff765b67c8e94474413b88921bfeb2974a012b80043bc50c000010280fe3b"
    "657907adf480844a5000b104002009c22831313cd1f9695e053e90d587c94cbc"
    "977cbf3ad5daa4a2643521ae358342e1a8cc62029e920086b4fa20f3960ae000"
    "12013841d3005077e57edafdefffbbfe8f4d00858038180b76163bf4eabfd3be"
    "fffb5464070011d22a5b4b0911d013200add00020087643f89a7b063704600aa"
    "b4200c06f2d0c71f701cdc590aa03ed331630c5b032944b21b3a0320a322e85f"
    "fd0d4fd77f47c367496bd94b7523b342c5c001c01ca00c00507be5bf92f24eff"
    "ffdbf2c9e51b8db6e468a00b6b61f8408690db0de331e36f80f94bc741b831d9"
    "3cd87b0720890245e7ae44527f40610f7396e11077dccbea75c196bdad30a04a"
    "8380062c000641f71f811dfe80c6fda8298d4020840149b4cb435f549a44345c"
)
FIXTURE = bytes.fromhex(FIXTURE_HEX)


class Decoder(ctypes.Structure):
    _fields_ = [("handle", ctypes.c_void_p)]


class FrameInfo(ctypes.Structure):
    _fields_ = [("frame_bytes", ctypes.c_int), ("channels", ctypes.c_int),
                ("hz", ctypes.c_int), ("bitrate_kbps", ctypes.c_int)]


GUARDED_INPUT = r'''
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "nabo_radio_decoder.h"
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

int guarded_input(const uint8_t *input, size_t bytes, int *consumed) {
    if (!input || bytes>576 || !consumed) return -1;
    size_t page;
    uint8_t *mapping;
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    page=si.dwPageSize;
    mapping=VirtualAlloc(NULL,page*2,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if (!mapping) return -2;
    DWORD old_protect;
    if (!VirtualProtect(mapping+page,page,PAGE_NOACCESS,&old_protect)) {
        VirtualFree(mapping,0,MEM_RELEASE); return -2;
    }
#else
    page=(size_t)sysconf(_SC_PAGESIZE);
    mapping=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (mapping==MAP_FAILED) return -2;
    if (mprotect(mapping+page,page,PROT_NONE)) { munmap(mapping,page*2); return -2; }
#endif
    uint8_t *bounded=mapping+page-bytes;
    if (bytes) memcpy(bounded,input,bytes);
    nabo_radio_decoder_t decoder={0};
    nabo_radio_frame_info_t info={0};
    int16_t pcm[NABO_RADIO_MAX_SAMPLES_PER_FRAME+2];
    pcm[0]=12345; pcm[NABO_RADIO_MAX_SAMPLES_PER_FRAME+1]=-12345;
    int result=-3;
    if (nabo_radio_decoder_init(&decoder)) {
        result=nabo_radio_decoder_decode(&decoder,bounded,(int)bytes,pcm+1,&info);
        if (pcm[0]!=12345 || pcm[NABO_RADIO_MAX_SAMPLES_PER_FRAME+1]!=-12345) result=-4;
    }
    *consumed=info.frame_bytes;
    nabo_radio_decoder_free(&decoder);
#if defined(_WIN32)
    VirtualFree(mapping,0,MEM_RELEASE);
#else
    munmap(mapping,page*2);
#endif
    return result;
}
'''


class RadioDecoderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CC") or shutil.which("gcc")
        if not compiler:
            candidate = Path("gcc")
            compiler = str(candidate) if candidate.is_file() else None
        if not compiler:
            raise RuntimeError("host gcc is required for actual-C MP3 decoder tests")
        cls.temp = tempfile.TemporaryDirectory(prefix="nabo-radio-decoder-")
        cls.addClassCleanup(cls.temp.cleanup)
        temp = Path(cls.temp.name)
        helper = temp / "guarded.c"
        helper.write_text(GUARDED_INPUT, encoding="utf-8")
        library = temp / ("decoder.dll" if os.name == "nt" else "decoder.so")
        vendor = ROOT / "third_party/helix-mp3"
        sources = sorted(vendor.glob("*.c")) + sorted((vendor / "real").glob("*.c"))
        command = [compiler, "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
                   "-Wno-unused-but-set-variable", "-Wno-unused-parameter", "-DNABO_HELIX_PORTABLE",
                   "-shared", "-I", str(vendor / "pub"), "-I", str(vendor / "real"),
                   "-I", str(ROOT / "firmware"), *(str(path) for path in sources),
                   str(ROOT / "firmware/nabo_radio_decoder.c"), str(helper), "-o", str(library)]
        command.insert(1, "-Wl,--export-all-symbols" if os.name == "nt" else "-fPIC")
        subprocess.run(command, check=True, capture_output=True)
        cls.lib = ctypes.CDLL(str(library))
        if os.name == "nt":
            import _ctypes
            cls.addClassCleanup(_ctypes.FreeLibrary, cls.lib._handle)
        byte_ptr, pcm_ptr = ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_int16)
        cls.lib.nabo_radio_decoder_init.argtypes = [ctypes.POINTER(Decoder)]
        cls.lib.nabo_radio_decoder_init.restype = ctypes.c_bool
        cls.lib.nabo_radio_decoder_free.argtypes = [ctypes.POINTER(Decoder)]
        cls.lib.nabo_radio_decoder_decode.argtypes = [ctypes.POINTER(Decoder), byte_ptr,
                                                     ctypes.c_int, pcm_ptr, ctypes.POINTER(FrameInfo)]
        cls.lib.nabo_radio_decoder_decode.restype = ctypes.c_int
        cls.lib.guarded_input.argtypes = [byte_ptr, ctypes.c_size_t, ctypes.POINTER(ctypes.c_int)]
        cls.lib.guarded_input.restype = ctypes.c_int

    def open_decoder(self):
        decoder = Decoder()
        self.assertTrue(self.lib.nabo_radio_decoder_init(ctypes.byref(decoder)))
        self.addCleanup(self.lib.nabo_radio_decoder_free, ctypes.byref(decoder))
        return decoder

    def decode(self, decoder, data):
        incoming = (ctypes.c_uint8 * max(1, len(data)))()
        if data:
            ctypes.memmove(incoming, data, len(data))
        guarded = (ctypes.c_int16 * 2306)(*([12345] * 2306))
        destination = ctypes.cast(ctypes.byref(guarded, 2), ctypes.POINTER(ctypes.c_int16))
        info = FrameInfo()
        count = self.lib.nabo_radio_decoder_decode(ctypes.byref(decoder), incoming, len(data),
                                                   destination, ctypes.byref(info))
        self.assertEqual(guarded[0], 12345)
        self.assertEqual(guarded[-1], 12345)
        self.assertTrue(0 <= count <= 1152)
        self.assertTrue(0 <= info.frame_bytes <= len(data))
        return count, info, list(guarded[1:-1])

    def decode_stream(self, data, stride):
        decoder = self.open_decoder()
        pending = bytearray()
        result = []
        decoded = []
        for start in range(0, len(data), stride):
            pending.extend(data[start:start + stride])
            while pending:
                count, info, pcm = self.decode(decoder, bytes(pending))
                if not info.frame_bytes:
                    self.assertEqual(count, 0)
                    break
                del pending[:info.frame_bytes]
                if count:
                    result.extend(pcm[:count * info.channels])
                    decoded.append((count, info.channels, info.hz, info.bitrate_kbps))
        return result, decoded, bytes(pending)

    def test_real_fixture_decodes_two_nonzero_48k_stereo_frames(self):
        self.assertEqual(len(FIXTURE), 576)
        pcm, info, pending = self.decode_stream(FIXTURE, len(FIXTURE))
        self.assertEqual(info, [(1152, 2, 48000, 64), (1152, 2, 48000, 64)])
        self.assertEqual(pending, b"")
        self.assertEqual(len(pcm), 4608)
        self.assertGreater(max(abs(sample) for sample in pcm), 1000)

    def test_actual_pcm_matches_across_every_network_chunk_size(self):
        whole = self.decode_stream(FIXTURE, len(FIXTURE))
        for stride in (1, 2, 3, 4, 5, 7, 17, 63, 191, 192, 193, 511):
            with self.subTest(stride=stride):
                self.assertEqual(self.decode_stream(FIXTURE, stride), whole)

    def test_all_truncated_frame_prefixes_keep_input_and_pcm(self):
        for size in range(192):
            with self.subTest(size=size):
                decoder = self.open_decoder()
                count, info, pcm = self.decode(decoder, FIXTURE[:size])
                self.assertEqual(count, 0)
                self.assertEqual(info.frame_bytes, 0)
                self.assertEqual(pcm, [12345] * 2304)
                self.lib.nabo_radio_decoder_free(ctypes.byref(decoder))

    def test_short_frame_reads_and_output_writes_stay_within_guard_pages(self):
        # The byte immediately after the supplied input is an OS no-access page,
        # making an over-read fail instead of merely reading allocator padding.
        incoming = (ctypes.c_uint8 * len(FIXTURE)).from_buffer_copy(FIXTURE)
        for size in (*range(192), 192, 384, 576):
            with self.subTest(size=size):
                consumed = ctypes.c_int(-1)
                count = self.lib.guarded_input(incoming, size, ctypes.byref(consumed))
                self.assertGreaterEqual(count, 0)
                self.assertEqual(consumed.value, 0 if size < 192 else 192)

    def test_reservoir_underflow_consumes_complete_frame(self):
        decoder = self.open_decoder()
        first, info, _ = self.decode(decoder, FIXTURE[:192])
        self.assertEqual(first, 0)
        self.assertEqual(info.frame_bytes, 192)
        second, info, pcm = self.decode(decoder, FIXTURE[192:384])
        self.assertEqual(second, 1152)
        self.assertEqual((info.frame_bytes, info.hz, info.channels), (192, 48000, 2))
        self.assertGreater(max(abs(sample) for sample in pcm), 1000)

    def test_truncation_then_complete_input_keeps_reservoir_consistent(self):
        decoder = self.open_decoder()
        for size in (1, 4, 5, 6, 17, 35, 191):
            count, info, _ = self.decode(decoder, FIXTURE[:size])
            self.assertEqual((count, info.frame_bytes), (0, 0))
        count, info, _ = self.decode(decoder, FIXTURE)
        self.assertEqual((count, info.frame_bytes), (0, 192))
        count, info, _ = self.decode(decoder, FIXTURE[192:])
        self.assertEqual((count, info.frame_bytes), (1152, 192))

    def test_no_sync_preserves_last_byte_for_split_sync(self):
        decoder = self.open_decoder()
        count, info, _ = self.decode(decoder, b"garbage\xff")
        self.assertEqual((count, info.frame_bytes), (0, 7))
        actual = self.decode_stream(b"garbage" + FIXTURE, 8)
        self.assertEqual(actual, self.decode_stream(FIXTURE, len(FIXTURE)))

    def test_leading_garbage_before_truncated_frame_is_skipped_only(self):
        decoder = self.open_decoder()
        count, info, _ = self.decode(decoder, b"prefix" + FIXTURE[:100])
        self.assertEqual((count, info.frame_bytes), (0, 6))

    def test_invalid_headers_make_bounded_forward_progress(self):
        invalid = (b"\xff\xfb\x00\x64", b"\xff\xfb\xf4\x64", b"\xff\xfb\x5c\x64",
                   b"\xff\xeb\x54\x64", b"\xff\xfd\x54\x64")
        for header in invalid:
            with self.subTest(header=header.hex()):
                decoder = self.open_decoder()
                count, info, _ = self.decode(decoder, header + b"\x00" * 200)
                self.assertEqual(count, 0)
                self.assertTrue(1 <= info.frame_bytes <= 204)

    def test_null_inputs_zero_handles_and_repeated_free_are_safe(self):
        self.assertFalse(self.lib.nabo_radio_decoder_init(None))
        self.lib.nabo_radio_decoder_free(None)
        decoder = self.open_decoder()
        self.lib.nabo_radio_decoder_free(ctypes.byref(decoder))
        self.assertIsNone(decoder.handle)
        self.lib.nabo_radio_decoder_free(ctypes.byref(decoder))
        incoming = (ctypes.c_uint8 * len(FIXTURE)).from_buffer_copy(FIXTURE)
        pcm, info = (ctypes.c_int16 * 2304)(), FrameInfo()
        for args in ((None, incoming, len(FIXTURE), pcm, ctypes.byref(info)),
                     (ctypes.byref(decoder), incoming, len(FIXTURE), pcm, ctypes.byref(info))):
            self.assertEqual(self.lib.nabo_radio_decoder_decode(*args), 0)
            self.assertEqual(info.frame_bytes, 0)
        decoder = self.open_decoder()
        for args in ((ctypes.byref(decoder), None, len(FIXTURE), pcm, ctypes.byref(info)),
                     (ctypes.byref(decoder), incoming, len(FIXTURE), None, ctypes.byref(info)),
                     (ctypes.byref(decoder), incoming, len(FIXTURE), pcm, None),
                     (ctypes.byref(decoder), incoming, -1, pcm, ctypes.byref(info))):
            self.assertEqual(self.lib.nabo_radio_decoder_decode(*args), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
