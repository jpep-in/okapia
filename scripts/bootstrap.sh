#!/usr/bin/env bash
# Fetch and pin the build dependencies. Idempotent: safe to re-run after a
# network failure. Reference repos are NOT fetched here — see fetch-reference.sh.
set -euo pipefail

CIRCLE_STDLIB_URL="https://codeberg.org/larchcone/circle-stdlib.git"
CIRCLE_STDLIB_REF="v20"                                       # upstream tags releases
MACEMU_URL="https://github.com/kanjitalk755/macemu.git"
MACEMU_REF="474ea0ab55cac933de35fa349820c9dd0944bf77"         # no useful tags: pin a SHA

# circle-stdlib also declares doctest, json, mbedtls and mongoose. Those serve
# its TLS support and its samples only: we never build them.
CIRCLE_STDLIB_SUBMODULES="libs/circle libs/circle-newlib"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

say()  { printf '\n==> %s\n' "$1"; }
fail() { printf 'FAILED: %s\n' "$1" >&2; exit 1; }

[ -f scripts/env.sh ] || fail "run scripts/install-tools.sh first"

add() {  # url path ref
    local url="$1" path="$2" ref="$3"
    if [ -e "${path}/.git" ]; then
        echo "  ${path} already cloned"
    else
        say "Cloning ${path}"
        git submodule add --force "$url" "$path"
    fi
    git -C "$path" fetch --quiet --tags origin
    git -C "$path" checkout --quiet "$ref"
}

say "Build dependencies"
add "$CIRCLE_STDLIB_URL" external/circle-stdlib "$CIRCLE_STDLIB_REF"
# --force re-checks-out worktrees left empty by an interrupted clone: git records
# the submodule as initialised even when the checkout never happened.
git -C external/circle-stdlib submodule update --init --force $CIRCLE_STDLIB_SUBMODULES

add "$MACEMU_URL" external/macemu "$MACEMU_REF"

say "Checks"
check() { [ -e "$1" ] && printf '  ok    %s\n' "$2" || fail "missing $1 ($2)"; }
check external/circle-stdlib/libs/circle/lib                      "Circle sources"
check external/circle-stdlib/libs/circle/doc/multicore.txt        "Circle docs"
check external/circle-stdlib/libs/circle-newlib/newlib            "circle-newlib"
check external/circle-stdlib/configure                            "circle-stdlib configure"
check external/macemu/BasiliskII/src/uae_cpu_2021/newcpu.cpp      "macemu uae_cpu_2021"
check external/macemu/BasiliskII/src/dummy                        "macemu dummy stubs"

say "Pinned"
printf '  circle-stdlib  %s  %s\n' \
    "$(git -C external/circle-stdlib rev-parse --short HEAD)" \
    "$(git -C external/circle-stdlib describe --tags --exact-match 2>/dev/null || echo -)"
printf '  circle         %s  %s\n' \
    "$(git -C external/circle-stdlib/libs/circle rev-parse --short HEAD)" \
    "$(git -C external/circle-stdlib/libs/circle describe --tags --exact-match 2>/dev/null || echo -)"
printf '  macemu         %s  %s\n' \
    "$(git -C external/macemu rev-parse --short HEAD)" \
    "$(git -C external/macemu log -1 --format=%cd --date=short)"

say "Done — commit the pins."
