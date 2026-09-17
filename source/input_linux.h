/* SDL3 input for the Android GTA:SA native gamepad interfaces. */
#ifndef GTAVC_INPUT_LINUX_H
#define GTAVC_INPUT_LINUX_H

#include <SDL3/SDL.h>
#include "so_util.h"

/* All four functions belong on SDL's main/event thread. Initialize AFTER
 * implOnInitialSetup/SurfaceCreated/SurfaceChanged/Resume (native input startup
 * clears its state). Returns 0 on success, -1 on failure; safe to retry/shutdown.
 * Forward every polled event, then update once BEFORE implOnDrawFrame.
 * Shutdown before native activity destruction/module unload and SDL_Quit.
 *
 * Physical SDL gamepad positions are preserved, including Nintendo controllers.
 * Up to four pads; native Start/Back buttons, sticks and analog triggers.
 * No keyboard emulation or keyboard/mouse fallback.
 * Set GTAVC_INPUT_DEBUG=1 for native input dispatch diagnostics.
 */
int linux_input_init(so_module *module);
void linux_input_event(const SDL_Event *event);
void linux_input_update(void);
void linux_input_shutdown(void);

#endif
