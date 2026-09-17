# Linux SDL3 port working notes

## Scope and provenance

- Generic Linux is primary. `Makefile` / `source/main.c` retain Switch reference
  code; the Linux CMake target uses separate platform modules.
- Public source foundation: MIT `NaGaa95/gtavc_nx`. The current tested Android
  payload is arm64-v8a `libGame.so` v2.11.311.
- Game assets/APKs/OBBs are user-supplied and must not be added to Git. The
  Android NDK `libc++_shared.so` runtime is the port's tracked vendored runtime.
  No commits/pushes without permission; preserve this dirty tree.
- Assets and libraries live **inside** `gtavc/` beside `gtavc_linux`. The launcher
  is the sibling `Grand Theft Auto Vice City.sh` in the ports directory.
- `ASSET_PREPARATION.md` is the authoritative working guide for the user-supplied
  Android asset tree, required runtime donors, and extraction paths. Keep
  proprietary inputs outside the repository.

## Verified cross-build environment

Persistent container `gtavc-aarch64-build` is amd64 Debian Bookworm. Project is
mounted at `/src`; compiler is `aarch64-linux-gnu-gcc`; SDL source/build/install
are `/opt/SDL`, `/opt/SDL-build`, `/opt/target-aarch64`.

Incremental build (do not recreate the container or wipe its dependency trees):

```sh
podman exec gtavc-aarch64-build cmake --build /src/build-aarch64 -j2
podman exec gtavc-aarch64-build cp -L /opt/target-aarch64/lib/libSDL3.so.0 /src/libSDL3.so.0
```

When reconfiguring, target pkg-config must precede host paths:
`PKG_CONFIG_PATH=/opt/target-aarch64/lib/pkgconfig`,
`PKG_CONFIG_LIBDIR=/opt/target-aarch64/lib/pkgconfig:/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig`.
The existing game cache uses Linux/aarch64, RelWithDebInfo, DEBUG_LOG=OFF, and
find roots `/usr/aarch64-linux-gnu;/opt/target-aarch64`.
Enable `-DGTAVC_DEBUG_LOG=ON` only for a diagnostic build; normal handheld
builds must leave it off so EGL compatibility traces do not reach the log.

The PortMaster runtime is an SDL3-to-system-SDL2 shim with GLES/GPU support;
the shim dynamically loads each target's patched SDL2. Debian arm64 development packages:
`libpipewire-0.3-dev:arm64 libpulse-dev:arm64 libasound2-dev:arm64`.
For the old native SDL3 build, the old SDL cache had every useful audio backend disabled! Explicitly configure
`SDL_PIPEWIRE=ON`, `SDL_PIPEWIRE_SHARED=ON`, `SDL_PULSEAUDIO=ON`,
`SDL_PULSEAUDIO_SHARED=ON`, `SDL_ALSA=ON`, `SDL_ALSA_SHARED=ON`.
Its strict find-root cache needed `ALSA_INCLUDE_DIR=/usr/include` and
`ALSA_LIBRARY=/usr/lib/aarch64-linux-gnu/libasound.so`; wanted=ON alone did not
mean the backend compiled. Inspect configure output and test on target.

## Tests

```sh
cmake -S . -B build-linux -DBUILD_TESTING=ON
cmake --build build-linux -j2
ctest --test-dir build-linux --output-on-failure
```

- `input_linux_test`: SDL virtual gamepad, native-callback mocks; button IDs,
  normalized sticks/triggers, focus release, startup, hotplug, slot compaction,
  missing JNI. Requires an otherwise controller-free test environment.
- `audio_linux_test`: real OpenAL loopback mixer → SDL dummy stream. Validates
  non-silent samples, close/reopen, stopped callback after device teardown.
- `tests/sdl_video_smoke.c`: isolated SDL GLES RGB test with pixel readback and
  render-thread context handoff; build separately against SDL3/GLES2.
- Native `audio_linux_test` with `SDL_AUDIO_DRIVER=pipewire` emits two low-level
  tones. `GTAVC_AUDIO_TEST_MS=5000` lengthens each to five seconds.
- Host sanitizer link was blocked by a missing system libasan shared object;
  ordinary -Wall/-Wextra/-Werror standalone test builds and CTest passed.

## Runtime and evidence

Knulli target: `/userdata/roms/ports/gtavc/`. Do not hardcode SSH credentials or
store them in this repo. Hash local and remote artifacts after deployment.
Use `.new` + rename, not overwriting an actively mapped executable/library.

- SDL input is **gamepad only**, no keyboard emulation/fallback. Use SDL mappings,
  including PortMaster's `SDL_GAMECONTROLLERCONFIG`, not custom evdev IDs.
- v2.11.311 uses `implOnGamepadConnected`, `implOnGamepadDisconnected`, and
  `implOnGamepadResume`. Button JNI IDs 0..13: South/East/West/North/Start/Back/L1/R1/Up/Down/Left/Right/L3/R3.
  Axis order lx/ly/rx/ry/lt/rt; triggers 0..1, sticks -1..1 (SDL Y unchanged).
- `implOnBackButtonPressed` is a native no-op in this APK. Do not pretend that
  calling it implements pause. Current Start/Back are native gamepad buttons;
  a future pause-menu issue needs a gamepad-specific, source-verified solution.
- The Linux input layer owns a PortMaster-style quit chord: Guide/Home + Start,
  or Back/Select/Minus + Start. It sets `jni_quit_requested` before the next frame,
  then the main loop runs `hard_exit()` to close input/audio and terminate.
- OpenAL Soft keeps spatial mixing; SDL gets float32 stereo 48 kHz via loopback.
  `GTAVC_INPUT_DEBUG=1` traces dispatch; `GTAVC_AUDIO_DEBUG=1` emits PCM counters.
- Knulli's PipeWire daemon runs with `XDG_RUNTIME_DIR=/var/run`, socket
  `/var/run/pipewire-0`. SSH lacks that variable; the launcher fills it only if
  unset and the socket exists. Explicit audio backend overrides are retained.
- Read-only routing checks: `XDG_RUNTIME_DIR=/var/run wpctl status`, hardware
  `amixer -c 0 scontents`. Capture only the sink monitor if needed, never a mic:
  `parec --device=@DEFAULT_MONITOR@ --raw --format=float32le --rate=48000 --channels=2`.
- Nonzero SDL PCM counters do NOT prove audibility. Verify active PipeWire
  playback links, nonzero sink-monitor samples, and get physical confirmation.
- Keep bounded SSH tests and persistent logs. Do not infer stability from timeout
  exit 124; that is intentional test termination. Keep log categories separate.
- On KMSDRM inspect DRM ownership; do not compete with another game/front end.
  If authorized, stop `/etc/init.d/S31emulationstation`, test using `/dev/tty1`,
  and restore EmulationStation afterward. Never kill an unrelated game.

## Critical rendering finding

The old upstream `disable_ped_spec` patch at `BuildPixelSource+0x244` removes a
`strcat` call in v2.11.264, not a specular instruction. It deletes `Out_Color`'s
GLSL declaration. The render thread then intentionally faults at NULL in
`RQ_Command_rqBuildShader` after shader compilation fails. This was confirmed
by remote GDB/disassembly, not merely the final log line.

The Linux target now links `source/hooks/game_linux.c` (symbol-based thread,
JNI, screen, cloud platform hooks), not `source/hooks/game.c` or Switch assembly.
Do not restore offset-based gameplay patches without exact-version validation.

Android `GameView` here is a SurfaceView, not GLSurfaceView. `GameThread` calls
`implOnDrawFrame(dt)` for logic; native `GraphicsThread` renders and swaps via
its imported EGL bridge. Do not add main-thread swapping on assumptions from
re3/older payloads. SDL-only RGB scanout and gameplay rendering were separately
physically confirmed. Gamepad operation and game audio were also user-confirmed
on the TRIMUI Smart Pro S after integration.
