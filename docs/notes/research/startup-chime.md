# The startup chime, read from the ROM

> Research of 2026-09-14, implemented the same day (`src/circle/rom_chime.{h,cpp}`, `BoardChime*` in
> `src/circle/board_sound_circle.cpp`). The user-facing summary is in [Sound](../../topics/sound.md#startup-chime). **No Apple
> sound or code in the repository**: Okapia reads the chime from the ROM the user put on the card, at startup, and
> nowhere else.

## Goal

Play the chime of the Macintosh about to start, as early as possible after power-on and again at every restart, like a
real Mac. The chime depends on the ROM — that is the point, and it is what makes a modified ROM (ROM-inator II) audible
as it is.

## History first

Lesson of this research: the user had given the lead ("a frequency to play, I thought that was on older Macs"), and the
[Macintosh startup](https://en.wikipedia.org/wiki/Macintosh_startup) page confirms it. Read earlier, it would have
avoided concluding that the Quadra ROM kept no recorded chime.

| Machines | Chime | Nature | In Okapia |
|---|---|---|---|
| Macintosh (1984) | 600 Hz square beep, software on the VIA | synthesised | — |
| Macintosh II (1987) | C major chord in fourths, Mark Lentczner, through the ASC | **synthesised** | table at `0x7158`: C, F, C, C — validated |
| LC, LC II, Classic II | "programmed" F major chord in fifths | synthesised | to check (routine at `0x45C0A`) |
| Quadra 700 to 800 | C major chord, Jim Reekes, Korg Wavestation EX | **recorded** | `snd ` at `0xC67BE`: G, C, G, C, E |
| Quadra 840AV onwards | the "bong", Jim Reekes, several synthesisers | recorded | `MARIOBOOTSOUND`, not handled |
| First Power Macintosh | guitar chord, Stanley Jordan | recorded | 9600 `beep` resource |
| Power Macintosh 5200–6300 | composed on a Fairlight CMI | recorded | — |
| iMac G3 (1998) to 2016 | the 840AV bong transposed to F♯ major | recorded | out of scope (New World) |

Rule: **the chime is synthesised while it is a plain chord of pure notes, and recorded once it becomes a rich synthesiser
sound** — from the Quadras on. For an unknown ROM, look for a recorded sound first, in the resources **and** as raw data,
before looking for a routine.

Chimes of death (same page): a rising major arpeggio on the Mac II; a softer, lower version followed by three or four
notes on Quadra, Centris, Performa, LC and Classic II; recorded noises (car crash, fanfare, breaking glass) on Power
Macintosh.

## Power Macintosh 9600: the `beep` resource

Verified on a 4 MB 9600 ROM:

- Resource map walked as SheepShaver does (`rom_patches.cpp:226`): 180 resources, `beep` 0 (chime, data at `0x200010`)
  and `beep` 1 (error, `0x240010`).
- A "Kurt" header before the data, resource size at +8 (`0x32BFC` for the chime).
- Data: four big-endian longs — command list offset (`0x10`), channels (2), sample bytes (`0x32B60`), sample address
  (`0xFFE000A0`, masked by the image size: file offset `0x2000A0`).
- Then a **DBDMA** command list (16 bytes each, little-endian): six `OUTPUT_MORE` of `0x8000` bytes and an `OUTPUT_LAST`
  of `0x2B60`, contiguous, ending in `STOP`.
- Samples: 16-bit signed **big-endian** stereo, 51,928 frames (smooth read big-endian, noise little-endian).
- **Sample rate not written anywhere in the resource.** 22,050 Hz deduced (2.35 s decaying to silence), confirmed by ear
  on the Pi 4, then against Mactracker's "PCI based Power Mac Startup" recording: same 2.36 s, same main partials (240,
  286, 722 Hz).

SuperMario confirms the principle: `Resources/RomResources.r:1143`, `'rraa' (105, "TNT Boot beep")`, type `beep` ID 0,
conditional on `hasGrandCentral`.

## 68k machines

### What SuperMario has, and lacks

- `Make/VectorTable.a:251-266` places `BOOTBEEP6`, `ERRORBEEP1…4`, `DOBEEP`, `BOOTSOUND`, `MARIOBOOTSOUND` in
  `OS:Beep:BootBeep.a`, `CYCLONEBEEP` in `OS:Beep:CycloneBeep.a`.
- **`OS/Beep/` is missing** from the dump; the patchsets do not rebuild it (`patchset/Vanilla/3-brave-attempt.patch`
  adds a 6-byte `Beep.lib` placeholder so the build completes).
- What remains: the calls (`OS/StartMgr/USTStartUp.a:1024`, `BigBSR6 BootBeep6`), sound hardware init
  (`OS/IoPrimitives/SndPrimitives.a`), and the ASC register map (`patchset/Cube-E/6-source.patch:3581`: `ascVersion $800`,
  `ascMode $801` — "2 means waveform" — `ascChipControl $802`, `ascFifoControl $803`, `ascFifoInt $804`,
  `ascWaveOneShot $805`, `ascVolControl $806`, `ascClockRate $807`, plus Batman's `$F04`…`$F29`). Batman is a superset of
  the ASC (`PowerMgrPrimitives.a:3400`, `SndPrimitives.a:699`).

### The same code in the IIci and Quadra ROMs

The IIci ROM map (`MPW-3.2.3/ROM Maps/MacIIciROM.map:219-220`) puts `BOOTBEEP` and `BOOTBEEP6` at `7F,7040` and `7F,7052`.
**Bytes `0x7040`–`0x71FF` are identical** in the IIci ROM (`368CADFE`) and the Quadra ROM (`F1ACAD13`): same routine,
same tables, same address. The routine tests `ascVersion` for waveform and volume, so it runs on the original ASC and its
successors. The Quadra does **not** play it — its chime is sampled (below); the routine is inherited universal code.

### The routine, from our disassembly

Described as behaviour, no code copied. Each entry point loads a 32-byte table address then joins the body:

| Entry | Table | Role |
|---|---|---|
| `0x7040`, `0x7052` | `0x7158` | startup chime |
| `0x7058`, `0x705E`, `0x7064`, `0x706A` | `0x7178`, `0x7198`, `0x71AC`, `0x71C4` | error chimes |

Table (32 bytes, big-endian; format confirmed by Doug Brown): volume (0–1, shifted into `ascVolControl` by an amount
depending on `ascVersion`), step delay (2–5, a wait loop between steps), voice offset (6–9, steps before the next voice
starts), total steps (10–13), voice count (14–15, 1 to 4), four frequencies (16–31, one long per voice, written to the
voice's increment register).

1. Mode off, clock 0 (22,257 Hz), volume, chip control; the 32 voice register bytes (`$810`–`$82F`) cleared;
   `$830`–`$837` set to `$FE` (role unknown).
2. Wavetable mode (`ascMode = 2`).
3. **Initial waveform**: the four tables' 2,048 bytes filled in 64-byte runs of a rotating 4-byte value — a stairs wave of
   period 256 samples, two periods per 512-byte table. Original ASC (`ascVersion` 0): `$10 $3F $01 $30`; later chips
   `$40 $FF $01 $C0`, centred on `$80`.
4. **`total + 1` steps**, each: one table byte smoothed with its neighbour, `t[i+1] ← (t[i] + t[i+1]) / 2`, the index
   advancing one byte per step and wrapping at 512, written to **all four** tables; a wait of `delay + 1` VIA register
   reads; every `offset` steps the next voice gets its frequency (an arpeggio for a large offset, a chord for zero).
5. Frequencies cleared, mode off.

**Why it decays**: repeated smoothing is a low-pass filter applied to the waveform itself. The timbre softens, then the
wave converges on its mean — a constant, silence. The decay is not a volume envelope; the table flattens.

Startup table `0x7158`: volume `$0204`, delay 13, offset 300, total 30,000, 4 voices, frequencies `$18000`, `$20000`,
`$18179`, `$30000`. With 17.15 fixed point, a 256-sample waveform period and 22,257 Hz: **260.8, 347.8, 261.8, 521.6 Hz** —
C, F, a second C about 7 cents sharp (the beat), C an octave up.

### The chip model

Two local, independent emulators agree: Snow `core/src/mac/asc.rs:158` (MIT) and Mini vMac `src/ASCEMDEV.c:700` (**GPL v2
only**: read, never copied). 22,257 Hz; four 512-byte tables at `$000`–`$7FF`; voice registers at `$810 + 8 × voice`,
phase in the first four bytes, increment in the next four; per sample `phase += increment`, index `(phase >> 15) & $1FF`
(Mini vMac rounds with `+ $4000`); the four voices summed. MAME emulates the same chip and was not needed; Doug Brown used
it in 2011 for lack of another reference.

### Step duration

The routine's wait loop (read a VIA register, `dbra`) is exactly the loop calibrated by the low-memory global
`TimeViaDB`, "number of VIA accesses per millisecond". SuperMario gives Apple's fixed values: Mac II `$0310` = **784 per
ms** (`Patches/PatchIIROM.a:6351`) — 1.276 µs a turn, the VIA E clock period (783.36 kHz), consistent; Mac SE `$0105`
(`PatchSEROM.a:5036`). First estimate: 14 turns × 1.276 µs = 17.9 µs plus the step body, ~20.4 µs, a 0.61 s chime.
**Fitted on a recording: 22.95 µs per step** (17.9 µs of VIA turns and ~5 µs of body), 0.69 s, voices 6.9 ms apart. Snow
could not calibrate it: it emulates the II, IIx, IIcx and SE/30, not the IIci or Quadra.

### The Quadra: a sampled chime outside the resource map

- The Quadra ROM's resource map (120 resources) has one `snd `, ID 1, the system beep. The chime is not there — hence the
  wrong first conclusion.
- Scanning the image for smooth, tonal 8-bit data found a region from about `0xC6800` to `0xCE100`.
- Just before it, at `0xC67BE`, a complete **format 1** `snd ` laid down as data: one data format (`sampledSynth`, 5),
  `initOption $A0`, one command (`bufferCmd` with the offset flag, `$8051`), sound header at +20 (`0xC67D2`): **31,178
  samples, 22,254.5454 Hz (`$56EE8BA3`), loop `$79C8`–`$79C9`, encoding 0 (8-bit unsigned), base note 60 — 1.40 s**.
- Robust search pattern: `$0001 $0001 $0005`, a `$8051` command with offset 20, then rate `$56EE8BA3` and encoding 0. One
  hit in the Quadra ROM, none in the IIci and SE ROMs, no true hit in the 9600 ROM. How the Quadra's code finds it is still
  unknown; the pattern suffices.

## Validation against recordings

References: Mactracker's `Chimes/*.m4a`, converted locally to WAV with `afconvert`, **never copied into the repository**.
Analysis: RMS envelope per 20 ms, spectral peaks (Goertzel, 0.5–2 Hz steps), six partials tracked over 50 ms windows.

- **9600**: duration and partials identical (above).
- **Quadra** (`Macintosh_Quadra_Startup`, 1.37 s) against the extracted `snd ` (1.40 s): at 0.05 s, recording
  263.5/196.5/131/98/662.5/574.5/527/335 Hz, ROM 263.5/574/131/196.5/860.5/663/526.5/334.5 Hz; at 0.5 s, recording
  264/99.5/132/267/396.5/332.5/198.5, ROM 264/99.5/132.5/332.5/396.5/198.5. Same chord, same duration: **this is the
  chime**.
- **Mac II** (`Macintosh_II_Startup`, 0.70 s) against the synthesis of `0x7158`: recording peaks 522, 1,042, 694, 1,564,
  348 Hz; synthesis 522, 694, 1,042, 1,562, 348. The stairs waveform puts its energy on each voice's second harmonic
  (computed: 4.3× the fundamental), hence 522 = 2 × 261 and a weak 260 Hz fundamental, as recorded. Partial levels (out
  of 20, 50 ms windows):

  | Partial | Recording | Original ASC, 22.8 µs | Later chip, 22.8 µs |
  |---|---|---|---|
  | 260 Hz | 3 1 0 1 2 3 5 6 6 8 8 7 5 | 2 0 1 2 3 4 5 5 5 5 5 5 3 | 2 0 1 2 3 3 4 5 5 6 6 6 6 |
  | 522 Hz | 19 20 16 12 9 5 2 4 7 8 7 5 3 | 17 20 17 13 8 4 3 5 6 6 6 4 3 | 16 20 19 15 11 6 2 6 11 15 16 17 16 |
  | 694 Hz | 11 11 8 7 7 6 5 4 4 4 3 2 1 | 9 9 9 7 7 6 4 4 3 3 2 1 1 | 8 9 10 8 9 9 8 8 8 8 7 7 7 |
  | 1,042 Hz | 9 12 9 8 8 7 6 5 5 4 4 3 2 | 7 10 9 8 7 5 4 4 3 3 2 2 2 | 6 10 10 9 9 8 8 8 7 7 7 6 6 |

  The beats (voices 0 and 2 a hertz apart, entering 600 steps apart) fall at the same moments and the decay follows: **the
  algorithm is right, with the original ASC's waveform**; the later chips' waveform does not decay on this chime. Step
  duration by least squares on these tracks: error 2.01 at 20.4 µs, 0.53 at 22.3, 0.24 at 22.8, **0.175 at 23.0**, 0.22 at
  23.1, 0.73 at 23.3, 3.55 at 24 — a clear minimum near **22.95 µs**. The dip is narrow because the beat phase depends on
  exactly when each voice enters.
- The C renderer was compared to the recording again: error 0.181 against 0.175 for the Python prototype. Approved by ear.

## Implementation

- `RomChimeFind()` tries, in order, `beep` 0, the out-of-map sampled `snd ` (at least 11,025 samples), the ASC table;
  `RomChimeRender()` renders 44.1 kHz stereo, high-passed at 20 Hz (the AC-coupled output) and peak-normalised. Pure, tested
  in `tests/host/check_platform.cpp` on fabricated ROMs and, when present on the machine, the 9600, Quadra and IIci ROMs.
- Played from a kernel timer while USB and the boot menu come up; the engine waits for the end before using the sound
  output. At power-on and every restart (68k: `CKernel::Run`'s loop; PowerPC: `ether_reset`, restart in place).
- ROM chosen: the one of the Mac about to start (first `disk`, its `engine` line). Without a usable chime: the other ROM
  of the preferences, then any 512 KB–4 MB file at the root.
- The ROM is read whole (4 MB) then searched in memory: field-by-field reads took 1.6 s under QEMU, one read 0.68 s. Under
  QEMU with the Quadra ROM: found and rendered in 173 ms. On the Pi, the log says `found in … ms` and
  `playing … ms after the board started`.
- On the way: `Blink (2)` in the board's constructor cost 1.4 blocking seconds at every power-on (`actled.cpp:95`), removed.

## Open questions

1. `TimeViaDB` of the IIci: the fit holds for the Mac II recording; the IIci runs the same routine on the same chip, its
   step may differ slightly.
2. Mixing: Snow sums then divides by 4; check saturation and `ascVolControl`'s effect (no effect on shape, the render is
   normalised).
3. Registers `$830`–`$837` set to `$FE`: role.
4. The later chips' waveform does not decay on this chime: which machine really plays it, with which sample scale?
5. Other ROMs — SE/30, IIsi, IIfx, LC: the IIsi and LC maps (`MacIIsiROM.map:231-232,2097`, `MacLCROM.map:231-232,2098`)
   keep `ORIGBOOTBEEP` and `ORIGBOOTBEEP6` at `7F,7040`/`7F,7052` but put the `BOOTBEEP6` in service at `7F,45C0A` — the
   address the ROM-inator tutorial modifies. Compare with `0x7040`.
6. `MARIOBOOTSOUND` (`vDataTable`): sampled chime of the "Mario" machines (Quadra AV, Cyclone) — format to find if such a
   ROM turns up.
7. How the Quadra's code locates its `snd `.
8. Chimes of death: tables `0x7178`–`0x71C4` and the "Macintosh II Death" recording would validate the same way.

## ROM-inator II and other modified ROMs

- **Big Mess o' Wires, ROM-inator II**: a downloadable base ROM (512 KB, derived from the IIsi ROM, so Apple code, never in
  the repository); published source for the ROM disk driver (`romdrv1.2.sit`) and the FC8 tool; **no source for the ROM
  image or the chime**. Its chime is a synthesised "major 9th arpeggio". A research summary attributes to BMOW the phrase
  "the chimes are synthesised with the data tables at `0x7158`" for the IIsi — the same address; not re-read, check before
  relying on it.
- If the ROM-inator changed the table (frequencies, offset), the synthesiser would play its arpeggio knowing nothing of it.
  Basilisk should accept that ROM (32-bit clean like the IIci): untested.
- **Doug Brown**: the 32-byte table format; a IIci patch replacing the chime with a sampled sound (8-bit unsigned,
  22,254 Hz, the ASC FIFO), source published; notes on FIFO status bits differing between IIci and LC III.

## Sources

- [Macintosh startup — Wikipedia](https://en.wikipedia.org/wiki/Macintosh_startup)
- SuperMario, `reference/supermario/` (read only, no line reused)
- Mactracker recordings (installed application, `Chimes/*.m4a`), analysed locally, not redistributed
- Snow `reference/snow/core/src/mac/asc.rs`, Mini vMac `reference/minivmac/src/ASCEMDEV.c`
- [Introducing the Mac ROM-inator II](https://www.bigmessowires.com/2016/05/23/introducing-the-mac-rom-inator-ii/),
  [Mac ROM-inator II Programming](https://www.bigmessowires.com/mac-rom-inator-ii-programming/),
  [ROM hacking tutorial with ROM-inator II](https://www.bigmessowires.com/2016/06/10/rom-hacking-tutorial-with-rom-inator-ii/)
  — Big Mess o' Wires
- [Mac IIci custom startup sound ROM hack](https://www.downtowndougbrown.com/2011/08/mac-iici-custom-startup-sound-rom-hack/),
  [part II](https://www.downtowndougbrown.com/2011/08/mac-iici-custom-startup-chime-part-ii/) — Downtown Doug Brown;
  [mac-rom-simm-programmer](https://github.com/dougg3/mac-rom-simm-programmer)

## Journal

- **2026-09-14** — `beep` found in the 9600 ROM, DBDMA format decoded, played and heard on the Pi 4. Fallback chime, replay
  at restart, whole-ROM read, `Blink (2)` removed.
- **2026-09-14** — SuperMario: `OS/Beep/` missing. IIci and Quadra ROMs: identical routine and tables. Routine
  disassembled; pitches computed; ASC model confirmed by Snow and Mini vMac.
- **2026-09-14** — Step duration estimated from the Mac II's `TimeViaDB`; four prototype WAVs sent for listening.
- **2026-09-14** — Compared with Mactracker. 9600: 22,050 Hz confirmed. Quadra: the chime is a sampled `snd ` outside the
  resource map — the "no recorded chime" conclusion was wrong. Mac II: algorithm validated partial by partial, step fitted
  to 22.95 µs. Approved by ear; the Wikipedia history added, which gave the synthesised/recorded split from the start.
- **2026-09-14** — Implemented in `rom_chime.cpp`; C synthesiser compared to the recording again; host tests on three
  fabricated and three local ROMs.
