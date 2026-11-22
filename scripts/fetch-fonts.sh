#!/usr/bin/env bash
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
# Fetch TeX Gyre Heros and vendor its regular and bold faces into assets/fonts/,
# trimmed to the characters the firmware can show.
#
# Why these: a Helvetica of its own, the face the boot menu had as X11 bitmaps,
# now as outlines that src/firmware/okapia_font_vector.cpp rasterises at the
# height the theme asks for, antialiased (docs/topics/boot-menu.md, Fonts).
#
# Licence: the GUST Font License, a LaTeX Project Public Licence for fonts;
# redistribution, modified or not, is allowed with the licence kept beside the
# faces. Compatible with the repository's GPLv3. GUST-FONT-LICENSE.txt is
# vendored with them and must stay there.
#
# The subset keeps Latin-1, Latin Extended-A, typographic punctuation and the
# few symbols a Macintosh interface names, with the pair kerning (GPOS 'kern')
# and without the hints, which the rasteriser does not read. It needs fontTools
# (pip install fonttools) when this is run, never when the firmware is built:
# the result is committed, so the build needs neither the network nor Python
# packages.
#
#   usage: fetch-fonts.sh [python with fontTools]
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${REPO_ROOT}/assets/fonts"
PYTHON="${1:-python3}"
VERSION="2.004"
ARCHIVE="https://www.gust.org.pl/projects/e-foundry/tex-gyre/heros/qhv${VERSION}otf.zip"
LICENCE="https://www.gust.org.pl/projects/e-foundry/licenses/GUST-FONT-LICENSE.txt"

# U+0020-007E, U+00A0-00FF, Latin Extended-A, the dashes, quotes, bullet,
# ellipsis, per mille and guillemets, the euro, the trade mark, the arrows,
# and the Command and Option signs.
UNICODES="U+0020-007E,U+00A0-017F,U+2010-2027,U+2030,U+2039-203A,U+20AC,U+2122,U+2190-2193,U+2318,U+2325"

"$PYTHON" -c "import fontTools" 2>/dev/null \
    || { echo "fontTools is needed: pip install fonttools, then give that python" >&2; exit 1; }

WORK="$(mktemp -d -t okapiafonts)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$DEST"

printf 'Fetching TeX Gyre Heros %s\n' "$VERSION"
curl -fsSL -o "${WORK}/heros.zip" "$ARCHIVE"
curl -fsSL -o "${DEST}/GUST-FONT-LICENSE.txt" "$LICENCE"
unzip -q -o "${WORK}/heros.zip" texgyreheros-regular.otf texgyreheros-bold.otf -d "$WORK"

for face in regular bold; do
    out="${DEST}/texgyreheros-${face}.otf"
    "$PYTHON" -m fontTools.subset "${WORK}/texgyreheros-${face}.otf" \
        --unicodes="$UNICODES" --layout-features=kern --no-hinting \
        --name-IDs='*' --name-legacy --name-languages='*' \
        --output-file="$out"
    printf '  %-28s %s bytes\n' "$(basename "$out")" "$(wc -c < "$out" | tr -d ' ')"
done
