# Storage

> Disk and CD images on the SD card, what an interrupted session does to them, how Okapia repairs a volume,
> the inventory of the card, and provisioning. Data safety outranks every other constraint in this project.
> Main files: `src/circle/hfs_volume_circle.{h,cpp}`, `src/circle/card_circle.cpp` (`CardPrepareVolumes`),
> `src/circle/trace_disk_circle.cpp`, `external/hfsutils` (libhfs), `patches/circle-stdlib/0002`,
> `scripts/run-test.sh`, `scripts/check-large-volume.sh`, `scripts/make-sd-image.sh`.

## Using it

- **Disk images** are plain files at the card's root, listed by `disk` lines ([Preferences](../preferences.md));
  **the order is the setting** and the [boot menu](boot-menu.md) writes it. A `*` prefix mounts read-only.
- **CD images** (Toast images work) are `cdrom` lines; a disc can be the startup volume through `bootdriver -62`.
- **New volume**: the boot menu makes an empty HFS volume of a chosen name and size.
- **Shut down from Mac OS.** Pulling the plug keeps your data but marks the volume in use; Okapia repairs it at the
  next power-on (`hfsrepair true`, the default) and Mac OS then shows its period "not shut down properly" message.
  With `hfsrepair false` the Mac refuses to start from that volume (question-mark floppy).
- **Large volumes**: an image can be read to the end up to FAT32's limit of 4 GiB − 1 per file.
- **In the log**: the inventory of every HFS volume on the card (name, size, free space, files, System version,
  clean or in use), then `Boot volume: <path>`.

## How it works

### The file layer: no cache

The Mac's disk driver (Basilisk's `sys_unix.cpp` over circle-stdlib's `open`/`read`/`write`/`lseek`, over FatFs)
writes whole 512-byte sectors at sector boundaries, which FatFs passes straight through to the card. **There is no
write-back cache between the guest and the card**, which is the whole durability model — the same as BlueSCSI's,
whose `SYNCHRONIZE CACHE` answers "We don't have a cache. do nothing." (`BlueSCSI_disk.cpp:2740`). Measured with
`--wrap=Sys_write` under `OKAPIA_TRACE=1`: a boot to the Finder issues 39 writes, every one complete. An interrupted
session loses only what Mac OS still held in its own RAM cache — the exposure a real Mac has.

### What a pulled plug does — measured

Investigation of 2026-08-28, from a volume Disk First Aid certified healthy, with `Sys_read`/`Sys_write` traces:

| Fact | Measurement |
|---|---|
| Lost writes | none |
| Damage after a kill | `drAtrb` `0000` and "MDB needs minor repair", nothing else |
| Refusal or slowness? | refusal: 346 reads in 0.44 s, then nothing for 37 s |
| Writes during that check | zero: no repair attempted |
| Periodic flush | yes, MDB and dirty nodes rewritten ~50 s after startup finishes |
| Kill *after* the flush | fails too: the bit stays clear while the volume is mounted |
| `drAtrb` bit 8 set back by hand (offset 1034) | boots, and Mac OS still says "not shut down properly" |

Traced at the Toolbox (`op_illg_1`, watching `ioResult`): `MountVol` on a clean volume returns `noErr` and writes
(Mac OS clears bit 8 to mark it in use); on a volume already marked in use it returns **`badMDBErr` (−60)**, the only
non-zero result in the whole run. `Status` csCode 8 and `Control` csCode 9 of the substituted `.Disk` driver return
`noErr` in both cases. **The refusal is the Mac OS File Manager's deliberate safety policy**, not a bug of Basilisk or
Okapia; the same happens on a real Mac, and with Basilisk on macOS.

Eliminated one by one: lost writes, structural damage, the partition map (the same volume inside an Apple partition
map refuses identically — Basilisk only uses the map to locate the volume, `disk.cpp:120`), the embedded driver
(Basilisk never loads it; substituting `.Disk` has no measurable effect).

**A Mac refuses to start from a volume marked in use, but mounts it as a secondary disk, and that mount runs the HFS
scavenge that repairs it.** Demonstrated here with two disks: boot from a clean volume, the dirty one appears on the
desktop and Mac OS writes to it (`drAtrb` `0000` → `0003`).

### Repairing the volume ourselves: libhfs

`libhfs` (Robert Leslie, GPL v2 or later, `external/hfsutils`) does exactly what that secondary mount does: mounting
read-write runs `v_scavenge()` when `HFS_ATRB_UMOUNTED` is clear (`volume.c:440`), and unmounting sets the bit back
(`volume.c:140`). Its system calls are isolated in `os.h`; `os.c` works unchanged on newlib. 14 objects, 48 KB.

`CardPrepareVolumes()` (`card_circle.cpp`), before **either** engine starts — `CKernel::StartMacintosh()` and
`CKernelPPC::Run()` both call it once the firmware has chosen the engine, after `CardInventory()` and
`CardRefineClock()`:

1. `HfsInspect()` reads the unmount bit **before** any mount (mounting repairs), locating the volume as Basilisk
   does (`find_hfs_partition`), not as libhfs does.
2. If it is clear and `hfsrepair` is on, `HfsRepair()` mounts read-write and unmounts:

   ```
   okapia-hfs: marked in use (drAtrb 0000) — the last session did not shut down
   okapia-hfs: repairing
   okapia-hfs: repaired and marked clean
   ```

3. CD images are checked, never touched: `CDROMInit()` opens them read-only whatever is asked (`cdrom.cpp:324`).
4. The first **bootable** configured volume (not the first present) is the boot volume, logged as
   `Boot volume: <path>`; if no configured disk exists, the first bootable volume of the inventory whose System
   this engine can start is used (`HfsFlavourOf`; an unreadable version is accepted).

A card carrying `/repair-only` stops after the repair, on either engine, so `run-test.sh` can judge the volume in the state the repair
leaves it (booting would mark it in use again within seconds).

### The inventory

`HfsInventory()` mounts every HFS file at the card's root **read-only** — libhfs then writes nothing, not even the
scavenge (`volume.c:1059`) — and describes it with `hfs_vstat()`: name, size, free space, files and folders,
blessed System folder, clean or in use. `HfsSystemVersion()` finds the System file by type and creator
(`zsys`/`MACS`, never by name — a French System is `Système`) and decodes its `vers` resource (reading a resource
fork read-only is safe: `hfs_setfork` truncates, but `f_trunc` exits at once on a read-only volume, `file.c:16`).
`HfsFlavourOf()` tells 68k, PowerPC or universal ([Boot menu](boot-menu.md#engines)). The boot menu's chooser is built
on it, and `modelid` follows from it ([68k engine](engine-68k.md#model-id)).

`HfsFormat()` writes an empty volume over a file the caller has already sized (`hfs_format` takes the medium's
size from the file, `volume.c:241`), partition 0; libhfs refuses a name that is empty, longer than 27 characters or
contains a colon (`hfs.c:53`).

### Volumes past 2 GiB

circle-newlib's `_lseek()` took its offset as an `int` and computed the new position in one, while newlib's `lseek()`
passes an `off_t` (64 bits on AArch64) and FatFs positions are `FSIZE_t` (4 GiB − 1 on FAT32). Every offset from
2 GiB on arrived negative and was refused, cleanly: `Sys_read` returned 0, libhfs could not size the file, and
`lseek(fd, 0, SEEK_END)` answered a negative size. `patches/circle-stdlib/0002` carries the `off_t` through
`CGlueIO::LSeek` and its three implementations, and refuses a position FatFs cannot hold with `EOVERFLOW` rather than
truncating it into a valid one.

`scripts/check-large-volume.sh` proves it: `tests/host/make_large_volume` builds a 3 GB volume whose blessed System
file starts at 2,127 MB, the script puts it alone on a throwaway 4 GB card, and the kernel must log the System version
it reads from that file. Verified 2026-09-15: `says System 7.1.2` with the patch, `no System version found` without.

### CD images

**A CD image is partition 1, a disk image partition 0**, and libhfs refuses the wrong one outright ("not a
Macintosh HFS volume" one way, "invalid partition map" the other); `MountReadOnly()` tries both. A Toast image's
driver descriptor announces 2,048-byte blocks while its partition map is written at 512: the emulator reads it only
because `find_hfs_partition()` assumes 512 throughout (`cdrom.cpp:194`, `disk.cpp:120`), and a reader trusting the
descriptor finds nothing. Verified on a Mac OS 8.6 install CD whose HFS volume starts at byte 170,496 = block
333 × 512.

### Proving it: `run-test.sh`

`scripts/run-test.sh [seconds] [image]` runs the guest headless **on a copy** of the card — not to look away from
damage but to see it: from a known-good copy, whatever is found was caused by that run. It ends the guest with
**SIGKILL** (SIGTERM would let QEMU flush), boots the card again with `/repair-only` so `HfsRepair` runs, and judges
the volume **after** the repair, reading which volume from the `Boot volume:` log line. It reports the bytes the
guest wrote, so a green verdict cannot come from a run that exercised nothing, and INCONCLUSIVE when no volume is
named. A failing copy is kept. Given an image (`qemu/sd-contents/boot71.img`), it builds its own throwaway card.
Verified on System 7.1 and 7.6: flag `0100` and `fsck_hfs` clean after repair.

## Status

- [x] Raw images on the card, read and write; a SIGKILL leaves the volume structurally sound (2026-08-26)
- [x] Block-level disk trace with short-read detection, `OKAPIA_TRACE=1` (2026-08-26)
- [x] Pulled-plug investigation down to `badMDBErr` (2026-08-28)
- [x] Volume pre-check and libhfs repair at startup; `run-test.sh` verdict after repair (2026-08-28)
- [x] Inventory, System version, bootable fallback (2026-08-29)
- [x] CD images, partition 1, Toast block size; a disc as startup volume (2026-09-09)
- [x] New empty volume from the boot menu (2026-09-09)
- [x] Repair, inventory and boot volume on the PowerPC path: `card_circle.cpp`, called by both kernels; verified under
      QEMU on a 7.6 volume marked in use and started on SheepShaver (2026-09-15)
- [x] Images read past 2 GiB, up to FAT32's 4 GiB − 1: `patches/circle-stdlib/0002`, `check-large-volume.sh`
      (2026-09-15)
- [ ] Repair dialog instead of a silent scavenge — tracked in [Boot menu](boot-menu.md)
- [ ] Error cases never exercised: full card, a write error from the SD layer, a read-only image, removal
      mid-write, shutdown during a write — each owes a test
- [ ] Format check at open, as BlueSCSI's `QuirksCheck.cpp` does: report a bare HFS volume where a real Mac expects a
      device image
- [ ] Rescue volume (optional now that repair works): a small System as a second `disk`, read-only
- [ ] FatFs fast seek (`FF_USE_FASTSEEK`) for large images — see the
      [bottleneck study](../notes/research/bottlenecks.md)
- [ ] `hfsck`-level checks (MDB and B-trees) before trusting a repair on a damaged volume

### Provisioning

Goal: **try Okapia without preparing anything**. Nothing built yet.

- [ ] An empty persistent disk created at first power-on when the card has none (Infinite Mac's "Saved HD")
- [ ] Online catalogue as a tree (System 6 / System 7 / Mac OS 8 / Mac OS 9, then versions): Circle's `CHTTPClient`
      with **HTTPS through mbed TLS** (circle-stdlib `--opt-tls`)
- [ ] Download in chunks with a manifest, resumable after a cut; `zlib` for `.gz`/`.zip` (Mac formats such as
      `.sit` are StuffIt Expander's job inside the Mac, through the [shared folder](shared-folder.md))
- [ ] Software library separate from the System, metadata only, files fetched from their original hosts
- [ ] Catalogue address configurable, feature can be disabled, **nothing rehosted**

Needs the [network](network.md). Ideas, not code, from Infinite Mac (Apache-2.0), which uses the same macemu fork.
On a Pi 3 (Ethernet over USB) downloads will be slow.

## Pitfalls

- **Never mark a volume clean ourselves.** Setting `drAtrb` bit 8 would hide real corruption; the repair runs the
  scavenge, which checks before it sets the bit.
- **Never run two emulators on the same card.** Two QEMUs writing `qemu/sd.img` destroy the volume, and it looks like
  random corruption — most of what early boot "non-determinism" really was. `run-live.sh` refuses to start when the
  image is open; `run-test.sh` and `screenshot.sh` work on copies.
- **Killing QEMU is pulling the plug.** Before the repair existed, damage accumulated run after run until the Mac gave
  up. Check a suspect image with `dd … skip=1034 count=2`: `0100` clean, `0000` in use.
- **QEMU's drive defaults to `cache=writeback`**, which invents a durability hole hardware does not have; both run
  scripts pass `cache=writethrough`.
- **`fsck_hfs` on macOS speaks HFS standard poorly**: it called the reference image corrupt and could not repair it,
  B-tree rebuild included. Treat it as a hint; Disk First Aid inside the guest is the period-correct check, and a
  volume that boots is worth more.
- **A volume the file layer cannot read whole still looks repairable.** Without the `_lseek` fix, a 3 GB volume was
  found "marked in use", scavenged and "repaired and marked clean" by a kernel that could not read its second
  gigabyte. A read that fails cleanly is not harmless when the next step writes.
- **The steps a card is owed belong to the card, not to an engine.** The repair and the clock floor lived in the 68k
  kernel, and the firmware hands a PowerPC volume over before that kernel starts anything, so SheepShaver reached
  volumes unrepaired.
- **Check the baseline first.** Early readings blamed catalogue destruction on a baseline that was already corrupt.
- **A guest that reads its disk and still won't boot has a volume problem, not a driver problem.** With
  `OKAPIA_TRACE=1`: successful reads with no short reads, then a catalogue scan and a second driver init, means the
  file layer is fine.
- **Damage to a System shows as wrong fonts before it shows as a boot failure**: a 7.1 volume after a few killed
  sessions drew icon labels in a huge clipped serif face, which reads like a compositor bug. Restage the volume
  before suspecting video.
- **A test that picks its subject by guessing can go green on the wrong volume.** Both kernels log
  `Boot volume:`; the PowerPC one once did not, and `run-test.sh` printed OK on a guess.
- **A PRAM zap moves the startup disk**: the Mac falls back to the first `disk` line, which may be a System 6 volume
  that stops on "System 6.0.8 does not work with 32-bit addressing". `run-test.sh` then reports no writes — not a
  regression. Look at the screen.
- **A script that replaces a file must build beside it and move it into place.** `make-sd-image.sh` once deleted a
  working 1 GB card and wrote a truncated 64 MB one ("Disk full"). It now sizes the card from what is staged, refuses
  an explicit size that cannot hold it before touching anything, and stages into `sd.img.new`.
- **Don't read a boot failure as a code regression before rebuilding the card and looking at the screen** — that
  mistake cost two long investigations.

## Development notes

- **2026-08-28** — Pulled-plug investigation; rescue volume planned; then libhfs found to do the scavenge, and the
  rescue volume became optional.
- **2026-08-28** — BlueSCSI read: durability from the absence of cache, no knowledge of the guest file system, a
  format check rather than a power-cut guard.
- **2026-08-29** — Preferences from the card, inventory, `modelid` from the System.
- **2026-09-09** — Volumes as hard disk, read-only or CD-ROM from the boot menu; new volume.
- **2026-09-15** — Card preparation moved to `card_circle.cpp` for both engines; volumes past 2 GiB.

## References

- `BasiliskII/src/disk.cpp:120`, `:161`; `cdrom.cpp:194`, `:324`; `Unix/sys_unix.cpp`
- libhfs `volume.c:140`, `:241`, `:440`, `:1059`; `file.c:16`; `hfs.c:53`
- BlueSCSI v2 `BlueSCSI_disk.cpp:2740`, `QuirksCheck.cpp`
- circle-newlib `libgloss/circle/io.cpp` (`CGlueIoFatFs::LSeek`, `_lseek`), `cglueio.h`
- [Testing and debugging](../contributing/testing-and-debugging.md)
