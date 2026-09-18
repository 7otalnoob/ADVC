#ifndef __HOOKS_H__
#define __HOOKS_H__

void patch_game(void);

void keep_game_frame_limiter_off(void);

// Direct write to CMenuManager::m_PrefsLookSensitivity (see config.look_sensitivity).
// Safe: plain data symbol, no hook, no offset guessing.
void apply_look_sensitivity(void);

// Temporary debug-only HUD widget ID probe (see game_linux.c). Safe to call
// every frame: no-ops once done or if the required symbols didn't resolve.
void dump_hud_widget_ids(void);

// Terminate the process, skipping the mobile engine's crashy teardown (see main.c).
// Commits the SD first so any just-written save persists. Never returns.
void hard_exit(void);

void deinit_openal(void);

// Queue a cheat code (from the on-screen keyboard in main.c); fed to the game's
// CCheat::AddToCheatString by the DoCheats hook. Defined in hooks/game.c.
void cheats_enqueue(const char *s);

#endif
