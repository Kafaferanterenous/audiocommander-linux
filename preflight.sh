#!/usr/bin/env sh
set -u
printf '%s\n' '=== Architecture ==='
uname -m
printf '%s\n' '=== Operating system ==='
if [ -r /etc/os-release ]; then cat /etc/os-release; else printf '%s\n' 'missing /etc/os-release'; fi
printf '%s\n' '=== Desktop session ==='
printf 'XDG_CURRENT_DESKTOP=%s\n' "${XDG_CURRENT_DESKTOP-}"
printf 'XDG_SESSION_TYPE=%s\n' "${XDG_SESSION_TYPE-}"
printf '%s\n' '=== Tools ==='
for tool in cc gcc clang cmake ctest pkg-config git appimagetool linuxdeploy; do
    if command -v "$tool" >/dev/null 2>&1; then
        printf '%-14s %s\n' "$tool" "$(command -v "$tool")"
    else
        printf '%-14s %s\n' "$tool" 'not found'
    fi
done
printf '%s\n' '=== Development packages ==='
for package in gtk+-3.0 gio-2.0 libavformat libavcodec libavutil libswresample libopenmpt sdl2; do
    if pkg-config --exists "$package" 2>/dev/null; then
        printf '%-14s %s\n' "$package" "$(pkg-config --modversion "$package")"
    else
        printf '%-14s %s\n' "$package" 'not found'
    fi
done

