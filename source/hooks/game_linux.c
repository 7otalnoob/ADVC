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

// --- one-shot HUD widget ID dump (debug only, temporary) --------------------
// GTouchscreen is a global pointer to the Touchscreen singleton.
// Touchscreen::GetHUDElementAt(float x, float y) resolves whatever widget
// sits at a screen point; we CALL it (a real function call by symbol, not a
// byte patch) for the centre of every widget rect already decoded from a
// real gta_vc.set from this exact build, and log whatever ID the game
// itself returns. Read-only: never writes anything, changes no behaviour.
typedef struct { float x, y; } HudProbePoint;
static const HudProbePoint g_hud_probe_points[] = {
  {670.54f,283.25f},{600.87f,380.92f},{670.94f,331.42f},{660.94f,380.88f},
  {683.76f,246.99f},{621.20f,331.98f},{6.67f,93.05f},{569.09f,329.69f},
  {547.23f,377.30f},{6.31f,380.84f},{62.16f,380.84f},{670.54f,283.25f},
  {600.87f,380.92f},{621.20f,331.98f},{660.94f,380.88f},{670.62f,331.98f},
  {13.51f,142.46f},{683.76f,246.99f},{6.67f,93.05f},{63.04f,335.12f},
  {16.43f,335.12f},{671.62f,6.27f},{6.03f,6.03f},{584.04f,6.27f},
  {86.38f,6.43f},{252.32f,423.75f},{23.62f,131.30f},{-8.52f,170.68f},
  {55.77f,170.68f},{683.82f,176.47f},{671.62f,5.62f},{671.62f,431.62f},
  {670.54f,198.87f},{670.54f,283.25f},{660.94f,380.88f},{660.94f,380.88f},
  {600.87f,380.92f},{609.91f,331.50f},{659.73f,330.90f},{659.09f,232.62f},
  {660.98f,283.85f},{610.35f,282.64f},{288.88f,7.77f},
};
#define HUD_PROBE_COUNT (sizeof(g_hud_probe_points) / sizeof(g_hud_probe_points[0]))

static void **g_touchscreen_slot; // address of the GTouchscreen global (a pointer var)
static intptr_t (*GetHUDElementAt)(void *self, float x, float y);
static intptr_t g_hud_last_id[HUD_PROBE_COUNT]; // -1 = never probed yet
static int g_hud_probe_pass;
static unsigned g_hud_probe_frame_count;

void dump_hud_widget_ids(void) {
  if (!g_touchscreen_slot || !GetHUDElementAt)
    return;
  void *touchscreen = *g_touchscreen_slot;
  if (!touchscreen)
    return; // Touchscreen singleton not constructed yet; retry later

  // Re-probe roughly every 3 seconds (assuming ~60 fps) for as long as the
  // game runs, so playing normally for a bit -- getting in a car, taking out
  // a weapon, etc. -- surfaces widgets that are only "hit-testable" in that
  // context. Only ever WRITES a line when a point's answer changes, so the
  // log stays small; nothing is overwritten, everything accumulates.
  if (g_hud_probe_frame_count++ % 180 != 0)
    return;
  int pass = ++g_hud_probe_pass;
  if (pass == 1) {
    for (size_t i = 0; i < HUD_PROBE_COUNT; i++)
      g_hud_last_id[i] = -1; // force the first pass to log every point once
  }

  FILE *f = fopen("hud_probe.log", "a"); // append: keeps every session's history
  if (f && pass == 1)
    fprintf(f, "--- new run, GTouchscreen=%p, %zu points, probing every ~3s ---\n",
            touchscreen, (size_t)HUD_PROBE_COUNT);

  for (size_t i = 0; i < HUD_PROBE_COUNT; i++) {
    intptr_t id = GetHUDElementAt(touchscreen, g_hud_probe_points[i].x,
                                   g_hud_probe_points[i].y);
    if (id == g_hud_last_id[i])
      continue; // nothing new to report for this point
    if (f)
      fprintf(f,
              "pass %3d: record %2zu (x=%.2f y=%.2f) id %ld -> %ld (0x%lx)\n",
              pass, i, g_hud_probe_points[i].x, g_hud_probe_points[i].y,
              (long)g_hud_last_id[i], (long)id, (unsigned long)id);
    debugPrintf(
        "hud-probe: pass %3d record %2zu (x=%.2f y=%.2f) id %ld -> %ld (0x%lx)\n",
        pass, i, g_hud_probe_points[i].x, g_hud_probe_points[i].y,
        (long)g_hud_last_id[i], (long)id, (unsigned long)id);
    g_hud_last_id[i] = id;
  }
  if (f) {
    fflush(f);
    fclose(f);
  }
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
  GetHUDElementAt = (intptr_t (*)(void *, float, float))
      so_try_find_addr_rx(&game_mod, "_ZN11Touchscreen15GetHUDElementAtEff");

  debugPrintf("hooks: Linux platform only; version-sensitive gameplay offsets disabled\n");
}
