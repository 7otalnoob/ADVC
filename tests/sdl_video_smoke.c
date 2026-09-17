/* Isolate SDL scanout and render-thread context handoff from the Android game.
 * Run through the same launcher/session as gtavc_linux. No game data needed.
 */
#include <SDL3/SDL.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <string.h>

static SDL_Window *window;
static SDL_GLContext context;
static SDL_AtomicInt done;

static int render(void *unused) {
  (void)unused;
  int result = 1;
  if (!SDL_GL_MakeCurrent(window, context)) {
    fprintf(stderr, "acquire failed: %s\n", SDL_GetError());
    goto end;
  }
  fprintf(stderr, "GL vendor=%s renderer=%s version=%s\n",
          glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
  SDL_GL_SetSwapInterval(1);
  for (int color = 0; color < 3; color++) {
    unsigned char expected[4] = {0, 0, 0, 255}, pixel[4] = {0};
    expected[color] = 255;
    const Uint64 until = SDL_GetTicks() + 2000;
    unsigned frames = 0;
    do {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glDisable(GL_SCISSOR_TEST);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glClearColor(color == 0, color == 1, color == 2, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      if (!frames) {
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        GLenum error = glGetError();
        fprintf(stderr, "color=%d pixel=%u,%u,%u,%u GL_error=0x%x\n",
                color, pixel[0], pixel[1], pixel[2], pixel[3], error);
        if (error != GL_NO_ERROR || memcmp(pixel, expected, 3))
          goto release;
      }
      if (!SDL_GL_SwapWindow(window)) {
        fprintf(stderr, "swap failed: %s\n", SDL_GetError());
        goto release;
      }
      frames++;
      SDL_Delay(1);
    } while (SDL_GetTicks() < until);
    fprintf(stderr, "color=%d successful swaps=%u\n", color, frames);
  }
  result = 0;
release:
  if (!SDL_GL_MakeCurrent(window, NULL)) {
    fprintf(stderr, "release failed: %s\n", SDL_GetError());
    result = 1;
  }
end:
  SDL_SetAtomicInt(&done, 1);
  return result;
}

int main(void) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  window = SDL_CreateWindow("GTA SDL3 video test", 1280, 720,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
  context = window ? SDL_GL_CreateContext(window) : NULL;
  if (!context || !SDL_GL_MakeCurrent(window, NULL)) {
    fprintf(stderr, "SDL window/context: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  fprintf(stderr, "SDL driver=%s; red, green, blue for two seconds each\n",
          SDL_GetCurrentVideoDriver());
  SDL_Thread *thread = SDL_CreateThread(render, "video-test", NULL);
  if (!thread) {
    fprintf(stderr, "SDL_CreateThread: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  while (!SDL_GetAtomicInt(&done)) {
    SDL_PumpEvents();
    SDL_Delay(10);
  }
  int result;
  SDL_WaitThread(thread, &result);
  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  fprintf(stderr, "video test exit=%d (panel output needs visual confirmation)\n", result);
  return result;
}
