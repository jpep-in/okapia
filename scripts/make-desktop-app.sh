#!/usr/bin/env bash
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Package the desktop build as a macOS application: build/desktop/Okapia.app.
#   usage: make-desktop-app.sh
#
# The executable is src/hosts/desktop's, unchanged. The bundle carries SDL3 in
# Contents/Frameworks, so it runs on a Mac without Homebrew, the Okapia logo as
# its icon, and an ad-hoc signature with the allow-jit entitlement the 68k
# translator's MAP_JIT memory asks for under a hardened runtime (the bundle is
# not signed with one, so it is not needed today; it is there for the day it
# is). Signed ad hoc, it opens on the machine that built it; another Mac asks
# for a right-click > Open the first time, as for any unnotarised application.
#
# Opened from the Finder, Okapia looks for its card folder in this order: the
# folder holding Okapia.app when that is a card folder (Okapia_Prefs in it),
# the last folder opened, otherwise it asks (docs/topics/desktop.md).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "${REPO_ROOT}/scripts/env.sh"

"$MAKE" -C "${REPO_ROOT}/src/hosts/desktop" >/dev/null

OUT="${REPO_ROOT}/build/desktop"
APP="${OUT}/Okapia.app"
BIN="${OUT}/okapia"
VERSION="$(sed -n 's/.*OKAPIA_VERSION "\(.*\)".*/\1/p' "${OUT}/generated/okapia_version.h")"

rm -rf "$APP"
mkdir -p "${APP}/Contents/MacOS" "${APP}/Contents/Frameworks" "${APP}/Contents/Resources"
cp "$BIN" "${APP}/Contents/MacOS/Okapia"

# SDL3, inside the bundle, found relative to the executable.
SDL="$(otool -L "$BIN" | awk '/libSDL3/ { print $1; exit }')"
[ -n "$SDL" ] || { echo "The desktop build does not link SDL3?" >&2; exit 1; }
cp "$SDL" "${APP}/Contents/Frameworks/libSDL3.0.dylib"
chmod u+w "${APP}/Contents/Frameworks/libSDL3.0.dylib"
install_name_tool -id @rpath/libSDL3.0.dylib "${APP}/Contents/Frameworks/libSDL3.0.dylib"
install_name_tool -change "$SDL" @rpath/libSDL3.0.dylib "${APP}/Contents/MacOS/Okapia"
install_name_tool -add_rpath @executable_path/../Frameworks "${APP}/Contents/MacOS/Okapia"

# The icon, from the logo's PNGs (assets/logo, scripts/gen-logo.py).
ICONSET="$(mktemp -d)/Okapia.iconset"
mkdir -p "$ICONSET"
LOGO="${REPO_ROOT}/assets/logo"
for size in 16 32 128 256 512; do
    cp "${LOGO}/okapia-${size}.png" "${ICONSET}/icon_${size}x${size}.png"
    cp "${LOGO}/okapia-$((size * 2)).png" "${ICONSET}/icon_${size}x${size}@2x.png"
done
iconutil -c icns -o "${APP}/Contents/Resources/Okapia.icns" "$ICONSET"
rm -rf "$(dirname "$ICONSET")"

cat > "${APP}/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>                 <string>Okapia</string>
    <key>CFBundleDisplayName</key>          <string>Okapia</string>
    <key>CFBundleIdentifier</key>           <string>org.okapia.desktop</string>
    <key>CFBundleExecutable</key>           <string>Okapia</string>
    <key>CFBundleIconFile</key>             <string>Okapia</string>
    <key>CFBundlePackageType</key>          <string>APPL</string>
    <key>CFBundleShortVersionString</key>   <string>${VERSION}</string>
    <key>CFBundleVersion</key>              <string>${VERSION}</string>
    <key>LSApplicationCategoryType</key>    <string>public.app-category.utilities</string>
    <key>NSHighResolutionCapable</key>      <true/>
    <key>NSHumanReadableCopyright</key>     <string>Copyright © 2026 Jonathan Pepin</string>
</dict>
</plist>
PLIST

ENTITLEMENTS="$(mktemp)"
cat > "$ENTITLEMENTS" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.cs.allow-jit</key> <true/>
</dict>
</plist>
PLIST
codesign --force --sign - "${APP}/Contents/Frameworks/libSDL3.0.dylib"
codesign --force --sign - --entitlements "$ENTITLEMENTS" "$APP"
rm -f "$ENTITLEMENTS"
codesign --verify --deep "$APP"

printf 'Application: %s (%s)\n' "$APP" "$VERSION"
