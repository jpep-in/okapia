#!/usr/bin/env python3
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: MIT
"""Boot a Macintosh that runs MacBench from Startup Items, and collect its results.

For a desktop Basilisk II (SDL), to compare engines on the host. On Okapia,
see docs/contributing/benchmarks.md#macbench.

usage: run.py <BasiliskII binary> <rom> <boot image> <workdir> [name=pref-lines ...]
Each configuration boots a fresh copy of the image; the emulator is killed once
the results file is complete. Example configurations:  interp="jit false"  jit="jit true"
"""
import os, shutil, subprocess, sys, time

binary, rom, image, work = sys.argv[1:5]
configs = [a.split("=", 1) for a in sys.argv[5:]] or [["interp", "jit false"], ["jit", "jit true"]]
here = os.path.dirname(os.path.abspath(__file__))
tools = os.environ.get("RETRO68_BIN", os.path.expanduser("~/github-repositories/Retro68-build/toolchain/bin"))
os.makedirs(work, exist_ok=True)

def sh(*cmd):
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)

def boot(name, extra):
    disk = os.path.join(work, name + ".dsk")
    share = os.path.join(work, name + ".share")
    shutil.copyfile(image, disk)
    shutil.rmtree(share, ignore_errors=True); os.makedirs(share)
    sh(os.path.join(tools, "hmount"), disk)
    sh(os.path.join(tools, "hcopy"), "-m", os.environ.get("MACBENCH_BIN", os.path.join(here, "..", "..", "build", "macbench", "MacBench.bin")), ":System Folder:Startup Items:")
    sh(os.path.join(tools, "humount"))
    prefs = os.path.join(work, name + ".prefs")
    with open(prefs, "w") as f:
        f.write(f"rom {rom}\ndisk {disk}\nextfs {share}\nramsize 67108864\nscreen win/640/480\n"
                "displaycolordepth 8\nmodelid 14\ncpu 4\nfpu true\nnocdrom true\nnosound true\nnogui true\nidlewait true\n"
                + extra.replace(";", "\n") + "\n")
    env = dict(os.environ, SDL_AUDIODRIVER="dummy", HOME=work)
    log = open(os.path.join(work, name + ".log"), "w")
    p = subprocess.Popen([binary, "--config", prefs], env=env, stdout=subprocess.DEVNULL, stderr=log)
    result = os.path.join(share, "MacBench Results")
    t0 = time.time(); text = None
    try:
        while time.time() - t0 < float(os.environ.get("MACBENCH_TIMEOUT", "600")):
            time.sleep(0.25)
            if p.poll() is not None: break
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
for name, extra in configs:
    text, wall = boot(name, extra)
    if text is None:
        print(f"{name}: no result after {wall:.0f} s"); continue
    rows = [l.split() for l in text.replace("\r", "\n").splitlines()[1:] if l and l != "END"]
    results[name] = {r[0]: (int(r[1]), r[2]) for r in rows}
    print(f"{name}: {len(rows)} tests, {sum(v[0] for v in results[name].values()) / 60:.1f} s of tests, {wall:.1f} s wall", flush=True)

names = list(results)
if names:
    base = names[0]
    print(f"\n{'test':14s}" + "".join(f"{n:>10s}" for n in names) + "".join(f"{'x ' + n:>10s}" for n in names[1:]) + "  same work")
    for test in results[base]:
        ticks = [results[n].get(test, (0, "?"))[0] for n in names]
        checks = {results[n].get(test, (0, "?"))[1] for n in names}
        line = f"{test:14s}" + "".join(f"{t:10d}" for t in ticks)
        line += "".join(f"{ticks[0] / t:10.2f}" if t else f"{'-':>10s}" for t in ticks[1:])
        print(line + ("  yes" if len(checks) == 1 else "  NO " + " ".join(sorted(checks))))
