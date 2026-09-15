#!/usr/bin/env bash
# Apply Okapia's changes to the upstream submodules.
#   usage: apply-patches.sh [--check]
#
# The contributor guide says never to edit external/ by hand. This is how the exceptions
# get in: patches/<submodule>/*.patch, applied to external/<submodule> — macemu
# and circle-stdlib so far — each minimal, each explaining itself in its own
# header, each a candidate to send upstream.
#
# Idempotent on purpose — bootstrap runs it, and so does anyone who is not sure.
# A patch already applied is reported and skipped, never applied twice.
#
# --check applies nothing and answers with its exit status, which is what a
# build rule wants: a kernel compiled against an unpatched tree would fail in
# ways that point nowhere near the cause.
#
# Copyright (C) 2026  Okapia contributors
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHECK_ONLY=0
[ "${1:-}" = "--check" ] && CHECK_ONLY=1

missing=0
found=0
for set in "${REPO_ROOT}"/patches/*/; do
    [ -d "${set}" ] || continue
    submodule="$(basename "${set}")"
    TARGET="${REPO_ROOT}/external/${submodule}"
    if [ ! -d "${TARGET}" ]; then
        echo "  NO SUBMODULE     external/${submodule} for patches/${submodule}" >&2
        exit 1
    fi

    for patch in "${set}"*.patch; do
        [ -e "${patch}" ] || continue
        found=1
        name="${submodule}/$(basename "${patch}")"

        # Already in? git says so by refusing to apply it forwards and accepting it
        # in reverse. That is cheaper and more honest than a marker of our own.
        if git -C "${TARGET}" apply --reverse --check "${patch}" 2>/dev/null; then
            [ "${CHECK_ONLY}" -eq 1 ] || echo "  already applied  ${name}"
            continue
        fi

        if [ "${CHECK_ONLY}" -eq 1 ]; then
            echo "  NOT applied      ${name}"
            missing=$((missing + 1))
            continue
        fi

        if ! git -C "${TARGET}" apply --check "${patch}" 2>/dev/null; then
            echo "  REFUSED          ${name}" >&2
            echo "The submodule has moved under this patch. Rebase it against the" >&2
            echo "pinned commit rather than forcing it in." >&2
            exit 1
        fi

        git -C "${TARGET}" apply "${patch}"
        echo "  applied          ${name}"
    done
done

if [ "${found}" -eq 0 ]; then
    echo "No patches to apply."
    exit 0
fi

if [ "${CHECK_ONLY}" -eq 1 ]; then
    if [ "${missing}" -gt 0 ]; then
        echo "${missing} patch(es) missing. Run scripts/apply-patches.sh." >&2
        exit 1
    fi
    echo "All patches are in."
fi
