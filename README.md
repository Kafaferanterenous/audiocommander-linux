# AudioCommander for Linux Mint

> ## USE IT AT YOUR OWN RISK !
> This is an experimental, unofficial, community-oriented Linux port. It is
> provided as-is with **no warranty of any kind**. Backup your data before
> using it; the author accepts no liability for data loss, damage, or any
> other consequence of use.

This directory is a prepared handoff for a native Linux port of the accepted
AudioCommander v9.2 Windows release.

Start by reading, in order:

1. `AGENTS.md`
2. `PROMPT_FOR_LINUX_AI.md`
3. `PORTING_SPEC.md`
4. `dev_notes.txt`
5. `reference_windows_v92/README_v92.txt`
6. `reference_windows_v92/V92_PROGRESS_2026_08_10.md`
7. `reference_windows_v92/src/`

`reference_windows_v92/SOURCE_SHA256.txt` authenticates the source snapshot.
It intentionally excludes Windows executables, DLLs, packages, build trees and
user settings.

On Linux, run `sh verify_reference.sh` to verify the snapshot and
`sh preflight.sh` for a read-only environment/dependency report.

Current state: working native Linux port. GTK3 dual-pane audio browser with
file ops, skins/themes, 4 languages, and FFmpeg/PulseAudio playback. 7/7
tests green; AppImage packaging in `packaging/` (see `packaging/README.md`).
Playback needs `libavformat-dev libavcodec-dev libavutil-dev
libswresample-dev libpulse-dev` at build time. See `dev_notes.txt` for the
full history.
