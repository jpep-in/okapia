# AppleTalk, LocalTalk, AFP, SMB and MacIP gateways

> Survey of 2026-09-14: existing projects that connect classic Macintosh to a modern network. None of them runs on
> Okapia; they are what an Okapia Mac on the LAN could talk to, and prior art for any gateway work. See
> [Network](../../topics/network.md).

Some components (`tashtalkd`, `vfs_fruit`, TimeLord) are parts of larger projects and have no repository of their own.

| Project | Role | What it gives | Source |
|---|---|---|---|
| **MacIPRpi** | Preconfigured Raspberry Pi distribution for old Macs | Turns a Pi into a gateway and server: MacIP, AFP/AppleShare, Samba, WebOne, WRP, printing, time server, FTP | [macip.net](https://www.macip.net/); MacIP part: [jasonking3/macipgw](https://github.com/jasonking3/macipgw) |
| **macipgw** | MacIP gateway | Wraps IP in AppleTalk/DDP so a Mac using MacIP reaches modern TCP/IP; active development merged into Netatalk 4 | [jasonking3/macipgw](https://github.com/jasonking3/macipgw), [Netatalk/netatalk](https://github.com/Netatalk/netatalk) |
| **Netatalk 4** | Modern AFP/AppleShare server and AppleTalk stack | Linux or a Pi appears as an AppleShare server; AFP over TCP/IP and over AppleTalk/DDP for very old machines; AppleTalk tools, print server, TimeLord, MacIP | [Netatalk/netatalk](https://github.com/Netatalk/netatalk) |
| **Netatalk Client** | Modern AFP client | Linux mounts an AFP server's volumes through FUSE — a building block to re-share AFP over SMB | [Netatalk/netatalk-client](https://github.com/Netatalk/netatalk-client) |
| **Samba** | SMB server and client | Modern Macs, Windows and NAS reach files the gateway exposes, or remote SMB shares | [samba-team/samba](https://github.com/samba-team/samba) (GitHub mirror) |
| **Samba `vfs_fruit`** | Samba module for Macintosh compatibility | Resource forks, FinderInfo, AppleDouble, metadata and locking compatible with Netatalk/macOS — keeps Classic files intact | [source3/modules/vfs_fruit.c](https://github.com/samba-team/samba/blob/master/source3/modules/vfs_fruit.c) |
| **TashTalk** | LocalTalk hardware interface | A modern machine, typically a Pi, joins a physical LocalTalk network; a microcontroller handles the protocol's real-time constraints | [lampmerchant/tashtalk](https://github.com/lampmerchant/tashtalk) |
| **tashtalkd** | TashTalk's software bridge | LocalTalk frames ↔ LToUDP, extending physical LocalTalk over IP | in `lampmerchant/tashtalk`, `tashtalkd/` |
| **TashRouter** | AppleTalk router | Routes AppleTalk between TashTalk/LocalTalk, LToUDP, EtherTalk — e.g. a LocalTalk Mac Plus talking to a Netatalk server on Ethernet | [lampmerchant/tashrouter](https://github.com/lampmerchant/tashrouter) |
| **TailTalk** | Modern AppleTalk stack in Rust | User-space AppleTalk with TashTalk USB support and service examples; a base for new gateways | [FeralFirmware/TailTalk](https://github.com/FeralFirmware/TailTalk) |
| **WebOne** | HTTP/HTTPS proxy for old browsers | HTTP/HTTPS conversion, TLS, encodings and some content for period browsers | [atauenis/webone](https://github.com/atauenis/webone) |
| **WRP** | Web rendering proxy | Loads pages in a modern browser and returns them as a clickable image or simplified HTML | [tenox7/wrp](https://github.com/tenox7/wrp) |
| **TimeLord** | AppleTalk time server | Old Macs set their clock over AppleTalk; now part of Netatalk | in [Netatalk/netatalk](https://github.com/Netatalk/netatalk) |

## A possible architecture

```text
                MODERN NETWORK
            macOS / Windows / NAS
                     │
                    SMB
                     │
             Samba + vfs_fruit
                     │
            ┌────────┴────────┐
            │  Linux gateway  │
            │ / Raspberry Pi  │
      Netatalk 4       Netatalk Client
            │                 │
          AFP             AFP client
            │                 │
      AppleTalk / DDP / EtherTalk
                     │
                TashRouter
                     │
          TashTalk / tashtalkd
                     │
                 LocalTalk
                     │
            Mac Plus / SE/30
```

- **TashTalk**: physical LocalTalk. **tashtalkd**: LocalTalk ↔ LToUDP. **TashRouter**: AppleTalk routing.
- **Netatalk**: AFP/AppleShare server and AppleTalk services. **Netatalk Client**: AFP client from Linux.
- **Samba** with **vfs_fruit**: modern SMB access preserving Macintosh file specifics.
- **macipgw**: TCP/IP from an old Mac via MacIP. **WebOne / WRP**: the modern web in an old browser.
- **TimeLord**: clock over AppleTalk. **TailTalk**: a modern base for new AppleTalk software.

## A transparent AFP ↔ SMB gateway

Existing pieces cover most of it — SMB ↔ Samba ↔ Linux file system ↔ Netatalk ↔ AFP/AppleTalk one way, and an old Mac's
AFP server ↔ Netatalk Client ↔ FUSE ↔ Samba ↔ SMB the other. What is missing for "each machine appears individually on the
other side" is a layer of **discovery, dynamic share creation, renaming and re-announcement** between AppleTalk/AFP and
SMB.
