#!/usr/bin/env bash
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Make a release of Okapia (docs/contributing/release.md): one version for every
# product, the image for each board and the desktop app, described by one
# okapia-manifest.json.
#
#   usage: release.sh <YY.MM.N> [--publish]
#
# Without --publish it stops with the assets in build/release/<YY.MM.N>/ and the
# tag v<YY.MM.N> made locally, nothing sent anywhere: a rehearsal, or the first
# half of a release. With --publish it pushes the tag and makes the GitHub
# release, the manifest uploaded last so that an app checking at that moment
# never reads a manifest naming assets not yet there. Publishing is outward-
# facing and cannot be taken back (an app may read the manifest at once): the
# script asks before it.
#
# Steps, each stopping the release on failure:
#   1. checks: the number, a clean tree on main and up to date, a new tag greater
#      than the last, the patches in, the notes in docs/releases/<YY.MM.N>.md
#   2. tests: the host tests; QEMU to the Finder and the durability verdict; the
#      desktop to the Finder of System 7.1, headless
#   3. the tag v<YY.MM.N>, annotated with the notes
#   4. builds from the tag: the Pi 4 kernel and its card, the desktop app (signed
#      with OKAPIA_SIGN_IDENTITY when set, ad hoc otherwise), each zipped
#   5. the manifest (scripts/make-release.sh)
#   6. the hardware gate, by hand: a real Pi 4 boots the board's zip
#   7. with --publish: push the tag, create the release, upload the assets
#
# Needs: qemu/sd-contents with the test card (boot71.img, a 68k ROM), QEMU, the
# network (the firmware licences, GitHub). The GitHub token for --publish is read
# from the keychain, never from the repository:
#   security add-generic-password -s okapia-release -a github -w <token>
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"
# shellcheck source=/dev/null
. scripts/env.sh

VERSION="${1:-}"
PUBLISH=0
[ "${2:-}" = "--publish" ] && PUBLISH=1
REPOSITORY="jpep-in/okapia"                     # install.h, OKAPIA_REPOSITORY
TAG="v${VERSION}"
OUT="${REPO_ROOT}/build/release/${VERSION}"
NOTES="${REPO_ROOT}/docs/releases/${VERSION}.md"

say()  { printf '\n==> %s\n' "$1"; }
fail() { printf 'release: %s\n' "$1" >&2; exit 1; }
ask()  { local a; read -r -p "$1 [y/N] " a; [ "$a" = "y" ] || [ "$a" = "Y" ]; }

# 1. Checks ----------------------------------------------------------------------
say "Checks"
[[ "$VERSION" =~ ^[0-9]{2}\.(1[0-2]|[1-9])\.[1-9][0-9]*$ ]] \
    || fail "usage: release.sh <YY.MM.N> [--publish]   (26.11.1: year, month, counter)"
[ -z "$(git status --porcelain)" ] || fail "the tree has changes: a release is built from a clean checkout"
[ "$(git rev-parse --abbrev-ref HEAD)" = "main" ] || fail "not on main"
git fetch -q origin main --tags
[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] || fail "main is not origin/main"
git rev-parse -q --verify "refs/tags/${TAG}" >/dev/null && fail "${TAG} exists already"
LAST="$(git tag --list 'v[0-9]*' | sed 's/^v//' | sort -t. -k1,1n -k2,2n -k3,3n | tail -1)"
if [ -n "$LAST" ]; then
    NEWER="$(printf '%s\n%s\n' "$LAST" "$VERSION" | sort -t. -k1,1n -k2,2n -k3,3n | tail -1)"
    [ "$NEWER" = "$VERSION" ] && [ "$LAST" != "$VERSION" ] || fail "${VERSION} is not after ${LAST}"
fi
scripts/apply-patches.sh --check >/dev/null || fail "patches missing: scripts/apply-patches.sh"
[ -s "$NOTES" ] || fail "no notes: write ${NOTES#"$REPO_ROOT"/} (docs/releases/README.md)"
echo "Okapia ${VERSION}, after ${LAST:-nothing}, from $(git rev-parse --short HEAD)"

# 2. Tests -----------------------------------------------------------------------
say "Host tests"
make -C tests/host >/dev/null || fail "host tests"

say "QEMU: System 7.1 to the Finder, then the durability verdict"
scripts/build-libs.sh qemu >/dev/null
"$MAKE" -C src/kernel okapia-clean >/dev/null
"$MAKE" -C src/kernel -j8 >/dev/null || fail "the QEMU kernel"
scripts/make-sd-image.sh >/dev/null
scripts/screenshot.sh 60 | tail -1
scripts/run-test.sh | tail -3 || fail "durability (run-test.sh)"
ask "Did the QEMU screenshot above show the Finder?" || fail "QEMU did not reach the Finder"

say "The desktop: headless to the Finder"
make -C src/hosts/desktop -j8 >/dev/null || fail "the desktop app"
DESK="$(mktemp -d -t okapiarelease)"
OKAPIA_MENU_ANSWERED=1 build/desktop/okapia --headless --work-dir "${DESK}/card" --seconds 25 \
    --screenshot "${DESK}/finder.png" --quiet qemu/sd-contents || fail "the desktop run"
echo "Screenshot: ${DESK}/finder.png"
open "${DESK}/finder.png" 2>/dev/null || true
ask "Does it show the Finder?" || fail "the desktop did not reach the Finder"

# 3. The tag ---------------------------------------------------------------------
say "Tag ${TAG}"
git tag -a "$TAG" -F "$NOTES"
FINISHED=""
# Any stop from here on, a failed command or a refusal, removes the local tag:
# a tag that names no release would be taken for one by the next run.
on_exit() {
    if [ -z "$FINISHED" ]; then
        git tag -d "$TAG" >/dev/null 2>&1 || true
        echo "release: stopped; the local tag ${TAG} is removed" >&2
    fi
}
trap on_exit EXIT

# 4. Builds, from the tag (HEAD is the tag and the tree clean: OKAPIA_RELEASE is set)
say "The Pi 4 kernel and its card"
scripts/build-libs.sh pi4 >/dev/null
"$MAKE" -C src/kernel okapia-clean >/dev/null
"$MAKE" -C src/kernel -j8 >/dev/null
grep -q "OKAPIA_RELEASE \"${VERSION}\"" build/generated/okapia_version.h \
    || fail "the build does not carry ${VERSION}: is HEAD the tag, the tree clean?"
scripts/make-pi-sd.sh >/dev/null

say "The desktop app"
scripts/make-desktop-app.sh | tail -1
rm -rf "$OUT"
mkdir -p "$OUT"
APP_ZIP="${OUT}/Okapia-${VERSION}-macos-arm64.zip"
ditto -c -k --sequesterRsrc --keepParent build/desktop/Okapia.app "$APP_ZIP"
TEAM=""
if [ -n "${OKAPIA_SIGN_IDENTITY:-}" ]; then
    TEAM="$(codesign -dv build/desktop/Okapia.app 2>&1 | sed -n 's/^TeamIdentifier=//p')"
fi

# 5. The manifest ----------------------------------------------------------------
say "The manifest"
OKAPIA_RELEASE="$VERSION" OKAPIA_PAGE="https://github.com/${REPOSITORY}/releases/tag/${TAG}" \
OKAPIA_APP_ZIP="$APP_ZIP" OKAPIA_APP_PLATFORM=macos-arm64 OKAPIA_APP_MIN_OS=14.0 OKAPIA_APP_TEAM="$TEAM" \
    scripts/make-release.sh "$OUT"
ls -la "$OUT"

# 6. The hardware gate -----------------------------------------------------------
say "On a real Raspberry Pi 4"
echo "Unzip ${OUT}/okapia-${VERSION}-pi4.zip onto a FAT32 card with a ROM and a System,"
echo "or prepare it from the desktop app: build/desktop/okapia --release ${OUT}"
ask "Did the Pi 4 boot it to the Finder, and shut down from the Special menu?" || fail "the hardware gate"

if [ "$PUBLISH" != 1 ]; then
    FINISHED=1
    say "Done, not published: ${OUT}, tag ${TAG} kept locally (git tag -d ${TAG} to drop it)"
    exit 0
fi

# 7. Publish ---------------------------------------------------------------------
TOKEN="$(security find-generic-password -s okapia-release -a github -w 2>/dev/null)" \
    || fail "no GitHub token in the keychain (security add-generic-password -s okapia-release -a github -w …)"
ask "Publish Okapia ${VERSION} on github.com/${REPOSITORY}? This cannot be taken back" || fail "not published"

say "Publishing"
git push origin "$TAG"
FINISHED=1                                      # pushed: the tag stays, whatever follows
API="https://api.github.com/repos/${REPOSITORY}"
AUTH=(-H "Authorization: Bearer ${TOKEN}" -H "Accept: application/vnd.github+json")
BODY="$(python3 -c 'import json,sys; print(json.dumps({"tag_name": sys.argv[1], "name": "Okapia " + sys.argv[2],
    "body": open(sys.argv[3], encoding="utf-8").read()}))' "$TAG" "$VERSION" "$NOTES")"
RELEASE="$(curl -fsS "${AUTH[@]}" -X POST "${API}/releases" -d "$BODY")"
UPLOAD="$(printf '%s' "$RELEASE" | python3 -c 'import json,sys; print(json.load(sys.stdin)["upload_url"].split("{")[0])')"
upload() {
    curl -fsS "${AUTH[@]}" -H "Content-Type: application/octet-stream" --data-binary "@$1" \
        "${UPLOAD}?name=$(basename "$1")" >/dev/null || fail "upload of $(basename "$1")"
    echo "  $(basename "$1")"
}
for f in "$OUT"/*.zip; do upload "$f"; done
upload "$OUT/okapia-manifest.json"                # last: the record that the release is whole
say "Published: https://github.com/${REPOSITORY}/releases/tag/${TAG}"
echo "Then: the status lines of the docs, docs/README.md's version (docs/contributing/release.md, step 8)."
