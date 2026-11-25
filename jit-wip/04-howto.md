# 04 — How to: build, test, deploy, capture; and the traps

All commands from the repository root. `. scripts/env.sh` sets the toolchain (`aarch64-none-elf-`, GNU make as `$MAKE`).
The user's machine is macOS 27 on Apple silicon (BSD `sed -i ''`, host tests need `SDKROOT` of SDK 26.5: see the
`sdk-hote-casse` memory note if `make -C tests/host` fails to link).

## One-time

```bash
./scripts/bootstrap.sh          # clones external/macemu-jit at its pin and applies every patches/*/ set
./scripts/apply-patches.sh --check   # "All patches are in." — must hold before any build
```

## Libraries: one target at a time

```bash
./scripts/build-libs.sh qemu    # or pi4
```

One tree is configured for one target; switching moves the other's build aside under `build/libs/<target>/` (a rename if it
was built before, a full build otherwise — the Pi 4 libraries took several minutes). **A QEMU kernel on a Pi dies silently and
a Pi kernel under QEMU goes quiet**: every script checks `scripts/target.sh` and refuses. The 16 MB kernel reservation is in
the library stamp, so a target built before it is rebuilt automatically.

## Kernels

```bash
. scripts/env.sh
# interpreter (default)
$MAKE -C src/kernel okapia-clean && $MAKE -C src/kernel -j8
# JIT: generate its tables first (writes build/generated-jit)
./scripts/gen-cpu.sh jit
$MAKE -C src/kernel okapia-clean
$MAKE -C src/kernel OKAPIA_JIT=1 -j8                                    # plain JIT
$MAKE -C src/kernel OKAPIA_JIT=1 OKAPIA_JIT_DIAG=1 -j8                  # + the core's counters
$MAKE -C src/kernel OKAPIA_JIT=1 OKAPIA_JIT_CONSTJUMP=1 -j8             # + constant jumps followed
OKAPIA_JIT_FPU=1 ./scripts/gen-cpu.sh jit && \
$MAKE -C src/kernel OKAPIA_JIT=1 OKAPIA_JIT_FPU=1 -j8                   # + compiled FPU (regenerate WITHOUT the variable after)
```

`okapia-clean` between any two variants: **objects do not depend on the Makefile or on these variables**. The image is
`src/kernel/kernel8.img`; copy it aside (`/tmp/kernel8-jit.img`) before building the next, they overwrite each other.
The JIT kernel logs `jit: JIT on, translation cache N KB` and `jit: Arena: N MB at …, executable` at start; the interpreter
kernel logs neither. Trace builds (`OKAPIA_TRACE=1`) add the self-tests, including `execmem: PASS`.

## QEMU tests (functional, not speed)

```bash
# Finder and counters, JIT on, a throwaway copy of the card
POST_IDLE=15 EXTRA_PREFS='jit true\njitcachesize 65536\njitlazyflush true' bash jit-wip/tools/jitshot.sh 150
OKAPIA_SERIAL_OUT=/tmp/s.log python3 -I jit-wip/tools/diag_totals.py /tmp/s.log      # needs a OKAPIA_JIT_DIAG=1 kernel
# the durability verdict (SIGKILL, scavenge, fsck_hfs), JIT on
OKAPIA_EXTRA_PREFS='jit true\njitcachesize 65536\njitlazyflush true' ./scripts/run-test.sh 60
```

Expected: "the Mac reported itself idle" in 11–14 s (interpreter 8.7 s), `VERDICT : OK`, 36,915 bytes written. The screenshot
path is printed; open it and look — an idle line with an empty menu bar means it is still drawing, raise `POST_IDLE`.
`jitshot.sh` writes only to a copy of `qemu/sd.img`; `scripts/run-live.sh` is for interactive use and must end with Shut Down.

## The Pi 4

```bash
./scripts/build-libs.sh pi4
./scripts/gen-cpu.sh jit && $MAKE -C src/kernel okapia-clean && $MAKE -C src/kernel OKAPIA_JIT=1 -j8
cp src/kernel/kernel8.img /tmp/kernel8-jit.img
$MAKE -C src/kernel okapia-clean && $MAKE -C src/kernel -j8                     # the interpreter again, in the tree
./scripts/make-pi-sd.sh /tmp/kernel8-jit.img      # rpi4-sd-contents/: kernel8-rpi4.img (interpreter) + kernel8-jit.img
```

Extra variants were copied by hand into `rpi4-sd-contents/` (`kernel8-jitfpu.img`, `kernel8-jitcj.img`). `make-pi-sd.sh`
excludes disk images on purpose.

### The card

The user's card is **`/Volumes/OKAPIA`** (FAT32, 59 GB). It holds their working state: the ROMs, `machd76.image` and
`boot71.img` (written by the Mac, never replace them), `BasiliskII_XPRAM`, `okapia.log`, and **their own `config.txt`** (a
custom HDMI mode 87 for a 2560×1440 monitor, active). Never reformat or `cp -R` over it. The procedure that worked, each time:

1. Back up what you will touch: `cp /Volumes/OKAPIA/Okapia_Prefs <backup>`; for any run that can write wrong data, a copy of
   `machd76.image` (500 MB; it took 6 s).
2. Copy kernels with `COPYFILE_DISABLE=1 cp` (no `._` files), `sync`, `cmp` against the source.
3. **Edit existing lines in place** (a Python `str.replace` with an `assert count == 1`): the kernel wrote a full preferences file
   (`jit false`, `jitcachesize 0`, `jitlazyflush false`, `jitinline false` … near line 124) and **Basilisk takes the first
   occurrence of a key**, so appending a second `jit true` does nothing.
4. Choose the kernel in `config.txt` under `[all]` (after the `[pi4]` section, so it wins): `kernel=kernel8-jit.img`,
   `kernel8-jitfpu.img` or `kernel8-jitcj.img`; no `kernel=` line there = the interpreter (`kernel8-rpi4.img`).
5. The user ejects (`diskutil eject /Volumes/OKAPIA`), moves the card, boots, runs **three Speedometer passes**, ends with
   Special → Shut Down, and tells you the figures.

**Card state at the end of this session** (verify, do not assume): `config.txt` has `#kernel=…` or `kernel=kernel8-jitcj.img`
(the last test was the constant-jump kernel; to return to the interpreter comment the line out); `Okapia_Prefs` has
`jit true`, `jitfpu false`, `jitcachesize 65536`, `jitlazyflush true`, `jitinline true`; the card carries
`kernel8-rpi4.img` (interpreter), `kernel8-jit.img` (0003, no 0004), `kernel8-jitfpu.img`, `kernel8-jitcj.img`.

**Backups on the Mac, in a temporary folder** (`/private/tmp` is cleaned by macOS): 
`/private/tmp/claude-501/-Users-john--claude-spares-spare-NacnKF/ffd1edcb-9a39-43a5-997c-5075fc0d10ff/scratchpad/card-backup/`
holds `machd76.image.before-lazy` and `.before-jitfpu` (500 MB each), five preferences backups and a `config.txt`. **Move them out
(and out of the repository: no disk image is ever committed) or they will disappear.** The JIT's modes were run on the real
volume after those copies; run Disk First Aid on the Mac before trusting it, and note it was last used under the
unvalidated FPU build once.

### Serial port

A USB adapter on the Pi's UART, seen as `/dev/cu.usbserial-BG040NV7`, 115200 8N1:

```bash
bash jit-wip/tools/serial_capture.sh /dev/cu.usbserial-BG040NV7 /tmp/pi.log &      # leave it running across reboots
python3 -I jit-wip/tools/diag_totals.py /tmp/pi.log
```

The core's stderr (`JIT_DIAG`, `RTE_BAD`, `M68K_SETPC_BAD`) and Okapia's logger share the port, character by character: **lines
interleave and truncate**. Do not read counters by eye; `diag_totals.py` takes the largest value of each field. Raw logs contain the
user's Wi-Fi name and access-point addresses: **do not commit or paste them**.

## Making and carrying a patch for the fork

1. Edit `external/macemu-jit/…` (the working tree of a submodule; it is dirty by design, patches are applied with `git apply`).
2. `git -C external/macemu-jit diff -- <file>` → `patches/macemu-jit/NNNN-slug.patch`, **commit message first**, then the
   `diff --git` blocks (see 0003 and 0004). A patch must apply **in order on the pristine blob**: test with `git init` in a temp
   directory, `git show HEAD:<file>` for each file, apply 0001…N with `git apply --check`.
3. `./scripts/apply-patches.sh --check` (reverse-applicability means "already in"). The kernel's `patches-check` runs it on every
   `make`; **a hand edit of the fork that is not in a patch makes `make` fail** — for a throwaway probe use
   `$MAKE -C src/kernel -o patches-check …` and restore the file afterwards (`cp` a copy before probing).

## Traps that cost time

- **A shell heredoc that contains the word `EOF` inside a Python string** ends early; use `<<'PYEOF'`.
- **Counters and probes in the fork print to stderr = the serial port**: a probe that prints too often slows the Pi and mangles
  the log.
- **QEMU's speed says nothing about the JIT** (QEMU translates the generated code); use it for correctness and for counters.
- `scripts/screenshot.sh` deletes its serial log; `jitshot.sh` keeps it.
- `jit_circle.cpp`'s `vm_release` only resets the arena when *every* acquisition has been released (a restart of the Mac);
  nothing is ever returned piecemeal.
- The "k opcodes" figure in `okapia-68k: running:` is not instructions with the JIT.
- The user is French-speaking, the repo English; say what was **read** and what was **measured**, and say when a claim is
  retracted — they asked for the code to be checked more than once and were right each time.
