# AudioCommander Linux workspace instructions

## Scope

Build a native x86-64 Linux Mint Cinnamon application and AppImage that ports
the accepted AudioCommander v9.2 behavior. Work only inside this
`linux_version` directory unless the user explicitly authorizes otherwise.

## Preservation

- Treat `reference_windows_v92` as read-only evidence. Never edit it.
- Do not alter, rebuild, rename, or package anything in the parent Windows
  project.
- Keep `dev_notes.txt` append-only. Begin every entry with `YYYY_MM_DD`.
- Preserve every working Linux milestone before replacing it.

## File safety

- Never silently overwrite an existing destination.
- Use the FreeDesktop Trash path when recoverable deletion is available.
- Permanent deletion, move, removable-media writes, and overwrite tests need
  explicit authorization and disposable fixtures.
- Open media read-only and stop playback cleanly before moving/deleting it.
- Never use personal media or removable drives for automated tests.

## Environment safety

- Do not install dependencies without explaining the change and obtaining
  permission.
- Do not launch the GUI, play audio, modify volume, or exercise file mutations
  until the relevant live test is authorized.
- Do not claim AppImage or Linux Mint compatibility without testing it there.

## Initial target

- Linux Mint 22.x Cinnamon, x86-64, with X11 first.
- Native C11, GTK 3, CMake and a type-2 AppImage produced from an AppDir.

