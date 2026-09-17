#ifndef GTAVC_AUDIO_LINUX_H
#define GTAVC_AUDIO_LINUX_H

#include <stdbool.h>
#include <stdint.h>
#include <AL/alc.h>

bool linux_audio_init(void);
/* Counters describe mixed PCM, not proof of audible speaker output. */
void linux_audio_get_stats(uint64_t *frames, uint64_t *nonzero_frames);
ALCdevice *alcOpenDeviceHook(const char *name);
ALCcontext *alcCreateContextHook(ALCdevice *device, const ALCint *attributes);
ALCboolean alcCloseDeviceHook(ALCdevice *device);
void deinit_openal(void);

#endif
