#!/usr/bin/env bash
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Assemble a release of Okapia: the assets a GitHub release carries, which the
# desktop's welcome page reads to prepare an SD card (src/hosts/desktop/install.h).
# Moved here from okapia-installer's make-demo-release.sh, with the installer.
#   usage: make-release.sh [output folder]      (default build/release)
#
# scripts/release.sh calls it with, in the environment:
#   OKAPIA_RELEASE   the release number, 26.11.1 (default: the build identifier)
#   OKAPIA_PAGE      the release's web page
#   OKAPIA_APP_ZIP   the desktop app, zipped, for OKAPIA_APP_PLATFORM (macos-arm64),
#                    with OKAPIA_APP_MIN_OS and OKAPIA_APP_TEAM (the Developer ID's
#                    Team ID; empty while the app is signed ad hoc)
# (docs/contributing/release.md).
#
# Build the Pi 4 card first: scripts/make-pi-sd.sh stages rpi4-sd-contents/.
#
# The output:
#   okapia-manifest.json          what the welcome page reads first: the boards
#                                 the release is built for (the Raspberry Pi 4
#                                 here), each with its zip and its files; a
#                                 board added here appears in its For menu
#                                 without a new Okapia
#   okapia-<version>-rpi4.zip     everything that goes on a Pi 4's card, laid
#                                 out as on the card, the licences included,
#                                 and the manifest resolved to that board:
#                                 unzipped onto a FAT32 card, it boots, and
#                                 Okapia can update it later
#
# The zip carries Okapia's kernel and config.txt, Circle's ARM stub, the
# Raspberry Pi firmware and the Pi 4's Wi-Fi firmware, taken from the copies
# Circle downloaded at the revisions it pins, and the licence of each:
#   start4.elf, fixup4.dat         Broadcom: redistribution in binary form for
#                                  use on a Raspberry Pi (LICENCE.broadcom)
#   bcm2711-rpi-4-b.dtb            GPL-2.0, compiled from raspberrypi/linux
#   armstub8-rpi4.bin              BSD 3-clause, Raspberry Pi Ltd and Stephen
#                                  Warren, modified by Circle
#   firmware/*.bin, *.clm_blob     Cypress: redistribution in object code for
#                                  use with Cypress chips (debian/copyright of
#                                  RPi-Distro/firmware-nonfree)
#   firmware/*.txt                 GPL-2.0 (Linaro, Raspberry Pi Ltd)
# The licences of the firmware are fetched from their repositories at the
# pinned revisions, so this needs the network.
#
# The preferences schema is built from Okapia's own strings
# (assets/strings.tsv), so its labels are the boot menu's.
#
# Nothing here reads or copies a ROM, a System or a disk image.
set -euo pipefail

OKAPIA="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-${OKAPIA}/build/release}"
STAGED="${OKAPIA}/rpi4-sd-contents"

for f in kernel8-rpi4.img armstub8-rpi4.bin config.txt; do
    [ -f "${STAGED}/${f}" ] || { echo "No ${STAGED}/${f}: run scripts/make-pi-sd.sh first" >&2; exit 1; }
done

# The build identifier gen-version.sh gives a clean checkout of that commit, and
# the release number when there is one.
BUILD="$(git -C "$OKAPIA" log -1 --format=%cd --date=format:%y%m%d)-$(git -C "$OKAPIA" rev-parse --short=7 HEAD)"
VERSION="${OKAPIA_RELEASE:-$BUILD}"
export OKAPIA_BUILD="$BUILD"

rm -rf "$OUT"
mkdir -p "$OUT"
python3 -I - "$OKAPIA" "$OUT" "$VERSION" <<'PY'
import hashlib, json, os, re, sys, urllib.request, zipfile

okapia, out, version = sys.argv[1:4]
staged = os.path.join(okapia, "rpi4-sd-contents")
circle = os.path.join(okapia, "external/circle-stdlib/libs/circle")
boot = os.path.join(circle, "boot")
wlan = os.path.join(circle, "addon/wlan/firmware")

def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": "okapia-release"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()

def pinned(makefile):
    return re.search(r"^FIRMWARE \?= ([0-9a-f]{40})", open(makefile).read(), re.M).group(1)

# What goes on the card: (path on the card, bytes, component).
card = []
def add(name, data, component):
    card.append((name, data, component))
def add_file(name, path, component):
    add(name, open(path, "rb").read(), component)

for n in ("kernel8-rpi4.img", "config.txt"):
    add_file(n, os.path.join(staged, n), "Okapia")
add_file("armstub8-rpi4.bin", os.path.join(staged, "armstub8-rpi4.bin"), "Circle")

fw_rev = pinned(os.path.join(boot, "Makefile"))
for n in ("start4.elf", "fixup4.dat", "bcm2711-rpi-4-b.dtb"):
    add_file(n, os.path.join(boot, n), "Raspberry Pi firmware")

# The Pi 4's radio: the wget lines of Circle's Makefile give the source path
# of each file and the name it takes on the card.
wl_rev = pinned(os.path.join(wlan, "Makefile"))
wl_base = f"https://github.com/RPi-Distro/firmware-nonfree/raw/{wl_rev}/debian/config/brcm80211"
wl_sources = []
for dest, src in re.findall(r"wget -q -O (brcmfmac43455-sdio\.[a-z_]+) \$\(BASEURL\)/(\S+)",
                            open(os.path.join(wlan, "Makefile")).read()):
    add_file(f"firmware/{dest}", os.path.join(wlan, dest), "Wi-Fi firmware")
    wl_sources.append((dest, f"{wl_base}/{src}"))

# Licences --------------------------------------------------------------------
add_file("licences/LICENCE.broadcom", os.path.join(boot, "LICENCE.broadcom"), "Licences")
add_file("licences/COPYING.linux", os.path.join(boot, "COPYING.linux"), "Licences")

# The ARM stub's notice: the second comment block of its source, which holds
# the copyright lines and the BSD terms.
src = open(os.path.join(boot, "armstub/armstub8.S")).read()
start = src.index("/*", src.index("*/"))
block = src[start + 2 : src.index("*/", start)]
notice = "\n".join(re.sub(r"^\s*\* ?", "", l) for l in block.splitlines()).strip() + "\n"
add("licences/LICENCE.armstub", ("armstub8-rpi4.bin is built from Circle's boot/armstub/armstub8.S,\n"
     "\"Modified version for the Raspberry Pi 4 which allows using FIQ with Circle\".\n\n" + notice).encode(), "Licences")

# Cypress's licence, as Debian's copyright file for firmware-nonfree states it.
copyright = fetch(f"https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/{wl_rev}/debian/copyright").decode()
m = re.search(r"^License: binary-redist-Cypress\n((?: .*\n)+)", copyright, re.M)
if not m:
    sys.exit("No binary-redist-Cypress licence in firmware-nonfree's debian/copyright")
cypress = "\n".join("" if l.strip() == "." else l[1:] for l in m.group(1).splitlines()) + "\n"
add("licences/LICENCE.cypress", cypress.encode(), "Licences")

linux = fetch(f"https://raw.githubusercontent.com/raspberrypi/firmware/{fw_rev}/extra/git_hash").decode().strip()
sources = "".join(f"    firmware/{d}: {u}\n" for d, u in wl_sources)
add("licences/NOTICE.txt", f"""Third-party files on this card
==============================

start4.elf, fixup4.dat
    Raspberry Pi firmware, https://github.com/raspberrypi/firmware at {fw_rev}
    Licence: LICENCE.broadcom

bcm2711-rpi-4-b.dtb
    Device tree compiled from arch/arm/boot/dts/broadcom/bcm2711-rpi-4-b.dts,
    https://github.com/raspberrypi/linux at {linux}
    (the commit raspberrypi/firmware records in extra/git_hash)
    Licence: GPL-2.0, COPYING.linux; the source is at that commit.

armstub8-rpi4.bin
    Built from Circle, https://github.com/rsta2/circle, boot/armstub/armstub8.S
    Licence: BSD 3-clause, LICENCE.armstub

firmware/brcmfmac43455-sdio.bin, firmware/brcmfmac43455-sdio.clm_blob
    Cypress Wi-Fi firmware, https://github.com/RPi-Distro/firmware-nonfree at {wl_rev}
    Licence: LICENCE.cypress

firmware/brcmfmac43455-sdio.txt
    Wi-Fi chip settings, same repository and revision.
    Copyright 2018 Linaro Ltd, 2018 Raspberry Pi (Trading) Ltd. Licence: GPL-2.0, COPYING.linux

Where each Wi-Fi file comes from:
{sources}
kernel8-rpi4.img and config.txt are Okapia, GPL-3.0-or-later:
https://github.com/jpep-in/okapia
""".encode(), "Licences")

# The manifest ------------------------------------------------------------------
strings = {}
for line in open(os.path.join(okapia, "assets/strings.tsv"), encoding="utf-8"):
    if line.startswith("#") or "\t" not in line:
        continue
    key, *langs = line.rstrip("\n").split("\t")
    strings[key] = {"en": langs[0], "fr": langs[1] if len(langs) > 1 else langs[0]}

def label(key, en, fr):
    return strings.get(key, {"en": en, "fr": fr})

zones = [k[5:] for k in strings if k.startswith("zone_") and k != "zone_fixed"]
prefs = [
    {"key": "language", "type": "choice", "label": label("language", "Language", "Langue"), "default": "en",
     "choices": [{"value": "en", "label": {"en": "English", "fr": "Anglais"}},
                 {"value": "fr", "label": {"en": "French", "fr": "Français"}}]},
    {"key": "zone", "type": "choice", "label": label("time_zone", "Time zone", "Fuseau horaire"), "default": "utc",
     "choices": [{"value": z, "label": strings["zone_" + z]} for z in zones]},
    {"key": "ether", "type": "choice", "label": {"en": "Network", "fr": "Réseau"}, "default": "",
     "choices": [{"value": "", "label": {"en": "None", "fr": "Aucun"}},
                 {"value": "eth", "label": {"en": "Ethernet cable", "fr": "Câble Ethernet"}},
                 {"value": "wlan", "label": {"en": "Wi-Fi", "fr": "Wi-Fi"}}]},
    {"key": "wifissid", "type": "text", "max": 32, "when": {"ether": "wlan"},
     "label": {"en": "Wi-Fi network", "fr": "Réseau Wi-Fi"}},
    {"key": "wifipassword", "type": "password", "max": 64, "when": {"ether": "wlan"},
     "label": {"en": "Password", "fr": "Mot de passe"}},
    {"key": "wificountry", "type": "text", "max": 2, "when": {"ether": "wlan"},
     "label": {"en": "Country (two letters)", "fr": "Pays (deux lettres)"}},
    {"key": "bootmenu", "type": "bool", "default": "false",
     "label": {"en": "Show the boot menu at every start", "fr": "Afficher le menu à chaque démarrage"}},
]

archive = f"okapia-{version}-pi4.zip"
notes = "Assembled by scripts/make-release.sh."
build = os.environ.get("OKAPIA_BUILD", version)
page = os.environ.get("OKAPIA_PAGE", "")
files = [{"name": n, "size": len(d), "sha256": hashlib.sha256(d).hexdigest(), "component": c} for n, d, c in card]
okapia_entry = {"version": version, "build": build, "notes": notes}
if page:
    okapia_entry["page"] = page

# The desktop app, already zipped and signed by scripts/release.sh.
apps = []
app_zip = os.environ.get("OKAPIA_APP_ZIP", "")
if app_zip:
    import shutil
    data = open(app_zip, "rb").read()
    name = os.path.basename(app_zip)
    shutil.copyfile(app_zip, os.path.join(out, name))
    apps.append({"platform": os.environ.get("OKAPIA_APP_PLATFORM", "macos-arm64"), "archive": name,
                 "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                 "min_os": os.environ.get("OKAPIA_APP_MIN_OS", ""), "team": os.environ.get("OKAPIA_APP_TEAM", "")})

manifest = {
    "schema": 1,
    "installer_min": "1.0.0",
    "okapia": okapia_entry,
    "boards": [{"id": "pi4", "name": "Raspberry Pi 4", "archive": archive, "files": files}],
    "apps": apps,
    "prefs": prefs,
}
with open(os.path.join(out, "okapia-manifest.json"), "w", encoding="utf-8") as f:
    f.write(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")

# The zip carries the manifest resolved to its board, as the installer writes
# it on a card: a card unzipped by hand is a card the installer can update.
resolved = {
    "schema": 1,
    "installer_min": "1.0.0",
    "okapia": {"version": version, "build": build, "board": "pi4", "board_name": "Raspberry Pi 4", "notes": notes},
    "files": files,
    "archive": archive,
    "prefs": prefs,
}
with zipfile.ZipFile(os.path.join(out, archive), "w", zipfile.ZIP_DEFLATED) as z:
    for n, d, _ in card:
        z.writestr(n, d)
    z.writestr("okapia-manifest.json", json.dumps(resolved, indent=2, ensure_ascii=False) + "\n")

size = os.path.getsize(os.path.join(out, archive))
print(f"==> {out}: Okapia {version} ({build}), {archive} ({size // 1024} KB, {len(card)} files), "
      f"{len(apps)} app(s), {len(prefs)} preferences")
PY
