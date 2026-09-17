# Sound

> The Macintosh's sound on the jack, HDMI or USB: the board-owned device, the pull from the Mac's mixer,
> volume, switching outputs, and the startup chime read from the ROM. Main files:
> `src/circle/audio_circle.cpp` (compiled once per engine), `src/circle/hal_circle.h` (the declarations)
> and `src/circle/board_sound_circle.cpp` (`BoardSoundClaim`, `BoardChime*`), `src/circle/rom_chime.{h,cpp}`,
> `src/firmware/circle/okapia_firmware.cpp` (the setting).

## Using it

- **Output**: Settings → Sound in the [boot menu](boot-menu.md), stored as `soundoutput`
  ([Preferences](../preferences.md)): `jack`, `hdmi`, `usb` (Pi 4 and 5) or `off`. The change applies at the next
  start of the Macintosh, without powering the board off.
- **Volume**: the Sound control panel's slider and mute work on every output; the volume is applied to the samples.
- **Startup chime**: played at power-on and at every restart, from the ROM of the Macintosh about to start.
  Silent when the output is `off`.
- **System 7.1**: Basilisk's sound driver is a Sound Manager 3 component; a bare System 7.1 predates it, and
  gives no sound (see Status).
- **Under QEMU** there is no sound output to hear; `discard` exercises the whole pull and throws the samples away.

## How it works

### One device, owned by the board

Circle's sound devices cannot be given back while they play. `Cancel()` only asks the DMA to stop at the end of its
buffer, and deleting the device before it has trips `CHDMISoundBaseDevice`'s destructor assertion
(`hdmisoundbasedevice.cpp:166`) or frees a PWM device its interrupt still calls into. A Circle assertion halts, and
`AudioExit()` runs inside `ExitAll()` **before** `DiskExit()`: every restart with sound on could freeze the board
with the disk image still open. And `audio_circle.cpp` is compiled once per engine, so a device per engine would be
two claims on one socket.

So `BoardSoundClaim (pWhere, nQueueMsecs)` (`board_sound_circle.cpp`) creates the device once — 44.1 kHz, 16-bit signed
stereo, a 100 ms queue — starts it, and hands the same one to every start of either Macintosh. A queue-mode device
plays silence while its queue is empty, so an engine that stops feeding it is all a close is: `AudioExit()` forgets
the pointer and deletes nothing.

**Switching output** (the boot menu changed it): the claim stops the chime, calls `Cancel()`, waits up to one second
for `IsActive()` to fall, then deletes and creates the new device. Called only between two starts, never while a
Macintosh feeds the old one. A device that will not stop is kept and the new output refused until the board
restarts; a device that will not start answers 0, for that output, without retrying.

### The pull

Basilisk's contract (`audio_sdl.cpp` is the model): raising `INTFLAG_AUDIO` makes the interpreter call
`AudioInterrupt()` (`emul_op.cpp:504`), which runs **68k code** to ask the Mac's mixer for the next block. The pull
must therefore start from the emulation side, never from a DMA interrupt.

1. `AudioPump()`, from the tick, raises the flag while the Mac has sources **and** the queue has room for a block
   (`FramesHeld() − FramesWaiting() ≥ 1024`). `GetQueueFramesAvail()` is the frames *waiting*, not the room left
   (`soundbasedevice.h:157`) — reading it as room is what once kept a first sound from ever being pulled.
2. `AudioInterrupt()` calls the mixer, byte-swaps the big-endian block (8-bit mono is doubled into both channels),
   applies the gain and writes it to Circle's queue.
3. **Grace**: the mixer reports `num_sources == 0` when the last source has *finished feeding*, not when its last
   sample was fetched, so ten more asks follow (a sixth of a second) — without them an alert loses its tail as a
   click.
4. **Stall net**: a Macintosh playing a sound waits for it to end. After 30 ticks (half a second) with no room, the
   pump asks anyway and the block is dropped, so a device that stopped draining cannot hold the Mac frozen with its
   pointer still moving. Logged once per occurrence.

`AudioReport()` prints sources, asks, answers (empty), blocks, short writes, stalls and queue fill under
`perfreport`. `SOUND_SELFTEST` (trace builds, 68k) plays one `SysBeep` at the first idle: `SysBeep` waits for its
sound, so a pull that never happens shows as a "returned" line that never comes.

### Volume

Neither the jack nor HDMI has a mixer Circle can drive (`CPWMSoundBaseDevice` and `CHDMISoundBaseDevice` offer no
controller), so volume is applied to the samples, as upstream's SDL port does (`audio_sdl.cpp:386`). Each Mac volume
word holds left in its high half and right in its low, 0 to `0x100`; the gain per channel is main × speaker (0 to
`0x10000`), 0 when either mute is set.

### Startup chime

A Macintosh's chime is in its ROM, and the chime depends on the ROM — which is the point: a modified ROM is heard as
it is. Nothing Apple ships in the repository; Okapia reads the ROM the user put on the card.

`BoardChimePlay()` runs at power-on right after the Mac RAM block is allocated, and again at every restart (68k: the
`CKernel::Run` loop; PowerPC: `ether_reset`, [in place](startup-and-shutdown.md#restart-from-mac-os)):

1. **Which ROM**: the one the startup volume's Macintosh will use — the first `disk` and its `engine` line pick
   `rom` or `romppc`. If it keeps no chime this reader knows, the other ROM of the preferences, then any file at the
   card's root sized 512 KB to 4 MB. The search is redone only when the wanted ROM changes.
2. **Read** whole into a 4 MB buffer, then searched in memory (field-by-field reads took 1.6 s under QEMU, a single
   read 0.68 s; QEMU's SD is slow, the Pi log says `found in … ms`).
3. **Find and render** (`RomChimeFind`, `RomChimeRender`, pure) to 44.1 kHz stereo, once, up to six seconds.
4. **Play** from a kernel timer that refills the queue every two ticks while USB and the boot menu come up.
   `BoardChimeFinish()` (up to four seconds) lets it end before an engine feeds the same device;
   `BoardChimeStop()` does not wait.

The three forms, tried in this order:

| Form | Machines | Where | Format |
|---|---|---|---|
| `beep` resource ID 0 | Power Macintosh 9500/9600 (Old World) | ROM resource map | "Kurt" header, DBDMA command list (`OUTPUT_MORE`…`OUTPUT_LAST`), 16-bit signed big-endian stereo, 22,050 Hz (not written in the resource; confirmed against a recording) |
| Sampled `snd ` | Quadra/Centris 610/650/800 (`F1ACAD13`) | raw data at `0xC67BE`, **outside** the resource map | format 1, `bufferCmd`, 31,178 samples 8-bit unsigned at 22,254.5454 Hz, 1.40 s |
| ASC wavetable table | Mac II, IIci (also present in the Quadra ROM, unused there) | 32-byte table at `0x7158` | synthesised: see below |

**The synthesised chime.** The 32-byte table gives volume, step delay, voice offset (steps before the next voice
enters), total steps, voice count and four 17.15 fixed-point frequencies — for `0x7158`: 30,000 steps, a voice every
300, C, F, a second C seven cents off (the beat) and the C above. The routine fills the ASC's four 512-byte
wavetables with a stairs waveform (64 bytes each of `$10 $3F $01 $30` on the original ASC), then at every step
smooths one byte with its neighbour, `t[i+1] = (t[i] + t[i+1]) / 2`, in all four tables. The decay is not an
envelope: repeated smoothing low-passes the waveform until it converges on its mean — silence.
`rom_chime.cpp` replays that at 22,257 Hz per the chip model (index `(phase >> 15) & 0x1FF`, four voices summed),
with **22.95 µs per step**, high-passes at 20 Hz as the AC-coupled output did, and normalises the peak.

**Validation**: all three renders were compared with Mactracker's recordings — same duration and partials for the
9600 (240, 286, 722 Hz) and the Quadra (G, C, G, C, E), partial-by-partial evolution for the Mac II, with the step
duration fitted by least squares — and approved by ear. See the
[startup chime study](../notes/research/startup-chime.md).

## Status

- [x] Platform layer: 44.1 kHz 16-bit stereo, pull from the 68k context, byte swap (2026-08-28)
- [x] `soundoutput` with `jack`, `hdmi`, `usb`, `off`; `nosound` still honoured (2026-09-04)
- [x] `discard` output to exercise the pull under QEMU
- [x] Heard on a Pi 4, System 7.6, jack and HDMI (2026-09-14)
- [x] Board-owned device: restarts with sound no longer freeze (2026-09-14)
- [x] Volume and mute applied to the samples (2026-09-14)
- [x] Output switched from the boot menu without powering off (2026-09-14)
- [x] Startup chime from the ROM, three forms, fallback ROM, replay at every restart (2026-09-14)
- [ ] System 7.1: no sound. Check Sound Manager 3.0 installed as an extension, as the shared folder needs FSM 1.2
- [ ] System 7.1 crash with odd sound from the jack, then a restart to the boot menu (2026-09-14): leading
      hypothesis a guest write through the unredirected ASC address
      ([68k engine](engine-68k.md#memory)), unconfirmed
- [ ] Hear the Quadra and IIci chimes on a Pi (validated on the host only)
- [ ] Automatic output from the EDID (an HDMI sink declaring audio → `hdmi`, otherwise `jack`) — a proposal; note a
      sink may play audio its EDID does not declare
- [ ] Other ROMs: LC, IIsi (the replacement routine at `0x45C0A`), Quadra AV `MARIOBOOTSOUND`, ROM-inator II
- [ ] Headphone plug detection: the Pi 4 jack has none; nothing to read
- [ ] Trials: system sounds, AIFF playback, games

## Pitfalls

- **Never claim a device the guest cannot use.** Sound reported open while QEMU models no output made the Mac play
  its alert, wait for a completion that never came, and freeze — and the hard stop cost a card. A device that will not
  start leaves the platform exactly as `src/dummy/` does (`audio_open` false), decided at init, not in a hook the 68k
  calls. An unknown `soundoutput` value is silence for the same reason.
- **Never delete a Circle sound device that may be playing**, and never claim one per engine or per start (above).
- **`GetQueueFramesAvail()` is the frames waiting**, not the room left.
- **The sample-rate lists are vectors that survive a restart**: `AudioInit()` clears them first, or the Sound
  Manager is offered the same rate once more per restart.
- **The chime must not be waited for when it is not playing**: `BoardChimeFinish()` polls a flag set only while
  frames remain, otherwise every start paid four seconds.
- **`libsound.a` is not in circle-stdlib's link line**; Okapia adds it.

## Development notes

- **2026-08-28** — Pull written. QEMU froze on the first alert with the device claimed; sound disabled under QEMU
  ("Do not promise the Mac a sound it will never hear"); `discard` later made the pull testable there.
- **2026-09-14** — First Pi 4 feedback: fine on 7.6, silent on 7.1, too loud with the slider inert, restarts
  freezing. Gain applied; device moved to the board. Output switch then took effect only after a power cycle, fixed
  by replacing the device between starts.
- **2026-09-14** — Chime: 9600 `beep` heard first. The Quadra ROM was wrongly thought to keep no recorded chime
  because its resource map has none; the history of the chime (synthesised until the Quadra, recorded after) pointed
  the right way, and a scan found the `snd ` as raw data. Mac II algorithm reconstructed and fitted on a recording.

## References

- `BasiliskII/src/audio.cpp:398`, `emul_op.cpp:504`, `SDL/audio_sdl.cpp:386`
- Circle `lib/sound/soundbasedevice.h:157`, `hdmisoundbasedevice.cpp:166`, `lib/sound/Makefile:37`
- ASC model: Snow `core/src/mac/asc.rs:158` (MIT), Mini vMac `ASCEMDEV.c:700` (GPL v2 only: read, never copied)
- [Startup chime study](../notes/research/startup-chime.md);
  [Macintosh startup — Wikipedia](https://en.wikipedia.org/wiki/Macintosh_startup)
