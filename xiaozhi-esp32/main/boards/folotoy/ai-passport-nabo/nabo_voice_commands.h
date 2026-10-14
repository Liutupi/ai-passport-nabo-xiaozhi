#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// Returns an existing canonical app name only for an explicit launch request.
const char* nabo_voice_launch_app(const char* transcript);
#ifdef __cplusplus
}
#endif
