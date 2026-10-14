"""Use caller-selected or PATH compilers; support Windows and POSIX hosts."""
import _ctypes
import os
import shutil

CC = os.environ.get('CC') or shutil.which('gcc') or 'gcc'
CXX = os.environ.get('CXX') or shutil.which('g++') or 'g++'
SHARED_EXPORT = '-Wl,--export-all-symbols' if os.name == 'nt' else '-fPIC'

def unload_library(handle):
    if os.name == 'nt':
        _ctypes.FreeLibrary(handle)
    else:
        _ctypes.dlclose(handle)
