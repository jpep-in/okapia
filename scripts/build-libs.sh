#!/usr/bin/env bash
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
# Configure and build circle-stdlib (newlib + Circle) for one target, and switch
# between targets without rebuilding what was already built.
#   usage: build-libs.sh qemu | pi3 | pi4 | pi5
#
# One tree can only be configured for one board: newlib records its CFLAGS in an
# autoconf cache, Circle's objects carry -mcpu and -DRASPPI and depend on
# neither, and so do ours. Reconfiguring over the top fails halfway with
# "changes in the environment can compromise the build", on a tree that is then
# neither target — and Circle would be worse, since a stale object links into
# the library and nothing says so.
#
# So a target is not reconfigured, it is *moved aside*: everything generated for
# the current target goes to build/libs/<target>/ before the new one is set up,
# and comes back when it is asked for again. The first build of each target
# costs what it costs; every switch afterwards is a rename.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STDLIB="${REPO_ROOT}/external/circle-stdlib"
CACHE_ROOT="${REPO_ROOT}/build/libs"
TARGET="${1:-qemu}"

# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/env.sh"
# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/target.sh"

# A bare number is still accepted — it is what circle-stdlib's own configure
# takes — but pi4 is the name used everywhere else, including the directory a
# build is kept in.
case "$TARGET" in
    3|4|5) TARGET="pi${TARGET}" ;;
esac

case "$TARGET" in
    qemu)          CONFIGURE_ARGS=(-r 3 --qemu) ;;
    pi3|pi4|pi5)   CONFIGURE_ARGS=(-r "${TARGET#pi}") ;;
    *) echo "usage: $(basename "$0") qemu|pi3|pi4|pi5" >&2; exit 1 ;;
esac

# macOS ships Bash 3.2 (no mapfile) and BSD getopt (no --long): circle-stdlib's
# configure needs GNU versions of both. install-tools.sh puts GNU getopt on PATH.
BASH5="$(brew --prefix bash)/bin/bash"
[ -x "$BASH5" ] || { echo "GNU bash 5 missing: brew install bash" >&2; exit 1; }


# Everything a target owns, relative to the repository root. Directories are
# moved whole, which is a rename and therefore instant; the loose files are the
# ones a build leaves at the top of a directory it shares with sources.
#
# engine-ppc.rename is deliberately absent: it is generated, but it is also
# committed, and moving it would show up as a deletion in git status.
STATE_PATHS=(
    external/circle-stdlib/Config.mk
    external/circle-stdlib/libs/circle/Config.mk
    src/kernel/emu-basilisk
    src/kernel/emu-sheepshaver
    src/kernel/obj-shared
    src/kernel/kernel8.img
    src/kernel/kernel8.elf
    src/kernel/kernel8.lst
    src/kernel/kernel8.map
    src/kernel/engine-68k.o
    src/kernel/engine-ppc.o
    src/kernel/engine-ppc-renamed.o
    src/firmware/circle/obj
    src/firmware/circle/kernel8.img
    src/firmware/circle/kernel8.elf
    src/firmware/circle/kernel8.lst
    src/firmware/circle/kernel8.map
)

# These two exist in git — each carries a .gitignore — so the directory has to
# stay where it is and only its contents travel. Moving the directory itself
# deletes a tracked file, which shows up as a dirty submodule, and circle-stdlib's
# own configure then stops with "cd: build/circle-newlib: No such file or
# directory".
STATE_TREES=(
    external/circle-stdlib/build/circle-newlib
    external/circle-stdlib/install
)

# Circle builds its objects beside its sources, so they cannot be moved as a
# directory. They are collected into one archive instead — 36 MB, a second.
CIRCLE_TREE="${STDLIB}/libs/circle"
CIRCLE_OBJECTS='circle-objects.tar'

move_state ()               # $1 = from root, $2 = to root
{
    local path src dst
    for path in "${STATE_PATHS[@]}"; do
        src="${1}/${path}"
        dst="${2}/${path}"
        [ -e "$src" ] || continue
        mkdir -p "$(dirname "$dst")"
        rm -rf "$dst"
        mv "$src" "$dst"
    done
}

move_tree_contents ()       # $1 = from root, $2 = to root
{
    local tree src dst entry name
    for tree in "${STATE_TREES[@]}"; do
        src="${1}/${tree}"
        dst="${2}/${tree}"
        [ -d "$src" ] || continue
        mkdir -p "$dst"
        for entry in "$src"/* "$src"/.[!.]*; do
            [ -e "$entry" ] || continue
            name="$(basename "$entry")"
            [ "$name" = .gitignore ] && continue
            rm -rf "${dst}/${name}"
            mv "$entry" "${dst}/${name}"
        done
    done
}

stash_circle_objects ()     # $1 = destination directory
{
    local list="${1}/${CIRCLE_OBJECTS}.list"
    local tracked="${1}/${CIRCLE_OBJECTS}.tracked"
    mkdir -p "$1"
    # Not every .a under Circle is built: its lvgl submodule ships prebuilt ones
    # (libnemagfx*.a), and taking those left the submodule showing deleted files
    # and the library gone from whichever target was not active. So ask git what
    # it tracks — across the nested submodules too — and take everything else.
    ( cd "$CIRCLE_TREE" && git ls-files --recurse-submodules ) \
        | sed 's|^|./|' > "$tracked"
    ( cd "$CIRCLE_TREE" && find . \( -name '*.o' -o -name '*.a' -o -name '*.d' \) ) \
        | grep -F -x -v -f "$tracked" > "$list" || true
    rm -f "$tracked"
    [ -s "$list" ] || { rm -f "$list"; return 0; }
    ( cd "$CIRCLE_TREE" && tar -cf "${1}/${CIRCLE_OBJECTS}" -T "$list" )
    ( cd "$CIRCLE_TREE" && xargs rm -f < "$list" )
    rm -f "$list"
}

unstash_circle_objects ()   # $1 = source directory
{
    [ -f "${1}/${CIRCLE_OBJECTS}" ] || return 0
    ( cd "$CIRCLE_TREE" && tar -xf "${1}/${CIRCLE_OBJECTS}" )
    rm -f "${1}/${CIRCLE_OBJECTS}"
}

CURRENT="$(okapia_target "$REPO_ROOT")"
RESTORED=no

if [ "$CURRENT" != "$TARGET" ]; then
    if [ "$CURRENT" != none ]; then
        printf '\n==> Putting the %s build aside in %s\n' \
            "$CURRENT" "build/libs/${CURRENT}"
        rm -rf "${CACHE_ROOT}/${CURRENT}"
        mkdir -p "${CACHE_ROOT}/${CURRENT}"
        move_state "$REPO_ROOT" "${CACHE_ROOT}/${CURRENT}"
        move_tree_contents "$REPO_ROOT" "${CACHE_ROOT}/${CURRENT}"
        stash_circle_objects "${CACHE_ROOT}/${CURRENT}"
    fi

    if [ -d "${CACHE_ROOT}/${TARGET}" ]; then
        printf '==> Bringing the %s build back\n' "$TARGET"
        move_state "${CACHE_ROOT}/${TARGET}" "$REPO_ROOT"
        move_tree_contents "${CACHE_ROOT}/${TARGET}" "$REPO_ROOT"
        unstash_circle_objects "${CACHE_ROOT}/${TARGET}"
        rm -rf "${CACHE_ROOT}/${TARGET}"
        RESTORED=yes
    else
        printf '==> No %s build yet: this one builds from scratch\n' "$TARGET"
    fi
fi

# The patches the libraries were built with. A restored build is only finished
# if they are still the ones in patches/circle-stdlib: a patch added since —
# the _lseek fix was one — would otherwise be missing from a board's libraries
# without a word, because the source tree says "applied" either way. Kept in
# install/, which travels with the target.
PATCH_STAMP="${STDLIB}/install/.okapia-patches"
patch_digest ()
{
    cat "${REPO_ROOT}"/patches/circle-stdlib/*.patch 2>/dev/null | shasum | cut -d' ' -f1
}

check_libraries ()          # 0 when every library this project links is there
{
    local lib missing=0
    cd "$STDLIB"
    for lib in libs/circle/lib/libcircle.a libs/circle/lib/usb/libusb.a \
           libs/circle/lib/input/libinput.a libs/circle/lib/fs/libfs.a \
           libs/circle/lib/sched/libsched.a libs/circle/lib/net/libnet.a \
           libs/circle/addon/SDCard/libsdcard.a libs/circle/addon/fatfs/libfatfs.a \
           libs/circle/addon/qemu/libqemusupport.a \
           libs/circle/addon/wlan/libwlan.a \
           install/aarch64-none-circle/lib/libc.a install/aarch64-none-circle/lib/libm.a \
           install/aarch64-none-circle/lib/libcirclenewlib.a; do
        if [ -f "$lib" ]; then printf '  ok        %s\n' "${lib##*/}"
        else printf '  MISSING   %s\n' "$lib"; missing=1; fi
    done
    return "$missing"
}

ready ()
{
    printf '\n==> circle-stdlib ready for %s\n' "$TARGET"
    if [ -f "${REPO_ROOT}/src/kernel/kernel8.img" ]; then
        printf '==> src/kernel/kernel8.img is the %s kernel\n' "$TARGET"
    fi
    exit 0
}

# A restored build is finished by definition — it was checked when it was made,
# and nothing under external/ is ever modified here, so there is nothing for a
# rebuild to pick up. Running configure and two recursive makes over it anyway
# costs the better part of a minute and produces nothing; skipping them is what
# turns a switch into a rename. --rebuild forces the long way.
if [ "$RESTORED" = yes ] && [ "${2:-}" != --rebuild ]; then
    printf '\n==> Checking the restored libraries\n'
    if [ "$(cat "$PATCH_STAMP" 2>/dev/null)" != "$(patch_digest)" ]; then
        printf '==> Built with other patches than patches/circle-stdlib: building after all\n'
    elif check_libraries; then
        ready
    else
        printf '==> Incomplete: building after all\n'
    fi
fi

printf '\n==> Configuring circle-stdlib for %s\n' "$TARGET"
cd "$STDLIB"
"$BASH5" ./configure --aarch64 -p aarch64-none-elf- "${CONFIGURE_ARGS[@]}"

printf '\n==> Building newlib and Circle\n'
"$MAKE" newlib circle

printf '\n==> Checking libraries\n'
check_libraries || { echo "Build incomplete." >&2; exit 1; }
patch_digest > "$PATCH_STAMP"

ready
