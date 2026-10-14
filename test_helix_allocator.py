from host_tools import CC, CXX, SHARED_EXPORT, unload_library
"""Exercise the C3 decoder allocator against a fragmented heap and cleanup paths."""
import ctypes
import subprocess
import tempfile
import _ctypes
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BOARD = ROOT/'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
SHIM = r'''
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "coder.h"
static _Alignas(max_align_t) unsigned char arena[3][24000];
static size_t limits[3], used[3];
static void* allocations[32];
static int live, bad;
void reset_heap(size_t a,size_t b,size_t c) {
    limits[0]=a;limits[1]=b;limits[2]=c;
    memset(used,0,sizeof(used));memset(allocations,0,sizeof(allocations));live=bad=0;
}
void* arena_alloc(size_t size) {
    size=(size+15)&~(size_t)15;
    for(int i=0;i<3;i++) if(size<=limits[i]-used[i]) {
        void* p=arena[i]+used[i];used[i]+=size;
        for(int j=0;j<32;j++) if(!allocations[j]) {allocations[j]=p;live++;return p;}
    }
    return 0;
}
void arena_free(void* p) {
    if(!p)return;
    for(int i=0;i<32;i++) if(allocations[i]==p){allocations[i]=0;live--;return;}
    bad++;
}
int live_allocations(void){return live;}
int bad_frees(void){return bad;}
void* open_decoder(void){return AllocateBuffers();}
void close_decoder(void* p){if(p)FreeBuffers((MP3DecInfo*)p);}
'''
with tempfile.TemporaryDirectory() as temp:
    folder = Path(temp)
    (folder/'shim.c').write_text(SHIM)
    for label, source in [('old', BOARD/'helix/real/buffers.c'), ('new', BOARD/'nabo_helix_buffers.c')]:
        obj, dll = folder/(label+'.o'), folder/(label+'.dll')
        common = [CC, '-std=c11', '-O2',
                  '-I', str(BOARD/'helix/real'), '-I', str(BOARD/'helix/pub')]
        subprocess.run(common+['-Dmalloc=arena_alloc', '-Dfree=arena_free', '-c', str(source), '-o', str(obj)],check=True)
        subprocess.run(common+['-shared', SHARED_EXPORT, str(obj), str(folder/'shim.c'), '-o', str(dll)],check=True)
        lib = ctypes.CDLL(str(dll))
        lib.reset_heap.argtypes = [ctypes.c_size_t]*3
        lib.open_decoder.restype = ctypes.c_void_p
        lib.close_decoder.argtypes = [ctypes.c_void_p]
        for limits, expected in [((17408,7680,7000), label=='new'), ((24000,24000,24000), True), ((7500,7500,7500), False)]:
            lib.reset_heap(*limits)
            decoder = lib.open_decoder()
            assert bool(decoder)==expected, (label,limits)
            lib.close_decoder(decoder)
            assert lib.live_allocations()==0 and lib.bad_frees()==0, (label,limits)
        unload_library(lib._handle)
print('PASS: fragmented heap succeeds with new allocator; healthy/failure cleanup preserved')
