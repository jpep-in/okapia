# 02 — Results: every measurement, with its conditions

Conditions unless stated: **Raspberry Pi 4** (ARM clock 1500 MHz, SoC 45–46 °C), System **7.6.1** (`machd76.image`), ROM
`okapia.rom` (a Quadra/Centris 650-class ROM, `modelid 5`), 256 MB of Mac RAM, 640×480 8-bit, `nosound true`, Wi-Fi on,
`perfreport true`. Speedometer 4.0.2 inside the guest, the "This Machine" column, run by **the user** (three passes,
the figures quoted are theirs). QEMU = `raspi3b` on the Mac, System 7.1 (`boot71.img`).

## Speedometer on the Pi 4

Interpreter reference. Rev2 table of `docs/contributing/benchmarks.md` (Pi 4): CPU **6.84**, Graf **3.18**, Disk 3.72, Math
**94.45**, PR 4.82, Bench. Ave. 7.15, FPU Ave. **2.98**, Color Ave. 5.58; against a Quadra 650: CPU 5.22×. The user re-ran
the interpreter on the card the same day ("same order of magnitude"; K'Whet **25**, FPU **2.91** as they quoted them).

| # | JIT configuration | CPU | Graf | Math | K'Whet | FPU | Notes |
|---|---|---:|---:|---:|---:|---:|---|
| ref | interpreter | **6.8** | 3.18 | **93** | **25** | **2.91** | |
| 1 | first JIT kernel: hard flush forced, cache 16 MB, arena 48 MB | 0.93 | – | 30 | 2.5 | 1.13 | idle after 49 s |
| 2 | lazy flush honoured, cache 16 MB | "similar" | | | | | no figures given |
| 3 | + core counters (`OKAPIA_JIT_DIAG`), cache 16 MB | "a bit worse" | | | | | counters cost a few % |
| 4 | + counters, cache 128 MB, arena 160 MB | 0.68 | 1.53 | 33.6 | 2.49 | 1.05 | the "before the fix" baseline |
| 5 | **patch 0003**, cache 128 MB, arena 160 MB | **6.40** | 2.83 | 92 | 2.43 | 2.37 | **crashed after ~230 s**: `Could not allocate block pool!` |
| 6 | 0003, cache 64 MB, arena **320 MB** (256 MB margin) | 6.17 | 2.58 | 75 | 2.35 | 2.34 | no crash; **laggy**, frame gaps up to 94 ms |
| 7 | 0003 + 0004, `OKAPIA_JIT_FPU` build, `jitfpu true`, `jitinline true` (inert) | 6.17 | 2.64 | 83 | 2.39 | 2.29 | FPU path made no difference |
| 8 | 0003 + 0004, **`OKAPIA_JIT_CONSTJUMP`**, `jitfpu false` | **6.42** | **3.08** | 86 | 2.56 | 2.24 | blocks 3.5 → 4.5 insns; nothing reported wrong |

Reading: after the fix the JIT is **within about 6 % of the interpreter on the integer lines and below it on every
line**, K'Whet at one tenth. Run-to-run noise on this board is of the order of ±5 %, so rows 6–8 are not distinguishable
from each other except Graf (3.08 vs 2.58–2.64) and perhaps Math.

## The core's own counters (`OKAPIA_JIT_DIAG=1`, parse with `tools/diag_totals.py`)

Meaning: `dispatch` = entries into the dispatcher helpers; `compile` = `compile_block` calls; `fresh` = first compilation of a
block; `recomp` = compilation of a block that already existed; `recompile_block` = a block's countdown expiring;
`opt0` = blocks emitted at level 0 ("interpreter dispatch": the block just calls `exec_nostats`); `opt>0` = native blocks;
`check_checksum` = entries to the revalidation helper; `flush_hard` = whole-cache flushes (cache full).

### Pi 4, before patch 0003

| | 81 s, cache 16 MB | 187 s, cache 128 MB |
|---|---:|---:|
| dispatch | 70,377,153 (≈ 870,000 / s) | 137,953,107 |
| compile | 4,772,599 | 6,906,258 |
| fresh | 318,290 | **220,509** |
| recomp | 4,454,309 | **6,685,749** |
| recompile_block | 4,447,247 | **6,667,748** |
| opt0 / opt>0 | 2,727,263 / 67,618 | **6,322,527 / 30,220** |
| check_checksum (good / bad) | 7,167,956 (6,944,642 / 24,652) | 11,203,174 (10,814,108 / 30,495) |
| flush_hard (cache full) | **28** | 5 |
| avg block | 3.7 insns | 4.0 insns (a mix of blocks compiled at several levels) |

The `exec_nostats` field in these logs is garbled by interleaving (2.5×10⁹ is not a real value); the other fields are
consistent. Boot guest cache flushes: 560–1,250 a second early, 20–70 in steady state, bursts of 6,297 in 5 s; 39,344
in ~200 s.

### QEMU, System 7.1 boot, counters on

| | JIT before 0003 | after 0003 (cache 128 MB) | after 0003 + 0004 | + CONSTJUMP |
|---|---:|---:|---:|---:|
| idle at | 21.4–25.8 s | 12.2 s | 11.8 s | 13.9 s |
| window of counters | | t=22 s | t=8 s | t=10 s |
| dispatch | | 1,016,622 | 515,809 | 657,208 |
| compile | millions | 44,259 | 36,902 | 99,430 |
| fresh / recomp | | 34,719 / 9,540 | | / 48,322 |
| opt0 / opt>0 | | 28,278 / 15,981 | 22,714 / 14,188 | 55,937 / 43,493 |
| check_checksum | | 506,224 | 326,334 | 354,127 |
| avg block | 3.5 | | 3.5 | **4.5** |

Interpreter in the same JIT build (`jit` not set): idle at **8.7 s**, 3,200 k "opcodes"/s (the seam counter).

### Probes (temporary code, not kept), QEMU boot, JIT on, lazy flush

- Every `CINV`/`CPUSH` the Mac executes is **`CPUSHA`** (opcode `F4F8`): 8,000 of 8,000 in 13 s. None is a line or page form.
- `flush_icache_lazy` called 8,497 times in 13 s, marking on average **118 active blocks** each (1,001,393 total).
- `FlushCodeCache` (the BlockMove trap) called 8,000 times, 670 bytes on average.
- **99.8 % of entries to `check_checksum()` find the block in `BI_NEED_CHECK`** (the others: 395 active, 23 need-recomp,
  4 invalid out of 400,000), at about 30,000 entries a second against some 870,000 dispatcher entries a second measured on the
  Pi: a large share of the *count*, a small share of the *time* (not measured directly).

## Other measurements

| What | Value |
|---|---|
| `kernel8.img` interpreter, Pi 4 / QEMU | 3,397 KB / 3,348 KB |
| `kernel8.img` JIT (`OKAPIA_JIT=1`) | 5,450 KB (Pi 4), 5,398 KB (QEMU); +FPU 5,475 KB; +CONSTJUMP 5,452 KB |
| What the JIT adds (compile probe) | 1,590,491 bytes of text and read-only data, **4.86 MB of `.bss`** (`cache_tags` 2 MB, `baseaddr` / `mem_banks` / `compfunctbl` / `nfcompfunctbl` 512 KB each, `prop` and `raw_cputbl_count` 256 KB each) |
| Arena used on the Pi after ~5 min | ≈ 100 MB of 320 MB (64 MB cache allocated at start) |
| Boot to idle on the Pi 4, JIT kernel | 49 s (System 7.6.1) — no matching figure for the interpreter was recorded |
| `run-test.sh 60` (kill, scavenge, fsck) | OK, 36,905 B written (interpreter) / 36,915 B (JIT, all variants) |
| `ExecMemSelfTest` under QEMU | PASS: PXN before 1, after 0, re-emitted code ran, neighbouring page still PXN 1 |
| Compile probe of the fork against Okapia's flags | 22 units, 0 errors, 17 s (`tools/probe.sh`) |
| Dispatcher work implied by one block boundary (read from the emitters, not measured) | ≈ 15–25 instructions and several memory accesses; see `03-findings.md` |

## What was **not** measured

- Cycles per 68k instruction of the interpreter (PMU), the single most useful missing number (see `05-next.md`).
- How much of the run time is in native code and how much in the dispatcher: nothing counts inside generated code.
- The price of the compiler's bursts (the pauses).
- Anything for Mac OS 8, applications, restarts, SheepShaver.
- The interpreter's boot time on the Pi in the same session as the JIT's.
