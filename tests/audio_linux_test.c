/* Real OpenAL mixing through SDL. Use SDL_AUDIO_DRIVER=dummy in CI, or
 * pipewire on-device for a brief, quiet 440 Hz speaker test. */
#include "audio_linux.h"
#include <AL/al.h>
#include <SDL3/SDL.h>
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  const char *duration = getenv("GTAVC_AUDIO_TEST_MS");
  int test_ms = duration ? atoi(duration) : 500;
  if (test_ms < 500 || test_ms > 10000) return 2;
  if (!linux_audio_init()) return 1;
  for (int run = 0; run < 2; run++) {
    uint64_t before, before_nonzero, frames, nonzero;
    linux_audio_get_stats(&before, &before_nonzero);
    ALCdevice *device = alcOpenDeviceHook(NULL);
    assert(device);
    const ALCint attributes[] = { ALC_FREQUENCY, 22050,
                                  ALC_MONO_SOURCES, 32, 0 };
    ALCcontext *context = alcCreateContextHook(device, attributes);
    assert(context && alcMakeContextCurrent(context));
    int16_t pcm[4800];
    for (int i = 0; i < 4800; i++)
      pcm[i] = (int16_t)(2600.0 * sin(i * 2.0 * 3.141592653589793 * 440.0 / 48000.0));
    ALuint buffer, source;
    alGenBuffers(1, &buffer);
    alBufferData(buffer, AL_FORMAT_MONO16, pcm, sizeof(pcm), 48000);
    alGenSources(1, &source);
    alSourcei(source, AL_BUFFER, buffer);
    alSourcei(source, AL_LOOPING, AL_TRUE);
    alSourcePlay(source);
    assert(alGetError() == AL_NO_ERROR);
    SDL_Delay(test_ms);
    linux_audio_get_stats(&frames, &nonzero);
    assert(frames > before && nonzero > before_nonzero + 1000);
    printf("audio smoke: run=%d mixed=%llu nonzero=%llu frames\n", run,
           (unsigned long long)(frames - before),
           (unsigned long long)(nonzero - before_nonzero));
    alSourceStop(source);
    alDeleteSources(1, &source);
    alDeleteBuffers(1, &buffer);
    assert(alcMakeContextCurrent(NULL));
    alcDestroyContext(context);
    assert(alcCloseDeviceHook(device));
    linux_audio_get_stats(&before, &before_nonzero);
    SDL_Delay(100);
    linux_audio_get_stats(&frames, &nonzero);
    assert(frames == before && nonzero == before_nonzero);
  }
  deinit_openal();
  SDL_Quit();
  puts("audio smoke: PASS (mixer, callback, device reopen and teardown)");
  return 0;
}
