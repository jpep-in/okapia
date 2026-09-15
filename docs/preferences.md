# Preferences

> Every keyword of `BasiliskII_Prefs`, what it does on Okapia, and which upstream keywords do nothing here.

## The file

`/BasiliskII_Prefs` at the root of the card, in Basilisk II's own format and read by Basilisk II's own
parser (`LoadPrefsFromStream`, `SavePrefsToStream`): a prefs file from a desktop Basilisk II works here, and
the other way round. The same file serves both Macintosh; its name does not change with the engine.

- One `keyword value` per line. A `#` or `;` **in the first column** starts a comment; there is no trailing
  comment syntax, so `ramsize 268435456 # 256 MB` sets the value to `268435456 # 256 MB`.
- An unknown keyword is warned about **and kept**: `SavePrefs()` writes back lines it does not recognise, so a
  card written by a later version, or by the other engine, loses nothing.
- Written with commented examples on the first boot if absent. The boot menu edits it; the file remains the
  source of truth.
- A keyword must be declared in a table to be read *and* written: `common_prefs_items` upstream, or
  `platform_prefs_items[]` in `src/circle/prefs_circle.cpp`. A value set only with `PrefsReplace*` works at
  runtime and silently vanishes from the file — it cost one whole measurement.

## Machine and memory

| Keyword | Default | Meaning |
|---|---|---|
| `ramsize` | `268435456` | Mac RAM in bytes; 256 MB is the ceiling. On the PowerPC engine a value ≤ 1000 is read as megabytes and the floor is 16 MB. Changing it needs an Okapia restart: the block is allocated before USB |
| `rom` | `/okapia.rom` | the 68k ROM |
| `romppc` | *(falls back to `rom`)* | the PowerPC ROM |
| `modelidauto` | `true` | set `modelid` from the System on the boot volume |
| `modelid` | `5` | `5` = Mac IIci, `14` = Quadra 900; used when `modelidauto` is off or detection fails — see [68k engine](topics/engine-68k.md#model-id) |
| `cpu` | `4` | `0` = 68000 … `4` = 68040 |
| `fpu` | `true` | FPU emulation |
| `engine` | — | multiple: `<path> 68k` or `<path> powerpc`, one per **universal** volume only. Written by the boot menu |

## Disks

| Keyword | Default | Meaning |
|---|---|---|
| `disk` | `/machd76.image` | multiple: a disk image. **The order is the setting**: the Mac starts from the first bootable one. A `*` prefix mounts it read-only |
| `cdrom` | — | multiple: a CD image (a Toast image works). The boot menu mounts a volume as a CD-ROM |
| `bootdriver` | `0` | the driver the ROM starts from; `-62` (CD-ROM driver) to start from a disc |
| `bootdrive` | `0` | the drive the ROM starts from; `0` is the default order |
| `nocdrom` | `false` | don't install the CD-ROM driver |
| `floppy` | *(empty)* | leave the empty entry: without a `floppy` line the Mac tries to mount two floppy drives that do not exist |
| `hfsrepair` | `true` | scavenge a volume an interrupted session left in use — see [Storage](topics/storage.md) |
| `hfsinventory` | `true` | list the card's volumes in the log at startup (the inventory itself always runs) |

## Screen

| Keyword | Default | Meaning |
|---|---|---|
| `screen` | — | upstream's spelling, `win/<width>/<height>` (or `dga/…`): the resolution to start in. `0` means the display's own width or height. The size is also added to the Monitors list if missing |
| `frameskip` | `0` | `0` = dynamic (default), `1` = every VBL, `2` = 30 Hz, `4` = 15 Hz, `6` = 10 Hz. The Mac draws its own cursor, so a low rate reads as a laggy mouse — see [Display](topics/display.md) |

## Sound

| Keyword | Default | Meaning |
|---|---|---|
| `soundoutput` | `jack` | `off`, `hdmi`, `jack` or `usb` (Pi 4 and 5 only). `discard` pulls sound from the Mac and throws it away, for tests |
| `nosound` | `false` | the older switch; wins over `soundoutput` when they disagree. On under QEMU, which models no sound output |

## Input

| Keyword | Default | Meaning |
|---|---|---|
| `mousedpi` | `200` | what the pointing device reports per inch. `200` is what the Macintosh assumes; raise it for a modern mouse that feels too fast — see [Input](topics/input.md) |
| `keyboardtype` | `5` | ADB keyboard type |
| `keycodefile` | `/BasiliskII.keycodes` | a key-mapping file in Basilisk's own format; absent means the built-in table stands |

## Shared folder

| Keyword | Default | Meaning |
|---|---|---|
| `extfs` | `/shared` | the shared folder on the card; empty for none |
| `extfsname` | `Okapia` | its volume name on the desktop, 27 characters at most |

## Clock

| Keyword | Default | Meaning |
|---|---|---|
| `timezone` | `0` | minutes east of UTC — `60` for CET, `120` for CEST, `-300` for EST. A Mac of this era has no time zone: its clock *is* local time |
| `yearofs`, `dayofs` | `0` | offsets on the Mac's clock |

## Boot menu and diagnostics

| Keyword | Default | Meaning |
|---|---|---|
| `bootmenu` | `false` | always open the boot menu, without holding Option |
| `language` | `en` | the boot menu's language, a two-letter code (`en`, `fr`); an unknown code falls back to the first language |
| `idlewait` | `true` | let the Mac's idle patch replace `SynchIdleTime`; its first call is logged as `Macintosh idle`, the boot-finished signal the test scripts wait for |
| `perfreport` | `false` | print engine, video, input, sound and clock counters every five seconds (always on under QEMU) |
| `nonet` | `true` | networking is not implemented yet |

## Keywords that do nothing here

They are written back unchanged, so a value that seems ignored is ignored on purpose.

| Keywords | Why |
|---|---|
| `displaycolordepth`, `title`, `init_grab`, `hotkey`, `scale_*`, `mag_rate`, `gammaramp`, `sdlrender`, `nogui` | they configure a window on a host desktop; the Mac owns the frame buffer and Monitors picks the depth |
| `jit*` | Circle marks everything past `_etext` non-executable; the interpreter is the only CPU here |
| `ether`, `etherconfig`, `udptunnel`, `udpport`, `redir`, `host_domain` | no networking yet |
| `seriala`, `serialb` | the Pi's usable UART carries Okapia's log |
| `dsp`, `mixer`, `sound_buffer` | OSS device paths and host buffers; sound goes through Circle's device |
| `scsi0`…`scsi6` | host SCSI pass-through; there is no host |
| `keycodes`, `mousewheel*`, `swap_opt_cmd` | X11 and SDL keyboard translation; Okapia feeds ADB from raw HID reports |
| `noclipconversion`, `name_encoding` | host clipboard and host file name conversion |
| `ignoresegv` | installs a POSIX `SIGSEGV` handler; bare metal has no signals |
| `xpram` | the PRAM file names are fixed, one per engine |
| `delay`, `fbdevicefile` | host-specific tuning |
