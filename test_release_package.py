"""Reject corrupt/unsafe install packages without touching a serial port."""
import json
import struct
import tempfile
import unittest
from pathlib import Path
from tools.flash_release import IMAGES, PARTITIONS, digest, validate_package

class ReleasePackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        table = b''.join(struct.pack('<HBBII16sI', 0x50aa, kind, subtype, offset, size,
                                     name.encode().ljust(16,b'\0'), 0)
                         for name, (kind, subtype, offset, size) in PARTITIONS.items()) + b'\xff'*32
        self.manifest = {'release_version': '1.0.0', 'board':'ai-passport-nabo', 'chip':'esp32c3',
                         'flash_bytes':0x800000, 'images':[]}
        for name, (offset, _) in IMAGES.items():
            data = table if name == 'partition-table.bin' else b'\xe9'+bytes(range(64))
            (self.root/name).write_bytes(data)
            self.manifest['images'].append({'file':name, 'offset':hex(offset), 'bytes':len(data), 'sha256':digest(data)})
        self.save()

    def save(self):
        (self.root/'manifest.json').write_text(json.dumps(self.manifest))

    def test_valid_package(self):
        self.assertEqual(len(validate_package(self.root)[1]), 4)

    def test_corrupt_binary(self):
        (self.root/'xiaozhi.bin').write_bytes(b'\xe9broken')
        with self.assertRaises(ValueError): validate_package(self.root)

    def test_path_traversal_is_rejected(self):
        self.manifest['images'][2]['file'] = '../xiaozhi.bin'; self.save()
        with self.assertRaises(ValueError): validate_package(self.root)

    def test_write_to_nvs_is_rejected(self):
        self.manifest['images'][2]['offset'] = '0x9000'; self.save()
        with self.assertRaises(ValueError): validate_package(self.root)

    def test_wrong_board_is_rejected(self):
        self.manifest['chip'] = 'esp32s3'; self.save()
        with self.assertRaises(ValueError): validate_package(self.root)

    def test_overlarge_app_is_rejected(self):
        data = b'\xe9'+b'\0'*0x3f0000
        (self.root/'xiaozhi.bin').write_bytes(data)
        self.manifest['images'][2].update(bytes=len(data),sha256=digest(data)); self.save()
        with self.assertRaises(ValueError): validate_package(self.root)

    def test_changed_partition_is_rejected_even_with_valid_hash(self):
        path = self.root/'partition-table.bin'
        data = bytearray(path.read_bytes()); struct.pack_into('<I', data, 4, 0xa000)
        path.write_bytes(data)
        self.manifest['images'][1]['sha256'] = digest(data); self.save()
        with self.assertRaises(ValueError): validate_package(self.root)

if __name__ == '__main__':
    unittest.main()
