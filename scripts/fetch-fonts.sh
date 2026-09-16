#!/usr/bin/env bash
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
# Fetch the X11 Adobe Helvetica bitmap faces and vendor the ones the firmware
# uses into assets/fonts/, trimmed to the characters it can show.
#
# Why these: Geneva was in substance a Helvetica fitted to a small pixel grid,
# so a bitmap Helvetica is the closest thing to it that anyone may redistribute.
# The X11 BDFs are plain text — no rasteriser, no build dependency, and our own
# parser reads them (scripts/gen-font.py).
#
# Licence: Adobe Systems 1984-1989/1994 and Digital Equipment 1988/1994, with
# permission to use, copy, modify, distribute and sell without fee provided the
# notices are kept. Compatible with the repository's GPLv3. COPYING is vendored
# beside the faces and must stay there.
#
# Run once; the result is committed, so the build never needs the network.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${REPO_ROOT}/assets/fonts"
BASE="https://www.x.org/releases/individual/font"
VERSION="1.0.4"

# <dpi> <point size>: the pair that yields the pixel cell we want. The cell is
# FONT_ASCENT + FONT_DESCENT, not the point size — helvR12 at 75 dpi is a
# fourteen-row cell, and the ladder is built from the cells, not the names.
WANTED_75="10 12 14 24"
WANTED_100="14 24"

WORK="$(mktemp -d -t okapiafonts)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$DEST"

for dpi in 75 100; do
    pkg="font-adobe-${dpi}dpi-${VERSION}"
    printf 'Fetching %s\n' "$pkg"
    curl -sSL -o "${WORK}/${pkg}.tar.gz" "${BASE}/${pkg}.tar.gz"
    tar xzf "${WORK}/${pkg}.tar.gz" -C "$WORK"

    eval "sizes=\$WANTED_${dpi}"
    for size in $sizes; do
        for weight in R B; do
            src="${WORK}/${pkg}/helv${weight}$(printf '%02d' "$size").bdf"
            [ -f "$src" ] || { echo "missing $src" >&2; exit 1; }
            out="${DEST}/helv${weight}${size}-${dpi}.bdf"
            "${REPO_ROOT}/scripts/trim-bdf.py" "$src" "$out"
            printf '  %-28s %s\n' "$(basename "$out")" \
                   "$(wc -c < "$out" | tr -d ' ') bytes"
        done
    done

    if [ ! -f "${DEST}/COPYING" ]; then
        cp "${WORK}/${pkg}/COPYING" "${DEST}/COPYING"
    fi
done

printf '\nVendored in %s — commit them; the build must not need the network.\n' "$DEST"
