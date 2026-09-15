# Glossary

| Term | Meaning |
|---|---|
| **ADB** | Apple Desktop Bus, the Macintosh's keyboard and mouse bus. Okapia presents USB devices to the Mac as ADB devices (`adb.cpp`). |
| **ASC** | Apple Sound Chip, the sound hardware of the Macintosh II family: a FIFO mode for samples and a four-voice wavetable mode. Batman (version `$B0`) and other successors keep its register map. |
| **Basilisk II** | The 68k Macintosh emulator (kanjitalk755 fork of macemu). It replaces the Toolbox's hardware access with patches rather than emulating the chips. |
| **Board** | `COkapiaBoard` in `hal_circle.cpp`: the Raspberry Pi brought up once — serial, log, interrupts, timer, USB, card — and the hardware resources claimed once for its life. |
| **Boot menu** | The Okapia firmware's window before the Mac starts: system chooser, settings, information, shut down. |
| **`bootmenu`** | Preference that opens the boot menu at every start without holding Option. |
| **Circle** | The C++ bare-metal environment for Raspberry Pi that Okapia runs on. |
| **circle-stdlib** | newlib + libstdc++ built for Circle; carries Circle as a submodule. |
| **Compositor** | `compositor_circle.cpp`: converts the Mac's frame buffer to the output, scales by an integer, centres, and redraws only changed tiles. |
| **DBDMA** | Descriptor-Based DMA, the Power Macintosh's DMA command lists; the 9500-family boot chime is stored with one. |
| **Device memory** | Pages Circle maps with `ATTRINDX_DEVICE` (the GPU frame buffer among them): every access must be naturally aligned, so vectorised copies fault. |
| **Dirty volume** | An HFS volume whose MDB `drAtrb` bit 8 is clear — "in use" — as any interrupted session leaves it. Mac OS refuses to boot from it. |
| **`DIRECT_ADDRESSING`** | Basilisk's memory model: host address = Mac address + `MEMBaseDiff`. |
| **`drAtrb`, `drLsMod`** | MDB fields: volume attributes (bit 8 = unmounted cleanly, image offset 1034) and last modification date. |
| **EmulOp** | An illegal 68k opcode Basilisk uses to call into the emulator from patched ROM code (`M68K_EMUL_OP_*`). |
| **Engine** | One of the two emulators in the image: 68k (Basilisk II) or PowerPC (SheepShaver). |
| **ExtFS** | Basilisk's external file system: a host directory shown as a Mac volume — the shared folder. |
| **Firmware** | Okapia's own boot-time program (`src/firmware/`), not the Raspberry Pi firmware (`start4.elf`). The context says which. |
| **FSM** | File System Manager 1.2, required by ExtFS; built into Mac OS 7.6, an extension for earlier Systems. |
| **Gestalt** | The Mac OS call that reports machine and software capabilities. |
| **HFS** | Hierarchical File System, the classic Mac volume format. Read and repaired on the card by libhfs. |
| **`kpx_cpu`** | SheepShaver's PowerPC CPU core, used interpreted, with a decode cache. |
| **libhfs** | Robert Leslie's HFS library (hfsutils), GPLv2+, used for the volume inventory, System version and repair. |
| **MDB** | Master Directory Block, the HFS volume header. |
| **`modelid`** | Gestalt model ID minus 6, patched into the ROM: `5` = Mac IIci, `14` = Quadra 900. |
| **Nanokernel** | The PowerPC ROM's low-level kernel, which also hosts its 68k emulator. |
| **New World / Old World** | PowerPC ROM generations: a `<CHRP-BOOT>` "Mac OS ROM" file (8.1+) versus a 4 MB ROM image (7.5.2+). |
| **PRAM / XPRAM** | Parameter RAM: startup disk, sound level, mouse tracking, screen depth. Kept per engine on the card. |
| **`perfreport`** | Preference printing engine, video, input and clock counters every five seconds. |
| **QEMU `raspi3b`** | The emulated Raspberry Pi 3 used for development. |
| **Seam** | A periodic call from inside an engine's interpreter loop, the only place blocking work may run: `cpu_do_check_ticks` (68k), `powerpc_check_ticks` (PowerPC). |
| **SheepShaver** | The PowerPC Macintosh emulator (same macemu fork). It runs the real PowerMac ROM. |
| **Shadow buffer** | The compositor's copy of the last frame drawn, compared tile by tile. |
| **Specimen** | The firmware screen showing every widget in every state; rendered identically under QEMU and on the host. |
| **Tick** | The Mac's 60 Hz interrupt (16,625 µs), driving `Ticks` and VBL tasks. |
| **Universal System** | Mac OS 7.5.2 to 8.1, whose System file carries both 68k and PowerPC code; the `engine` preference records which emulator starts it. |
| **VBL** | Vertical blanking interrupt; on the Mac, the 60 Hz tick. |
| **Wrap** | `--wrap=<symbol>` at link time, used to hook upstream functions for tracing without patching `external/`. |
