# Network

> Putting the Macintosh on the local network, by cable or Wi-Fi. The Mac shares the Pi's MAC address and is a node
> of its own: MacTCP or Open Transport, AppleTalk, a native SMB client such as Mamba, period browsers. Main files:
> `src/common/ether_okapia.cpp` (the Mac's driver, per engine), `src/hosts/circle/board_net_circle.cpp` (the interface,
> once for the board), `src/hosts/circle/wlan_circle.cpp` (Wi-Fi and its WPA supplicant),
> `src/common/net_config_okapia.cpp` (the preferences, pure), `src/hosts/circle/wlan_hostap.c` (what the boot menu asks the
> supplicant), `src/firmware/okapia_network.cpp` (the boot menu's Network page).

## Using it

**From the boot menu**: the **Network** page ([Boot menu](boot-menu.md#using-it)) chooses the connection — none, the
Ethernet cable or the Wi-Fi — lists the Wi-Fi networks in range with a lock and four signal bars, and takes the
password and the country. **Join** tries it there and then, and the line under the page says whether the access
point took the key, before the Macintosh starts. Choosing a connection starts it at once if the board has none; once
one is running, changing it needs Okapia to restart (Start reads **Save and restart**), since Circle cannot give a
network device back. What the page changes is written when the Macintosh starts or the board shuts down.

**Or by hand**, three lines in `/Okapia_Prefs` ([Preferences](../preferences.md#network)), in the spirit of a
BlueSCSI's `WiFiSSID` and `WiFiPassword`:

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
which `CNetDevice` does not have and Wi-Fi does not allow. Okapia runs no network stack of its own. What it asks
itself goes beside the Mac's frames on the same address, told apart by a transaction number or a port of its own:
today the time ([Clock and PRAM](clock-and-pram.md#time-from-the-network)), whose answers `BoardNetPoll()` takes
out before the Mac's ring.

### The board's half, claimed once

`board_net_circle.cpp` is in `SHARED_SRCS`: the device, the receive ring and, for Wi-Fi, the scheduler exist once for
the life of the board, and both engines' `ether_okapia.cpp` call into that one copy
([Architecture](../project/architecture.md#resources-taken-once-for-the-life-of-the-board)).

- **`BoardNetStart()`** (`board_net_circle.cpp:195`) reads the preferences on the first start of either Macintosh —
  or earlier, when the boot menu's Network page asks — and answers the same thereafter. A start that found no
  `ether` to obey is not spent (`BoardNetTried()`): the menu may choose a connection after a Macintosh ran without
  one. A device that failed is final. Wired: the Pi 4's GENET (`CBcm54213Device`), else the first USB Ethernet device
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

`ether_okapia.cpp` replaces `dummy/ether_dummy.cpp` in both engines. It is adapted from upstream's
`Unix/ether_unix.cpp`, with the reception thread replaced by polling.

- **`EtherPoll()`** (`ether_okapia.cpp:413`) runs in each engine's 1 kHz seam, after `InputDrain()`
  (`cpu_ticks_okapia.cpp:141`, `sheepshaver/cpu_ticks_okapia.cpp:134`). It polls the board and, when a frame waits,
  raises `INTFLAG_ETHER`; the Mac's interrupt empties the ring straight into a Mac-side packet
  (`EtherInterrupt()`, `:211`; `EtherIRQ()` on the PowerPC side, `:380`).
- **No interrupt before the System asks for frames.** Basilisk's `EthernetPacket` allocates its buffer with
  `NewPtrSysClear` from inside the interrupt (`ether.cpp:468-481`). Until a protocol handler is attached (68k) or the
  DLPI driver is open (PowerPC, `ether_driver_opened`), waiting frames are discarded instead (`BoardNetDiscard()`).
- **68k**: upstream's dispatch to protocol handlers, in a fixed table of 16 rather than a `std::map`; 802.3 frames go
  to the handler for type 0 (AppleTalk Phase 2).
- **PowerPC**: upstream's glue (`EtherInit`, `AO_*`, `EtherIRQ`). `ether_reset()` stays in
  `sheepshaver/platform_bits_okapia.cpp`, where it is the PowerPC Macintosh's restart signal
  ([Startup and shutdown](startup-and-shutdown.md#restart-from-mac-os)).
- **Every frame sent carries the interface's address as its source**, set again at send time.
- **No UDP tunnel**: `ether_start_udp_thread()` refuses, so `udptunnel true` means no network.

### Wi-Fi

Circle's `addon/wlan` is a port of Plan 9's driver and of `wpa_supplicant`. Three things it takes from elsewhere in
Circle's own sample are supplied by `wlan_circle.cpp`:

- **A scheduler.** The driver runs `wifireader` and `wifitimer` as Circle tasks (`ether4330.c:2616`), and the
  supplicant is a third. `WlanStart()` (`wlan_circle.cpp:279`) creates the `CScheduler` the first time Wi-Fi is asked
  for; the emulation thread becomes its main task, and the tasks run only when it yields: `WlanYield()` from
  `BoardNetPoll()`, so from the engine seams and from the boot menu's loop (`HostPoll()`, `okapia_menu.cpp:828`, `:1818`).
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

The configuration (`NetWpaConfigBuild()`, `net_config_okapia.cpp`) gives the SSID in hex and the **key**, derived
from the passphrase with hostap's own `pbkdf2_sha1()`, never the passphrase: the supplicant's line reader cuts at a
`#` that an earlier `"` fooled (`hostap/src/utils/config.c:66-84`). With no SSID it is the country alone
(`NetWpaConfigNoNetwork()`): the supplicant runs, joins nothing and answers scans, which is how the boot menu lists
networks before one is chosen. `tests/host/check_net.cpp` checks both.

### What the boot menu asks the supplicant

The networks in range, the state of the join and a new network without a restart all live in
`struct wpa_supplicant`, whose layout depends on the `CONFIG_*` switches the library was built with. So
`wlan_hostap.c` is C, compiled with `Makefile.circle`'s switches copied into `WLAN_HOSTAP_FLAGS`
(`src/kernel/Makefile`), and exposes no hostap type (`wlan_hostap.h`):

- **The interface pointer** is kept static in Circle's `main_okapia.c`, so `wpa_supplicant_add_iface()` is wrapped on
  the final link and the wrap keeps what it returns.
- **Scan**: a `MANUAL_SCAN_REQ`, which the supplicant honours with no network enabled. The page asks again every ten
  seconds while nothing is joined. **Results**: the BSS table, signal in dBm, the privacy bit for the lock; the page
  keeps one line per name at its strongest, drops hidden networks and sorts by signal (`NetworkSortSeen()`).
- **Join**: `WlanApply()` rebuilds the configuration in memory and calls `wpa_supplicant_reload_configuration()`,
  which reads it again through the same `f_open` wrap. **State**: `WPA_COMPLETED` is joined; a configured network
  with `auth_failures` is a refused key.
- **The menu lets the tasks run**: it waits in `CScheduler::MsSleep()` once a scheduler exists (`FwSleep()`,
  `HostSleep()`, `okapia_host_circle.cpp`), not in `CTimer::MsDelay()`, which held them still.

### Resources

The Pi 4 kernel with Wi-Fi is 3336 KB, under the 16 MB `KERNEL_MAX_SIZE`. The QEMU build compiles the Wi-Fi out
(below), 2862 KB. Polling costs about 3% of guest speed under QEMU at idle with a USB adapter (24.7 against
25.4 M opcodes/s, 2026-11-10), mostly QEMU's own USB: each poll submits a USB request. On the Pi 4's port a poll is
a register read.

### The bench

`net_bench_circle.cpp` measures what the interface carries with no Macintosh in the way, against
`scripts/netbench.py` on another machine. It is a measuring tool, so a normal kernel does not carry it: build with
`$MAKE -C src/kernel OKAPIA_NETBENCH=1` (after `okapia-clean`); without it a `netbench` line only earns one line in
the log saying it is ignored. `netbench <address>` in the preferences runs it once per power-on, before the
window: the board takes the clock's DHCP lease (so `ntp` must name a server), finds the other machine by ARP, says
`HELLO` on UDP 9000 until the script answers, then sends 1472-byte datagrams for the seconds asked and receives them
for as long. Both ends print frames, Mbit/s and losses; the board's lines start `Network bench:`. `--rate` caps what
the script sends, to find where the board starts to lose frames. The board paces its own sending: each frame its
ring refuses spaces the next ones by an eighth more, so a handful of refusals find the rate the interface keeps. The datagrams carry no UDP checksum (RFC 768 allows
it), so the frame is built once and only its sequence number changes.

The bench for the Pi 4 by cable: the Pi straight into a Mac's USB Ethernet adapter, set by hand to `192.168.2.1`
with no Internet Sharing, and `netbench 192.168.2.1 192.168.2.2` — the second address because nothing then serves
DHCP. Internet Sharing works too, but only switched on **after** the link is up (Pitfalls).

## Status

- [x] `ether_okapia.cpp` with the Pi's MAC address shared, both engines (2026-11-10)
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
- [x] Pi 4 by cable to a Mac's USB Ethernet adapter (ASIX AX88179, which macOS 27 needs ASIX's driver for):
      DHCP from Internet Sharing, NTP, and Mamba on System 7.6.1 at **2.2 to 2.5 MB/s** (40 MB in 18 s, 80 MB in
      32 s) — sixteen times the Wi-Fi's. No frame dropped; the 68k interpreter ran at 27 to 32 M instructions a
      second during the copy, against 35 M the most seen in the log (2026-11-12)
- [x] The bench on the Pi 4 by cable (1000BASE-T, AX88179 on a Mac set by hand), datagrams of 1472 bytes for 10 s:
      **sent 709 Mbit/s**, none lost, paced at last to one frame every 20.3 µs after 16 refusals by the ring
      (1 Gbit/s is one every 12.3 µs); **received 936 Mbit/s**, 0.3 % lost. A first run without pacing read
      104 Mbit/s: Circle logs every refused frame (`TX frame dropped`) on a 115,200-baud line, and 2,611 of them
      measured the log. Either way the Macintosh's own 2.2 MB/s (18 Mbit/s) is far below both: the limit is the
      emulated machine, not the network (2026-11-13)
- [ ] The bench by Wi-Fi
- [ ] Trials on the Pi 4: a group rekey (an hour or more connected); reconnection after the access
      point restarts
- [ ] Wi-Fi drops a few seconds after joining while the Macintosh starts (below), then rejoins: understand it
- [ ] Trials: Mamba under System 7.1 with MacTCP, against Samba; AppleTalk over Wi-Fi (802.3 frames through the
      Broadcom firmware)
- [ ] PowerPC engine with Open Transport on Mac OS 8.6: compiled, never run
- [ ] Restart from Mac OS five times and switch engines with the network on, free heap logged per round
- [x] The boot menu's Network page: connection, networks in range with lock and signal, password, country, Join
      with the answer on the page; host checks and renders (2026-11-12)
- [ ] The Network page tried on the Pi 4: scan, Join, a refused password, the cable's status
- [ ] An extension to choose the network and type the password from the Mac, as BlueSCSI's Wi-Fi DA does
- [ ] WPA3: Circle's supplicant is built without `CONFIG_SAE` (`Makefile.circle:77-85`)
- [x] Okapia's own question on the shared MAC: the time, in raw frames (DHCP, ARP, DNS, SNTP), told apart from the
      Mac's by transaction number and port, in the background (`ClockNetTake()`, `net_clock.cpp`; [Clock and
      PRAM](clock-and-pram.md#time-from-the-network)) (2026-11-12)
- [ ] Network brought up before the Mac for provisioning, then handed to the Mac — see
      [Storage](storage.md#provisioning)
- [ ] slirp as an option, if isolation or NAT turns out to be wanted
- [ ] AppleTalk enabled in the default PRAM, as Infinite Mac does (`ether_helpers.h`), so a System finds the network
      with no setting — see the [Infinite Mac study](../notes/research/infinite-mac.md)

## Pitfalls

- **`netbench` holds up every start for as long as it waits**, before the window: the link (Wi-Fi associates in
  about seven seconds), the lease, the other machine's ARP, the script. A card left with the line after the bench
  measured a 22-second boot on 2026-11-15 — Wi-Fi joined, NTP answered, then ten seconds of ARP for a Mac no longer
  on the cable. The waits are now 20 s for the lease, 15 s for the link and 2 s for ARP; the line belongs on the
  card only while measuring, and a normal kernel no longer carries the bench at all (`OKAPIA_NETBENCH`). Without it
  nothing on the network blocks the start but the Wi-Fi firmware's download
  (about 0.7 s, in `BoardNetStart()`): association, DHCP and NTP run in the background.
- **Internet Sharing on a Mac with an ASIX AX88179 adapter stops hearing the Pi when the link goes down and up.**
  Sharing bridges the adapter (`bridge100`, its own MAC) and asks the adapter to take frames for the bridge; with
  ASIX's DriverKit driver (2.4.0, macOS 27) that holds only if Sharing starts after the link is up. Every restart of
  the Pi drops the link, and from then on the adapter hands macOS nothing addressed to the bridge — broadcasts pass,
  so DHCP still works and the failure looks like everything else: DNS unanswered, the Macintosh's replies lost, no
  error anywhere (`netstat -I en9`: a handful received against hundreds sent). Measured on 2026-11-12 by elimination:
  the kernel of the evening before failed the same way, and switching Sharing on after the Pi was up made it work.

- **Stock QEMU's `usb-net` never hands Circle a frame.** Circle's `doc/qemu.txt` says networking needs a patched QEMU;
  measured with QEMU 11.1: 40,000 polls, 20 frames sent from the host, none received. Sending works. The receive path
  is proved instead by `NET_SELFTEST` in the trace build (`ether_okapia.cpp`, [Testing](../contributing/testing-and-debugging.md#qemu-network)).
- **No Wi-Fi in the QEMU build**: with `NO_SDHOST` on a Pi 3 target the Wi-Fi's controller is the card's, and starting
  the chip would take the card away from the Mac. `WlanStart()` refuses at compile time.
- **The Wi-Fi tasks run only when the emulation thread yields.** A loop that waits without calling `BoardNetPoll()`
  starves the supplicant and can lose the connection; the boot menu's two loops call it.
- **The page writes the preferences in memory before it starts a connection** (`BoardNetStart()` reads them), so
  what it compares against when saving must follow: comparing with the card's values, a cable started from the page
  and the Wi-Fi chosen again looked unchanged, and the file was written with `ether eth` (Pi 4, 2026-11-12). And the
  connection the card already names is started when the page is first turned to, not on a click: the list stayed
  empty until a radio was changed.
- **`wlan_hostap.c` must be compiled with the library's switches.** Read with others — or from C++ — every field of
  `struct wpa_supplicant` after the first conditional one is read from the wrong place. A change to
  `Makefile.circle`'s `DEFINE` owes the same change to `WLAN_HOSTAP_FLAGS`.
- **`src/common` has a `config.h` of its own**, and a quoted include is looked for beside the including file first:
  `wlan_hostap.c` names hostap's headers by their directory (`"wpa_supplicant/config.h"`).
- **The supplicant must run for the whole session**, not only to join: routers renew the group key, typically every
  hour.
- **Circle's driver refuses to join without a country** (`driver_circle.cpp:563`, "Country code not set").
- **Interrupting the Mac before its driver is open allocates in a System heap that may not exist yet**
  (`ether.cpp:468-481`): wait for a protocol handler.
- **SMSC951x and LAN7800 receive nothing until `UpdatePHY()` has seen the link** (Circle 51.1, `e0d10bba`): their
  `ReceiveFrame()` no longer submits a blocking bulk-IN with the cable out, which used to stall the caller — our
  1 kHz seam. The link state is now only what the last `UpdatePHY()` read, hence the call at start and every two
  seconds.
- **Hidden Wi-Fi networks need Circle 51.1 or later**: `scan_ssid=1` (`net_config_okapia.cpp`) was always written,
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
- **2026-11-12** — The Network page of the boot menu, with the window turned into tabs. Starting the network from the
  menu needed `BoardNetStart()` not to spend its one attempt on an absent `ether`, a supplicant that runs with no
  network, and a menu loop that sleeps through the scheduler.

## References

- `BasiliskII/src/ether.cpp`, `Unix/ether_unix.cpp`, `dummy/ether_dummy.cpp`; `SheepShaver/src/ether.cpp`,
  `SheepShaver/src/emul_op.cpp:286`
- Circle `include/circle/netdevice.h`, `lib/bcm54213.cpp`, `lib/usb/usbcdcethernet.cpp`, `addon/wlan/` (`bcm4343.cpp`,
  `ether4330.c`, `hostap/wpa_supplicant/wpasupplicant.cpp`, `hostap/src/l2_packet/l2_packet_circle.cpp`,
  `firmware/Makefile`), `doc/qemu.txt`
- BlueSCSI v2 `src/BlueSCSI.cpp`, `lib/BlueSCSI_platform_RP2MCU/BlueSCSI_platform_network.cpp` (in `reference/`)
- Mamba, a native SMB2/3 client for the classic Macintosh over MacTCP
