#!/usr/bin/env bash
# Install the Japanese fonts bundled with the AppImage into an AppDir.
# Usage: install-fonts.sh <AppDir>
set -euo pipefail

appdir="$1"
dest="$appdir/usr/share/BibiQtReader/fonts"
here="$(cd "$(dirname "$0")" && pwd)"

# Noto Sans/Serif CJK JP subsets (SIL OFL 1.1), pinned to a commit and hash.
commit=f8d157532fbfaeda587e826d4cd5b21a49186f7c
base="https://raw.githubusercontent.com/notofonts/noto-cjk/$commit"
files=(
  "Sans/SubsetOTF/JP/NotoSansJP-Regular.otf dff723ba59d57d136764a04b9b2d03205544f7cd785a711442d6d2d085ac5073"
  "Sans/SubsetOTF/JP/NotoSansJP-Bold.otf 1b0edfb500b73a4fa8a4fcaae1bbbd403994e08e73e3e0da37e70d3853f42c5f"
  "Serif/SubsetOTF/JP/NotoSerifJP-Regular.otf 2c9a12dbd4f2408c4610c7ee84a108b62d7236c3775baed618c64d9cb44b2f04"
  "Serif/SubsetOTF/JP/NotoSerifJP-Bold.otf 1e03488a0d5e819f07fcd74f54703a7961ba466d3ae900f8a2a730541e6d4543"
  "Sans/LICENSE 6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2"
)

mkdir -p "$dest"
for entry in "${files[@]}"; do
  read -r path sha256 <<<"$entry"
  out="$dest/$(basename "$path")"
  [ "$(basename "$path")" = LICENSE ] && out="$dest/LICENSE-NotoCJK.txt"
  curl -fsSL --retry 3 -o "$out" "$base/$path"
  echo "$sha256  $out" | sha256sum -c --quiet -
done
install -m644 "$here/fonts.conf" "$dest/fonts.conf"
