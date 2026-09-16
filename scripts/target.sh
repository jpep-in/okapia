# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
# Which target the tree is currently built for — sourced, never run.
#
# One tree, one target at a time: circle-stdlib configures newlib and Circle for
# a single board, and our own objects carry the same flags. scripts/build-libs.sh
# switches between them by moving each target's build aside into build/libs/, so
# the whole tree — libraries, objects, kernel image — always belongs to exactly
# one target, and libs/circle/Config.mk is what says which.
#
# That matters because nothing else would say it: a kernel built for the Pi 4
# addresses the peripherals at 0xFE000000 and QEMU's raspi3b puts them at
# 0x3F000000, so the wrong one does not fail, it goes quiet.

okapia_target ()            # $1 = repository root
{
    local config="${1}/external/circle-stdlib/libs/circle/Config.mk"
    if [ ! -f "$config" ]; then
        printf 'none'
        return
    fi

    if grep -q CIRCLE_QEMU "$config"; then
        printf 'qemu'
    else
        local rasppi
        rasppi="$(sed -n 's/^RASPPI = //p' "$config")"
        # Config.mk speaks in bare numbers; everything above this line speaks in
        # board names, because "the 4 build" reads like a version and "pi4" does
        # not.
        case "$rasppi" in
            3|4|5) printf 'pi%s' "$rasppi" ;;
            *)     printf 'none' ;;
        esac
    fi
}

okapia_require_target ()    # $1 = repository root, $2 = target wanted, $3 = what for
{
    local have
    have="$(okapia_target "$1")"
    [ "$have" = "$2" ] && return 0

    printf '%s needs the "%s" build, and the tree is built for "%s".\n' "$3" "$2" "$have" >&2
    printf 'Switch with: scripts/build-libs.sh %s\n' "$2" >&2
    printf 'The other build is kept in build/libs/, so switching back costs seconds.\n' >&2
    return 1
}
