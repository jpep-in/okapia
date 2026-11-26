#!/usr/bin/env python3
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: MIT
"""Boot a Macintosh that runs MacBench from Startup Items, collect its results, compare.

For a desktop Basilisk II (SDL), to compare engines, FPU cores or builds on the
host. On Okapia, see docs/contributing/benchmarks.md#macbench.

usage: run.py [options] <BasiliskII binary> <rom> <boot image> <workdir> [config ...]

A config is  name=prefs  or  name=binary|prefs : a name for the column, the
binary to run when it is not the default one, and preference lines separated
by ';'. Without configs: interp="jit false" and jit="jit true". Each run boots
a fresh copy of the image; the emulator is killed once the results file is
complete.

options:
  --repeat N       run every config N times and keep each test's median
  --only PREFIX    show only the tests whose name starts with PREFIX (fpu_, qd_...)
  --save FILE      write the results as JSON
  --compare FILE   add a column per config: its time against the same config in FILE

Example, three native builds of the same Basilisk II, the FPU tests alone:
  run.py --repeat 3 --only fpu_ build/native/mpfr/BasiliskII okapia.rom sys755.img /tmp/mb \\
         mpfr="jit true" "softfloat=build/native/softfloat/BasiliskII|jit true" \\
         "interp=build/native/softfloat/BasiliskII|jit false"

The last column says whether every config computed the same thing: an exact
FPU gives a 68881's bits on any host, so the checks of the fpu_ tests agree
between exact cores and differ for one that is not exact (or, for fpu_whet
and fpu_trig, one whose transcendentals are not the 68040 FPSP's).
"""
import argparse, json, os, shutil, statistics, subprocess, sys, time

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("binary"); ap.add_argument("rom"); ap.add_argument("image"); ap.add_argument("work")
ap.add_argument("configs", nargs="*")
ap.add_argument("--repeat", type=int, default=1)
ap.add_argument("--only", default="")
ap.add_argument("--save")
ap.add_argument("--compare")
args = ap.parse_args()

configs = []
for a in args.configs or ["interp=jit false", "jit=jit true"]:
    name, rest = a.split("=", 1)
    binary, prefs = rest.split("|", 1) if "|" in rest else (args.binary, rest)
    configs.append((name, os.path.abspath(binary), prefs))

here = os.path.dirname(os.path.abspath(__file__))
tools = os.environ.get("RETRO68_BIN", os.path.expanduser("~/github-repositories/Retro68-build/toolchain/bin"))
macbench = os.environ.get("MACBENCH_BIN", os.path.join(here, "..", "..", "build", "macbench", "MacBench.bin"))
os.makedirs(args.work, exist_ok=True)


def sh(*cmd):
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)


def boot(tag, binary, extra):
    disk = os.path.join(args.work, tag + ".dsk")
    share = os.path.join(args.work, tag + ".share")
    shutil.copyfile(args.image, disk)
    shutil.rmtree(share, ignore_errors=True); os.makedirs(share)
    sh(os.path.join(tools, "hmount"), disk)
    sh(os.path.join(tools, "hcopy"), "-m", macbench, ":System Folder:Startup Items:")
    sh(os.path.join(tools, "humount"))
    prefs = os.path.join(args.work, tag + ".prefs")
    with open(prefs, "w") as f:
        f.write(f"rom {os.path.abspath(args.rom)}\ndisk {disk}\nextfs {share}\nramsize 67108864\nscreen win/640/480\n"
                "displaycolordepth 8\nmodelid 14\ncpu 4\nfpu true\nnocdrom true\nnosound true\nnogui true\nidlewait true\n"
                + extra.replace(";", "\n") + "\n")
    env = dict(os.environ, SDL_AUDIODRIVER="dummy", HOME=args.work)
    log = open(os.path.join(args.work, tag + ".log"), "w")
    p = subprocess.Popen([binary, "--config", prefs], env=env, stdout=subprocess.DEVNULL, stderr=log)
    result = os.path.join(share, "MacBench Results")
    t0 = time.time(); text = None
    try:
        while time.time() - t0 < float(os.environ.get("MACBENCH_TIMEOUT", "900")):
            time.sleep(0.25)
            if p.poll() is not None:
                break
            try:
                data = open(result, "rb").read().decode("mac_roman")
            except OSError:
                continue
            if "END" in data:
                text = data; break
    finally:
        p.kill(); p.wait()
    return text, time.time() - t0


results = {}
for name, binary, extra in configs:
    runs = []
    for r in range(args.repeat):
        text, wall = boot(f"{name}.{r}", binary, extra)
        if text is None:
            print(f"{name} run {r + 1}: no result after {wall:.0f} s", flush=True); continue
        rows = [l.split() for l in text.replace("\r", "\n").splitlines()[1:] if l and l != "END"]
        runs.append({row[0]: (int(row[1]), row[2]) for row in rows})
        print(f"{name} run {r + 1}: {len(rows)} tests, {sum(v[0] for v in runs[-1].values()) / 60:.1f} s of tests, "
              f"{wall:.1f} s wall", flush=True)
    if not runs:
        continue
    merged = {}
    for test in runs[0]:
        ticks = [run[test][0] for run in runs if test in run]
        checks = sorted({run[test][1] for run in runs if test in run})
        merged[test] = {"ticks": statistics.median(ticks), "all": ticks, "check": checks[0] if len(checks) == 1 else "unstable:" + ",".join(checks)}
    results[name] = merged

if args.save:
    with open(args.save, "w") as f:
        json.dump({"configs": [{"name": n, "binary": b, "prefs": p} for n, b, p in configs], "results": results}, f, indent=1)

baseline = {}
if args.compare:
    with open(args.compare) as f:
        baseline = json.load(f)["results"]

names = list(results)
if not names:
    sys.exit(1)
tests = [t for t in results[names[0]] if t.startswith(args.only)]
cols = [f"{n:>10s}" for n in names] + [f"{'x ' + n:>10s}" for n in names[1:]]
cols += [f"{'vs saved ' + n:>16s}" for n in names if n in baseline]
print(f"\n{'test (ticks)':14s}" + "".join(cols) + "  same work")
totals = {n: 0.0 for n in names}
for test in tests:
    ticks = [results[n].get(test, {}).get("ticks", 0) for n in names]
    for n, t in zip(names, ticks):
        totals[n] += t
    line = f"{test:14s}" + "".join(f"{t:10.0f}" for t in ticks)
    line += "".join(f"{ticks[0] / t:10.2f}" if t else f"{'-':>10s}" for t in ticks[1:])
    for n, t in zip(names, ticks):
        if n in baseline:
            old = baseline[n].get(test, {}).get("ticks", 0)
            line += f"{old / t:16.2f}" if t and old else f"{'-':>16s}"
    checks = {results[n].get(test, {}).get("check", "?") for n in names}
    print(line + ("  yes" if len(checks) == 1 else "  NO  " + " ".join(f"{n}:{results[n].get(test, {}).get('check', '?')}" for n in names)))
line = f"{'total':14s}" + "".join(f"{totals[n]:10.0f}" for n in names)
line += "".join(f"{totals[names[0]] / totals[n]:10.2f}" if totals[n] else f"{'-':>10s}" for n in names[1:])
print(line)
