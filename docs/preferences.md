# Preferences

> Every keyword of `Okapia_Prefs`, what it does on Okapia, and which upstream keywords do nothing here.

## The file

`/Okapia_Prefs` at the root of the card, in Basilisk II's own format and read by Basilisk II's own
parser (`LoadPrefsFromStream`, `SavePrefsToStream`). The same file serves Basilisk II and SheepShaver and
includes Okapia-specific keywords; its name does not change with the engine. Upstream preferences keep
their format compatibility when copied into this file.

For an existing workspace or Pi card, rename `BasiliskII_Prefs` to `Okapia_Prefs` before starting Okapia
to preserve its settings. Only `Okapia_Prefs` is read and written.

- One `keyword value` per line. A `#` or `;` **in the first column** starts a comment; there is no trailing
  comment syntax, so `ramsize 268435456 # 256 MB` sets the value to `268435456 # 256 MB`.
- An unknown keyword is warned about **and kept**: `SavePrefs()` writes back lines it does not recognise, so a
  card written by a later version, or by the other engine, loses nothing.
- Written with commented examples on the first boot if absent. The boot menu edits it; the file remains the
  source of truth.
- A keyword must be declared in a table to be read *and* written: `common_prefs_items` upstream, or
  `platform_prefs_items[]` in `src/common/prefs_okapia.cpp`. A value set only with `PrefsReplace*` works at
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
| `macmode` | — | repeatable `<slot> <width>x<height>`, such as `0 960x600`. Slots 0–5 have persistent driver IDs. Keep a slot assigned to the same size because Mac OS saves its choice by ID |
| `machidemode` | — | repeatable `<width>x<height>`, such as `1280x720`. Removes that size from Monitors on both engines. The 640×480 recovery mode is kept when it fits |
| `frameskip` | `0` | `0` = dynamic (default), `1` = every VBL, `2` = 30 Hz, `4` = 15 Hz, `6` = 10 Hz. The Mac draws its own cursor, so a low rate reads as a laggy mouse — see [Display](topics/display.md) |
| `qdaccel` | `off` | Experimental 68k QuickDraw. `sync` accelerates boolean copies and fills at every depth, and some shapes and conversions at 8 bits; `parallel` also uses core 2 for large transfers with a multicore Pi 4 build; `probe` logs eligibility and executes the original guest code; `verify` lets the guest draw, draws each `BitBlt` and `RgnBlt` again natively and logs any difference. See [QuickDraw](topics/display.md#68k-quickdraw). The boot menu's *QuickDraw acceleration* box writes `sync` or `off`, and only when it is changed. |
| `qdaccelmin` | `4096` | With `jit true` only: a rectangular or region transfer whose clipped area holds fewer bytes of pixels than this stays in the guest (4096 is 4096 pixels at 8 bits, 1024 at 32); 1 and 2 bits always do, where the translator already runs QuickDraw as ARM64 code. Tested in 68k by the trampoline, so a refused transfer never leaves translated code. See [QuickDraw](topics/display.md#68k-quickdraw). |

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
| `zone` | — | the time zone, one of the boot menu's 22: `utc`, `london`, `paris`, `athens`, `moscow`, `dubai`, `delhi`, `bangkok`, `beijing`, `tokyo`, `brisbane`, `sydney`, `noumea`, `auckland`, `saopaulo`, `martinique`, `newyork`, `chicago`, `denver`, `losangeles`, `anchorage`, `honolulu`. Summer time follows on its own. A Mac of this era has no time zone: its clock *is* local time ([Clock and PRAM](topics/clock-and-pram.md#time-zones-and-summer-time)) |
| `timezone` | `0` | minutes east of UTC, **without summer time** — the older way, read only when `zone` is empty. The boot menu offers it as itself until a city replaces it |
| `ntp` | `pool.ntp.org` | the time server, a name or an address, asked a few seconds after the network is up and once a day; empty for none ([Clock and PRAM](topics/clock-and-pram.md#time-from-the-network)) |
| `yearofs`, `dayofs` | `0` | offsets on the Mac's clock |

## Boot menu and diagnostics

| Keyword | Default | Meaning |
|---|---|---|
| `bootmenu` | `false` | always open the boot menu, without holding Option |
| `language` | `en` | the boot menu's language, a two-letter code (`en`, `fr`); an unknown code falls back to the first language |
| `idlewait` | `true` | let the Mac's idle patch replace `SynchIdleTime`; its first call is logged as `Macintosh idle`, the boot-finished signal the test scripts wait for |
| `serial` | `false` | send the log's lines to the serial port as well as to the card. The port is polled: about 87 µs a character, around 0.7 s of a boot, whether or not a cable is attached. The card has every line either way and the boot menu's *Logs* page reads it. A panic or a halt opens the port again and replays what the card has not drained yet. Always on under QEMU, whose scripts read the port. The first lines (banner, card) go out before the preference is read. The boot menu's *Serial output* box |
| `perfreport` | `false` | print engine, video, input, sound and clock counters every five seconds (always on under QEMU). With `serial` off they go to the log on the card and cost the Macintosh nothing on the wire. The boot menu's *Performance counters* box |

## Network

See [Network](topics/network.md). The boot menu's Network page writes all four; by hand, each value runs to the end
of the line: spaces, `#` and `;` need no quotes. The page removes a key it would leave empty rather than writing a
blank line.

| Keyword | Default | Meaning |
|---|---|---|
| `ether` | — | `eth`: the Pi 4's port or a USB Ethernet adapter; `wlan`: the on-board Wi-Fi. Anything else, or nothing, is no network and no device claimed |
| `wifissid` | — | the Wi-Fi network's name, 1 to 32 bytes |
| `wifipassword` | — | its WPA2 passphrase, 8 to 63 printable ASCII characters, or the key as 64 hex digits; empty for an open network. Kept in clear on the card. WPA3-only networks cannot be joined |
| `wificountry` | `US` when absent (the boot menu offers `FR` in French, `US` otherwise) | two letters for the radio's channels (`FR`, `DE`, …); without it channels 12 and 13 are out of reach |
| `netbench` | — | the address of a machine running `scripts/netbench.py`, then this board's own when nothing on the cable serves DHCP (`192.168.2.1 192.168.2.2`): the network is measured once at power-on, before the window, by a kernel built with `OKAPIA_NETBENCH=1` ([Network](topics/network.md#the-bench)). Empty, or a normal kernel: never |
| `nonet` | `false` | **ignored**: `ether` decides. Every card written before the network existed says `true` |

## Keywords that do nothing here

They are written back unchanged, so a value that seems ignored is ignored on purpose.

| Keywords | Why |
|---|---|
| `displaycolordepth`, `title`, `init_grab`, `hotkey`, `scale_*`, `mag_rate`, `gammaramp`, `sdlrender`, `nogui` | they configure a window on a host desktop; the Mac owns the frame buffer and Monitors picks the depth |
| `jit*` | the interpreter is the only CPU in a standard kernel. `jit true` is read by the experimental kernels only (`OKAPIA_JIT_ARM64=1`, `OKAPIA_JIT=1`). The boot menu's *JIT* box sets `jit`, and is grey in an image without the `OKAPIA_JIT_ARM64` translator |
| `etherconfig`, `redir`, `host_domain` | host network scripts and slirp's forwarding; the Mac is a node on the real network |
| `udptunnel`, `udpport` | no UDP tunnel: `udptunnel true` means no network |
| `seriala`, `serialb` | the Pi's usable UART carries Okapia's log |
| `dsp`, `mixer`, `sound_buffer` | OSS device paths and host buffers; sound goes through Circle's device |
| `scsi0`…`scsi6` | host SCSI pass-through; there is no host |
| `keycodes`, `mousewheel*`, `swap_opt_cmd` | X11 and SDL keyboard translation; Okapia feeds ADB from raw HID reports |
| `noclipconversion`, `name_encoding` | host clipboard and host file name conversion |
| `ignoresegv` | installs a POSIX `SIGSEGV` handler; bare metal has no signals |
| `xpram` | the PRAM file names are fixed, one per engine |
| `delay`, `fbdevicefile` | host-specific tuning |
