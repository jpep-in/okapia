# The PowerPC Macintosh's mouse, and what the ROM says about it

> Session of 9 September 2026, branch `ppc_mouse_improv`. Two requests: give the PowerPC seam a rate held by the clock,
> and make the Mouse control panel work under SheepShaver. Measured under QEMU `raspi3b` on Mac OS 8.6 (PowerPC engine)
> and System 7.1 (68k engine). Topic pages: [Input](../../topics/input.md), [PowerPC engine](../../topics/engine-powerpc.md).

## The PowerPC seam

`PPC_CHECK_TICKS` was an instruction count fixed at 50,000, and an instruction count is a rate only on a machine whose
speed does not change. Measured: **87 Hz on average over a run whose last window was at 116** — the rate followed what the
guest was doing.

`patches/macemu/0004` now compares against a variable, `ppc_check_ticks_quantum`, whose initial value is
`PPC_CHECK_TICKS`; a port that leaves it alone keeps the old behaviour exactly. The calibration arithmetic is the 68k's,
unchanged. Measured after: **~950 Hz**, quantum settling near 7,000 and falling to its floor when the guest struggles —
intended behaviour.

**Where the symbol must live.** `ppc_check_ticks_quantum` is declared in `ppc-cpu.hpp` (patched) and defined in
`sheepshaver/cpu_ticks_circle.cpp`, both in the PowerPC half, so definition and reference are renamed together by the
`ppc__` prefix and the link binds. **Defining it in a shared file would have broken**: the reference would have become
`ppc__ppc_check_ticks_quantum` while the definition kept its bare name.

## Why the control panel did nothing

By construction, predating this session. The PowerPC engine gives the Mac a **position**, through `CursorDeviceDispatch`
selector 1, `MoveTo` (`adb.cpp:405`). Only selector 0, `Move`, runs deltas through the acceleration tables
(`CrsrDev.a:137`: "dh and dv will be run through the acceleration algorithm"). `MoveTo` puts the cursor where it is told.
The control panel's slider drives `CrsrDevSetAccel` (selector 8), which fills tables `MoveTo` never reads.

## The ROM's curve, decoded

Calling selector 0 would have meant running 68k code **on every mouse report**. The chosen solution does the same
arithmetic on our side, with no guest code — and the curve is not invented, it is the ROM's.

`MiscROMRsrcs.r`, resource `'accl'` 1, `classMouse`, table marked "New, better-feeling", Fixed 16.16:

| Input speed (in/s) | Output speed | Gain |
|---|---|---|
| 0.44 | 0.375 | **0.85** |
| 4.31 | 16.5 | 3.83 |
| 12.0 | 95.0 | **7.92** |
| 22.9 | 139.0 | 6.06 |
| 29.2 | 148.5 | 5.08 |
| 34.5 → 40.0 | 150.0 | ceiling |

Three properties a home-made curve would not have had, which explain why mine did not "feel" right:

1. **the gain is below 1 at very low speed** — the Mac *slows* the pointer to aim, it does not merely not accelerate it;
2. **the maximum is in the middle**, ~8× near 12 in/s, not at the top;
3. **the output saturates at 150 in/s** — a ceiling on *speed*, not gain.

### Units, the key

`CrsrDev.a:1257` settles them: the device resolution is divided by `frameRate << 16` with `_FixDiv` to scale device
speed. The table's numbers are **inches per second**, converted to counts by `dpi / frameRate` and to pixels by
`screenRes / frameRate`.

| Constant | Value | Source |
|---|---|---|
| `frameRate` | 67 frames/s | `CrsrDevEqu.a:150` |
| `screenRes` | 72 dpi (a field, initialised) | `CrsrDev.a:2232` |
| Assumed device resolution | **200 dpi** | `CrsrDev.a:2045` |

The Mac even tags the mouse `'@200'` and calls any device refusing the protocol a "stupid 4th party device".

### The slider is an interpolation

`'accl' (1)` holds two tables: acceleration 0.0 → identity, acceleration 1.0 → the curve. `CrsrDevSetAccel` interpolates
between the two enclosing tables (`CrsrDev.a:1102`). The setting is a Fixed between 0 and 1, and **the tablet position is
the identity** — not a convention, the result of interpolating at zero.

## Where the setting lives: measured, not deduced

The ROM offers two plausible homes and System 7 uses one. Over eight changes of the slider, then a sweep both ways:

| Source | Behaviour |
|---|---|
| `CrsrThresh` (`0x8EC`) | **stuck at 6**, its startup value (`StartInit.a:2944`) |
| PRAM (`XPRAM[0x20]`, classic PRAM at `XPRAM[0x10]`, `main.cpp:115`) | **stuck at 0** |
| **`SPVolCtl` (`0x208`), bits 5:3** | follows every position |

Down `6 5 4 3 2 1 0` then up `0 1 2 3 4 5 6`: **seven positions, tablet 0, "Slow" 1, "Fast" 6**, 7 never reached.
Verified **on System 7.1 and on Mac OS 8.6**, same address, same encoding.

## Telling the Macintosh the truth, on the 68k side

The 68k engine does not have this problem: it gives deltas and the Mac accelerates. But it scales its curve by a
resolution it **believes** — 200 — with no way to learn otherwise, so a modern mouse is accelerated as if it moved five to
eight times slower than it does.

`CrsrDevSetUnitsPerInch` (selector 10) exists for this — "May be called if the software knows more about the resolution of
the device than can be found from the ADB bus" — and recomputes the tables from the new figure (`CrsrDev.a:488`). Two
calls, once, after the Macintosh has said it finished starting. **Selector 11 walks the global device list**
(`CrsrDev.a:516`) rather than guessing a pointer — a wrong pointer would have the ROM write a resolution into whatever it
addressed. Verified on System 7.1: record found at `0x25680`, call accepted, the Mac carries on.

## The setting is in the boot menu

Six values, 100 to 1000, the unit carried by the value so the label stays short in every language; a card carrying a value
not offered comes back as the nearest one.

The reasoning "it describes the hardware, so it belongs in the preferences file" was the wrong one: correcting this setting
is correcting the pointer's comfort, and nobody should have to take the card out and find another computer for that. "dpi"
is also a word anyone installing a bare-metal emulator knows.

**The default is 200**, what the Macintosh assumes of itself: a card that says nothing keeps exactly the previous
behaviour. A new setting that changes the feel for those who did not ask is a bug, however right its arithmetic — learnt by
setting it to 1000 and making the mouse five times slower under 7.1.

## Three traps met

### An undeclared preference key is read, then dropped, silently

`mousedpi` had its default but no entry in `prefs_circle.cpp`'s table. The card's line was read, did nothing, and said
nothing. **A whole measurement was made against it**: the pointer was judged at a tenth of the expected speed, described as
"an extremely dirty ball mouse", and the same curve became "turbo" the moment the figure was actually read. Nothing in the
curve had changed.

### The Toolbox is not called from the drain

The stub asks the Memory Manager for a block and returns it, so it re-enters the Toolbox. From the drain that is between
two arbitrary instructions — possibly while the Mac is itself inside the Memory Manager. A heap corrupted there surfaces
**later and elsewhere**: measured once as a data abort in `DiskInterrupt()`, a second and a half afterwards, with nothing to
connect the two. `idle_wait()` is the Mac announcing it has no more work, from its own event loop: a defined point, the
System fully up, nothing of ours half done. Four twenty-second runs, clean.

### A hardware claim from a start path must be shared

`CUserTimer` connects `ARM_IRQ_TIMER1`, and `interrupt.cpp:145` asserts `m_apIRQHandler[nIRQ] == 0`. A Circle assertion
halts the board — which under QEMU looks like the emulator quitting on its own, and that is exactly how it was reported:
"it crashes, maybe just QEMU quitting". The guard existed, but in `tick_circle.cpp`, which is in `PLATFORM_SRCS` and so
**compiled once per engine**: each Macintosh had its own "already armed" and neither saw the other's. The claim now lives in
`hal_circle.cpp` (`SHARED_SRCS`), the timer and its interrupt taken once for the life of the board and the handler swapped —
the pattern of the single mouse registration and the single frame buffer. Confirmed after a full 68k → restart → PowerPC
round:

```
okapia-board: Fine timer already claimed; handler swapped for the other engine
```

## Left to measure then

- **On the Pi 4**, with a real USB mouse: the right `mousedpi` will be a third figure. On this bench, a MacBook Pro trackpad
  seen through macOS and QEMU measures ~200 — a period mouse's density, by coincidence — and comfort was finally set at 400,
  a comfort choice, not an exact description.
- **The difference in feel between the engines** is not a calibration defect: both see the same density. It comes from two
  curves — 7.6's native tables on one side, our implementation of the 1994 ROM table on the other.
- **`SCREEN_DPI` is fixed at 72** in our code. The whole result is proportional to it, and the ROM makes it a field
  (`CrsrDev.a:2232`) a System can change.
