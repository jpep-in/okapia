# Network

> Putting the Macintosh on the local network, by cable or Wi-Fi. The Mac shares the Pi's MAC address and is a node
> of its own: MacTCP or Open Transport, AppleTalk, a native SMB client such as Mamba, period browsers. Main files:
> `src/circle/ether_circle.cpp` (the Mac's driver, per engine), `src/circle/board_net_circle.cpp` (the interface,
> once for the board), `src/circle/wlan_circle.cpp` (Wi-Fi and its WPA supplicant),
> `src/circle/net_config_circle.cpp` (the preferences, pure).

## Using it

Three lines in `/BasiliskII_Prefs` ([Preferences](../preferences.md#network)), in the spirit of a BlueSCSI's
`WiFiSSID` and `WiFiPassword`:

```
ether wlan
wifissid My network
wifipassword my password
wificountry FR
```

- **`ether`** decides: `eth` is the Pi 4's own port, or a USB Ethernet adapter on any board; `wlan` is the on-board
  Wi-Fi. Absent, or anything else (a desktop Basilisk II's `slirp`), means no network and no device claimed.
- **`nonet` is ignored.** Every card written before the network existed says `nonet true`, because the default was
  forced and `SavePrefs()` wrote it out; honouring it would make `ether wlan` do nothing on exactly those cards.
- **The value runs to the end of the line**: spaces, `#` and `;` in an SSID or a password need no quotes. A Windows
  `\r` is dropped. An empty `wifipassword` is an open network; 64 hex digits are taken as the key itself.
- **`wificountry`** sets the channels the radio may use. Circle's driver will not join without one; absent, `US` is
  used and the log says channels 12 and 13 are out of reach.
- **Wi-Fi needs the Broadcom firmware in `/firmware` on the card**: `brcmfmac43455-sdio.bin`, `.txt` and `.clm_blob`
  for a Pi 4. `make -C external/circle-stdlib/libs/circle/addon/wlan/firmware firmware` downloads them at the revision
  Circle pins, and `scripts/make-pi-sd.sh` copies them.
- **The password is kept in clear on the card**, as on a BlueSCSI. The key that `wpa_passphrase` prints (64 hex
  digits) works in its place for whoever would rather not.
- On the Mac: the Network control panel (or AppleTalk) set to EtherTalk, and MacTCP or TCP/IP set to DHCP. The Mac
  takes its own address from the router; Okapia has none.

The log says what happened: `Network: Wi-Fi, the Macintosh is <MAC>`, then `Wi-Fi: connected to "<SSID>"` when the
handshake completes; `Ethernet: link up (<speed>)` on a cable.

## How it works

### The Mac shares the Pi's MAC address

The Mac's frames go out as they are through `CNetDevice::SendFrame()`, and every frame the interface receives is
the Mac's, with the Pi's address as its own. A layer-2 bridge with a foreign address would need promiscuous mode,
which `CNetDevice` does not have and Wi-Fi does not allow. Okapia runs no network stack of its own: anything it
would want before the Mac starts (NTP, provisioning) must finish and hand the interface over.

### The board's half, claimed once

`board_net_circle.cpp` is in `SHARED_SRCS`: the device, the receive ring and, for Wi-Fi, the scheduler exist once for
the life of the board, and both engines' `ether_circle.cpp` call into that one copy
([Architecture](../project/architecture.md#resources-taken-once-for-the-life-of-the-board)).

- **`BoardNetStart()`** (`board_net_circle.cpp:195`) reads the preferences on the first start of either Macintosh and
  answers the same thereafter. Wired: the Pi 4's GENET (`CBcm54213Device`), else the first USB Ethernet device
  Circle registered — USB is attached before the Mac starts and not again, so it is there or not. Either way
  `UpdatePHY()` is called at once.
- **The ring**: 64 slots of 1514 bytes, allocated at the first start (`board_net_circle.cpp:52`).
  `BoardNetPoll()` (`:351`) takes up to 16 frames a visit off the device; frames received while no engine listens are
  dropped, as a closed driver would.
- **Multicast**: up to 15 groups (`:83`), handed to `SetMulticastFilter()` whole whenever the list changes.
- **The link** is looked at about once a second; on a cable `UpdatePHY()` every two seconds, as Circle's `CPHYTask`
  does. Frames dropped are reported every sixteen seconds at most.
- **Counters** every five seconds with `perfreport`: link, whether the Mac listens, frames received (to the Mac and to
  the supplicant), sent and refused, multicast groups — `okapia-net: Wi-Fi up, Mac listening: rx …`. The link's
  transitions are said once, at a moment a serial cable plugged in later never sees.
- **Short frames are padded to 60 bytes** (`BoardNetSend()`, `:442`): a Macintosh sends 36-byte AARP probes and
  leaves the padding to its card.

### The Mac's half, per engine

`ether_circle.cpp` replaces `dummy/ether_dummy.cpp` in both engines. It is adapted from upstream's
`Unix/ether_unix.cpp`, with the reception thread replaced by polling.

- **`EtherPoll()`** (`ether_circle.cpp:413`) runs in each engine's 1 kHz seam, after `InputDrain()`
  (`cpu_ticks_circle.cpp:141`, `sheepshaver/cpu_ticks_circle.cpp:134`). It polls the board and, when a frame waits,
  raises `INTFLAG_ETHER`; the Mac's interrupt empties the ring straight into a Mac-side packet
  (`EtherInterrupt()`, `:211`; `EtherIRQ()` on the PowerPC side, `:380`).
- **No interrupt before the System asks for frames.** Basilisk's `EthernetPacket` allocates its buffer with
  `NewPtrSysClear` from inside the interrupt (`ether.cpp:468-481`). Until a protocol handler is attached (68k) or the
  DLPI driver is open (PowerPC, `ether_driver_opened`), waiting frames are discarded instead (`BoardNetDiscard()`).
- **68k**: upstream's dispatch to protocol handlers, in a fixed table of 16 rather than a `std::map`; 802.3 frames go
  to the handler for type 0 (AppleTalk Phase 2).
- **PowerPC**: upstream's glue (`EtherInit`, `AO_*`, `EtherIRQ`). `ether_reset()` stays in
  `sheepshaver/platform_bits_circle.cpp`, where it is the PowerPC Macintosh's restart signal
  ([Startup and shutdown](startup-and-shutdown.md#restart-from-mac-os)).
- **Every frame sent carries the interface's address as its source**, set again at send time.
- **No UDP tunnel**: `ether_start_udp_thread()` refuses, so `udptunnel true` means no network.

### Wi-Fi

Circle's `addon/wlan` is a port of Plan 9's driver and of `wpa_supplicant`. Three things it takes from elsewhere in
Circle's own sample are supplied by `wlan_circle.cpp`:

- **A scheduler.** The driver runs `wifireader` and `wifitimer` as Circle tasks (`ether4330.c:2616`), and the
  supplicant is a third. `WlanStart()` (`wlan_circle.cpp:279`) creates the `CScheduler` the first time Wi-Fi is asked
  for; the emulation thread becomes its main task, and the tasks run only when it yields: `WlanYield()` from
  `BoardNetPoll()`, so from the engine seams and from the boot menu's loop (`okapia_firmware.cpp:697`, `:1234`).
- **A link layer for the handshake.** Upstream's `l2_packet_circle.cpp` reaches EAPOL frames through
  `CNetSubSystem`'s link layer (`l2_packet_circle.cpp:66-72`), which would take every IP and ARP frame meant for the
  Mac. `wlan_circle.cpp` defines every `l2_packet_*` symbol itself (`:227` on), so the linker never pulls upstream's
  member out of `libwpa_supplicant.a`; the board routes frames of type `0x888E` to it (`WlanTakeEAPOL()`, `:155`)
  and nowhere else.
- **A configuration that is never a file on the card.** Circle's port of the supplicant reads its networks straight
  through FatFs: `f_open()` (`config_file.c:336`), then `f_gets()` and `f_close()`, which hostap's `utils/config.h`
  substitutes for `fgets` and `fclose`. Writing a file at every start would be a FAT and directory update a pulled
  plug can catch half done ([Storage](storage.md)). The three are wrapped on the final link (`FINAL_WRAPS`,
  `src/kernel/Makefile:271`): one name opens a file object read from memory (`wlan_circle.cpp:81`), every other file
  goes to FatFs.

The configuration (`NetWpaConfigBuild()`, `net_config_circle.cpp`) gives the SSID in hex and the **key**, derived
from the passphrase with hostap's own `pbkdf2_sha1()`, never the passphrase: the supplicant's line reader cuts at a
`#` that an earlier `"` fooled (`hostap/src/utils/config.c:66-84`). `tests/host/check_net.cpp` checks it.

### Resources

The Pi 4 kernel with Wi-Fi is 3336 KB, under the 4 MB `KERNEL_MAX_SIZE`. The QEMU build compiles the Wi-Fi out
(below), 2862 KB. Polling costs about 3% of guest speed under QEMU at idle with a USB adapter (24.7 against
25.4 M opcodes/s, 2026-11-10), mostly QEMU's own USB: each poll submits a USB request. On the Pi 4's port a poll is
a register read.

## Status

- [x] `ether_circle.cpp` with the Pi's MAC address shared, both engines (2026-11-10)
- [x] `ether eth|wlan` decides; `nonet` ignored, with a line in the log (2026-11-10)
- [x] Wired: Pi 4 GENET, USB Ethernet adapters; sending verified under QEMU — System 7.6 EtherTalk probes and NBP
      lookups on the wire, padded to 60 bytes (2026-11-10)
- [x] Receive path verified under QEMU by the trace build's self-test: an AARP request put in the ring is answered
      by the Mac on the wire (`Network self-test: the Macintosh answered…`, 2026-11-10)
- [x] Wi-Fi through Circle's `addon/wlan`: scheduler, supplicant, `l2_packet` replaced, configuration from the
      preferences in memory, firmware staged by `make-pi-sd.sh` (2026-11-10)
- [x] On the Pi 4, Wi-Fi to a Freebox (WPA2, 2.4 and 5 GHz): join, DHCP from Open Transport on System 7.6.1, ping
      from the LAN (2026-11-11)
- [x] Mamba on System 7.6.1 over Wi-Fi: a 37 MB file copied from an SMB share in 268 s, **about 140 KB/s** — the
      Freebox reports 1.2 Mbit/s; 103 frames a second reach the Mac, none dropped. The SE/30 with a BlueSCSI does
      115 KB/s (2026-11-11)
- [x] Network counters every five seconds with `perfreport` (2026-11-11)
- [ ] Trials on the Pi 4: DHCP by cable; a group rekey (an hour or more connected); reconnection after the access
      point restarts
- [ ] Wi-Fi drops a few seconds after joining while the Macintosh starts (below), then rejoins: understand it
- [ ] Trials: Mamba under System 7.1 with MacTCP, against Samba; AppleTalk over Wi-Fi (802.3 frames through the
      Broadcom firmware)
- [ ] PowerPC engine with Open Transport on Mac OS 8.6: compiled, never run
- [ ] Restart from Mac OS five times and switch engines with the network on, free heap logged per round
- [ ] The boot menu's Info page shows the network: interface, SSID, link
- [ ] An extension to choose the network and type the password from the Mac, as BlueSCSI's Wi-Fi DA does; or a
      Wi-Fi page in the boot menu
- [ ] WPA3: Circle's supplicant is built without `CONFIG_SAE` (`Makefile.circle:77-85`)
- [ ] Network brought up before the Mac only for bounded, optional work (NTP, provisioning), then handed to the Mac —
      see [Clock and PRAM](clock-and-pram.md) and [Storage](storage.md#provisioning)
- [ ] slirp as an option, if isolation or NAT turns out to be wanted
- [ ] AppleTalk enabled in the default PRAM, as Infinite Mac does (`ether_helpers.h`), so a System finds the network
      with no setting — see the [Infinite Mac study](../notes/research/infinite-mac.md)

## Pitfalls

- **Stock QEMU's `usb-net` never hands Circle a frame.** Circle's `doc/qemu.txt` says networking needs a patched QEMU;
  measured with QEMU 11.1: 40,000 polls, 20 frames sent from the host, none received. Sending works. The receive path
  is proved instead by `NET_SELFTEST` in the trace build (`ether_circle.cpp`, [Testing](../contributing/testing-and-debugging.md#qemu-network)).
- **No Wi-Fi in the QEMU build**: with `NO_SDHOST` on a Pi 3 target the Wi-Fi's controller is the card's, and starting
  the chip would take the card away from the Mac. `WlanStart()` refuses at compile time.
- **The Wi-Fi tasks run only when the emulation thread yields.** A loop that waits without calling `BoardNetPoll()`
  starves the supplicant and can lose the connection; the boot menu's two loops call it.
- **The supplicant must run for the whole session**, not only to join: routers renew the group key, typically every
  hour.
- **Circle's driver refuses to join without a country** (`driver_circle.cpp:563`, "Country code not set").
- **Interrupting the Mac before its driver is open allocates in a System heap that may not exist yet**
  (`ether.cpp:468-481`): wait for a protocol handler.
- **SMSC951x and LAN7800 receive nothing until `UpdatePHY()` has seen the link** (Circle 51.1, `e0d10bba`): their
  `ReceiveFrame()` no longer submits a blocking bulk-IN with the cable out, which used to stall the caller — our
  1 kHz seam. The link state is now only what the last `UpdatePHY()` read, hence the call at start and every two
  seconds.
- **Hidden Wi-Fi networks need Circle 51.1 or later**: `scan_ssid=1` (`net_config_circle.cpp`) was always written,
  but Circle's supplicant driver made no directed scan before hostap `966cab4a`.
- **Circle's supplicant reads its configuration through FatFs, not stdio** (`config_file.c:336`, `utils/config.h`): a
  `--wrap=fopen` linked, and the supplicant logged `Failed to open config file …, error: 11` on the board. The QEMU
  build compiles the Wi-Fi out, so only a board shows it.
- **Open Transport asks for its address once, when TCP/IP loads.** At the first start the Wi-Fi was not joined yet,
  and the Mac kept no address until it restarted with the link already up; opening the TCP/IP control panel did not
  ask again.
- **Wi-Fi drops while the Macintosh starts.** Seen on the Pi 4 at power-on (5 GHz, 3 s after joining) and after a
  restart from Mac OS (4 s after `The Macintosh restarted`): `CTRL-EVENT-DISCONNECTED reason=0`, the firmware's own
  disassociation (`ether4330.c`, `E_LINK` down or `E_DISASSOC_IND`), then a rejoin within 8 s. Not understood; the
  start path's long stretches without a yield are the first suspect. A later power-on joined and held.
- **One MAC address**: `CNetDevice` has no promiscuous mode (`netdevice.h:90` is the only filter it offers).
- **Pi 1, 2 and 3 put Ethernet on USB**, shared with the keyboard and mouse: sustained traffic starves input. A Pi 4
  is the target for networking.
- **The Mac reads its clock before any stack exists**: the network can improve the clock for the next boot, never
  the current one.

## Development notes

- **2026-08-25** — Design chosen at planning time: shared MAC, slirp as fallback, no bridge.
- **2026-09-14** — Survey of the projects that connect classic Macs to modern networks (Netatalk, Samba `vfs_fruit`,
  TashTalk, TashRouter, MacIP, WebOne): see [AppleTalk and SMB gateways](../notes/research/appletalk-smb-gateways.md).
- **2026-11-10** — Implemented, cable and Wi-Fi, now that Mamba mounts an SMB share from the Chooser over TCP/IP alone.
  Configuration modelled on BlueSCSI's `WiFiSSID`/`WiFiPassword` (`BlueSCSI.cpp:1039`). Under QEMU, System 7.6
  starts EtherTalk on the USB adapter; the self-test's AARP request is answered on the first try.
- **2026-11-10** — Circle 51.1.1 (circle-stdlib v21): non-blocking receive on USB adapters with the link down, hidden
  networks, a fix for routers that append an optional IE after the RSN IE; `UpdatePHY()` now called at start.
- **2026-11-11** — First run on the Pi 4, followed on the serial port (a 3.3 V USB adapter on GPIO 14 and 15). The
  supplicant could not read its configuration (the FatFs pitfall above); fixed by wrapping `f_open`, `f_gets` and
  `f_close`. Then: WPA2 joined on 5 GHz, DHCP after a restart of the Mac, ping 10/10 (14 to 379 ms), Wi-Fi held 48
  minutes until the next power-on and from it through the copy, and Mamba copied 37 MB at about 140 KB/s.

## References

- `BasiliskII/src/ether.cpp`, `Unix/ether_unix.cpp`, `dummy/ether_dummy.cpp`; `SheepShaver/src/ether.cpp`,
  `SheepShaver/src/emul_op.cpp:286`
- Circle `include/circle/netdevice.h`, `lib/bcm54213.cpp`, `lib/usb/usbcdcethernet.cpp`, `addon/wlan/` (`bcm4343.cpp`,
  `ether4330.c`, `hostap/wpa_supplicant/wpasupplicant.cpp`, `hostap/src/l2_packet/l2_packet_circle.cpp`,
  `firmware/Makefile`), `doc/qemu.txt`
- BlueSCSI v2 `src/BlueSCSI.cpp`, `lib/BlueSCSI_platform_RP2MCU/BlueSCSI_platform_network.cpp` (in `reference/`)
- Mamba, a native SMB2/3 client for the classic Macintosh over MacTCP
