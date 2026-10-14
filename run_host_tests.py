from host_tools import CC, CXX, SHARED_EXPORT, unload_library
"""Run upstream tests with Windows-safe temporary-directory cleanup."""
import os
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent / 'xiaozhi-esp32'
original_exit = tempfile.TemporaryDirectory.__exit__

def windows_exit(self, *args):
    # Upstream tests restore cwd in a finally after leaving the temp context.
    # Windows cannot remove a directory while it is the process cwd.
    if os.name == 'nt' and Path.cwd().is_relative_to(Path(self.name)):
        os.chdir(ROOT)
    return original_exit(self, *args)

tempfile.TemporaryDirectory.__exit__ = windows_exit
os.chdir(ROOT)
suite = unittest.defaultTestLoader.discover(str(ROOT / 'scripts/tests'))
result = unittest.TextTestRunner(verbosity=2).run(suite)
raise SystemExit(0 if result.wasSuccessful() else 1)
