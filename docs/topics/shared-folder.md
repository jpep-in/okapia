# Shared folder

> A folder on the FAT card that appears on the Macintosh's desktop as a volume: the easiest way to get software
> in and files out, from any computer that can read an SD card. Main files: upstream `extfs.cpp` and
> `Unix/extfs_unix.cpp` (reused unchanged), `src/circle/extfs_sync_circle.cpp`, `src/circle/mac_encoding_circle.cpp`,
> `scripts/gen-macroman.py`, `src/kernel/kernel.cpp` (`PrepareSharedFolder`).

## Using it

- **Where**: `extfs` (default `/shared`, created at startup if missing; empty for none) and `extfsname` (the volume
  name, default `Okapia`, 27 characters at most) — [Preferences](../preferences.md), or Settings in the
  [boot menu](boot-menu.md).
- **System requirement**: the **File System Manager 1.2**, built into Mac OS 7.6 and later (measured: 7.6.1 works
  with nothing installed) and available as a system extension for earlier Systems, from Apple's SDK
  (<https://www.macintoshrepository.org/2070-file-system-manager-1-2-sdk>). Verified on French System 7.1.2 with the
  extension. Without it the log says `No FSM present, disabling ExtFS` and no volume appears.
- **The intended flow**: drop a StuffIt archive on the card from any computer, see it in the Finder with the right
  icon, expand it with StuffIt Expander — which recreates resource forks inside the Mac. No disk image to build for
  each application.
- **Limits**: a file copied from a PC has no resource fork (use archives); FAT does not accept every character a Mac
  name may contain; one shared folder only (use subfolders).

## How it works

### ExtFS on FAT, almost nothing to port

`extfs_unix.cpp` compiles unchanged against newlib: `extfs.cpp` uses only `stat`, `access`, `opendir`/`readdir`,
`open`/`read`/`write`/`lseek`, `mkdir`, `remove`, `rmdir`, `rename` and `utime`, all present in circle-stdlib except
the last, which Okapia supplies through FatFs's `f_utime` (`FF_USE_CHMOD` is 1 in Circle's `ffconf.h`, see
`src/circle/compat/`).

Resource forks and Finder info need no extended attributes (`Unix/extfs_unix.cpp:79-83`):

```
/path/.finf/file   → FInfo/DInfo (type, creator, Finder flags)
/path/.rsrc/file   → resource fork
```

Both are created on demand, and `e2t_translation[]` gives common extensions their type and creator (`.sit` →
`SIT!`, `.hqx`, `.bin`, `.zip`, `.txt`, `.pdf`…): a `.txt` dropped from a host arrives with a text document's icon.
Verified end to end: a file dropped into `qemu/sd-contents/shared/` shows in the Finder with the right icon and free
space, the Finder creates a folder, and the folder and both hidden directories appear on the card.

The volume name is a preference here; upstream it is a per-platform string (`STR_EXTFS_VOLUME_NAME`) that each port
rewrites in its own table.

### Writes go through

FatFs keeps the tail of a write in the file object and only records the new size in the directory entry at `f_sync`
or `f_close`. A disk image never opens that window (the Mac writes whole 512-byte sectors, passed straight through),
but the shared folder writes any length. Without a flush, a pulled plug leaves a directory entry saying zero bytes for
a file whose clusters are already on the card — a loss that looks like a success. `extfs_sync_circle.cpp` wraps
`extfs_write` (`--wrap=_Z11extfs_writeiPvm`, on each engine's partial link) and calls `fsync` after every write;
a failed `fsync` is logged. A pulled plug during a copy then costs the file being copied and nothing else.

### File names cross an alphabet

FatFs gives long names as UTF-8; a Macintosh names its files in MacRoman. Upstream's `extfs_unix.cpp` returns the
pointer it was given — right between two UTF-8 systems, wrong here: "Résumé.txt" reached a French System 7.1 as
mojibake. `mac_encoding_circle.cpp` replaces both conversion functions through `--wrap`:

- The 128-entry table is **generated** by `scripts/gen-macroman.py` from Python's `mac_roman` codec (Apple's
  `ROMAN.TXT`). One mistyped code point is one accented letter, in one language, wrong for years.
- **Only MacRoman is converted.** The Mac is asked its script through an 18-byte 68k stub calling `ScriptUtil()`
  (`smMacSysScript`), the technique of infinite-mac (`Unix/mac_encodings.cpp:316`), cached rather than asked twice per
  name. On a Japanese or Cyrillic System names pass untouched. The stub can only run once a processor is running —
  `MacIsExecuting()` says so; `ExtFSInit()` converts the (ASCII) volume name before that.
- A name that cannot be converted, or would not fit 1,024 bytes, comes back **whole and unconverted**: a name the
  Mac cannot read is a nuisance, half a name is a bug. Nothing allocates: the caller copies the result at once
  (`extfs.cpp:229`).

## Status

- [x] ExtFS on the card, `.finf`/`.rsrc`, extension table (2026-08-29)
- [x] `extfsname` preference; folder created when missing (2026-08-29)
- [x] Write-through flush policy (2026-08-29)
- [x] Verified on System 7.1.2 with FSM 1.2 and on 7.6.1 (2026-08-29)
- [x] MacRoman file names, generated table, script asked of the Mac (2026-09-08)
- [x] Shared folder in the boot menu's settings (2026-09-04)
- [ ] A boot menu hint when the startup System needs the FSM 1.2 extension
- [ ] Measure the flush's cost when copying many small files, and on the Pi's card
- [ ] Test the range between 7.1.2 and 7.6.1 (upstream's `TECH` says built in from 7.6; common usage says 7.5)
- [ ] Several shared folders would mean rewriting `extfs.cpp` (single `RootPath`, `ROOT_ID` and volume record) — not
      planned

## Pitfalls

- **The limit is the extension, not the System version.** `extfs.cpp:487` tests Gestalt
  (`gestaltHasFileSystemManager`, version ≥ 1.2), and an extension answers as well as a built-in. A bare 7.1 hides
  the volume, which made the feature look broken at first.
- **The File System Manager is not File Sharing.** File Sharing is AppleShare over the network, exposing the Mac's
  folders to other Macs; it provides nothing here.
- **`ExtFSInit` gives up silently on a path that is not a directory**; `PrepareSharedFolder()` creates it or says why.
- **A 68k stub needs a running processor.** Asking the script too early is not an error, it is a hang or garbage.

## Development notes

- **2026-08-29** — Ported in a day; the surprise was that FatFs was never the obstacle, the File System Manager was.
  Side effect: Circle's clock set at startup, since `get_fattime()` returned zero and dated files `1980-00-00`
  ([Clock and PRAM](clock-and-pram.md)).
- **2026-09-08** — Accented names fixed with a generated table.

## References

- `BasiliskII/TECH` §6.10; `extfs.cpp:229`, `:487`, `:491`; `Unix/extfs_unix.cpp:79-83`
- infinite-mac `Unix/mac_encodings.cpp:316`
