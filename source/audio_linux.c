/* Keep OpenAL Soft's spatial mixer; SDL3 owns the real playback device. */
#include "audio_linux.h"

#include <AL/alext.h>
#include <SDL3/SDL.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define MIX_RATE 48000
#define MIX_CHANNELS 2
#define MIX_FRAMES 1024

typedef struct AudioDevice {
  ALCdevice *device;
  SDL_AudioStream *stream;
  LPALCRENDERSAMPLESSOFT render;
  struct AudioDevice *next;
} AudioDevice;

static AudioDevice *devices;
static pthread_mutex_t devices_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_uint_fast64_t mixed_frames, nonzero_frames;

bool linux_audio_init(void) {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    fprintf(stderr, "audio: SDL initialization failed: %s\n", SDL_GetError());
    return false;
  }
  fprintf(stderr, "audio: SDL driver=%s\n", SDL_GetCurrentAudioDriver());
  return true;
}

void linux_audio_get_stats(uint64_t *frames, uint64_t *nonzero) {
  *frames = atomic_load_explicit(&mixed_frames, memory_order_relaxed);
  *nonzero = atomic_load_explicit(&nonzero_frames, memory_order_relaxed);
}

static void SDLCALL mix_audio(void *userdata, SDL_AudioStream *stream,
                             int additional_amount, int total_amount) {
  (void)total_amount;
  AudioDevice *audio = userdata;
  float samples[MIX_FRAMES * MIX_CHANNELS];
  const int frame_bytes = sizeof(float) * MIX_CHANNELS;
  while (additional_amount > 0) {
    int frames = additional_amount / frame_bytes;
    if (additional_amount % frame_bytes) frames++;
    if (frames > MIX_FRAMES) frames = MIX_FRAMES;
    audio->render(audio->device, samples, frames);
    uint64_t nonzero = 0;
    for (int i = 0; i < frames; i++) {
      float left = samples[i * 2], right = samples[i * 2 + 1];
      if (left > 0.00001f || left < -0.00001f ||
          right > 0.00001f || right < -0.00001f) nonzero++;
    }
    if (!SDL_PutAudioStreamData(stream, samples, frames * frame_bytes)) return;
    atomic_fetch_add_explicit(&mixed_frames, frames, memory_order_relaxed);
    atomic_fetch_add_explicit(&nonzero_frames, nonzero, memory_order_relaxed);
    additional_amount -= frames * frame_bytes;
  }
}

ALCdevice *alcOpenDeviceHook(const char *name) {
  /* Android device names aren't host SDL device names. Respect SDL's default
   * device/backend selection (SDL_AUDIO_DRIVER), not Android's name string. */
  (void)name;
  LPALCLOOPBACKOPENDEVICESOFT open_loopback =
      (LPALCLOOPBACKOPENDEVICESOFT)alcGetProcAddress(NULL, "alcLoopbackOpenDeviceSOFT");
  if (!open_loopback || !(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) {
    fprintf(stderr, "audio: SDL audio or ALC_SOFT_loopback is unavailable\n");
    return NULL;
  }
  AudioDevice *audio = calloc(1, sizeof(*audio));
  if (!audio) return NULL;
  audio->device = open_loopback(NULL);
  if (!audio->device) { free(audio); return NULL; }
  audio->render = (LPALCRENDERSAMPLESSOFT)alcGetProcAddress(
      audio->device, "alcRenderSamplesSOFT");
  if (!audio->render) {
    alcCloseDevice(audio->device);
    free(audio);
    return NULL;
  }
  pthread_mutex_lock(&devices_mutex);
  audio->next = devices;
  devices = audio;
  pthread_mutex_unlock(&devices_mutex);
  return audio->device;
}

ALCcontext *alcCreateContextHook(ALCdevice *device, const ALCint *attributes) {
  pthread_mutex_lock(&devices_mutex);
  AudioDevice *audio = devices;
  while (audio && audio->device != device) audio = audio->next;
  if (!audio) {
    pthread_mutex_unlock(&devices_mutex);
    return NULL;
  }
  /* Preserve the game's source limits etc., but the loopback PCM format must
   * match the SDL stream. Bound the attribute list rather than overflowing. */
  ALCint attrs[129];
  int n = 0;
  if (attributes) {
    for (int i = 0; ; i += 2) {
      if (i >= 120) {
        fprintf(stderr, "audio: oversized context attribute list\n");
        pthread_mutex_unlock(&devices_mutex);
        return NULL;
      }
      if (!attributes[i]) break;
      if (attributes[i] == ALC_FREQUENCY ||
          attributes[i] == ALC_FORMAT_CHANNELS_SOFT ||
          attributes[i] == ALC_FORMAT_TYPE_SOFT) continue;
      attrs[n++] = attributes[i];
      attrs[n++] = attributes[i + 1];
    }
  }
  attrs[n++] = ALC_FREQUENCY; attrs[n++] = MIX_RATE;
  attrs[n++] = ALC_FORMAT_CHANNELS_SOFT; attrs[n++] = ALC_STEREO_SOFT;
  attrs[n++] = ALC_FORMAT_TYPE_SOFT; attrs[n++] = ALC_FLOAT_SOFT;
  attrs[n] = 0;
  ALCcontext *context = alcCreateContext(device, attrs);
  if (context && !audio->stream) {
    const SDL_AudioSpec spec = { .format = SDL_AUDIO_F32,
                                .channels = MIX_CHANNELS, .freq = MIX_RATE };
    audio->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                             &spec, mix_audio, audio);
    if (!audio->stream || !SDL_ResumeAudioStreamDevice(audio->stream)) {
      fprintf(stderr, "audio: SDL playback failed: %s\n", SDL_GetError());
      if (audio->stream) SDL_DestroyAudioStream(audio->stream);
      audio->stream = NULL;
      alcDestroyContext(context);
      context = NULL;
    } else {
      fprintf(stderr, "audio: OpenAL loopback -> SDL %s, stereo float32 %d Hz\n",
              SDL_GetCurrentAudioDriver(), MIX_RATE);
    }
  }
  if (!context) fprintf(stderr, "audio: context creation failed (ALC %#x)\n",
                        alcGetError(device));
  pthread_mutex_unlock(&devices_mutex);
  return context;
}

ALCboolean alcCloseDeviceHook(ALCdevice *device) {
  pthread_mutex_lock(&devices_mutex);
  AudioDevice **link = &devices;
  while (*link && (*link)->device != device) link = &(*link)->next;
  AudioDevice *audio = *link;
  if (!audio) {
    pthread_mutex_unlock(&devices_mutex);
    return ALC_FALSE;
  }
  /* Pause/lock prevent the mixer accessing a device while it is closed. SDL
   * owns the callback lock; the callback never acquires devices_mutex. */
  if (audio->stream) {
    SDL_PauseAudioStreamDevice(audio->stream);
    SDL_LockAudioStream(audio->stream);
  }
  ALCboolean closed = alcCloseDevice(device);
  if (audio->stream) SDL_UnlockAudioStream(audio->stream);
  if (closed) {
    if (audio->stream) SDL_DestroyAudioStream(audio->stream);
    *link = audio->next;
    free(audio);
  } else if (audio->stream) {
    SDL_ResumeAudioStreamDevice(audio->stream);
  }
  pthread_mutex_unlock(&devices_mutex);
  return closed;
}

void deinit_openal(void) {
  /* Process-exit path: stop callbacks, but leave game-owned contexts/devices to
   * process teardown. Destroying them while a game thread runs is unsafe. */
  pthread_mutex_lock(&devices_mutex);
  for (AudioDevice *audio = devices; audio; audio = audio->next) {
    if (audio->stream) SDL_DestroyAudioStream(audio->stream);
    audio->stream = NULL;
  }
  pthread_mutex_unlock(&devices_mutex);
  uint64_t frames, nonzero;
  linux_audio_get_stats(&frames, &nonzero);
  fprintf(stderr, "audio: mixed=%llu nonzero=%llu frames\n",
          (unsigned long long)frames, (unsigned long long)nonzero);
}
