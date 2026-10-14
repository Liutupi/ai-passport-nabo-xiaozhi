"""Guarded C3 release installer/upgrader. Inspect is the default, never erase all."""
import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
from datetime import datetime
from pathlib import Path

IMAGES = {'bootloader.bin': (0, 0x8000), 'partition-table.bin': (0x8000, 0x1000),
          'xiaozhi.bin': (0x10000, 0x3f0000), 'assets.bin': (0x400000, 0x300000)}
PARTITIONS = {'nvs': (1, 2, 0x9000, 0x6000), 'phy_init': (1, 1, 0xf000, 0x1000),
              'factory': (0, 0, 0x10000, 0x3f0000), 'assets': (1, 0x82, 0x400000, 0x300000),
              'files': (1, 0x82, 0x700000, 0x100000)}

def digest(data):
    return hashlib.sha256(data).hexdigest()

def validate_package(folder):
    folder = Path(folder).resolve()
    manifest = json.loads((folder / 'manifest.json').read_text('utf-8'))
    if manifest.get('board') != 'ai-passport-nabo' or manifest.get('chip') != 'esp32c3' or manifest.get('flash_bytes') != 0x800000:
        raise ValueError('Wrong board/chip/flash manifest')
    entries = manifest.get('images', [])
    if len(entries) != 4 or {x.get('file') for x in entries} != set(IMAGES):
        raise ValueError('Package must contain exactly the four named release images')
    images = {}
    for entry in entries:
        name = entry['file']
        expected_offset, limit = IMAGES[name]
        if int(entry['offset'], 0) != expected_offset:
            raise ValueError('Unexpected flash offset for ' + name)
        path = (folder / name).resolve()
        if path.parent != folder:
            raise ValueError('Image must stay in the package directory')
        data = path.read_bytes()
        if not data or len(data) > limit or len(data) != entry['bytes'] or digest(data) != entry['sha256']:
            raise ValueError('Image size/checksum mismatch: ' + name)
        if name in ('bootloader.bin', 'xiaozhi.bin') and data[0] != 0xe9:
            raise ValueError('Invalid ESP image magic: ' + name)
        images[name] = (expected_offset, path, data)
    table = images['partition-table.bin'][2]
    actual = {}
    for pos in range(0, min(len(table), 0xc00), 32):
        if table[pos:pos+2] != b'\xaa\x50':
            break
        if pos + 32 > len(table):
            raise ValueError('Truncated partition table')
        _, kind, subtype, offset, size, label, flags = struct.unpack_from('<HBBII16sI', table, pos)
        name = label.split(b'\0')[0].decode('ascii')
        if name in actual or flags:
            raise ValueError('Duplicate/encrypted partition is unsupported')
        actual[name] = (kind, subtype, offset, size)
    if actual != PARTITIONS:
        raise ValueError('Partition map does not match this release')
    return manifest, images

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, default=Path('.'))
    parser.add_argument('--mode', choices=['inspect', 'upgrade', 'install'], default='inspect')
    parser.add_argument('--port')
    parser.add_argument('--expected-mac')
    args = parser.parse_args()
    manifest, images = validate_package(args.package)
    print('PASS: release package checksums, ESP image headers, sizes and partition map')
    if args.mode == 'inspect':
        print(json.dumps({'release_version': manifest['release_version'], 'images': manifest['images']}, indent=2))
        return
    if not args.port or not args.expected_mac or not re.fullmatch(r'(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}', args.expected_mac):
        parser.error('Writing requires --port and --expected-mac AA:BB:CC:DD:EE:FF')
    expected_mac = args.expected_mac.lower()
    private = Path('private').resolve()
    private.mkdir(parents=True, exist_ok=True)
    (private / 'backups').mkdir(exist_ok=True)
    stamp = datetime.now().strftime('%Y%m%d-%H%M%S-%f')

    def run(*arguments, name):
        command = [sys.executable, '-m', 'esptool', '--chip', 'esp32c3', '--port', args.port,
                   '--baud', '921600', '--after', 'no-reset', *map(str, arguments)]
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', errors='replace')
        output = result.stdout + result.stderr
        (private / (stamp+'-'+name+'.log')).write_text(output, 'utf-8')
        if result.returncode or expected_mac not in output.lower():
            raise RuntimeError(name+' failed or wrong device; stopped. See the private local log.')
        return output

    print('Checking device identity and security', flush=True)
    if 'Detected flash size: 8MB' not in run('flash-id', name='identity'):
        raise RuntimeError('8 MB flash required')
    security = run('get-security-info', name='security')
    if 'Secure Boot: Disabled' not in security or 'Flash Encryption: Disabled' not in security:
        raise RuntimeError('Encrypted or secure-boot device unsupported; no changes made')
    backup_path = private / 'backups' / ('before-'+args.mode+'-'+stamp+'.bin')
    print('Backing up and verifying the device before writing', flush=True)
    run('read-flash', '0', '0x800000', backup_path, name='backup')
    backup = backup_path.read_bytes()
    if len(backup) != 0x800000:
        raise RuntimeError('Incomplete backup; stopped')
    run('verify-flash', '0', backup_path, name='backup-verification')
    receipt = {'release_version': manifest['release_version'], 'mode': args.mode,
               'device_mac': expected_mac, 'port': args.port, 'backup_sha256': digest(backup),
               'backup_file': str(backup_path), 'status': 'BACKUP VERIFIED; NO WRITE YET'}
    receipt_path = private / (stamp+'-receipt.json')
    receipt_path.write_text(json.dumps(receipt, indent=2)+'\n', 'utf-8')
    if args.mode == 'upgrade':
        for name in ['bootloader.bin', 'partition-table.bin', 'assets.bin']:
            offset, _, data = images[name]
            if backup[offset:offset+len(data)] != data:
                raise RuntimeError(name+' differs; application-only upgrade refused before writing')
        selected = ['xiaozhi.bin']
        protected = [(0, 0x10000), (0x400000, 0x400000)]
    else:
        selected = list(IMAGES)
        protected = [(0x9000, 0x7000), (0x700000, 0x100000)]
    # Validate whole erase sectors against protected data, not merely image bytes.
    for name in selected:
        offset, _, data = images[name]
        end = offset + ((len(data)+4095)//4096)*4096
        if any(not (end <= start or offset >= start+length) for start, length in protected):
            raise RuntimeError('Write overlaps a preserved region')
    write = ['write-flash', '--flash-mode', 'dio', '--flash-freq', '80m', '--flash-size', '8MB']
    for name in selected:
        offset, path, _ = images[name]
        write.extend([hex(offset), path])
    print('Writing '+args.mode+' images; NVS/PHY/files preserved', flush=True)
    run(*write, name='write')
    verify = ['verify-flash']
    for name in selected:
        offset, path, _ = images[name]
        verify.extend([hex(offset), path])
    run(*verify, name='written-images')
    for index, (offset, size) in enumerate(protected):
        path = private / 'backups' / (stamp+'-preserved-'+str(index)+'.bin')
        path.write_bytes(backup[offset:offset+size])
        run('verify-flash', hex(offset), path, name='preserved-'+str(index))
    receipt['status'] = 'PASS: images and preserved regions verified; boot not yet observed'
    receipt['images'] = [entry for entry in manifest['images'] if entry['file'] in selected]
    receipt_path.write_text(json.dumps(receipt, indent=2)+'\n', 'utf-8')
    reset = subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32c3', '--port', args.port,
                            '--after', 'hard-reset', 'chip-id'], capture_output=True, text=True)
    if reset.returncode or expected_mac not in (reset.stdout+reset.stderr).lower():
        raise RuntimeError('Flash verified but automatic reset failed; power-cycle the device')
    print('PASS: write and preservation checks. Device restarted; verify screen, voice and buttons.')

if __name__ == '__main__':
    main()
