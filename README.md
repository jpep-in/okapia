# Okapia

A classic Macintosh on bare metal: [Basilisk II](https://github.com/kanjitalk755/macemu) ported onto
[Circle](https://github.com/rsta2/circle), so a Raspberry Pi boots straight into a 68k Mac with no Linux
underneath. The kernel image on the SD card *is* the emulator.

Disk images live on the card, and every write the Mac acknowledges is on the card when it says so.

**Status**: boots System 7.1 and 7.6, six colour depths, keyboard, mouse, shared folder, clean Shut Down.
No networking or sound yet. `planification.md` (French) tracks the phases.

## The card

FAT-formatted, files at the root:

| File | What it is |
|---|---|
| `kernel8.img` | Okapia, plus Circle's `config.txt` and firmware files |
| `okapia.rom` | a Macintosh ROM image (a Quadra 650 ROM is what this targets) |
| `machd76.image` | a disk image holding a System — any name works |
| `BasiliskII_Prefs` | preferences. Written for you on the first boot if absent |
| `BasiliskII_XPRAM` | the Mac's PRAM, 256 bytes. Written by Okapia |
| `BasiliskII.keycodes` | optional: a key-mapping override. See `keycodefile` |
| `shared/` | the shared folder, if you use one |

No ROM and no disk image ships with this project.

## Preferences

`/BasiliskII_Prefs` at the root of the card, in Basilisk II's format and read by Basilisk II's own parser
— a prefs file from a desktop Basilisk II works here, and vice versa. Edit it on anything that reads FAT.

One `keyword value` per line. A `#` or `;` **in the first column** starts a comment; there is no
trailing-comment syntax, so `ramsize 268435456 # 256 MB` sets the value to `268435456 # 256 MB`. An
unrecognised keyword warns and is ignored.

| Keyword | Default | Meaning |
|---|---|---|
| `disk` | `/machd76.image` | A disk image on the card. Repeat for more volumes; the Mac starts from the first bootable one. A `*` prefix mounts it read-only. |
| `rom` | `/okapia.rom` | The Macintosh ROM image. |
| `ramsize` | `268435456` | Mac RAM in bytes. 256 MB is the ceiling here. |
| `modelidauto` | `true` | Set `modelid` from the System on the boot volume. |
| `modelid` | `5` | `5` = Mac IIci, needed by System 7.0–7.1. `14` = Quadra 900, needed by Mac OS 8. Used only when `modelidauto` is off or detection fails. |
| `cpu` | `4` | `0` = 68000 … `4` = 68040. |
| `fpu` | `true` | FPU emulation. |
| `frameskip` | `0` | `0` = dynamic, `1` = every frame, `2` = 30 Hz, `4` = 15 Hz, `6` = 10 Hz. The Mac draws its own cursor, so a low rate reads as a laggy mouse. |
| `nosound` | `false` | On under QEMU, which models no audio output. |
| `nocdrom` | `false` | Don't install the CD-ROM driver. |
| `extfs` | `/shared` | The shared folder. Empty for none. |
| `extfsname` | `Okapia` | Its name on the desktop, 27 characters max. |
| `hfsrepair` | `true` | Repair a volume an interrupted session left in use. |
| `hfsinventory` | `true` | List the card's volumes at startup. |
| `floppy` | *(empty)* | Leave the empty entry: without a `floppy` line the Mac tries to mount two floppy drives that do not exist. |
| `bootdrive`, `bootdriver` | `0` | Which drive the ROM starts from; `0` is the default order. |
| `keyboardtype` | `5` | ADB keyboard type. |
| `keycodefile` | `/BasiliskII.keycodes` | A key-mapping file on the card, in Basilisk's own format — a `BasiliskII.keycodes` copied from a desktop Basilisk II works unchanged. Only needed for a keyboard that reports something unusual; absent means the built-in table stands. |
| `timezone` | `0` | Minutes east of UTC — `60` for CET, `120` for CEST, `-300` for EST. The clock starts at the time the kernel was built, in UTC, and a Mac of this era has no time zone: its clock *is* local time. Without this it runs an hour or two behind and looks like a stale build. There is no RTC on a Pi and no NTP yet. |
| `yearofs`, `dayofs` | `0` | Offsets on the Mac's clock. |

### Keywords that do nothing here

Written back into the file unchanged, so a value that seems ignored is ignored on purpose.

| Keywords | Why |
|---|---|
| `screen`, `displaycolordepth`, `title`, `init_grab`, `hotkey`, `scale_*`, `mag_rate`, `gammaramp`, `sdlrender`, `nogui` | They configure a window on a host desktop. The Mac owns the framebuffer directly; its Monitors control panel picks the depth. |
| `jit*` | Circle marks everything past `_etext` non-executable, so a JIT is impossible. The interpreter is the only CPU here. |
| `ether`, `etherconfig`, `udptunnel`, `udpport`, `redir`, `host_domain` | No networking yet. |
| `seriala`, `serialb` | The Pi's one usable UART carries Okapia's log. |
| `dsp`, `mixer` | OSS device paths. Sound goes through Circle's own device. |
| `scsi0`…`scsi6` | Host SCSI pass-through. There is no host. |
| `keycodes`, `mousewheel*`, `swap_opt_cmd` | X11 and SDL keyboard translation. Okapia feeds ADB from raw HID reports. |
| `noclipconversion`, `name_encoding` | Host clipboard and host filename conversion. |
| `ignoresegv` | Installs a POSIX `SIGSEGV` handler. Bare metal has no signals; faults arrive as C++ exceptions. |
| `cdrom` | No drive to name; pointing it at an image file is untested. |
| `xpram` | The PRAM file name is fixed. |
| `idlewait`, `sound_buffer`, `delay`, `fbdevicefile` | Host-specific tuning with no counterpart. |

## The shared folder

`extfs /shared` makes the `shared` directory on the card appear on the Mac's desktop as a volume, named by
`extfsname`. That is how files get in and out: copy them onto the card from anything that reads FAT.

It is built on Apple's **File System Manager 1.2**:

| System | |
|---|---|
| 7.0, 7.1 | install the File System Manager 1.2 extension |
| 7.5 and later | works as installed |

Without it the log says `No FSM present, disabling ExtFS` and no volume appears. The FSM is *not* File
Sharing — that is AppleShare over the network, and it does nothing here.

**One folder, not several**: Basilisk's ExtFS is single-volume by design. Put sub-folders inside it.

A file's Mac type, creator and resource fork live in `.finf/` and `.rsrc/` sub-directories beside it,
created as needed. A file you copied onto the card yourself has neither, so it gets a type from its
extension — a `.txt` arrives as a text document, a `.jpg` as a picture.

The clock starts from the later of two things: the time the kernel was built, and the last time any volume
on the card was written to. So a machine that ran yesterday comes back roughly where it left off rather
than at the build date. It never goes backwards. An RTC and NTP will come first in that order once they
exist.

Setting the date from the Mac's own Date & Time control panel does not stick: Basilisk drops writes to the
emulated clock on every platform. The host owns the time.

The Mac's PRAM — startup disk, sound volume, desktop pattern — is kept in `BasiliskII_XPRAM` at the root
of the card and written the moment it changes, so a power cut does not cost your settings.

## After a power cut

A Mac refuses to start from a volume whose Master Directory Block still says "in use", and an interrupted
session — pulled plug, reset, `kill -9` — always leaves it that way. That is a File Manager policy, not
damage: measured on a healthy volume, one killed session sets that one flag and changes nothing else. No
lost writes, no catalogue damage.

Okapia scavenges the volume itself before the Mac sees it, so a hard stop costs the boot and nothing else.
The Mac still tells you it was not shut down properly, which it works out another way.

Set `hfsrepair false` to switch that off: the volume is then left exactly as found and the Mac shows the
question-mark floppy. **Okapia never marks a volume clean without scavenging it** — that would hide real
corruption.

Still, shut the Mac down from the Finder. `Special → Shut Down` closes the disk image and halts the board.

## Building

macOS ships GNU Make 3.81; `scripts/env.sh` points `$MAKE` at a 4.x one.

```
./scripts/bootstrap.sh          # toolchain, submodules, reference sources
./scripts/build-libs.sh qemu    # Circle + newlib — once per target: qemu, 3, 4, 5
. scripts/env.sh && $MAKE -C src/kernel
```

Then, under QEMU:

```
./scripts/make-sd-image.sh      # a card built from qemu/sd-contents/, sized to fit
./scripts/run-live.sh           # a window you can use; end with Finder → Shut Down
./scripts/run-qemu.sh           # serial on stdout, GDB on :1234
./scripts/screenshot.sh         # boot a throwaway copy, capture the Mac's screen
./scripts/run-test.sh           # kill the guest, then check the volume survived
```

Objects under `src/kernel/emu/` do not depend on the Makefile, so after changing a flag run
`$MAKE -C src/kernel okapia-clean` or you will link stale ones. `AGENTS.md` has the rest.

## Licence

GPLv3 or later. Basilisk II is GPLv2-or-later (Christian Bauer and contributors), Circle is GPLv3-or-later
(Rene Stange), libhfs is GPLv2-or-later (Robert Leslie). No Apple ROM or system software is included.
