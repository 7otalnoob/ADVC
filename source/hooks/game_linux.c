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
static intptr_t (*GetButtonAt)(void *self, float x, float y);
static void (*MoveButton)(void *self, int id, float x, float y);
static void (*ResizeButtonFn)(void *self, int id);
static void (*RestoreDefaultsFn)(void *self);
static int g_hud_restore_done;
static intptr_t g_hud_last_id[HUD_PROBE_COUNT]; // -1 = never probed yet
static int g_hud_probe_pass;
static unsigned g_hud_probe_frame_count;
static int g_hud_sweep_done;

// Read-only full-screen sweep: unlike the 43 known button positions, this
// walks a fine grid across the whole virtual screen (0..690 x 0..435, 15px
// steps) calling GetHUDElementAt AND GetButtonAt at each point. Pure queries,
// nothing written anywhere -- safe even if health/money/weapon/timer turn out
// to live at ids or positions our 43-record table never covered. Logs the
// first coordinate where each distinct id shows up, once per id.
#define HUD_SWEEP_MAX_IDS 128
static void sweep_hud_elements(void *touchscreen) {
  if (g_hud_sweep_done)
    return;
  int seen_hud[HUD_SWEEP_MAX_IDS] = {0};
  int seen_btn[HUD_SWEEP_MAX_IDS] = {0};
  FILE *f = fopen("hud_probe.log", "a");
  if (f)
    fprintf(f, "--- full-screen sweep (15px grid, 0..690 x 0..435) ---\n");
  for (float y = 0.0f; y <= 435.0f; y += 15.0f) {
    for (float x = 0.0f; x <= 690.0f; x += 15.0f) {
      if (GetHUDElementAt) {
        intptr_t id = GetHUDElementAt(touchscreen, x, y);
        if (id >= 0 && id < HUD_SWEEP_MAX_IDS && !seen_hud[id]) {
          seen_hud[id] = 1;
          if (f)
            fprintf(f, "sweep: GetHUDElementAt first hit id=%ld at (x=%.0f y=%.0f)\n",
                    (long)id, x, y);
        }
      }
      if (GetButtonAt) {
        intptr_t id = GetButtonAt(touchscreen, x, y);
        if (id >= 0 && id < HUD_SWEEP_MAX_IDS && !seen_btn[id]) {
          seen_btn[id] = 1;
          if (f)
            fprintf(f, "sweep: GetButtonAt      first hit id=%ld at (x=%.0f y=%.0f)\n",
                    (long)id, x, y);
        }
      }
    }
  }
  if (f) {
    fflush(f);
    fclose(f);
  }
  g_hud_sweep_done = 1;
}
static int g_hud_id_test_done;

// One-shot, opt-in (config.hud_id_test): force-move only the two still-
// unconfirmed widget ids (24 and 42) to obvious, far-apart debug spots,
// leaving 22 (radar) and 23 (notifications, already confirmed) untouched.
// Real function calls by symbol only, nothing byte-patched.
static void run_hud_id_test(void *touchscreen) {
  if (g_hud_id_test_done || !config.hud_id_test)
    return;
  FILE *f = fopen("hud_probe.log", "a");
  if (f)
    fprintf(f, "--- hud_id_test: id 24 -> (150,300), id 42 -> (450,300) ---\n");

  if (MoveButton) {
    MoveButton(touchscreen, 24, 150.0f, 300.0f);
    MoveButton(touchscreen, 42, 450.0f, 300.0f);
  }
  if (ResizeButtonFn) {
    ResizeButtonFn(touchscreen, 24);
    ResizeButtonFn(touchscreen, 42);
  }
  if (f) {
    fprintf(f, "hud_id_test: done (MoveButton=%p ResizeButton=%p)\n",
            (void *)MoveButton, (void *)ResizeButtonFn);
    fflush(f);
    fclose(f);
  }
  g_hud_id_test_done = 1;
}

static void apply_adjustable_cfg(void *touchscreen); // defined below

void dump_hud_widget_ids(void) {
  if (!g_touchscreen_slot || !GetHUDElementAt)
    return;
  void *touchscreen = *g_touchscreen_slot;
  if (!touchscreen)
    return; // Touchscreen singleton not constructed yet; retry later

  apply_adjustable_cfg(touchscreen);

  if (config.hud_restore_defaults && !g_hud_restore_done && RestoreDefaultsFn) {
    RestoreDefaultsFn(touchscreen);
    FILE *rf = fopen("hud_probe.log", "a");
    if (rf) {
      fprintf(rf, "--- hud_restore_defaults: called Touchscreen::RestoreDefaults() ---\n");
      fclose(rf);
    }
    g_hud_restore_done = 1;
  }

  sweep_hud_elements(touchscreen);
  run_hud_id_test(touchscreen);

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

// --- real feature: Adjustable.cfg -------------------------------------
// Plain text, one widget per line: "name x y resize_taps". Lines starting
// with '#' and blank lines are ignored. Unknown names are ignored (logged
// to hud_probe.log so a typo doesn't fail silently). resize_taps is how
// many times to call Touchscreen::ResizeButton for that widget (0 = leave
// its size alone -- we don't yet know if ResizeButton takes an absolute
// size or cycles presets, so this is deliberately coarse for now).
// Applied once, at boot, after Touchscreen exists.
typedef struct { const char *name; int id; } HudWidgetName;
static const HudWidgetName g_hud_widget_names[] = {
  {"radar", 22}, {"notifications", 23}, {"info_panel", 24},
};
#define HUD_WIDGET_NAME_COUNT \
  (sizeof(g_hud_widget_names) / sizeof(g_hud_widget_names[0]))

static int g_adjustable_cfg_done;

static void apply_adjustable_cfg(void *touchscreen) {
  if (g_adjustable_cfg_done)
    return;
  g_adjustable_cfg_done = 1; // only ever try once, whether the file exists or not

  FILE *cfg = fopen("Adjustable.cfg", "r");
  if (!cfg)
    return; // no file = nothing to apply, not an error

  FILE *log = fopen("hud_probe.log", "a");
  if (log)
    fprintf(log, "--- Adjustable.cfg found, applying ---\n");

  char line[256];
  while (fgets(line, sizeof(line), cfg)) {
    char name[64];
    float x, y;
    int taps = 0;
    char *p = line;
    while (*p == ' ' || *p == '\t')
      p++;
    if (*p == '#' || *p == '\n' || *p == '\0')
      continue;
    int matched = sscanf(p, "%63s %f %f %d", name, &x, &y, &taps);
    if (matched < 3) {
      if (log)
        fprintf(log, "Adjustable.cfg: skipping unparseable line: %s", line);
      continue;
    }
    int id = -1;
    for (size_t i = 0; i < HUD_WIDGET_NAME_COUNT; i++) {
      if (strcmp(g_hud_widget_names[i].name, name) == 0) {
        id = g_hud_widget_names[i].id;
        break;
      }
    }
    if (id < 0) {
      if (log)
        fprintf(log, "Adjustable.cfg: unknown widget name '%s', skipping\n", name);
      continue;
    }
    if (MoveButton)
      MoveButton(touchscreen, id, x, y);
    for (int t = 0; t < taps; t++)
      if (ResizeButtonFn)
        ResizeButtonFn(touchscreen, id);
    if (log)
      fprintf(log, "Adjustable.cfg: %s (id %d) -> (%.1f,%.1f), resized x%d\n",
              name, id, x, y, taps);
  }
  fclose(cfg);
  if (log) {
    fflush(log);
    fclose(log);
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
  GetButtonAt = (intptr_t (*)(void *, float, float))
      so_try_find_addr_rx(&game_mod, "_ZN11Touchscreen11GetButtonAtEff");
  MoveButton = (void (*)(void *, int, float, float))
      so_try_find_addr_rx(&game_mod, "_ZN11Touchscreen10MoveButtonEiff");
  ResizeButtonFn = (void (*)(void *, int))
      so_try_find_addr_rx(&game_mod, "_ZN11Touchscreen12ResizeButtonEi");
  RestoreDefaultsFn = (void (*)(void *))
      so_try_find_addr_rx(&game_mod, "_ZN11Touchscreen15RestoreDefaultsEv");

  debugPrintf("hooks: Linux platform only; version-sensitive gameplay offsets disabled\n");
}
