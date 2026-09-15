# Input

> USB keyboard and mouse presented to the Macintosh as ADB: the generated key table, the cross-core hand-over,
> the 1 kHz drain, mouse resolution and acceleration on both engines. Main files: `src/circle/input_circle.cpp`,
> `src/firmware/circle/okapia_input.cpp`, `src/circle/cpu_ticks_circle.cpp`,
> `src/circle/sheepshaver/cpu_ticks_circle.cpp`, `scripts/gen-keycodes.py`, `patches/macemu/0004`.

## Using it

- **Keyboard and mouse**: any USB HID keyboard and mouse, directly or through a hub. The keypad is mapped.
- **Mouse speed**: the Mac's own **Mouse** control panel works on both engines.
- **Mouse resolution**: Settings → Mouse in the [boot menu](boot-menu.md), stored as `mousedpi`
  ([Preferences](../preferences.md)), 100 to 1000. `200` (the default) is what the Macintosh assumes of an ADB mouse;
  raise it when a modern mouse feels too fast. A value not in the menu comes back as the nearest one offered.
- **Remapping**: `keycodefile` names a file in Basilisk's own `keycodes` format; a `BasiliskII.keycodes` from a
  desktop Basilisk loads as it is (105 mappings, verified), and two lines are enough to fix two keys.
- **Startup keys**: Option (boot menu) and Command-Option-P-R (forget the parameter RAM of the Macintosh about to
  start) are read by the firmware
  ([Startup and shutdown](startup-and-shutdown.md)).

## How it works

### Keys: a generated table

`scripts/gen-keycodes.py` derives the table from `BasiliskII/src/Unix/keycodes`, section `sdl cocoa`, at build
time. **SDL2 scancodes are USB HID usage IDs**, so that section already is the table a USB host needs. Unmapped keys
are dropped rather than sent as something else. `KeycodesLoad()` overrides it from the card.

### Mouse: raw deltas

`CMouseDevice::RegisterStatusHandler()` hands over raw `dx/dy` and buttons. Circle's cooked
`RegisterEventHandler()` reports absolute coordinates, and drops every report until `Setup()` is given screen
dimensions.

### From core 0 to the emulation core

Circle's USB handlers run on core 0 (IRQ context), while `adb.cpp`'s key ring — `key_buffer[]`, `key_write_ptr`, the
key matrix — carries no lock (`adb.cpp:302`). Upstream survives because x86 publishes stores in order; AArch64 does
not, so the Mac could read a key code not yet written. The handlers therefore **only record**: a ring for keys and
buttons, an accumulator for motion, published with a release store. `InputDrain()` hands everything to `adb.cpp`
from the emulation thread (`JS/input_js.cpp:15` in infinite-mac has the same shape for the same reason). Every
`ADB*()` call raises `INTFLAG_ADB` and triggers the interrupt itself; the drain adds neither.

### The drain holds 1 kHz

The drain runs from the engine's periodic seam: `cpu_do_check_ticks()` on the 68k engine, fired when
`emulated_ticks` wraps (`newcpu.h:332`), and `powerpc_check_ticks()` on the PowerPC one. An instruction count is a
rate only if the engine's speed is fixed, and it is not: measured **209 to 1,076 visits per five seconds**, a drain
every 5 to 24 ms. Both seams now measure against the clock every 64 visits and correct the quantum to hold 1 kHz, as
infinite-mac does (`main_unix.cpp:342`). SheepShaver's `PPC_CHECK_TICKS` became a variable,
`ppc_check_ticks_quantum` (`patches/macemu/0004`), whose initial value is the old constant.

**Why a kilohertz**: the mouse reports at about 100 Hz. Below that order reports merge, and a merged report is one ADB
packet carrying up to 275 counts where a 200 cpi mouse never sent a dozen — the Mac's acceleration extrapolates a speed
no hand produces and the pointer leaps. At 1 kHz it is one ADB packet per USB report (measured 228→228, 530→530,
179→179; the one exception was a window where the drain had fallen to 668/s). Measured after: 999.4 drains/s on the
68k engine, ~950 Hz on the PowerPC.

### Acceleration, 68k engine: the Macintosh's own

The engine feeds `adb.cpp` **relative** deltas, which it packs into ADB reports for the Mac's own mouse driver. So
the acceleration and the Mouse control panel are the Mac's own. But the ROM scales its curve by a resolution it
**believes**: a mouse accepting the 200 dpi protocol is tagged `'@200'` (`CrsrDev.a:2045`), and nothing inside Mac OS
can learn otherwise. `TellMacTheMouseResolution()` calls `CursorDeviceDispatch` once the Macintosh is idle:
selector 11 (`CrsrDevNextDevice`, walking the device list from NIL, `CrsrDev.a:516`) then selector 10
(`CrsrDevSetUnitsPerInch`, which recomputes the tables, `CrsrDev.a:488`) with `mousedpi`. Verified on System 7.1:
record found at `0x25680`, call accepted.

### Acceleration, PowerPC engine: the ROM's curve, applied here

SheepShaver feeds the Mac a **position** through `CursorDeviceDispatch` selector 1, `MoveTo` (`adb.cpp:405`), which
does not accelerate — only selector 0, `Move`, runs deltas through the tables (`CrsrDev.a:137`). The Mouse control
panel was therefore inert on this engine by construction. Rather than run 68k code on every report, the USB handler
applies the same arithmetic: the ROM's `'accl'` resource 1 for a mouse (`MiscROMRsrcs.r`, the table marked
"New, better-feeling"), in inches per second — counts to in/s by `dpi`, through the curve, to pixels at 72 dpi, over
the measured interval between reports.

| Input (in/s) | Output (in/s) | Gain |
|---|---|---|
| 0.44 | 0.375 | 0.85 |
| 4.31 | 16.5 | 3.83 |
| 12.0 | 95.0 | 7.92 |
| 22.9 | 139.0 | 6.06 |
| 29.2 | 148.5 | 5.08 |
| 34.5 → 40.0 | 150.0 | ceiling |

The gain is below one at low speed (the Mac slows the pointer to aim), peaks in the middle, and the output saturates
at 150 in/s. The control panel's slider interpolates between the identity table (tablet) and this curve
(`CrsrDev.a:1102`).

### Where the Mouse control panel's setting lives

Measured, not deduced: over a sweep of the slider in both directions on System 7.1 and Mac OS 8.6, `CrsrThresh`
(`0x8EC`) stayed at its startup 6 (`StartInit.a:2944`), parameter RAM did not move, and **`SPVolCtl` (`0x208`),
bits 5:3** followed every position: seven, tablet 0, "Slow" 1, "Fast" 6, never 7. `ReadMouseSettings()` reads it
from the emulation core every 512 drains and publishes it to the handler, which must not read guest memory itself.

## Status

- [x] Key mapping, modifiers, press and release (2026-08-26)
- [x] Raw mouse deltas and buttons (2026-08-26)
- [x] Key table generated from upstream; `keycodefile` override (2026-08-29)
- [x] Events handed to `adb.cpp` from the emulation core (2026-09-08)
- [x] 68k drain recalibrated to 1 kHz (2026-09-08)
- [x] PowerPC seam recalibrated to 1 kHz; Mouse control panel honoured on PowerPC (2026-09-09)
- [x] `mousedpi` told to the Macintosh (68k) and used by the curve (PowerPC); in the boot menu (2026-09-09)
- [ ] Measure `mousedpi` for a real USB mouse on the Pi 4
- [ ] `SCREEN_DPI` is fixed at 72; the ROM keeps it as a field a System can change (`CrsrDev.a:2232`)
- [ ] Optionally compensate pointer speed for the display's integer scale ([Display](display.md))
- [ ] Debug shortcuts intercepted before the Mac
- [ ] Mouse wheel (reported by Circle, ignored)

## Pitfalls

- **Never type the key table by hand.** The hand-written one had all four arrows wrong: `ADBKeyDown()` takes *raw ADB*
  codes, which agree with Mac virtual key codes for letters and digits and differ for the arrows. Up went out as
  `0x7E` (virtual; ADB wants `0x3E`), landed beside the Power key and opened the shutdown dialog on every press. It
  also lacked the keypad. And a table whose initialiser is shorter than its size leaves the tail zeroed: **ADB `0x00`
  is the letter A**, so every unmapped key typed "a".
- **A USB keyboard reports changes, never state.** A key held from power-on sends one report when it goes down and
  nothing more; `CUSBKeyboardDevice` cannot say what is down. `FwInputWatch()` is therefore called from
  `CKernel::Initialize()` as soon as USB is up, and the firmware window only reads the latch.
- **Circle's mouse can be claimed once per boot and never given back**: `RegisterStatusHandler` asserts
  `m_pStatusHandler == 0` (`mouse.cpp:85`). `okapia_input.cpp` holds the one registration and forwards reports;
  `FwInputPassMouseTo()` is what `InputInit()` calls instead of registering.
- **The keyboard is nothing like it**: a raw handler simply replaces the last one, so `InputInit()` replacing it *is*
  the hand-over. `UnregisterKeyStatusHandlerRaw()` exists and is the only correct detach; Okapia never detaches.
- **`RegisterKeyStatusHandlerRaw (0)` does not detach, it changes mode**: a null raw handler makes `ReportHandler`
  fall through to the cooked path (`usbkeyboard.cpp:200`). Doing that to "give the keyboard back" stopped the
  kernel dead.
- **The Toolbox is not called from the drain.** A stub that allocates re-enters the Memory Manager between two
  arbitrary instructions; a heap corrupted there surfaced once as a data abort in `DiskInterrupt()` a second and a
  half later. Call it from `idle_wait()`.
- **A preference key missing from `prefs_circle.cpp`'s table is read and silently dropped.** A whole mouse
  measurement was once made against a `mousedpi` that was never read.
- **A new setting must not change what people who did not ask for it feel.** `mousedpi` briefly defaulted to 1000
  and made the mouse five times slower on 7.1; the default is 200, the Mac's own assumption.
- **A slow pointer moves in steps of the display's integer scale.** That is the magnification, not a defect.

## Development notes

- **2026-08-26** — Finder usable under QEMU with keyboard and mouse; the pointer dragged, not yet measured.
- **2026-09-08** — Stuttering pointer: four independent causes, three found by measuring and the main one by eye.
  An absolute 68k mouse with our own curve was built and removed: once the drain was fixed, relative mode was simpler
  and more faithful. See the [pointer note](../notes/dev/2026-09-08-stuttering-pointer.md).
- **2026-09-09** — PowerPC seam and Mouse control panel; the ROM's curve decoded; `mousedpi` added. See the
  [PowerPC mouse note](../notes/dev/2026-09-09-powerpc-mouse.md).

## References

- `BasiliskII/src/adb.cpp:302`, `:405`; `Unix/keycodes`; `uae_cpu_2021/newcpu.h:332`
- SuperMario `CrsrDev.a` (`:137`, `:488`, `:516`, `:1102`, `:1257`, `:2045`, `:2232`), `CrsrDevEqu.a:150`,
  `MiscROMRsrcs.r`, `StartInit.a:2944`
- Circle `lib/usb/usbkeyboard.cpp:200`, `lib/input/mouse.cpp:85`
- infinite-mac `JS/input_js.cpp:15`, `main_unix.cpp:342`
