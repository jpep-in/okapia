#!/usr/bin/env bash
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
# Log the Pi 4's UART to a file, from a USB serial adapter, until stopped (Ctrl-C).
#   usage: serial_capture.sh /dev/cu.usbserial-XXXX out.log
#
# 115200 8N1, raw. On the Pi the core's stderr diagnostics (JIT_DIAG, RTE_BAD, M68K_SETPC_BAD) and
# Okapia's logger share this port, so lines interleave; read counters with diag_totals.py, not by eye.
# Raw logs contain the Wi-Fi network name and access-point addresses: do not commit them.
set -euo pipefail
DEV="${1:?device, e.g. /dev/cu.usbserial-BG040NV7}"
OUT="${2:?output file}"
stty -f "$DEV" 115200 cs8 -cstopb -parenb raw -echo
: > "$OUT"
exec cat "$DEV" >> "$OUT"
