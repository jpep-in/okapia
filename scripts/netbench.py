#!/usr/bin/env python3
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
"""The other end of Okapia's network bench (src/hosts/circle/net_bench_circle.cpp).

    usage: netbench.py [--seconds N] [--rate MBITS] [--port 9000]

Set `netbench <this machine's address>` in the card's preferences, start the
board, and run this: the board waits up to two minutes for it at power-on,
before the window and the Macintosh. It then sends for N seconds (counted
here), and receives for N seconds (counted there), and both ends print what
they saw. --rate caps what this end sends, to find where the board starts to
lose frames; without it, as fast as this machine can.
"""
import argparse
import errno
import socket
import struct
import sys
import time

PAYLOAD = 1472


def mbits(nbytes, seconds):
    return nbytes * 8 / seconds / 1e6 if seconds > 0 else 0.0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--seconds", type=int, default=10)
    parser.add_argument("--rate", type=float, default=0.0, help="Mbit/s this end sends; 0 for no cap")
    parser.add_argument("--port", type=int, default=9000)
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 * 1024 * 1024)
    sock.bind(("0.0.0.0", args.port))
    print("waiting for the board on UDP %d…" % args.port, flush=True)

    while True:
        data, board = sock.recvfrom(2048)
        if data.startswith(b"OKBN HELLO"):
            break
    print("board at %s:%d" % board, flush=True)

    # The board sends.
    sock.sendto(b"OKBN TX %d" % args.seconds, board)
    frames = nbytes = 0
    first = last = None
    highest = -1
    done = None
    sock.settimeout(args.seconds + 10)
    while done is None:
        try:
            data, sender = sock.recvfrom(2048)
        except socket.timeout:
            break
        if sender != board:
            continue
        if data.startswith(b"OKBD"):
            now = time.monotonic()
            first = first if first is not None else now
            last = now
            frames += 1
            nbytes += len(data)
            highest = max(highest, struct.unpack(">I", data[4:8])[0])
        elif data.startswith(b"OKBN TX"):
            continue
        elif data.startswith(b"OKBN DONE"):
            done = [int(x) for x in data.split()[2:5]]
    span = (last - first) if first is not None and last is not None else 0.0
    sent = done[0] if done else highest + 1
    print("board -> here: %d of %d frames, %.1f Mbit/s received, %d lost%s"
          % (frames, sent, mbits(nbytes, span), max(sent - frames, 0),
             ("; the board says it sent at %.1f Mbit/s" % mbits(done[1], done[2] / 1e6)) if done else ""),
          flush=True)

    # The board receives.
    sock.sendto(b"OKBN RX %d" % args.seconds, board)
    payload = bytearray(PAYLOAD)
    payload[0:4] = b"OKBD"
    seq = 0
    refused = 0
    start = time.monotonic()
    end = start + args.seconds
    interval = PAYLOAD * 8 / (args.rate * 1e6) if args.rate > 0 else 0.0
    next_at = start
    while True:
        now = time.monotonic()
        if now >= end:
            break
        if interval and now < next_at:
            continue
        struct.pack_into(">I", payload, 4, seq)
        try:
            sock.sendto(payload, board)
            seq += 1
            next_at += interval
        except OSError as e:
            if e.errno in (errno.ENOBUFS, errno.EAGAIN):
                refused += 1
                continue
            raise
    elapsed = time.monotonic() - start
    print("here -> board: %d frames sent, %.1f Mbit/s offered%s"
          % (seq, mbits(seq * PAYLOAD, elapsed),
             ", %d refused by this machine's stack" % refused if refused else ""), flush=True)

    sock.settimeout(0.3)
    result = None
    for _ in range(20):
        # The send buffer is still full of the last datagrams: wait for room
        # rather than die with the result one message away.
        try:
            sock.sendto(b"OKBN END %d" % seq, board)
        except OSError as e:
            if e.errno not in (errno.ENOBUFS, errno.EAGAIN):
                raise
            time.sleep(0.05)
            continue
        try:
            while True:
                data, sender = sock.recvfrom(2048)
                if sender == board and data.startswith(b"OKBN RESULT"):
                    result = [int(x) for x in data.split()[2:5]]
                    break
        except socket.timeout:
            pass
        if result:
            break
    if result:
        print("board received %d of %d frames, %.1f Mbit/s, %d lost"
              % (result[0], seq, mbits(result[1], result[2] / 1e6), max(seq - result[0], 0)))
    else:
        print("no result from the board", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
