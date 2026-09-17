# Extracting a purchased Play Store copy from an existing Android install

Google Play normally installs GTA: Vice City as a split APK set rather than
one standalone APK. Use ADB against the Android installation containing your
purchased copy. First select the device explicitly if more than one appears in
`adb devices`:

### Short version

1. Install GTA: Vice City from Google Play using the purchased account.

2. Select the Android device and package:

    ```sh
    adb devices
    SERIAL=<serial-from-adb-devices>
    PACKAGE=com.rockstargames.gtavc
    ```

3. Wait for Play Asset Delivery to finish, then verify the version, ABI, and
   downloaded packs:

    ```sh
    # check game version and build arch
    adb shell dumpsys package "$PACKAGE" | tr -d '\r' | grep -E 'versionName=|primaryCpuAbi=|dataDir='
    # find game data
    adb shell find /data/user/0/com.rockstargames.gtavc/files/assetpacks -type d -name assets -print
    ```

    The package should report `versionName=2.11.311` and
    `primaryCpuAbi=arm64-v8a`. Wait until `data_sfx1`, `data_sfx2`, and
    `data_streams` have finished downloading.

4. Pull every APK split and the downloaded asset packs into a local staging
   directory. Do not pull directly into the final game directory:

    ```sh
    # staging directory
    STAGE="$PWD/gtavc-311-staging"
    mkdir -p "$STAGE/apks"

    # get path to APK
    adb shell pm path com.rockstargames.gtavc
    # you will get something like package:/data/app/~~.../com.rockstargames.gtavc-../base.apk ...
    # pull the path from output
    adb pull /data/app/~~<string>/com.rockstargames.gtavc-<string>/ "$STAGE/apks/"

    ```

> [!NOTE]
> APK archives are signed ZIP archives, you may extract them like any other ZIP archive, use 7-Zip or any archive manager to open directlt,
> or rename the `.apk` extension to `.zip` and your file manager
> may open it as a ZIP archive.

5. Extract the native libraries from `split_config.arm64_v8a.apk`. You should see `lib/arm64-v8a/libGame.so` and `lib/arm64-v8a/libc++_shared.so` in the staging directory.
   Extract those using `unzip` or your favorite archive manager.

6. Extract `split_data_main.apk`, and get the `assets/` directory from it.

7. Move the extracted files into the port's `gtavc/` directory.
   a. copy the contents of `assets/` into `gtavc/`
   b. copy files from `assetpacks/*/.../.../assets/audio/` into `gtavc/audio/`
   c. copy the libaries from `split_config.arm64_v8a.apk` into `gtavc/`

The finished port directory structure should be something like this:
```text
/userdata/roms/ports/
├── Grand Theft Auto Vice City.sh       # PortMaster launcher
└── gtavc/
    ├── gtavc_linux                        # port-owned AArch64 executable
    ├── libs.aarch64/
    │   └── libSDL3.so.0                   # SDL3-to-system-SDL2 shim
    ├── libGame.so                         # from split_config.arm64_v8a.apk
    ├── libc++_shared.so                   # matching ARM64 NDK runtime
    ├── assetfile.txt
    ├── Adjustable.cfg
    ├── anim/
    ├── audio/
    │   ├── config/                        # APK assets
    │   ├── sfx/                           # data_sfx1 + data_sfx2 packs
    │   └── streams/                       # data_streams pack/radio audio
    ├── data/
    ├── models/
    ├── rockstar/
    ├── texdb/
    ├── text/
    ├── textures/
    ├── scache.txt
    ├── scache_small.txt
    ├── scache_small_low.txt
    ├── stream.ini
    ├── version.txt
    └── ...
```

Do not leave an `assets/` directory around the game data in the final layout:
`assets/data/...` becomes `gtavc/data/...`, and
`assets/audio/streams/...` becomes `gtavc/audio/streams/...`.
