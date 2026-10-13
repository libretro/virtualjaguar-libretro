#!/usr/bin/env python3
"""Analyse a netlink_sim --btrace file (see netlink_sim_clockpatch.py for the
event kinds).  Three reports:

  cause FILE.bt FILE.trace ONE_WAY_MS [START_US]
      Where each one-way hop spends its time beyond the network delay,
      classified by what the RECEIVING instance was doing when the packet
      was due: blocked in JLinkAwaitReply (wake-up lag), emulating between
      waits (budget spent / pre-poll work), or between frames (dead window).
      Only meaningful for a fixed-delay link (no jitter/loss).

  tic FILE.bt [START_EVENT]
      Doom 2P message structure and predictability.  The link is a strict
      ping-pong; one pair = (byte from instance 0, reply byte from instance
      1) and a tic is six pairs ending in the (2, 2) marker.  For every pair
      position and each direction: distinct values, hit rate of the
      "same as last tic" predictor, "last + 1", and "equals a byte the
      receiver itself sent in the same tic" (best position).  Also the
      joint per-tic hit rate for the receiver with the best predictor per
      position.

  stall FILE.bt [START_US]
      Link-idle gaps (time between consecutive transmitted bytes): p50,
      p99, max, and how much of the run was spent in gaps > 50 ms.

Not part of `make test`.  Stdlib only."""
import bisect
import statistics
import sys


def load(path):
    ev = []
    for line in open(path):
        i, t, k, v = line.split()
        ev.append((int(t), int(i), int(k), int(v)))
    ev.sort(key=lambda e: e[0])
    return ev


def report_cause(bt, trace, one_way_ms, start):
    ev = load(bt)
    d = int(float(one_way_ms) * 1000)
    frames = {0: [], 1: []}
    for line in open(trace):
        x = line.split()
        frames[int(x[0])].append((int(x[2]), int(x[3])))
    waits = {0: [], 1: []}
    open_wait = {}
    for t, i, k, v in ev:
        if k == 5:
            open_wait[i] = t
        elif k == 6 and i in open_wait:
            waits[i].append((open_wait.pop(i), t))

    def inside(iv, t):
        j = bisect.bisect_right([a for a, b in iv], t) - 1
        return j >= 0 and iv[j][0] <= t < iv[j][1]

    tx = {0: [], 1: []}
    arr = {0: [], 1: []}
    for t, i, k, v in ev:
        if k == 1:
            tx[i].append(t)
        elif k == 2:
            arr[i].append(t)
    rows = []
    by_sender = {0: [], 1: []}
    for s in (0, 1):
        for t_tx, t_arr in zip(tx[s], arr[1 - s]):
            if t_tx < start:
                continue
            due = t_tx + d
            if inside(waits[1 - s], due):
                cls = 'peer blocked in wait (wake-up lag)'
            elif inside(frames[1 - s], due):
                cls = 'peer emulating (budget spent / pre-poll work)'
            else:
                cls = 'peer between frames (dead window)'
            rows.append((cls, t_arr - due))
            by_sender[s].append(t_arr - due)
    n = len(rows)
    tot = sum(e for c, e in rows)
    print("one-way hops=%d mean=%.0fus = network %dus + ours %.0fus"
          % (n, d + tot / n, d, tot / n))
    by = {}
    for c, e in rows:
        by.setdefault(c, []).append(e)
    for c, v in sorted(by.items()):
        print("  %-48s share=%5.1f%% mean=%6.0fus -> %5.0fus/hop"
              % (c, 100.0 * len(v) / n, statistics.mean(v), sum(v) / n))
    for s in (0, 1):
        v = by_sender[s]
        if v:
            print("  hops sent by instance %d (%s): mean extra %.0fus"
                  % (s, "master -> slave" if s == 0 else "slave -> master",
                     statistics.mean(v)))


def report_tic(bt, skip):
    ev = [e for e in load(bt) if e[2] == 1]
    ev = ev[skip:]
    while ev and ev[0][1] != 0:
        ev = ev[1:]
    pairs = [(a[3], b[3]) for a, b in zip(ev[0::2], ev[1::2])
             if a[1] == 0 and b[1] == 1]
    tics, cur = [], []
    for p in pairs:
        cur.append(p)
        if p == (2, 2):
            if len(cur) == 6:
                tics.append(cur)
            cur = []
    n = len(tics)
    print("tics=%d (6 pairs each = 12 one-way hops)" % n)

    def rate(fn):
        return 100.0 * sum(1 for t in range(2, n) if fn(t)) / (n - 2)

    joint = {0: [True] * n, 1: [True] * n}
    for who, idx, label in ((0, 0, "instance 0 -> 1 (received by 1)"),
                            (1, 1, "instance 1 -> 0 (received by 0)")):
        print(label)
        for k in range(6):
            def col(t, k=k, idx=idx):
                return tics[t][k][idx]
            same = rate(lambda t: col(t) == col(t - 1))
            inc = rate(lambda t: col(t) == ((col(t - 1) + 1) & 255))
            mine = [rate(lambda t, j=j: col(t) == tics[t][j][1 - idx])
                    for j in range(6)]
            best = max(mine)
            print("  pair%d distinct=%3d same-as-last=%5.1f%% last+1=%5.1f%% "
                  "= own byte in pair %d: %5.1f%%"
                  % (k, len(set(col(t) for t in range(n))), same, inc,
                     mine.index(best), best))
            use_own = best > same
            j = mine.index(best)
            for t in range(2, n):
                ok = (col(t) == tics[t][j][1 - idx]) if use_own \
                    else (col(t) == col(t - 1))
                joint[who][t] = joint[who][t] and ok
    for who in (0, 1):
        h = sum(1 for t in range(2, n) if joint[who][t])
        print("receiver %d: every byte of the tic predicted by the best simple "
              "predictor per position: %.1f%% of tics" % (1 - who, 100.0 * h / (n - 2)))


def report_stall(bt, start):
    tx = [e[0] for e in load(bt) if e[2] == 1 and e[0] >= start]
    g = [b - a for a, b in zip(tx, tx[1:])]
    span = (tx[-1] - tx[0]) / 1e6
    sg = sorted(g)
    big = [x for x in g if x > 50000]
    print("link-idle gaps: n=%d p50=%.1fms p99=%.1fms max=%.0fms | >50ms: %d "
          "(%.2f/s) | time in gaps>50ms: %.1f%% of %.0fs"
          % (len(g), sg[len(g) // 2] / 1000, sg[int(len(g) * .99)] / 1000,
             max(g) / 1000, len(big), len(big) / span,
             100.0 * sum(big) / 1e6 / span, span))


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == 'cause':
        report_cause(argv[2], argv[3], argv[4], int(argv[5]) if len(argv) > 5 else 14000000)
    elif cmd == 'tic':
        report_tic(argv[2], int(argv[3]) if len(argv) > 3 else 900)
    elif cmd == 'stall':
        report_stall(argv[2], int(argv[3]) if len(argv) > 3 else 14000000)
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
