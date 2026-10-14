from host_tools import CC, CXX, SHARED_EXPORT, unload_library
"""Check production conversion at PCM extrema and every chunk boundary."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as folder:
    source = Path(folder) / 'stereo.cpp'
    source.write_text(r'''
#include "stereo_frames.h"
#include <algorithm>
#include <cassert>
#include <vector>
int main() {
    for (int n : {1, 159, 160, 161, 240, 320, 960, 1440, 2880}) {
        std::vector<int16_t> mono(n), captured, mixed(n), source(n*2);
        for (int i=0;i<n;++i) {
            mono[i]=(int16_t)(i*7193);
            source[2*i]=mono[i]; source[2*i+1]=(int16_t)(i*271-32768);
        }
        int16_t scratch[kStereoChunkFrames*2];
        for (int at=0;at<n;at+=kStereoChunkFrames) {
            int count=std::min(kStereoChunkFrames,n-at);
            DuplicateMonoFrames(mono.data()+at,scratch,count);
            captured.insert(captured.end(),scratch,scratch+count*2);
            AverageStereoFrames(source.data()+at*2,mixed.data()+at,count);
        }
        assert(captured.size()==size_t(n*2));
        for (int i=0;i<n;++i) {
            assert(captured[2*i]==mono[i] && captured[2*i+1]==mono[i]);
            assert(mixed[i]==(int(source[2*i])+int(source[2*i+1]))/2);
        }
    }
    int16_t ends[]={-32768,-32768,32767,32767,-32768,32767};
    int16_t result[3]; AverageStereoFrames(ends,result,3);
    assert(result[0]==-32768 && result[1]==32767 && result[2]==0);
}
''', encoding='utf-8')
    exe = Path(folder) / 'stereo.exe'
    subprocess.run([CXX,'-std=c++17','-O2','-Wall','-Wextra','-Werror',
                    '-I',str(root/'xiaozhi-esp32/main/audio/codecs'),str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: production PCM conversion, extrema, 9 frame sizes, tails, 640-byte scratch bound')
