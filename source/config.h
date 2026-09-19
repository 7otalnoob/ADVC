/* config.h -- global configuration and config file handling
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

// MB reserved for the .so load region: it only holds the RW staging copies of
// the mapped libraries, and the rest of RAM goes to the newlib heap (game malloc
// + mesa GPU bos). Sizing this larger starves the GPU and OOMs SA's texture
// streaming, so keep it small.
#define MEMORY_SO_MB 64

// The Android NDK C++ runtime is vendored with the Linux port. The loader
// resolves libGame.so's std::/__cxa imports into it.
#define SO_NAME "libGame.so"
#define CXX_DONOR_SO_NAME "libc++_shared.so"
// the game ships its own binary "config.txt" at the data root, so the wrapper's
// config uses a distinct name to avoid parsing that blob as text
#ifndef GTAVC_CONFIG_NAME
#define GTAVC_CONFIG_NAME "gtavc_nx.cfg"
#endif
#define CONFIG_NAME GTAVC_CONFIG_NAME
#define LOG_NAME "debug.log"
// backing store for the engine's get/setAppLocalValue key/value pairs
#ifndef GTAVC_APPSTATE_NAME
#define GTAVC_APPSTATE_NAME "appstate.txt"
#endif
#define APPSTATE_NAME GTAVC_APPSTATE_NAME

// Define to write debug.log and nxlink stdout. Off for release (debugPrintf
// becomes a no-op).
//#define DEBUG_LOG 1

// actual screen size
extern int screen_width;
extern int screen_height;

typedef struct {
  int screen_width;
  int screen_height;
  int trilinear_filter;
  int show_fps;
  int fps_cap_30;
  int auto_boot_delay;
  // PS2-style rendering (SkyGFX-derived) toggles. Each 1 = on, 0 = off.
  int ps2_corona_rotation; // #1: coronas spin like PS2/PC
  int ps2_color_filter;    // #3: PS2 color-grade filter in CPostEffects::MobileRender
  // Gameplay-feel fixes (Vita Category A). 1 = fix applied, 0 = stock. Default 0.
  int sprint_any_surface;    // SurfaceInfos_c::CantSprintOn -> ret0 (sprint on sand/grass)
  int remove_air_resistance; // CCullZones::DoExtraAirResistanceForPlayer -> ret0
  int show_wanted_stars;     // CWidgetPlayerInfo::DrawWanted: always draw the stars
  int disable_ped_spec;      // BuildPixelSource: drop BONE3|BONE4 from the spec gate
  int no_offscreen_despawn;  // cars/peds don't despawn off-screen (re3); perf cost
  int mobile_widgets;        // 1 = show the mobile-only touch widgets; 0 (default) =
                             // hide them (steering-method popup, cutscene-skip button,
                             // and the 3 touch rows in the controls menu)
  int fuzzy_seek;            // 1 (default) = OR MPG123_FUZZY|SEEKBUFFER|GAPLESS into
                             // mpg123 flag calls (TheOfficialFloW's FuzzySeek port)
  float look_sensitivity;    // overrides CMenuManager::m_PrefsLookSensitivity every
                             // frame. <= 0 (default) = leave the game's own value
                             // alone. VC's mobile default feels very fast on a full
                             // analog stick (it was tuned for small touch swipes);
                             // try something like 0.3-0.6 and adjust to taste.
  int hud_id_test;           // 0 (default) = off. 1 = once at boot, force-move
                             // Touchscreen widget IDs 22 and 23 to obvious debug
                             // spots and resize them, to visually confirm which
                             // on-screen HUD element each ID is. Experimental.
  int hud_restore_defaults; // 0 (default) = off. 1 = once at boot, call
                             // Touchscreen::RestoreDefaults() before anything
                             // else, undoing any earlier hud_id_test moves that
                             // got saved. Experimental, turn back off after use.
} Config;

extern Config config;

int read_config(const char *file);
int write_config(const char *file);

#endif
