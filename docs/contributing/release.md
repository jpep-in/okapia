# Releases

> **Status: a plan (2026-11-22), nothing published yet.** One release carries every product of a version: the
> image for each Raspberry Pi board and the desktop application for each platform, described by one
> `okapia-manifest.json`. The desktop application reads that manifest to prepare SD cards
> ([Desktop](../topics/desktop.md)) and to replace itself when a newer release exists. Decisions taken with the
> author: date versions, the desktop app downloads and replaces itself, Developer ID signing and notarisation on
> macOS, a local release script before any CI.

## Versions

- **Release number**: `YY.MM.N`, the year, the month and a counter from 1 within the month — `26.11.1`, then
  `26.11.2`, then `26.12.1`. Tagged `v26.11.1`. It orders by comparing its three numbers (`VersionLess`,
  `src/hosts/desktop/install_manifest.cpp`) and says how old a version is at a glance.
- **Build identifier**: `YYMMDD-sha[+xxxx]` stays what `scripts/gen-version.sh` writes into every binary; it is
  what the logs, *Information* and the card diagnosis show. A release build is a clean checkout of its tag, so its
  identifier has no `+`.
- **One version for everything**: the kernel for each board and the desktop app of a release are built from the
  same commit and carry the same release number. A card prepared by the app of release *R* gets the kernel of the
  newest release the app knows, never older than *R*.
- `gen-version.sh` gains the release number when `HEAD` is exactly a `v*` tag (`26.11.1 (261121-8b8bf89)`), and
  only the build identifier otherwise; nothing else reads a release number from git.

## What one release carries

| Asset | Content |
|---|---|
| `okapia-manifest.json` | the release: number, notes, the boards, the desktop builds |
| `okapia-<release>-<board>.zip` | one per board: everything that goes on its card, licences included, and the manifest resolved to that board (unzipped onto a FAT32 card it boots) |
| `Okapia-<release>-macos-arm64.zip` | the desktop app, signed with a Developer ID, notarised and stapled |
| later `…-macos-x86_64.zip`, `…-linux-<arch>.tar.gz`, `…-windows-<arch>.zip` | the same, as the platforms come |

The manifest is the one place that says what exists. A board or a platform a release adds appears in an older app
without that app changing.

## The manifest

Schema 1: okapia-installer's (`boards`, `files`, `prefs`, `installer_min`), with the desktop builds and the release's
build and page. Nothing was ever published in the installer's shape alone, so these are part of the first schema
rather than a second one:

```json
{
  "schema": 1,
  "installer_min": "1.0.0",
  "okapia": { "version": "26.11.1", "build": "261121-8b8bf89", "notes": "…", "page": "https://…/v26.11.1" },
  "boards": [ { "id": "pi4", "name": "Raspberry Pi 4", "archive": "okapia-26.11.1-pi4.zip", "files": [ … ] } ],
  "apps": [
    { "platform": "macos-arm64", "archive": "Okapia-26.11.1-macos-arm64.zip", "size": 0, "sha256": "…",
      "min_os": "14.0", "team": "XXXXXXXXXX" }
  ],
  "prefs": [ … ]
}
```

- **The frozen core**: `schema`, `okapia.version`, `okapia.page` and `apps[]` (`platform`, `archive`, `size`,
  `sha256`, `min_os`, `team`) keep their names and meaning in every schema, forever. Every Okapia reads them
  whatever the schema says (`ManifestParse`), so an app of today can always learn that a release exists and update
  itself to it, however far the rest of the format has moved. A release whose core changes would strand every app
  published before it: that is never done; a new meaning gets a new key beside the old one.
- **Compatibility rules**, written into `install_manifest.cpp` and tested (`tests/host/check_install.cpp`):
  - a reader ignores the keys it does not know, so adding a key needs no schema change;
  - the schema goes up only when a key outside the core changes meaning or a reader must not go on; an older app
    reads the core only, prepares no card from that release (*Okapia 27.3.1 is available: update Okapia to prepare
    this card*) and still updates itself;
  - `installer_min` is the oldest app able to install the release on a card (`INSTALLER_VERSION`), per board
    when a board needs more;
  - `apps[].min_os` keeps an update away from a system the build cannot run on.
- **The card's copy** is resolved to one board, without the apps, so a card prepared by any version can be read and
  updated by any later one.
- The preferences, the XPRAM files and the disk images on a card are never part of a release: their formats are
  the engines' and only grow (a new preference is a new keyword; `prefs_okapia.cpp` keeps reading old ones).

## The desktop app keeps itself current

- **Checking**: once a day at most, after the window is up, in the background: `GET
  https://api.github.com/repos/<repo>/releases/latest`, then that release's `okapia-manifest.json`. A generic
  `User-Agent` (`Okapia/<version>`), nothing about the person or the machine. The time of the last check and the
  answer are kept in the app's support folder. Offline, it says nothing. *Check for Updates Automatically* in the Okapia menu turns it off; *Check for
  Updates…* asks now, the last answer not trusted.
- **Saying**: a button **Okapia 26.12.1 available…** at the top right, where there is room: at the end of the
  welcome page's title line, as the installer put its own, and at the right end of the boot menu's tab strip, above
  its rule (`DrawTabs` leaves that part of the strip alone). Its slot is kept when there is none, an empty label,
  so the focus, which a relayout keeps by index, does not move when the answer comes. Both open the release's page (`okapia.page`, HTTPS only) until the app replaces itself (R6).
  Only an app for this platform whose `min_os` this system meets is announced, and only to a release build: a
  development build (no `OKAPIA_RELEASE`) says nothing. When the workspace is a Pi card with an older Okapia than the
  release (its `okapia-manifest.json` names a board and an older version), the welcome page says so (*Okapia 26.11.1 is
  available.*, the footer's default button **Update**), and so does the boot menu, in the same top-right slot when
  the app itself is current: the same sentence on a line above the strip, over an **Update** button (`PageChromeUpdate`'s note), which opens
  the welcome page on that card, its board chosen, where the card is prepared with its progress. The boot menu looks
  the release up only for such a card, and not when the check is turned off; it reads the card once per menu
  (`MenuCardOutdated`, `okapia_menu.cpp`). The welcome page chooses the board a card's manifest names, a folder copy
  included, and takes the version from that manifest when neither the kernel nor a log says it.
- **Where it is kept**: the app's support folder, `update` (when, the version, its page, whether newer), read while
  less than a day old; `update-off` when the check is turned off. A lookup that fails keeps nothing. A test's
  `--release` is never kept.
- **Replacing the app** (macOS):
  1. download the platform's zip into the support folder, check its size and SHA-256 against the manifest;
  2. unzip it beside, check the code signature (`SecStaticCodeCheckValidity`) and that its Team ID is the
     manifest's and the running app's — never install something signed by anyone else;
  3. only when no Macintosh runs (the boot menu, the welcome page), replace `Okapia.app` by renaming the old one
     aside and the new one in (`rename`, same volume), then start the new one and remove the old;
  4. any failure leaves the running app untouched and says why; the app must be in a place the person can write
     (it says so if `/Applications` needs an administrator).
- Linux and Windows replace themselves the same way once their builds exist; a package manager's install is left
  alone (the app only says that a version is available).

## Trust

- **Public**: the source and the releases are public. The GPL asks it of anyone given the binaries, and a private
  release could only be fetched with a token the app would have to carry.
- **One official source**: the app reads releases from the official repository only, over HTTPS, and every file it
  installs or downloads is checked against the SHA-256 the manifest gives. The README and each release page name
  that repository, so a copy published elsewhere is recognisable as one.
- **The app's own code**: on macOS the Developer ID signature and its Team ID, checked before replacing the app.
- **No signature of the manifest itself** (decided 2026-11-22): a key would not stop anyone publishing a copy
  elsewhere, which is the risk that matters. What it alone would cover — someone publishing through the official
  account — is left to that account's protection: two-factor authentication, protected `v*` tags, the publishing
  token in the keychain only.

## Signing and notarisation (macOS)

- An Apple Developer Program membership; a **Developer ID Application** certificate in the keychain.
- `scripts/make-desktop-app.sh` signs with it when `OKAPIA_SIGN_IDENTITY` is set (ad hoc otherwise, for local
  builds): hardened runtime, the `allow-jit` entitlement, SDL3 inside signed first.
- `xcrun notarytool submit --wait` with an App Store Connect API key kept out of the repository
  (`OKAPIA_NOTARY_PROFILE`, a keychain profile), then `xcrun stapler staple`.
- Without notarisation the downloaded app opens only through right-click → Open; with it, it opens like any other.

## The procedure: `scripts/release.sh <YY.MM.N>`

Run on the release Mac, from a clean tree on `main`. Every step stops the release on failure.

1. **Checks**: clean tree, on `main` and up to date with `origin`, the number new and greater than the last tag,
   `apply-patches.sh --check`, the notes file `docs/releases/<YY.MM.N>.md` written.
2. **Tests**: `make -C tests/host`; QEMU: `make-sd-image.sh`, `screenshot.sh` to the Finder, `run-test.sh`
   (durability); the desktop: a headless run to the Finder of System 7.1.
3. **Tag** `v<YY.MM.N>` locally (annotated, the notes as message).
4. **Build**, from the tag: each board's kernel (`build-libs.sh pi4`, the kernel, `make-pi-sd.sh`), the desktop
   app (`make-desktop-app.sh`, signed, notarised, stapled), each zipped as a release asset.
5. **Manifest**: `make-release.sh`, the boards' files and the apps with their hashes.
6. **Hardware gate**, by hand: the board's zip unzipped onto a real card boots a Pi 4 to the Finder; the app
   downloaded as a stranger would (quarantined) opens and prepares a card. The script waits for a yes.
7. **Publish**: push the tag, create the GitHub release with the notes, upload the assets (`gh release create`,
   or the API with a token from the keychain); the manifest last, so an app checking at that moment never sees a
   manifest naming assets not yet there.
8. **After**: the docs' status lines, `docs/releases/`, the version in `docs/README.md`.

A release that turns out bad is not deleted (apps may have read its manifest): a new one with the next number goes
out, and the bad one is marked *pre-release* so `releases/latest` stops naming it.

## Order of work

| # | Work | Depends on |
|---|---|---|
| R1 | Release number in `gen-version.sh` (`OKAPIA_RELEASE`); the manifest's `apps`, `build` and `page`, in schema 1 (nothing published before them), read, tests — **done 2026-11-22** | — |
| R2 | The desktop prepares cards: libcurl, miniz, the *For* menu from the manifest's boards, *Prepare the card* — **done 2026-11-22**, a real card in a reader still to try | R1 |
| R3 | `make-desktop-app.sh` signs with a Developer ID, notarises, staples; `make-release.sh` records the Team ID | Apple account |
| R4 | `scripts/release.sh`, the notes folder (`docs/releases/`) — **done 2026-11-22**, signed ad hoc until R3; rehearsed by parts (the refusals, `make-release.sh` writing the manifest with the app, the app reading it), not yet run whole: it needs this work on `main`. A first release `v26.MM.1` then | R1, R2 |
| R5 | The app checks once a day and says so — **done 2026-11-23**: `HostUpdate` (`install_host.cpp`), a button at the top right of the welcome page and of the boot menu's tab strip, opening the release's page until R6; *Check for Updates…* and *Check for Updates Automatically* in the Okapia menu (macOS; Linux and Windows have no menu yet). *Update* in the boot menu, in the same slot, for a Pi card with an older Okapia (2026-11-24) | R2, R4 |
| R6 | The app replaces itself, signature and Team ID checked | R3, R5 |
| R7 | CI on GitHub Actions for the host tests, then for the builds | R4 |
| — | okapia-installer retired once R2 does what it did; its repository says so and points here | R2 |

## Open questions

- The Apple account (individual or organisation) decides the name shown by Gatekeeper.
- Where the release Mac's token for publishing lives (keychain item, never in the repository).
- Whether Linux and Windows builds come from this Mac (cross) or from CI runners.
