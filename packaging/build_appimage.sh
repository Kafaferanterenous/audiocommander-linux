#!/usr/bin/env bash
# Build a GTK3 AppImage of AudioCommander for Linux.
#
# What the AppImage contains:
#   - audiocommander binary
#   - the 8 external skins (data/skins) at usr/bin/skins (resolved at runtime)
#   - GTK3 and its runtime deps (linuxdeploy gtk plugin)
#   - FFmpeg client libs (libavformat/libavcodec/libavutil/libswresample)
#     and PulseAudio client libs (libpulse, libpulse-simple) pulled in
#     automatically from the binary's ldd dependencies.
# User settings are NOT bundled (they live in ~/.config/audiocommander).
#
# Host requirements before running:
#   sudo apt-get install -y cmake gcc pkg-config libgtk-3-dev \
#       libavformat-dev libavcodec-dev libavutil-dev libswresample-dev \
#       libpulse-dev wget
#
# Usage:
#   sh packaging/build_appimage.sh [build-dir]
# Output: ./build-appimage/AudioCommander-<version>-x86_64.AppImage

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${1:-$ROOT/build-appimage}"
TOOLS_DIR="${TOOLS_DIR:-$ROOT/.tools-appimage}"
VERSION="${VERSION:-$(date +%Y%m%d)}"
APPIMAGE_NAME="AudioCommander-${VERSION}-x86_64.AppImage"

mkdir -p "$TOOLS_DIR"

download() { # $1=url $2=out
    if command -v wget >/dev/null 2>&1; then
        wget -q --show-progress -O "$2" "$1"
    else
        curl -fL --progress-bar -o "$2" "$1"
    fi
}

# 1. linuxdeploy core (bundles ldd deps, runs plugins, makes the AppImage)
if [ ! -x "$TOOLS_DIR/linuxdeploy" ]; then
    echo "[1/5] downloading linuxdeploy"
    download "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" "$TOOLS_DIR/linuxdeploy"
    chmod +x "$TOOLS_DIR/linuxdeploy"
fi

# 2. GTK plugin (bundles the GTK3 runtime + gdk-pixbuf loaders)
if [ ! -x "$TOOLS_DIR/linuxdeploy-plugin-gtk.sh" ]; then
    echo "[2/5] downloading linuxdeploy GTK plugin"
    download "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh" "$TOOLS_DIR/linuxdeploy-plugin-gtk.sh"
    chmod +x "$TOOLS_DIR/linuxdeploy-plugin-gtk.sh"
fi

# 3. AppImage output helper
if [ ! -x "$TOOLS_DIR/linuxdeploy-plugin-appimage" ]; then
    echo "[3/5] downloading linuxdeploy AppImage plugin"
    download "https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/continuous/linuxdeploy-plugin-appimage-x86_64.AppImage" "$TOOLS_DIR/linuxdeploy-plugin-appimage"
    chmod +x "$TOOLS_DIR/linuxdeploy-plugin-appimage"
fi

# 4. Build the binary with a relocatable prefix
echo "[4/5] configuring + building (Release)"
cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD_DIR" -j"$(nproc)"

# 5. Stage the AppDir and package
echo "[5/5] staging AppDir + bundling deps"
rm -rf "$BUILD_DIR/AppDir"
mkdir -p "$BUILD_DIR/AppDir/usr/bin"
cp "$BUILD_DIR/audiocommander" "$BUILD_DIR/AppDir/usr/bin/audiocommander"
cp -r "$ROOT/data/skins" "$BUILD_DIR/AppDir/usr/bin/skins"
cp "$SCRIPT_DIR/audiocommander.desktop" "$BUILD_DIR/AppDir/audiocommander.desktop"
cp "$SCRIPT_DIR/audiocommander.png" "$BUILD_DIR/AppDir/audiocommander.png"
mkdir -p "$BUILD_DIR/AppDir/usr/share/icons/hicolor/256x256/apps"
cp "$SCRIPT_DIR/audiocommander.png" "$BUILD_DIR/AppDir/usr/share/icons/hicolor/256x256/apps/audiocommander.png"

# Bundle a SoundFont for MIDI playback
SOUNDFONT_FOUND=0
for sf in /usr/share/sounds/sf2/FluidR3_GM.sf2 \
          /usr/share/sounds/sf2/FluidR3_GS.sf2 \
          /usr/share/sounds/sf2/TimGM6mb.sf2 \
          /usr/share/soundfonts/FluidR3_GM.sf2 \
          /usr/share/soundfonts/default-GM.sf2 \
          /usr/share/soundfonts/yamaha.sf2; do
    if [ -f "$sf" ]; then
        mkdir -p "$BUILD_DIR/AppDir/usr/share/sounds/sf2"
        cp "$sf" "$BUILD_DIR/AppDir/usr/share/sounds/sf2/default-GM.sf2"
        SOUNDFONT_FOUND=1
        echo "Bundled SoundFont: $sf"
        break
    fi
done
if [ "$SOUNDFONT_FOUND" -eq 0 ]; then
    echo "WARNING: No SoundFont found - MIDI playback will not work"
fi

export PATH="$TOOLS_DIR:$PATH"
export OUTPUT="${BUILD_DIR}/${APPIMAGE_NAME}"
export VERSION

"$TOOLS_DIR/linuxdeploy" \
    --appdir "$BUILD_DIR/AppDir" \
    --executable "$BUILD_DIR/AppDir/usr/bin/audiocommander" \
    --desktop-file "$BUILD_DIR/AppDir/audiocommander.desktop" \
    --icon-file "$BUILD_DIR/AppDir/audiocommander.png" \
    --plugin gtk \
    --output appimage

echo
echo "DONE: ${OUTPUT}"
echo "Run it with:"
echo "  ${OUTPUT}          (needs libfuse2)"
echo "or, without FUSE:"
echo "  ${OUTPUT} --appimage-extract-and-run"
