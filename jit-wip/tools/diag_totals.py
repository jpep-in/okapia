#!/usr/bin/env python3
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
"""Totals of the macemu-jit core's own counters from a serial log.

The kernel must be built with OKAPIA_JIT_DIAG=1: the core then prints a "JIT_DIAG t=..s dispatch=.. compile=.."
line every few seconds on stderr, which on the Pi 4 shares the serial port with Okapia's own logger, so lines
arrive interleaved and truncated. This takes, for every counter, the largest value seen anywhere in the log
(they only grow) rather than trusting any one line.

    diag_totals.py serial.log

Counters: dispatch (entries into the dispatcher), exec_normal, compile (compile_block calls), fresh (first
compilations), recomp (compilations of a block that already existed), opt0 / opt>0 (blocks emitted at level 0,
the "interpreter dispatch" kind, or above), exec_nostats, cache_miss, recompile_block (countdown expiries),
check_checksum, good, bad, flush_hard (whole-cache flushes, i.e. a full translation cache).
"""
import re
import sys

KEYS = ['t', 'dispatch', 'exec_normal', 'cache_hit', 'compile', 'fresh', 'recomp', 'opt0', 'opt>0', 'do_nothing',
        'exec_nostats', 'cache_miss', 'recompile_block', 'check_checksum', 'good', 'bad', 'flush_hard']

def main(path):
    text = open(path, errors='replace').read().replace('\r', '')
    best = {}
    avg = []
    for seg in re.split(r'(?=JIT_DIAG t=)', text):
        if not seg.startswith('JIT_DIAG t='):
            continue
        head = seg[:900]
        for k in KEYS:
            m = re.search(re.escape(k) + r'=(\d+)', head[:700])
            if m:
                best[k] = max(best.get(k, 0), int(m.group(1)))
        m = re.search(r'avg_block=([\d.]+) insn avg_code=([\d.]+)B code_per_insn=([\d.]+)B', head)
        if m:
            avg.append(m.groups())
    print({k: best[k] for k in KEYS if k in best})
    if avg:
        print('avg_block (insn) / avg_code (B) / code_per_insn (B), last row:', avg[-1])

if __name__ == '__main__':
    main(sys.argv[1])
