#pragma once
#include <cstdint>

// Bound the temporary stereo data independently of the server's Opus frame.
constexpr int kStereoChunkFrames = 160;
inline void DuplicateMonoFrames(const int16_t* mono, int16_t* stereo, int count) {
    for (int i = 0; i < count; ++i)
        stereo[2 * i] = stereo[2 * i + 1] = mono[i];
}
inline void AverageStereoFrames(const int16_t* stereo, int16_t* mono, int count) {
    for (int i = 0; i < count; ++i)
        mono[i] = (int(stereo[2 * i]) + int(stereo[2 * i + 1])) / 2;
}
