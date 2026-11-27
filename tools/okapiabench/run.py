#!/usr/bin/env python3
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: MIT
"""Run OkapiaBench, or read its result files, and compare.

Two ways in:

  run.py [options] <BasiliskII binary> <rom> <boot image> <workdir> [config ...]
      Boot a desktop Basilisk II once per config with OkapiaBench in Startup
      Items, collect each run's file from the shared folder, compare.
      A config is  name=prefs  or  name=binary|prefs : a name for the column,
      the binary to run when it is not the default one, and preference lines
      separated by ';'. Without configs: interp="jit false" and jit="jit true".
      Each run boots a fresh copy of the image; the emulator is killed once
      the results file is complete.

  run.py report [options] <result file or saved JSON> ...
      No boot: the files a Pi, SheepShaver or a period Macintosh wrote
      ("OkapiaBench 2026-11-27 1432"), one column each.

options:
  --repeat N         run every config N times and keep each test's median (boot only)
  --only PREFIX      show only the tests whose name starts with PREFIX (fpu_, qd_...)
  --save FILE        write the results as JSON
  --compare FILE     add a column per config: its time against the same config in FILE
  --reference FILE   a result file or JSON: every column gets a score against it, per
                     test and per family (geometric mean over the tests both ran)
  --emit-header FILE write the reference (or the first column) as a C header for
                     OkapiaBench's recap: build with -DREFERENCE='\"FILE\"'
  --mhz N            a period machine's clock: Embench's score per MHz as well

Example, three native builds of the same Basilisk II, the FPU tests alone:
  run.py --repeat 3 --only fpu_ build/native/mpfr/BasiliskII okapia.rom sys755.img /tmp/mb \\
         mpfr="jit true" "softfloat=build/native/softfloat/BasiliskII|jit true" \\
         "interp=build/native/softfloat/BasiliskII|jit false"

Times are the Time Manager's microseconds. The column "same work" says
whether every column computed the same thing (the check: a hash, or
Embench's and CoreMark's own verdict). Embench's score is Embench's own
definition, reference time / time * gsf, geometric mean, against
external/embench-iot/baseline-data/speed.json (16 for its reference, a
Cortex-M4 at 16 MHz).
"""
import argparse, json, math, os, shutil, statistics, subprocess, sys, time

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.join(here, "..", "..")
FAMILIES = [("cpu_", "processor"), ("tb_", "Toolbox"), ("qd_", "QuickDraw"), ("cqd_", "Color QuickDraw"),
            ("fp_", "double"), ("fpu_", "68881"), ("app_", "real work"), ("emb_", "Embench")]
TICK_USEC = 1e6 / 60.15


def parse_file(text):
    """A result file: its header and its tests."""
    lines = [l for l in text.replace("\r", "\n").splitlines() if l]
    if not lines or not lines[0].startswith("OkapiaBench "):
        raise ValueError("not an OkapiaBench result file")
    if lines[0].split()[1] != "2":
        raise ValueError(f"{lines[0]}: format 1 (MacBench) is not comparable with OkapiaBench 2; run it again")
    header, tests = {}, {}
    for l in lines[1:]:
        if l == "END":
            break
        if l.startswith("# "):
            k, _, v = l[2:].partition(" ")
            header[k] = v
            continue
        f = l.split()
        if len(f) >= 2 and f[1] == "skip":
            tests[f[0]] = {"skip": " ".join(f[2:])}
        elif len(f) >= 4:
            tests[f[0]] = {"usec": int(f[1]), "ticks": int(f[2]), "check": f[3], "extra": " ".join(f[4:])}
    return {"header": header, "tests": tests, "complete": "END" in lines}


def merge(runs):
    """Several runs of one config: each test's median, its checks."""
    out = {"header": runs[0]["header"], "tests": {}}
    for test in runs[0]["tests"]:
        rows = [r["tests"][test] for r in runs if test in r["tests"]]
        if "skip" in rows[0]:
            out["tests"][test] = rows[0]; continue
        rows = [r for r in rows if "usec" in r]
        checks = sorted({r["check"] for r in rows})
        out["tests"][test] = {"usec": statistics.median(r["usec"] for r in rows), "all": [r["usec"] for r in rows],
                              "ticks": statistics.median(r["ticks"] for r in rows), "extra": rows[-1]["extra"],
                              "check": checks[0] if len(checks) == 1 else "unstable:" + ",".join(checks)}
    return out


def load(path):
    """A result file, or a JSON saved by --save (its first config)."""
    data = open(path, "rb").read()
    if data[:1] == b"{":
        j = json.loads(data)
        if "results" in j:
            name = next(iter(j["results"]))
            return j["results"][name]
        return j
    return parse_file(data.decode("mac_roman"))


def extra(t, key):
    for f in t.get("extra", "").split():
        if f.startswith(key + "="):
            return f[len(key) + 1:]
    return None


def embench_score(res, baseline):
    logs = []
    for name, t in res["tests"].items():
        if not name.startswith("emb_") or "usec" not in t or t["check"] != "ok":
            continue
        b, gsf = baseline.get(name[4:]), extra(t, "gsf")
        if b and gsf and t["usec"]:
            logs.append(math.log(b / (t["usec"] / 1000) * int(gsf)))
    if not logs:
        return None
    mean = sum(logs) / len(logs)
    sd = math.sqrt(sum((x - mean) ** 2 for x in logs) / len(logs))
    return math.exp(mean), math.exp(sd), len(logs)


def family_of(test):
    if "@" in test:
        return "depth sweep"
    for prefix, label in FAMILIES:
        if test.startswith(prefix):
            return label
    return "other"


def ap_common(ap):
    ap.add_argument("--only", default="")
    ap.add_argument("--save")
    ap.add_argument("--compare")
    ap.add_argument("--reference")
    ap.add_argument("--emit-header")
    ap.add_argument("--mhz", type=float)


# ---- booting a desktop Basilisk II ----

def boot_all(args):
    tools = os.environ.get("RETRO68_BIN", os.path.expanduser("~/github-repositories/Retro68-build/toolchain/bin"))
    app = os.environ.get("OKAPIABENCH_BIN", os.path.join(root, "build", "okapiabench", "OkapiaBench.bin"))
    configs = []
    for a in args.configs or ["interp=jit false", "jit=jit true"]:
        name, rest = a.split("=", 1)
        binary, prefs = rest.split("|", 1) if "|" in rest else (args.binary, rest)
        configs.append((name, os.path.abspath(binary), prefs))
    os.makedirs(args.work, exist_ok=True)

    def sh(*cmd):
        return subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode

    def boot(tag, binary, extra_prefs):
        disk = os.path.join(args.work, tag + ".dsk")
        share = os.path.join(args.work, tag + ".share")
        shutil.copyfile(args.image, disk)
        shutil.rmtree(share, ignore_errors=True); os.makedirs(share)
        sh(os.path.join(tools, "hmount"), disk)
        # Startup Items, as an English or a French System names it
        for folder in (b":System Folder:Startup Items:", ":Dossier Système:Ouverture au démarrage:".encode("mac_roman")):
            if subprocess.run([os.path.join(tools, "hcopy").encode(), b"-m", app.encode(), folder],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
                break
        else:
            sh(os.path.join(tools, "humount")); sys.exit("no Startup Items folder on " + args.image)
        sh(os.path.join(tools, "humount"))
        prefs = os.path.join(args.work, tag + ".prefs")
        with open(prefs, "w") as f:
            f.write(f"rom {os.path.abspath(args.rom)}\ndisk {disk}\nextfs {share}\nramsize 67108864\nscreen win/640/480\n"
                    "displaycolordepth 8\nmodelid 14\ncpu 4\nfpu true\nnocdrom true\nnosound true\nnogui true\nidlewait true\n"
                    + extra_prefs.replace(";", "\n") + "\n")
        env = dict(os.environ, SDL_AUDIODRIVER="dummy", HOME=args.work)
        log = open(os.path.join(args.work, tag + ".log"), "w")
        p = subprocess.Popen([binary, "--config", prefs], env=env, stdout=subprocess.DEVNULL, stderr=log)
        t0 = time.time(); text = None
        try:
            while time.time() - t0 < float(os.environ.get("OKAPIABENCH_TIMEOUT", "1800")):
                time.sleep(0.5)
                if p.poll() is not None:
                    break
                for n in os.listdir(share):
                    if n.startswith("OkapiaBench "):
                        data = open(os.path.join(share, n), "rb").read().decode("mac_roman")
                        if "\rEND\r" in data or data.endswith("END\r"):
                            text = data
                if text:
                    break
        finally:
            p.kill(); p.wait()
        return text, time.time() - t0

    results = {}
    for name, binary, extra_prefs in configs:
        runs = []
        for r in range(args.repeat):
            text, wall = boot(f"{name}.{r}", binary, extra_prefs)
            if text is None:
                print(f"{name} run {r + 1}: no result after {wall:.0f} s", flush=True); continue
            runs.append(parse_file(text))
            total = sum(t.get("usec", 0) for t in runs[-1]["tests"].values()) / 1e6
            print(f"{name} run {r + 1}: {len(runs[-1]['tests'])} tests, {total:.1f} s of tests, {wall:.1f} s wall", flush=True)
        if runs:
            results[name] = merge(runs)
    return results, [{"name": n, "binary": b, "prefs": p} for n, b, p in configs]


# ---- the table ----

def table(results, args):
    names = list(results)
    if not names:
        sys.exit("no results")
    baseline = {}
    if args.compare:
        baseline = json.load(open(args.compare))["results"]
    reference = load(args.reference) if args.reference else None
    speed = os.path.join(root, "external", "embench-iot", "baseline-data", "speed.json")
    emb_baseline = json.load(open(speed)) if os.path.exists(speed) else {}

    for n in names:
        h = results[n]["header"]
        print(f"{n}: code {h.get('code', '?')} on {h.get('sysarch', '?')}, machine {h.get('machine', '?')}, "
              f"cpu {h.get('cpu', '?')}{' cput ' + h['cput'] if 'cput' in h and h.get('sysarch') == 'ppc' else ''}, "
              f"System {h.get('system', '?')[-4:]}, screen {h.get('screen', '?')}, {h.get('date', '?')}")

    tests = []
    for n in names:
        for t in results[n]["tests"]:
            if t.startswith(args.only) and t not in tests:
                tests.append(t)
    w = max([16] + [len(t) + 1 for t in tests])
    cols = [f"{n[:11]:>12s}" for n in names] + [f"{'x ' + n[:9]:>12s}" for n in names[1:]]
    cols += [f"{'vs saved ' + n:>16s}" for n in names if n in baseline]
    cols += [f"{'score ' + n[:6]:>13s}" for n in names] if reference else []
    print(f"\n{'test (ms)':{w}s}" + "".join(cols) + "  same work")
    clock_warnings = []
    for test in tests:
        row = [results[n]["tests"].get(test, {}) for n in names]
        ms = [r.get("usec", 0) / 1000 for r in row]
        line = f"{test:{w}s}" + "".join(f"{m:12.1f}" if m else f"{('skip' if 'skip' in r else '-'):>12s}" for m, r in zip(ms, row))
        line += "".join(f"{ms[0] / m:12.2f}" if m and ms[0] else f"{'-':>12s}" for m in ms[1:])
        for n, m in zip(names, ms):
            if n in baseline:
                old = baseline[n].get("tests", baseline[n]).get(test, {}).get("usec", 0) / 1000
                line += f"{old / m:16.2f}" if m and old else f"{'-':>16s}"
        if reference:
            ref = reference["tests"].get(test, {}).get("usec", 0) / 1000
            line += "".join(f"{ref / m:13.2f}" if m and ref else f"{'-':>13s}" for m in ms)
        checks = {r.get("check", "?") for r in row if "usec" in r}
        print(line + ("  yes" if len(checks) <= 1 else "  NO  " + " ".join(f"{n}:{r.get('check', '?')}" for n, r in zip(names, row))))
        for n, r in zip(names, row):
            # 2 %, beyond the tick and a half either clock may lose to quantisation
            if r.get("ticks", 0) > 30 and abs(r["usec"] - r["ticks"] * TICK_USEC) > 0.02 * r["usec"] + 1.5 * TICK_USEC:
                clock_warnings.append(f"{n} {test}: {r['usec'] / 1000:.0f} ms by Microseconds, {r['ticks'] * TICK_USEC / 1000:.0f} ms by ticks")

    print()
    for label in [f for _, f in FAMILIES] + ["depth sweep"]:
        totals = []
        for n in names:
            ts = [t for k, t in results[n]["tests"].items() if family_of(k) == label and "usec" in t and k.startswith(args.only)]
            totals.append(sum(t["usec"] for t in ts) / 1e6 if ts else None)
        if not any(totals):
            continue
        line = f"{label + ' (s)':{w}s}" + "".join(f"{t:12.2f}" if t else f"{'-':>12s}" for t in totals)
        if reference:
            for n in names:
                logs = [math.log(reference["tests"][k]["usec"] / t["usec"]) for k, t in results[n]["tests"].items()
                        if family_of(k) == label and t.get("usec") and reference["tests"].get(k, {}).get("usec")]
                line += f"  {n}: score {math.exp(sum(logs) / len(logs)):.2f} over {len(logs)}" if logs else ""
        print(line)
    if reference:
        for n in names:
            common = [k for k, t in results[n]["tests"].items() if t.get("usec") and reference["tests"].get(k, {}).get("usec")]
            if common:
                g = math.exp(sum(math.log(reference["tests"][k]["usec"] / results[n]["tests"][k]["usec"]) for k in common) / len(common))
                print(f"overall {n}: {g:.2f} against the reference, over {len(common)} tests in common")
    print()
    for n in names:
        cm = results[n]["tests"].get("cpu_coremark")
        if cm and "usec" in cm:
            print(f"CoreMark {n}: {extra(cm, 'it/s') or '?'} iterations/s"
                  f"{'' if cm['check'] == 'ok' else ' (NOT validated)'}"
                  + (f", {float(extra(cm, 'it/s')) / args.mhz:.2f} /MHz" if args.mhz and extra(cm, 'it/s') else ""))
        e = embench_score(results[n], emb_baseline)
        if e:
            print(f"Embench-IoT {n}: speed {e[0]:.2f} (geometric sd {e[1]:.2f}, {e[2]} programs)"
                  + (f", {e[0] / args.mhz:.3f} /MHz" if args.mhz else ""))
    for wmsg in clock_warnings:
        print("clock:", wmsg)
    return names


def emit_header(path, res):
    with open(path, "w") as f:
        f.write("/* written by tools/okapiabench/run.py --emit-header: a reference for the recap */\n")
        f.write("static const RefTime reference[] = {\n")
        for k, t in res["tests"].items():
            if t.get("usec"):
                f.write(f'\t{{ "{k}", {int(t["usec"])} }},\n')
        f.write("};\n")
    print("reference header:", path)


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "report":
        ap = argparse.ArgumentParser(prog="run.py report", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
        ap.add_argument("files", nargs="+"); ap_common(ap)
        args = ap.parse_args(sys.argv[2:])
        results, configs = {}, []
        for p in args.files:
            name = os.path.basename(p)
            name = name[len("OkapiaBench "):] if name.startswith("OkapiaBench ") else name
            results[name] = load(p)
    else:
        ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
        ap.add_argument("binary"); ap.add_argument("rom"); ap.add_argument("image"); ap.add_argument("work")
        ap.add_argument("configs", nargs="*")
        ap.add_argument("--repeat", type=int, default=1)
        ap_common(ap)
        args = ap.parse_args()
        results, configs = boot_all(args)
    if args.save:
        with open(args.save, "w") as f:
            json.dump({"configs": configs, "results": results}, f, indent=1)
    table(results, args)
    if args.emit_header:
        emit_header(args.emit_header, load(args.reference) if args.reference else results[next(iter(results))])


if __name__ == "__main__":
    main()
