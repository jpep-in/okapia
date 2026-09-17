# The Okapia documentation

Okapia is a classic Macintosh on bare metal: Basilisk II (68k) and SheepShaver (PowerPC) ported onto
Circle, so a Raspberry Pi boots straight into Mac OS with no Linux underneath. This tree is the single
reference for how it works, what is done, what is left, and what it cost to learn.

Every page on a subject covers both how to use it and how it is built. A fact carries its evidence — a
`file:line`, a measurement, or a source — so it can be checked instead of believed.

## Reading paths

**You want to run it.** [Getting started](getting-started.md), then [Preferences](preferences.md). The
"Using it" section of any topic page covers the rest.

**You want to contribute.** [Vision](project/vision.md) → [Architecture](project/architecture.md) →
[Decisions](project/decisions.md) → [Contributor guide](contributing/guide.md) →
[Building](contributing/build.md) → [Testing and debugging](contributing/testing-and-debugging.md), then
the topic you are touching.

**You are an AI agent.** [`AGENTS.md`](../AGENTS.md) holds the rules for every session. Read
[Architecture](project/architecture.md) once, then the topic page before changing anything in its area:
the "Pitfalls" section there is what previous sessions paid for.

## Map

### Project

| Page | What it answers |
|---|---|
| [Vision](project/vision.md) | What Okapia is for, and what it is not |
| [Decisions](project/decisions.md) | What is settled, why, and what would reopen it |
| [Architecture](project/architecture.md) | Layers, the merged image, what exists once, memory, timing, repository layout |
| [Roadmap](project/roadmap.md) | Where each topic stands, and the milestones |
| [Glossary](project/glossary.md) | The vocabulary of the Macintosh, Circle and the two emulators |

### Using Okapia

| Page | What it answers |
|---|---|
| [Getting started](getting-started.md) | Hardware, the SD card, ROMs, the first boot |
| [Preferences](preferences.md) | Every keyword of `BasiliskII_Prefs`, including those that do nothing here |

### Topics

| Page | Subject |
|---|---|
| [Startup and shutdown](topics/startup-and-shutdown.md) | Power-on, engine switch, restart, shut down |
| [Boot menu](topics/boot-menu.md) | The Okapia firmware: chooser, settings, UI toolkit, translations |
| [68k engine](topics/engine-68k.md) | Basilisk II: CPU, FPU, ROM, model ID, performance |
| [PowerPC engine](topics/engine-powerpc.md) | SheepShaver: CPU, memory layout, ROMs, integration |
| [Display](topics/display.md) | Outputs, EDID, resolutions, the compositor |
| [Sound](topics/sound.md) | Outputs, volume, the audio pump, the startup chime |
| [Input](topics/input.md) | Keyboard and mouse |
| [Storage](topics/storage.md) | Disk images, data safety, repair, CD-ROM, provisioning |
| [Shared folder](topics/shared-folder.md) | Exchanging files with the card |
| [Clock and PRAM](topics/clock-and-pram.md) | Time, and what the Macintosh remembers |
| [Network](topics/network.md) | Design, and what is left to do |

### Contributing

| Page | What it answers |
|---|---|
| [Contributor guide](contributing/guide.md) | Working rules, code conventions, git, licence |
| [Building](contributing/build.md) | Toolchain, targets, the merged image, dependencies, patches |
| [Testing and debugging](contributing/testing-and-debugging.md) | Host tests, QEMU scripts, crash testing, traces, logs |
| [Benchmarks](contributing/benchmarks.md) | Method and results |

### Notes

Dated development notes and research studies, kept for the reasoning behind a result.

| Note | Subject |
|---|---|
| [2026-09-08 — The stuttering pointer](notes/dev/2026-09-08-stuttering-pointer.md) | Four causes, found by measuring and by eye |
| [2026-09-09 — The PowerPC mouse](notes/dev/2026-09-09-powerpc-mouse.md) | The ROM's acceleration curve and where its setting lives |
| [Startup chime](notes/research/startup-chime.md) | Reading each Macintosh's chime from its own ROM |
| [Bottleneck study](notes/research/bottlenecks.md) | Where the 68k engine spends its time on a Pi 4 |
| [infinite-mac](notes/research/infinite-mac.md) | What the WebAssembly port teaches a bare-metal one |
| [System 6 in 24-bit mode](notes/research/system6-24bit.md) | A third engine configuration for System 6 |
| [68k JIT](notes/research/jit-68k.md) | Whether and how to enable a 68k→AArch64 JIT |
| [AppleTalk and SMB gateways](notes/research/appletalk-smb-gateways.md) | Existing projects bridging classic Macs to modern shares |
