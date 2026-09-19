/* Linux platform hooks for the Android ARM64 game.
 *
 * Derived from the MIT-licensed gtavc_nx thread/platform hooks (game.c).
 * Do NOT apply that file's instruction-offset gameplay patches here: those
 * offsets target a different game build. The known v2.11.264 BuildPixelSource
 * offset is strcat, not the specular-lighting instruction; patching it corrupts
 * GLSL. The Linux path uses symbol-only platform hooks for v2.11.311 instead.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hooks.h"
#include "../config.h"
#include "../jni_fake.h"
#include "../so_util.h"
#include "../util.h"

extern so_module game_mod;



typedef struct {
  void *(*func)(void *);
  void *arg;
  char name[16];
} GameThreadStart;

static void *game_thread_start(void *arg) {
  GameThreadStart start = *(GameThreadStart *)arg;
  free(arg);
  pthread_setname_np(pthread_self(), start.name);
  thread_registry_add();
  /* Leave glibc's thread pointer intact. The game obtains JNIEnv through the
   * hook below and pthread TLS through our Bionic import adapters. */
  return start.func(start.arg);
}

static void *current_jni_env(void) {
  return fake_env;
}

/* NVThreadSpawnJNIThread(long*, const Android pthread_attr_t*, const char*,
 *                        void* (*)(void*), void*)
 * Android attributes are not layout-compatible with glibc. Use Linux defaults
 * rather than reinterpret them; retain a joinable native pthread_t handle.
 */
static int spawn_jni_thread(long *tid, const void *attr, const char *name,
                            void *(*func)(void *), void *arg) {
  (void)attr;
  _Static_assert(sizeof(pthread_t) == sizeof(long), "Android thread handle size");
  GameThreadStart *start = calloc(1, sizeof(*start));
  if (!start)
    return ENOMEM;
  start->func = func;
  start->arg = arg;
  strlcpy(start->name, name ? name : "game", sizeof(start->name));
  pthread_t thread;
  int err = pthread_create(&thread, NULL, game_thread_start, start);
  if (err) {
    free(start);
    return err;
  }
  if (tid)
    memcpy(tid, &thread, sizeof(thread));
  debugPrintf("thread: started %s\n", name ? name : "game");
  return 0;
}

static int screen_get_width(void) { return screen_width; }
static int screen_get_height(void) { return screen_height; }

static void *mobile_settings_settings;
void keep_game_frame_limiter_off(void) {
  // MobileSettings::settings doesn't resolve on this VC build (symbol absent),
  // so this stays a safe no-op until/unless a VC equivalent is found.
  if (mobile_settings_settings)
    *(int *)((uint8_t *)mobile_settings_settings + 1216) = 0;
}

static float *g_look_sensitivity;
void apply_look_sensitivity(void) {
  // CMenuManager::m_PrefsLookSensitivity: plain global float, confirmed
  // present in this VC build. Direct data write, no hook, no offset.
  if (config.look_sensitivity > 0.0f && g_look_sensitivity)
    *g_look_sensitivity = config.look_sensitivity;
}

// --- real feature: Adjustable.cfg -------------------------------------
// GTouchscreen is a global pointer to the Touchscreen singleton.
// Touchscreen::MoveButton(int id, float x, float y) and
// Touchscreen::ResizeButton(int id) are real functions in this VC build,
// called by symbol (no byte patching, no offset guessing). The three ids
// below were confirmed by live on-device testing (GetHUDElementAt probing
// + visually watching each one move):
//   22 = radar/map (top-left)
//   23 = notification/call banner (top-right)
//   24 = small info panel next to the radar (top-left)
// Plain text, one widget per line: "name x y resize_taps". Lines starting
// with '#' and blank lines are ignored. Unparseable lines and unknown
// names are silently skipped. resize_taps is how
// many times to call ResizeButton for that widget (0 = leave its size
// alone -- we don't yet know if ResizeButton takes an absolute size or
// cycles presets). Applied once, at boot, after Touchscreen exists.
static void **g_touchscreen_slot; // address of the GTouchscreen global (a pointer var)
static void (*MoveButton)(void *self, int id, float x, float y);
static void (*ResizeButtonFn)(void *self, int id);

typedef struct { const char *name; int id; } HudWidgetName;
static const HudWidgetName g_hud_widget_names[] = {
  {"radar", 22}, {"notifications", 23}, {"info_panel", 24},
};
#define HUD_WIDGET_NAME_COUNT \
  (sizeof(g_hud_widget_names) / sizeof(g_hud_widget_names[0]))

static int g_adjustable_cfg_done;
static unsigned g_adjustable_cfg_ready_frames;

void apply_adjustable_cfg(void) {
  if (g_adjustable_cfg_done || !g_touchscreen_slot)
    return;
  void *touchscreen = *g_touchscreen_slot;
  if (!touchscreen)
    return; // Touchscreen singleton not constructed yet; retry next frame

  // Give the game a couple of seconds after GTouchscreen first appears to
  // finish constructing EVERY widget slot, not just the first one -- moving
  // a not-yet-built widget silently does nothing, which looked like "only
  // the radar moves" even though the ids themselves are correct.
  if (g_adjustable_cfg_ready_frames++ < 120)
    return;
  g_adjustable_cfg_done = 1; // only ever try once, whether the file exists or not

  FILE *cfg = fopen("Adjustable.cfg", "r");
  if (!cfg)
    return; // no file = nothing to apply, not an error

  char line[256];
  while (fgets(line, sizeof(line), cfg)) {
    char name[64];
    float x, y;
    int taps = 0;
    char *p = line;
    while (*p == ' ' || *p == '\t')
      p++;
    if (*p == '#' || *p == '\n' || *p == 0)
      continue;
    int matched = sscanf(p, "%63s %f %f %d", name, &x, &y, &taps);
    if (matched < 3)
      continue;
    int id = -1;
    for (size_t i = 0; i < HUD_WIDGET_NAME_COUNT; i++) {
      if (strcmp(g_hud_widget_names[i].name, name) == 0) {
        id = g_hud_widget_names[i].id;
        break;
      }
    }
    if (id < 0)
      continue;
    if (MoveButton)
      MoveButton(touchscreen, id, x, y);
    for (int t = 0; t < taps; t++)
      if (ResizeButtonFn)
        ResizeButtonFn(touchscreen, id);
  }
  fclose(cfg);
}

void patch_game(void) {
  /* Whole-function replacements only, no displaced-instruction trampolines.
   * These platform entry signatures are present in the target v2.11.311
   * Android library; no version-sensitive gameplay offsets are applied. */
  const DynLibFunction hooks[] = {
    {"_Z22NVThreadSpawnJNIThreadPlPK14pthread_attr_tPKcPFPvS5_ES5_",
     (uintptr_t)spawn_jni_thread},
    {"_Z24NVThreadGetCurrentJNIEnvv", (uintptr_t)current_jni_env},
    {"_Z17OS_ScreenGetWidthv", (uintptr_t)screen_get_width},
    {"_Z18OS_ScreenGetHeightv", (uintptr_t)screen_get_height},
  };
  for (size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); i++)
    hook_arm64(so_find_addr(&game_mod, hooks[i].symbol), hooks[i].func);

  uintptr_t cloud_saves = so_try_find_addr_rx(&game_mod, "UseCloudSaves");
  if (cloud_saves)
    *(uint8_t *)cloud_saves = 0;

  mobile_settings_settings =
      (void *)so_try_find_addr_rx(&game_mod, "_ZN14MobileSettings8settingsE");
  g_look_sensitivity =
      (float *)so_try_find_addr_rx(&game_mod, "_ZN12CMenuManager22m_PrefsLookSensitivityE");

  g_touchscreen_slot = (void **)so_try_find_addr_rx(&game_mod, "GTouchscreen");
  MoveButton = (void (*)(void *, int, float, float))
      so_try_find_addr_rx(&game_mod, "_ZN11Touchscreen10MoveButtonEiff");
  ResizeButtonFn = (void (*)(void *, int))
      so_try_find_addr_rx(&game_mod, "_ZN11Touchscreen12ResizeButtonEi");

  debugPrintf("hooks: Linux platform only; version-sensitive gameplay offsets disabled\n");
}
