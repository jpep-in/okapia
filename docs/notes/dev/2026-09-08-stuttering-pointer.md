# The stuttering pointer — investigation and fixes

> Session of 8 September 2026. Starting point: under Mac OS the pointer caught, while in the boot menu it was perfectly
> smooth. Everything below was measured under QEMU `raspi3b` with a `cocoa` window on System 7.1
> (`qemu/sd-contents/boot71.img`), unless stated. Topic pages: [Display](../../topics/display.md),
> [Input](../../topics/input.md), [Architecture](../../project/architecture.md).

## What was wrong

Four independent defects, found in this order — which is not their order of importance.

### 1. The compositor lost sight of the pointer (the main one)

`CompositorRun()` ended with `memcpy(Watched, Moving)`. `Moving` holds only the tiles dirty **in the current frame**, so a
single still frame erased the compositor's memory. At low speed the pointer advances one pixel every two or three frames:
between its own steps the watched set emptied, and the next step was no longer in the cheap set — it waited its turn in
the eighths rotation. **Up to 133 ms, at random.**

Felt as "the pointer catches for an instant then goes on, especially slowly". **Invisible in the numbers**, because those
are frames where the compositor correctly concluded nothing had changed.

Fix: a 12-frame countdown per tile (`WatchFor[16][16]`, 256 bytes and one pass of decrements) instead of one frame of
memory.

### 2. The VBL beat 20/10/20

`PeriodicHandler()` emitted Mac ticks from an accumulator fed at `HZ`, which is **100** in Circle (`timer.h:30`). A Mac
tick is 16,625 µs, which does not divide 10,000: intervals came out 20, 10, 20, 20, 10 ms. The average is exact; **one
frame in three is half as long as its neighbours**, and the Macintosh redraws its pointer on that rhythm. Measured, and
invariant over sixteen five-second windows, at rest and sweeping fast:

```
before: min 8830 us, max 21208 us,  8:3 9:54 10:43 11:2 │ 18:4 19:105 20:83 21:10
        103 short intervals : 202 long, in every window
after:  min 15454 us, max 17816 us,           15:11 16:279 17:14
```

Fix: `CUserTimer` (`usertimer.h`) programs the system timer comparator with a delay in microseconds, re-armed from its own
handler on an absolute deadline. The periodic handler stays as the fallback, chosen at compile time by `RASPPI <= 4` —
beyond is the Pi 5, of which nothing is assumed. The kernel logs which is in force.

### 3. A late tick was paid back

Circle re-arms its own comparator by one period whatever the latency (`timer.cpp:577`), so a late interrupt calls the
handler again at once; our `while` added its own catch-up on top. Measured once: **sixteen ticks 0 ms apart followed by a
140 ms hole.**

Fix: at most one tick per visit, and resynchronise when behind — upstream's rule (`main_unix.cpp:1348` does exactly this
in its 60 Hz thread).

### 4. The drain was counted in opcodes, not time

`cpu_do_check_ticks()` fires when `emulated_ticks` overflows, every 65,536 opcodes. That is a rate **only if the engine's
speed is fixed**, and it is not: measured **209 to 1,076 visits** per five-second window, a drain every 5 to 24 ms. The
file's comment claimed four milliseconds.

Consequence: with the mouse reporting at ~100 Hz, several reports merged into one ADB packet. Amplitudes up to **275
counts in one packet** where a 200 cpi ADB mouse never sent more than a dozen — the Mac's acceleration curve extrapolated
an impossible speed and the pointer jumped.

Fix: the opcode quantum is measured against the clock every 64 visits and corrected to hold 1 kHz whatever the
interpretation speed. infinite-mac does the same for the same reason — the other port with no thread to spare
(`main_unix.cpp:342`, "Recalibrate 1000 Hz quantum every 10 ticks"). Measured after: **4,997 drains in 5 s, 999.4/s**,
quantum settling near 2,800.

**Why 1 kHz and not 100 Hz**: the drain must be clearly faster than the fastest input source or events merge. Over four
windows with movement, three give one packet per USB report (228→228, 530→530, 179→179); the fourth, the only one where
the drain had fallen to 668/s, gives 390→374. At 100 Hz merging would be the rule. Maximum rate to the Mac: **106
packets/s**, a real ADB's rate.

## Built, then removed

An **absolute** mouse for the 68k engine, like every other port (`video_macosx.mm:464`, `video_sdl.cpp:723`,
`input_js.cpp:13` all call `ADBSetRelMouseMode(false)`), with our own acceleration, a continuous ramp, a `mousespeed`
coefficient and a reading of the Mouse control panel.

**Removed by decision, and rightly.** Once the drain was fixed, ADB packets were back to the size of one mouse report, the
Macintosh's curve handled them as designed, and the Mouse control panel worked natively — without our approximation of
its curve. Relative mode is both simpler and more faithful. What was lost, and was real: the pointer no longer catches up
with the host's position (Basilisk's behaviour on macOS), and the boot menu and the Mac keep two separate pointers.

## Ablation: what really mattered

Asked at the end of the session, rightly. Five kernels built, fixes removed one by one, the tree restored and verified to
rebuild **bit-identical**.

| Kernel | Compositor | VBL | Drain | Mouse |
|---|---|---|---|---|
| K0 | one-frame memory | 10 ms grid | 65,536 opcodes | the Mac's |
| K1 | **12 frames** | 10 ms grid | 65,536 opcodes | the Mac's |
| K1a | 12 frames | **µs deadline** | 65,536 opcodes | the Mac's |
| K1b | 12 frames | µs deadline | **1 kHz** | the Mac's |
| K2 | 12 frames | µs deadline | 1 kHz | **absolute, our curve** |

**Verdict in use: K1 alone fixes 80% of the problem.** The compositor was the real culprit; the VBL beat and the drain
rate are real but secondary defects. K2 was discarded — it changed behaviour and brought nothing. Kept: **K1b**.

For next time: the costliest defect in use was the one that appeared in **no measurement**, and the other three were
found by measuring. Both approaches were needed; neither was enough.

## Established facts, not to search again

- **`HZ` is 100 in Circle** (`timer.h:30`), and no project setting changes it. Any rate derived from an accumulator on that
  grid beats 20/10/20.
- **`CrsrThresh` (`0x8EC`) does not move under System 7.1**: over eight changes of the slider it stays at 6, its startup
  value (`StartInit.a:2944`).
- **The Mouse control panel writes `SPVolCtl` (`0x208`), bits 5:3.** Swept both ways, 6 5 4 3 2 1 0 then 0 1 2 3 4 5 6:
  seven positions, **tablet = 0, "Slow" = 1, "Fast" = 6**, 7 never reached. PRAM (`XPRAM[0x20]` in Basilisk, classic PRAM
  starting at `XPRAM[0x10]`, `main.cpp:115`) stays at zero: the cdev does not write it.
- **The mouse negotiates extended ADB** (`Listen reg3` on device 3, handler ID 4), so the delta field is 10 bits, not 7.
  The largest delta ever measured is 376: **truncation never bit.**
- **`CursorDeviceDispatch`: selector 0 (`Move`) accelerates, selector 1 (`MoveTo`) does not** (`CrsrDev.a:137`).
  SheepShaver calls `MoveTo` (`adb.cpp:405`), so **the Mouse control panel is inert under SheepShaver by construction**,
  predating this session.
- **The PowerPC seam ran at ~135 Hz** with `PPC_CHECK_TICKS=50000`, and a cumulative average of 87 Hz shows it slower
  elsewhere in the session: the same defect as the 68k's 65,536 opcodes.
- **A slow pointer moves in steps of `nScale` screen pixels.** That is magnification, not a defect; only a guest mode equal
  to the output removes it.

## Left open then

- **The PowerPC seam** (making `PPC_CHECK_TICKS` a variable in `patches/macemu/0004`). *Done the next day — see the
  [PowerPC mouse note](2026-09-09-powerpc-mouse.md).*
- **The control panel under SheepShaver.** *Done the next day.*
- **Measure everything again on the Pi 4.** The 20/10/20 beat is arithmetic and holds as is; the rest does not
  extrapolate. `CUserTimer` had never run on hardware. *Since: it runs on the Pi 4.*

## Where the fixes landed

The session's code was committed with other work in progress; the commit messages were completed afterwards.

| Fix | Commit |
|---|---|
| Compositor, tile countdown | `31d44f2` *compositor: never draw the screen from a partial scan* |
| Drain rate, recalibrated quantum | `ee9b0ef` *input: hand the events to adb.cpp from the core that reads them* |
| VBL rate, `CUserTimer`, no pay-back | `3dff321` *firmware: the engine seam the previous commits left behind* |
| `patches-check` guard | `768eea4` *fpu: give the 68881 a register…, and fix two blitters* |

| File | Change |
|---|---|
| `src/circle/compositor_circle.{cpp,h}` | `WatchFor[16][16]`, 12-frame countdown |
| `src/circle/tick_circle.cpp` | `CUserTimer` with fallback, no pay-back, `VBL spacing` histogram |
| `src/circle/cpu_ticks_circle.cpp` | quantum recalibrated to 1 kHz, opcode accounting |
| `src/circle/input_circle.cpp` | drain counters; the mouse branch back to its original state |
| `src/circle/prefs_circle.cpp` | unchanged net — `mousespeed` added then removed with the absolute mouse |
