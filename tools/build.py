"""Build the frozen release configuration without any author-specific paths."""
import os
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
idf = os.environ.get('IDF_PATH')
if not idf or not (Path(idf) / 'tools/idf.py').is_file():
    raise SystemExit('Initialize the ESP-IDF 6.0.2 environment first (IDF_PATH is missing).')
source = root / 'xiaozhi-esp32'
build = source / 'build-release'
defaults = root / 'configs/release-v1.3.0.sdkconfig'
# idf.py only applies defaults to keys missing from an existing sdkconfig, so
# a build directory configured from an older release file must start over.
import hashlib
marker = build / 'release-defaults.txt'
stamp = defaults.name + ' ' + hashlib.sha256(defaults.read_bytes()).hexdigest()
if (build / 'sdkconfig').exists() and (not marker.exists() or marker.read_text().strip() != stamp):
    (build / 'sdkconfig').unlink()
    print('Release defaults changed; regenerating', build / 'sdkconfig')
build.mkdir(exist_ok=True)
marker.write_text(stamp + '\n')
command = [sys.executable, str(Path(idf) / 'tools/idf.py'), '-B', str(build),
           '-D', 'SDKCONFIG=' + str(build / 'sdkconfig'),
           '-D', 'SDKCONFIG_DEFAULTS=' + str(defaults),
           '-D', 'IDF_TARGET=esp32c3', 'build']
if os.name == 'nt':
    command[-1:-1] = ['-D', 'CMAKE_NINJA_FORCE_RESPONSE_FILE=ON']
subprocess.run(command, cwd=source, check=True)
print('Built:', build / 'xiaozhi.bin')
