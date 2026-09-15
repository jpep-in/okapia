# Network

> Putting the Macintosh on the local network. **Not implemented**: this page holds the design, its constraints and
> the work to do. Future file: `src/circle/ether_circle.cpp`.

## Using it

Nothing yet: `nonet` is `true` and the upstream keywords `ether`, `etherconfig`, `udptunnel`, `udpport`, `redir`
and `host_domain` have no effect ([Preferences](../preferences.md)). Files reach the Mac through the
[shared folder](shared-folder.md).

## How it will work

### Chosen design: the Mac shares the Pi's MAC address

The Mac's frames go out as they are through `CNetDevice::SendFrame()` and come back through `ReceiveFrame()`, with the
Pi's MAC address as the Mac's. The Mac runs its own ARP and DHCP and becomes a **real node on the local network**:
AppleShare, AppleTalk over Ethernet, TCP/IP, period browsers. No Circle patch, and it works over Wi-Fi as well, where
a layer-2 bridge with a foreign MAC would fail.

Constraint: one network stack at a time on the interface. Anything Okapia itself needs before the Mac starts (NTP,
provisioning downloads) must finish and hand the interface over.

### Fallback: slirp

`src/slirp/` (about 13,000 lines, already in the macemu tree) is user-space NAT using a handful of system calls that
circle-stdlib provides, `select()` included. Useful to give the Mac outgoing access without putting it on the LAN,
or to isolate it.

### Rejected: a bridge with a distinct MAC

`CNetDevice` has no promiscuous mode, and promiscuous mode does not work over Wi-Fi anyway.

### The Macintosh side

Basilisk's Ethernet driver (`ether.cpp`) calls the platform's `ether_init`, `ether_reset`, `ether_add_multicast`,
`ether_attach_ph`, `ether_write` and an interrupt path that delivers received packets from the 68k context — the same
"raise a flag, answer from the emulation thread" shape as [sound](sound.md) and [input](input.md). SheepShaver's
`ether_reset()` is already Okapia's, and serves today as the PowerPC restart hook
([Startup and shutdown](startup-and-shutdown.md#restart-from-mac-os)); a network implementation must keep that
behaviour. `ether.cpp`'s UDP tunnel is chosen at run time, so it compiles against stubs in `src/circle/compat/`
that log their call.

## Status

- [ ] `ether_circle.cpp` with the Pi's MAC address shared
- [ ] Honour `nonet` at startup; add a boot menu control only when there is a real choice (mode or interface), not
      for a plain on/off
- [ ] Network brought up before the Mac only for bounded, optional work (NTP, provisioning), then handed to the Mac —
      see [Clock and PRAM](clock-and-pram.md) and [Storage](storage.md#provisioning)
- [ ] Wi-Fi through Circle's `addon/wlan`: needs the Broadcom firmware on the card and an SSID and key in the
      preferences, which raises storing a secret in clear on a FAT card
- [ ] Trials: DHCP from the Mac, TCP/IP, AppleShare to a Netatalk server, file transfer, a period browser through a
      proxy
- [ ] slirp as an option, if isolation or NAT turns out to be wanted
- [ ] AppleTalk enabled in the default PRAM, as Infinite Mac does (`ether_helpers.h`), so a System finds the network
      with no setting — see the [Infinite Mac study](../notes/research/infinite-mac.md)

## Pitfalls

- **One MAC address**: `CNetDevice` has no promiscuous mode.
- **Pi 1, 2 and 3 put Ethernet on USB**, shared with the keyboard and mouse: sustained traffic starves input. A Pi 4
  is the target for networking; on older boards anything polling (an NTP daemon) must be spaced out and measured.
- **The Mac reads its clock before any stack exists**: the network can improve the clock for the next boot, never
  the current one.

## Development notes

- **2026-08-25** — Design chosen at planning time: shared MAC, slirp as fallback, no bridge.
- **2026-09-14** — Survey of the projects that connect classic Macs to modern networks (Netatalk, Samba `vfs_fruit`,
  TashTalk, TashRouter, MacIP, WebOne): see [AppleTalk and SMB gateways](../notes/research/appletalk-smb-gateways.md).

## References

- `BasiliskII/src/ether.cpp`, `src/slirp/`, `dummy/ether_dummy.cpp`; `SheepShaver/src/emul_op.cpp:286`
- Circle `include/circle/netdevice.h`, `addon/wlan/`
