# Prompt to paste into the Linux AI session

Continue the AudioCommander native Linux Mint port in this `linux_version`
directory.

First read `AGENTS.md`, `README.md`, `PORTING_SPEC.md`, `dev_notes.txt`, and the
v9.2 Windows reference documentation, C sources, tests and skins under
`reference_windows_v92`. Treat that snapshot and the parent Windows project as
read-only.

Before changing source:

1. Report `uname -m`, `/etc/os-release`, `$XDG_SESSION_TYPE`, Cinnamon version,
   compiler/CMake versions, and available GTK/FFmpeg/libopenmpt development
   packages.
2. Verify every line in `reference_windows_v92/SOURCE_SHA256.txt`.
3. Identify genuinely portable modules and Windows-only modules; do not claim
   a source file is portable merely because it is C.
4. Present the exact dependency-install command and obtain approval before
   using `apt`, Flatpak, Snap or another installer.
5. Append a numbered implementation plan to `dev_notes.txt`.

Implement only Goal 1 first:

- CMake project and C11/GTK 3 application skeleton;
- dual-pane folder browser with read-only navigation and audio-file filtering;
- directory summaries for audio-file count and total bytes;
- English localization wiring and XDG settings path;
- non-GUI tests for path joining, extension recognition, sorting, summaries,
  playlist order, localization bounds and settings defaults.

Do not implement copy, move, delete, overwrite, playback, volume, removable
media, GUI launch, dependency bundling or AppImage publication in Goal 1.
Define interfaces and test doubles for later use. Never mutate personal files.

At the end of each session, append an exact checkpoint to `dev_notes.txt` and
separate compiled, automatically tested, visually tested, live-audio tested,
packaged and still-unverified state.

