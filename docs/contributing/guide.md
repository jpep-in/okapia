# Contributor guide

> How work is done on Okapia: the rules, the code conventions, git, licensing, and how this documentation is kept.
> Written for people and AI agents alike; agents also read [`AGENTS.md`](../../AGENTS.md).

## Rules of work

1. **Data safety outranks everything else.** When in doubt, lose speed, never data. Every path that writes guest data
   must survive an abrupt stop at any instruction, and proves it with `run-test.sh` before it lands — see
   [Storage](../topics/storage.md).
2. **Write as little code as possible.** Look for the code upstream before writing any: Basilisk II, SheepShaver and
   Circle already solve most problems, and the adapted Unix files compile nearly as they are.
3. **Stay close to upstream.** Never edit `external/` or `reference/`. Upstream changes are minimal, documented
   patches in `patches/<submodule>/`, offered upstream when of general interest ([Build](build.md#patches)). Prefer
   the path upstream actually walks: an option no upstream platform enables is untested.
4. **Hook with the linker before patching**: `--wrap` on an engine's partial link reaches a function across a
   translation unit without touching `external/`.
5. **No silent stubs.** A stub logs its call and fails cleanly if it matters.
6. **Fix before optimising; measure before optimising** — but don't write obvious waste while waiting.
7. **A fixed reproducible bug comes with a test**, on the host where possible.
8. **QEMU keeps working** now that hardware is here; every model-specific optimisation has a generic fallback.
9. **Validate on hardware.** QEMU accepts video modes a Pi refuses and has no sound, RTC or EDID.
10. **Document assumptions** about caches, the MMU and DMA where the code relies on them.
11. **No Apple ROM, System or disk image in the repository**, ever. `roms/` is ignored.
12. **References are study material, not truth.** M5Tab-Macintosh declares no licence: read it, never copy it, and
    compare with upstream macemu. Mini vMac is GPL v2 only: read, never copy into this GPLv3 project.
13. **Document before analysing.** For a question about the Macintosh itself — a chip, a ROM, a period behaviour —
    read its history and the public documentation first (Wikipedia, period technical notes, the authors of similar
    projects), then the code. And take the hints of the people who used these machines seriously: they are often the
    shortest path. The startup chime study lost hours concluding the Quadra ROM had no recorded chime, when its
    history said the Quadras were the first to record it.
14. **Never cut a feature out to guard against a failure nobody has seen.** Log the pattern instead.

## Resource budget

The target is Quadra-class on a small board, not a workstation.

- **No allocation** (`new`, `malloc`, a growing container) in the emulation loop, the video compositor, the audio path
  or the frame path. Allocate at startup.
- The Mac RAM block is allocated **first**, before drivers: Circle's heap serves large blocks linearly
  ([Architecture](../project/architecture.md#memory)).
- No virtuals, no `std::function`, no exceptions in hot paths. Precomputed tables over per-pixel work
  (`ExpandMap[256]` is the model); process video by row or tile.
- Everything outside the Mac RAM block must fit a 1 GB board. Kernel under 4 MB.
- **Before adding any registration, claim or `push_back` to a start path, ask what the fifth restart does to it**, and
  whether the other engine can see it — see [Architecture](../project/architecture.md#what-exists-once-and-what-exists-twice).

## Code conventions

- **English everywhere**: code, comments, log messages, documentation, commit messages.
- **Follow each layer's convention**: `CClassName`/`m_member`/`pName` in Circle-side code, `snake_case` in Basilisk and
  SheepShaver code. Don't unify.
- **Adapted upstream files carry their origin and the list of modifications in their header.** New files carry the
  GPL-3.0-or-later SPDX line and "Okapia contributors".
- **Comments say why**, with the measurement or the upstream `file:line` that settled it; they do not restate the code.
- **Say what was actually obtained**: a mode, a device, a clock source — log what the firmware or the driver returned,
  never what was asked for.
- **Serial first**: nothing that can fail belongs in a member constructor, which runs before the log exists.
- **Generated, never typed twice**: key tables, MacRoman, interface strings, fonts, icons, opcode tables.
- **`config.h` declares, it never includes.**
- Every Makefile rule compiles with `-MMD -MP` ([Build](build.md)).

## Git

- **One commit per coherent change**, in the style `area: what it does, in a sentence` (`input: hand the events to
  adb.cpp from the core that reads them`). The body says why, and what was measured.
- Commit or push **only when asked** (applies to agents).
- Submodules pinned to a tag or SHA, never `master`/`main`/`latest`; moving a pin is its own commit
  ([Build](build.md#dependencies)).

## Licence

- Circle is GPL-3.0; Basilisk II and SheepShaver are GPL v2 **or later**; libhfs is GPL v2 or later. The combined work
  is distributed under **GPLv3** (`LICENSE`).
- **Apple ROMs and Systems** were never released and remain Apple's copyright; their status outside ownership of the
  original hardware is uncertain and depends on jurisdiction. Some neighbouring projects ship ROMs in their
  repository; **Okapia does not**: nothing is committed, nothing is rehosted, the user supplies their ROM, and a
  missing ROM gets an explicit message. The same holds for anything read from a ROM at run time, such as the
  startup chime.
- Reference projects' licences decide what may be reused: BlueSCSI (GPLv3) and Snow (MIT) code may be; Infinite
  Mac's site (Apache-2.0) gives ideas, and its macemu fork (GPL, like ours) may be reused, rewritten to our
  conventions; M5Tab (no licence) and Mini vMac (GPL v2 only) are read only.

## Documentation

This documentation is the project's single source of truth. Keep it true in the same change as the code.

- **One subject, one page.** A topic page covers both using a feature and how it works. Cross-cutting exceptions:
  [Getting started](../getting-started.md) and [Preferences](../preferences.md).
- **Topic page template**: summary with main files · *Using it* · *How it works* (the present state, with `file:line`
  references) · *Status* (`- [x] done (date)`, `- [ ] to do`) · *Pitfalls* (verified facts that cost time, with their
  evidence) · *Development notes* (dated history: trials, measurements, reversals) · *References*.
- **Checkboxes live only in topic pages** (and the contributing pages for their own work); the
  [roadmap](../project/roadmap.md) summarises them without copying.
- **Every claim keeps its reference or its measurement.** A fact that cannot be verified from a source or a
  measurement is not written down from memory.
- **Longer investigations** go to `docs/notes/dev/` (dated) or `docs/notes/research/`, and the topic page links to them.
- **When a decision changes**, update [Decisions](../project/decisions.md) with what reopened it.
- Links are relative and must resolve, anchors included.

## Development notes

- **2026-08-25** — Rules of work written with the first plan.
- **2026-09-15** — Documentation reorganised into this site, in English; rule 13 added after the chime study.
