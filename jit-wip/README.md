# jit-wip — handoff for the 68k JIT work (2026-11-13 / 14)

Read this folder first if you are picking the work up (another person or another LLM). It is written to be read cold, in this
order, about 25 minutes:

| File | What it holds |
|---|---|
| **this README** | the state in ten lines, the rules of the house you must follow, what to read next |
| [01-state.md](01-state.md) | what was built, where it is in the tree, what works and what does not |
| [02-results.md](02-results.md) | every measurement: Speedometer on the Pi 4, QEMU boots, the core's counters |
| [03-findings.md](03-findings.md) | the causes found (with `file:line`), the bugs fixed, and the **statements I made and later withdrew** |
| [04-howto.md](04-howto.md) | exact commands to build, test, deploy to the card, capture the serial port; the traps |
| [05-next.md](05-next.md) | open questions, ranked routes, the cheapest experiments first |
| [tools/](tools/) | the scripts used (QEMU capture with preferences, compile probe, counter parser, serial capture) |
| [artifacts/](artifacts/) | extracts of the measurements (no raw logs: they hold the user's Wi-Fi name) |

## The state in ten lines

1. **Goal**: make Okapia's 68k engine (Basilisk II, `uae_cpu_2021`, interpreted, about 5× a Quadra 650 on a Pi 4) much
   faster with a JIT. The user finds an extreme speed-up of the 68k fun; the PowerPC engine does not matter for now.
2. **Done**: the 68k core of `rcarmo/macemu-jit` (`uae_cpu_2026`, its AArch64 JIT) is integrated behind `OKAPIA_JIT=1`,
   boots System 7.1 (QEMU) and 7.6.1 (Pi 4), shuts down cleanly, keeps the durability verdict of the interpreter.
3. **Result**: **at best parity with the interpreter** on the Pi 4 (Speedometer CPU 6.42 against about 6.8; Graf 3.08
   against 3.18), and K'Whet ten times slower (2.5 against 25) whatever is switched. No gain.
4. **Two real defects of the fork were found and fixed** (`patches/macemu-jit/0003`: RAM blocks never promoted to native
   code, CPU 0.68 → 6.40; `0004`: a guest cache flush ignored its range, correct but no measurable effect).
5. **The remaining cost is structural**: blocks of 3.5 instructions, about twenty instructions and several memory
   accesses at every block boundary, the guest registers reloaded from memory around each block, and constant-jump
   following disabled by the fork on AArch64.
6. **Emu68 was read** (`docs/notes/research/emu68.md`): its speed comes from the opposite choices (registers pinned in
   AArch64 registers for good, 256-instruction traces along predicted branches, no per-block bookkeeping). **Route G**
   (copy that design, not its code) is the proposed next step; its first two experiments are cheap.
7. **The kernel reservation was raised from 4 MB to 16 MB** (decision of the user, in `decisions.md`).
8. **Nothing is committed.** The working tree mixes this work with older uncommitted work (network, Wi-Fi, firmware);
   see [01-state.md](01-state.md) for which files are which.
9. **The JIT is off by default and experimental**; the interpreter remains the engine. The SD card of the user may still
   boot the JIT kernel: see [04-howto.md](04-howto.md#the-card).
10. **Backups of the user's Mac volume are in a temporary folder** (path in 04-howto): move them before anything cleans
    `/private/tmp`.

## Rules of the house (from `AGENTS.md` and the user; they cost time when ignored)

- **English in the repository** (code, comments, logs, docs, commits). The user talks and reads **French** in the
  conversation; plans and answers to them are in French.
- **Never modify `external/` or `reference/`**: upstream changes go to `patches/<submodule>/`, applied by
  `scripts/apply-patches.sh`. `reference/` is read-only study material; ask before cloning something new.
- **Commit or push only when asked.** One commit per coherent change, `area: what it does`.
- **No ROM, no Apple system, no disk image in the repository.** Never run two emulators on the same card. End real
  sessions with Special → Shut Down; **data safety outranks speed** (a stale translated block is a stale disk write).
- **Judge a boot by the screen** (`scripts/screenshot.sh`), never by opcode rates. QEMU is a functional bench, not a
  predictor of speed. Report only what was measured; say what was read and not run.
- The user dislikes preliminary measurement steps when building the thing costs the same, and dislikes inherited
  constraints that have no stated cost (see `decisions.md`, kernel reservation). They want results, honestly stated:
  most of this session's value came from checking a claim in the code and retracting it when it was wrong.
- Preferences quirk: Basilisk's prefs file takes the **first** line of a key; there are no trailing comments.

## Where the rest of the story lives in the repo

- `docs/notes/research/jit-68k.md` — the study, updated as the work went (long; the sections "Survey", "macemu-jit since
  August", "Route A sized on Circle", "First boot", "The Pi 4 run" and its "Result" sub-sections are the log).
- `docs/notes/research/emu68.md` — the Emu68 reading and route G.
- `docs/notes/research/pom68k.md` — an earlier study (a hosted low-level emulator; not an engine for us).
- `docs/project/decisions.md` — "No JIT by default; an experimental one behind `OKAPIA_JIT=1`" and the 16 MB reservation.
