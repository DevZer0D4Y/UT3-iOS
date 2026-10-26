#!/usr/bin/env bash
# Package the built Mac app as a disk image: UT3.app and a link to
# /Applications, to drag it across.
#
#   APP   built bundle (default: out/build/mac-arm64-release/UT3.app)
#   OUT   output path (default: out/UT3-macOS.dmg)
#
# The app is signed ad hoc, which Apple Silicon requires to run anything; it
# names no certificate or team. It is not notarized, so the first launch has to
# be allowed in System Settings (see README.md). The bundle is checked first:
# no game files, no signing material, and no build machine paths in the
# binaries.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
APP="${APP:-$HERE/../out/build/mac-arm64-release/UT3.app}"
OUT="${OUT:-$HERE/../out/UT3-macOS.dmg}"

[ -d "$APP" ] || { echo "Built app not found at $APP - run the build first." >&2; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
# Clone the local APFS bundle to avoid duplicating the large executable.
# Fall back on filesystems that do not support copy-on-write clones.
if ! cp -cR "$APP" "$STAGE/" 2>/dev/null; then
  cp -R "$APP" "$STAGE/"
fi
BUNDLE="$STAGE/$(basename "$APP")"
EXE="$BUNDLE/Contents/MacOS/$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$BUNDLE/Contents/Info.plist")"

rm -rf "$BUNDLE/Contents/_CodeSignature" "$BUNDLE/Contents/embedded.provisionprofile"
find "$BUNDLE" \( -name '.DS_Store' -o -name '._*' \) -delete

# Never ship game data or signing material.
found="$(find "$BUNDLE" -type f \( -iname '*.iso' -o -iname '*.xiso' -o -iname '*.xex' \
           -o -iname '*.pac' -o -iname '*.xxx' -o -iname '*.tfc' -o -iname '*.bik' \
           -o -iname '*.provisionprofile' -o -iname '*.p12' -o -iname '*.cer' \) | head -5)"
if [ -n "$found" ]; then
  echo "Refusing to package game or signing files:" >&2
  echo "$found" >&2
  exit 1
fi
# Source file names end up in log and assert messages; they must not name
# the build machine's folders.
for binary in "$EXE" "$BUNDLE"/Contents/Frameworks/*.dylib; do
  if strings -a "$binary" | grep -qE '/Users/|/Volumes/|/private/var/folders/'; then
    echo "Refusing to package $(basename "$binary"): it contains build machine paths:" >&2
    strings -a "$binary" | grep -E '/Users/|/Volumes/|/private/var/folders/' | head -3 >&2
    exit 1
  fi
done

# Libraries first, then the bundle, which seals them and the resources.
for d in "$BUNDLE"/Contents/Frameworks/*.dylib; do codesign -f -s - "$d"; done
# ADHOC_BUNDLE_SIGN=0 keeps the linker's signature on the executable and
# leaves the bundle unsealed, should local codesign not process it.
if [ "${ADHOC_BUNDLE_SIGN:-1}" = 1 ]; then
  codesign -f -s - "$BUNDLE"
  codesign --verify --strict "$BUNDLE"
fi

ln -s /Applications "$STAGE/Applications"
mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
hdiutil create -quiet -volname "UT3" -srcfolder "$STAGE" -fs HFS+ -format UDZO "$OUT"
echo "Wrote $OUT"
