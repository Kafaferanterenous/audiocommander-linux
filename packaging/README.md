# AppImage packaging

> ## USE IT AT YOUR OWN RISK !
> Experimental unofficial port. No warranty; backup your data before use.

## What the AppImage contains

Bundled *inside* the AppImage:

- the `audiocommander` binary
- the 8 external skins from `data/skins` (resolved at runtime from
  `usr/bin/skins`, the exe-relative fallback in `settings.c`)
- the GTK3 runtime and its plugins (via the linuxdeploy GTK plugin)
- FFmpeg client libraries (`libavformat`, `libavcodec`, `libavutil`,
  `libswresample`) and PulseAudio client libraries (`libpulse`,
  `libpulse-simple`) — pulled in automatically from the binary's `ldd`
  dependencies, so target machines do **not** need FFmpeg installed.

Not bundled: user settings (stored in `~/.config/audiocommander/`), fonts,
PulseAudio *server* (the host's own sound server is used).

## Build on a Mint/Ubuntu host

Install build + dev dependencies first:

    sudo apt-get update
    sudo apt-get install -y cmake gcc pkg-config libgtk-3-dev \
        libavformat-dev libavcodec-dev libavutil-dev libswresample-dev \
        libpulse-dev wget

Then package:

    sh packaging/build_appimage.sh

Output: `build-appimage/AudioCommander-<YYYYMMDD>-x86_64.AppImage`

## Run the AppImage

    ./build-appimage/AudioCommander-*.AppImage

If FUSE is not available (common on containers / newer distros):

    ./build-appimage/AudioCommander-*.AppImage --appimage-extract-and-run

## Build the app natively (no AppImage)

    sudo apt-get install -y cmake gcc pkg-config libgtk-3-dev \
        libavformat-dev libavcodec-dev libavutil-dev libswresample-dev \
        libpulse-dev
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j
    ./build/audiocommander

## Run the tests

    cmake -S . -B build && cmake --build build && ctest --test-dir build

## Runtime troubleshooting

| Symptom | Fix |
| --- | --- |
| No sound on playback | PulseAudio/PipeWire not running: start your session's sound server, then restart the app |
| Skins missing in the theme list | The 8 `.skn` files are looked up in `$XDG_DATA_HOME/audiocommander/skins`, `~/.local/share/audiocommander/skins`, `AUDIOCOMMANDER_SKINS_DIR`, then the exe-relative `skins` folder. Copy `data/skins/*.skn` to one of those |
| AppImage won't start | Use `--appimage-extract-and-run` (FUSE missing), or install `libfuse2` |
