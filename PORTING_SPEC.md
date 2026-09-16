# AudioCommander Linux porting specification

## Product goal

Create a native Linux Mint Cinnamon dual-pane audio browser/player with the
accepted AudioCommander v9.2 behavior. Do not run or bundle the Windows release
through Wine.

## Preserved behavior

- Independent dual-pane navigation, sortable audio listings and active-pane
  operations.
- WAV, MP3, M4A/MP4 audio, FLAC, WMA, Ogg Vorbis, Opus, ALAC, MIDI, MOD, S3M
  and XM support where the packaged decoder policy genuinely supports them.
- Metadata/duration display, directory audio totals and sequential Play All.
- No silent overwrite. Recoverable Trash deletion where available. Strong,
  safe-default warning before permanent removable-media deletion.
- Responsive single-file copy with genuine byte progress. Fast copies show no
  dialog; copies active after 600 ms show progress and patient wording.
- English, Simplified Chinese, Italian and Polish localization.
- Pastel default, native recovery appearance and the eight external `.skn`
  palette names retained where practical through GTK CSS.

## Linux replacements

| Windows facility | Linux design |
| --- | --- |
| Win32 controls/message loop | C11 and GTK 3/GIO |
| drive letters and shell dialogs | mounted volumes through GIO/GVolumeMonitor |
| CopyFileEx/SHFileOperation | GIO or POSIX worker operations with explicit progress |
| Windows Recycle Bin | FreeDesktop Trash via GIO |
| Media Foundation | FFmpeg/libav decoding with a Linux output backend |
| WinMM/Windows audio output | SDL2, PipeWire or PulseAudio abstraction |
| SAPI/N/A | no speech requirement |
| sidecar Windows INI | `$XDG_CONFIG_HOME/audiocommander/settings.ini` |
| Win32 owner drawing | GTK CSS generated from validated `.skn` palettes |

## Proposed layout

```text
src/
  main.c
  app.c/.h
  browser.c/.h
  file_ops.c/.h
  copy_progress.c/.h
  playlist.c/.h
  playback.c/.h
  playback_ffmpeg.c/.h
  playback_openmpt.c/.h
  media_info.c/.h
  settings.c/.h
  localization.c/.h
  skin.c/.h
tests/
packaging/
skins/
```

## Milestones

1. Environment audit, CMake, read-only dual-pane browsing and portable tests.
2. GTK layout, scaling, localization, summaries, settings and authorized GUI
   validation.
3. Playback architecture, FFmpeg formats and separately authorized audio test.
4. libopenmpt tracker support and malformed-file regression fixtures.
5. Playlist/sequential playback and seek/volume stress tests.
6. Copy/move/overwrite implementation using disposable local fixtures.
7. Trash/permanent-delete/removable-media policy with safety test doubles;
   real removable tests require explicit authorization.
8. `.skn` to GTK CSS appearance and visual validation.
9. MIDI policy: choose and licence a real backend/soundfont or document it as
   unsupported; never claim support from file recognition alone.
10. AppDir/AppImage, dependency/licence audit, hashes and clean-Mint testing.

## Packaging acceptance

- Type-2 x86-64 AppImage with desktop file, icons and AppStream metadata.
- Bundle required non-base libraries and decoder notices without bundling user
  settings or pre-accepting the first-run notice.
- `--appimage-extract-and-run` fallback documented for systems without FUSE.
- Build on an intentionally chosen compatibility baseline and test on a clean
  Linux Mint Cinnamon machine.
- Verify every AppImage payload file and publish SHA-256 plus source/build
  recipes sufficient to reproduce the package.

