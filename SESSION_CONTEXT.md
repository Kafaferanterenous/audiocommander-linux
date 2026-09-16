# AudioCommander Session Context

## Current State
- **Project**: AudioCommander Linux port at `/home/dragon/opencode/004_2026_08_10_audiocomander_linux_version/`
- **Build**: CMake + GTK3 + FFmpeg + PulseAudio + FluidSynth (optional)
- **AppImage**: `build-appimage/AudioCommander-20260818-x86_64.AppImage` (76MB)
- **All tasks completed**: Swap button consolidated, MIDI crash fixed, SoundFont bundled, AppImage rebuilt

## Completed Changes
1. **Single swap button** — moved from pane toolbars to bottom bar, left of Stop button. Layout: `[summary-left] [Swap (F6)] [Stop] [Refresh (F5)] [summary-right]`
2. **MIDI crash fix** — `fluid_settings` use-after-free in `playback.c`. `delete_fluid_settings()` moved to `close_midi()` (was called in `open_midi()` before `fluid_synth_sfload`)
3. **SoundFont bundled** — `TimGM6mb.sf2` copied to AppImage as `default-GM.sf2`
4. **set_button_label crash fix** — `app.c:1107` was passing `char*` to function expecting `wchar_t*` — fixed with `utf8_to_buffer()`

## Key Files Modified
- `src/app.c` — toolbar layout, bottom bar (swap button), `apply_language`, `set_button_label` fix, smoke test
- `src/playback.c` — FluidSynth MIDI playback, `midi_settings` lifetime management
- `src/minimal_fluidsynth.h` — minimal FluidSynth type declarations (avoids `-dev` package)
- `packaging/build_appimage.sh` — SoundFont bundling, requires `bash` not `sh`

## Config Notes
- F-key scheme: left pane = F-key only, right pane = Ctrl+F-key
- No monospace font (removed from settings)
- Pastel theme: lavender/indigo palette (high contrast)
- `wchar_from_utf8` does not exist — use `utf8_to_buffer()`
- FluidSynth lib: `/usr/lib/x86_64-linux-gnu/libfluidsynth.so.3`
- SoundFont: `/usr/share/sounds/sf2/TimGM6mb.sf2`
- Smoke test pane width tolerance: 400px (right pane wider due to "Ctrl+" labels)

## Samba Shares (pending user manual setup)
- Script at: `setup_samba.sh`
- Shares: `GPT_Models` (D1_NTFS_Black500Gb) and `D3_NTFS_500GB`
- User: `dragon`, password: `dragon781`
