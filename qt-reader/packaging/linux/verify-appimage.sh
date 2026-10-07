#!/usr/bin/env bash
set -euo pipefail

appimage="$(realpath "$1")"
test -s "$appimage"
test -x "$appimage"
verify_dir="$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/bibi-appimage-check.XXXXXX")"
trap 'rm -rf "$verify_dir"' EXIT
(cd "$verify_dir" && "$appimage" --appimage-extract >/dev/null)
root="$verify_dir/squashfs-root"

# Check the archive itself, not just the AppDir used to build it. A deploy tool
# renamed to the application filename must never pass the release gate.
test -x "$root/usr/bin/BibiQtReader"
test -f "$root/usr/share/applications/BibiQtReader.desktop"
grep -Eq '^Exec=BibiQtReader( |$)' "$root/usr/share/applications/BibiQtReader.desktop"
test -n "$(find "$root/usr" -type f -name QtWebEngineProcess -print -quit)"
fonts="$root/usr/share/BibiQtReader/fonts"
test -f "$fonts/fonts.conf"
test -f "$fonts/LICENSE-NotoCJK.txt"
for font in NotoSansJP-Regular NotoSansJP-Bold NotoSerifJP-Regular NotoSerifJP-Bold; do
  test -s "$fonts/$font.otf"
done
echo "Verified BibiQtReader, QtWebEngineProcess and Japanese fonts in $appimage"
