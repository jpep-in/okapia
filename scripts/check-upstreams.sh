#!/usr/bin/env bash
# Report how far the pinned submodules lag behind upstream. Read-only: fetches,
# never checks out, never commits. The one script that needs the network.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

report() {
    local path="$1" branch="$2"
    [ -d "${REPO_ROOT}/${path}/.git" ] || { printf '%s: not initialised\n' "$path"; return; }
    cd "${REPO_ROOT}/${path}"

    local pinned date subject behind tag
    pinned="$(git rev-parse HEAD)"
    date="$(git log -1 --format=%cd --date=short)"
    subject="$(git log -1 --format=%s | cut -c1-60)"
    tag="$(git describe --tags --exact-match 2>/dev/null || echo '-')"

    git fetch --quiet origin "$branch" 2>/dev/null || { printf '%s: fetch failed\n' "$path"; return; }
    behind="$(git rev-list --count "HEAD..origin/${branch}")"

    printf '\n%s\n' "$path"
    printf '  pinned   %s  %s  %s\n' "${pinned:0:12}" "$date" "$tag"
    printf '  subject  %s\n' "$subject"
    if [ "$behind" -eq 0 ]; then
        printf '  status   up to date\n'
    else
        printf '  status   %s commit(s) behind origin/%s\n' "$behind" "$branch"
        git log --format='    %cd %s' --date=short "HEAD..origin/${branch}" | cut -c1-100 | head -8
        [ "$behind" -gt 8 ] && printf '    ... and %s more\n' "$((behind - 8))"
    fi
}

report external/circle-stdlib master
report external/macemu        master

cat <<'NOTE'

To move a pin: cd into the submodule, git checkout <tag-or-sha>, then commit the
submodule change in the parent repo. Never `git submodule update --remote` blind.
Validation required before committing a pin change: see docs/contributing/build.md.
NOTE
