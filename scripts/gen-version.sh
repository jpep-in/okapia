#!/usr/bin/env bash
# Writes the build's version into a header, and only when it changed.
#   usage: gen-version.sh <output.h>
#
# YYMMDD-sha: the date of the commit built (not of the build, which is never
# the same twice) and its short hash. A tree with changes carries a "+" and four
# characters of a hash of those changes, so two builds of one dirty tree read
# alike, a further edit reads differently, and a commit takes the suffix away:
#   261116-5926cec         a clean checkout of that commit
#   261116-5926cec+3fa9    the same commit with edits, tracked or new
#   unknown                no git, as in an archive
#
# OKAPIA_RELEASE: the release number, YY.MM.N, when HEAD is exactly a tag
# v<YY.MM.N> and the tree is clean — a release build (docs/contributing/release.md);
# "" otherwise, and the desktop app then knows it is a development build, which
# it never replaces with a release.
#
# Run by the Makefiles each time they are read, so the header always exists and
# is older than its objects unless the version moved: the two files that include
# it are rebuilt then and nothing else. external/ and reference/ are left out of
# the hash — a submodule's own work tree is not this project's change, and the
# patches applied to it are checked by apply-patches.sh --check.
#
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

OUT="${1:?usage: gen-version.sh <output.h>}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

VERSION="unknown"
RELEASE=""
if git -C "$REPO_ROOT" rev-parse --verify -q HEAD >/dev/null 2>&1; then
    SHA="$(git -C "$REPO_ROOT" rev-parse --short=7 HEAD)"
    DAY="$(git -C "$REPO_ROOT" log -1 --format=%cd --date=format:%y%m%d)"
    VERSION="${DAY}-${SHA}"
    EXCLUDE=(':(exclude)external' ':(exclude)reference' ':(exclude)scripts/__pycache__')
    CHANGES="$(
        {
            git -C "$REPO_ROOT" diff HEAD --no-ext-diff --no-color -- . "${EXCLUDE[@]}"
            git -C "$REPO_ROOT" ls-files --others --exclude-standard -z -- . "${EXCLUDE[@]}" \
                | (cd "$REPO_ROOT" && xargs -0 git hash-object --no-filters -- 2>/dev/null || true)
        } | git hash-object --stdin
    )"
    if [ -n "$(git -C "$REPO_ROOT" diff HEAD --name-only -- . "${EXCLUDE[@]}"
               git -C "$REPO_ROOT" ls-files --others --exclude-standard -- . "${EXCLUDE[@]}")" ]; then
        VERSION="${VERSION}+${CHANGES:0:4}"
    else
        for TAG in $(git -C "$REPO_ROOT" tag --points-at HEAD --list 'v[0-9]*' 2>/dev/null); do
            if [[ "$TAG" =~ ^v([0-9]{2}\.[0-9]{1,2}\.[0-9]+)$ ]]; then
                RELEASE="${BASH_REMATCH[1]}"
            fi
        done
    fi
fi

NEW="#define OKAPIA_VERSION \"${VERSION}\"
#define OKAPIA_RELEASE \"${RELEASE}\""
if [ ! -f "$OUT" ] || [ "$(cat "$OUT")" != "$NEW" ]; then
    mkdir -p "$(dirname "$OUT")"
    printf '%s\n' "$NEW" > "$OUT"
fi
