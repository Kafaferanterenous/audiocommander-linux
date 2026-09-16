#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$root/reference_windows_v92"
sha256sum -c SOURCE_SHA256.txt

