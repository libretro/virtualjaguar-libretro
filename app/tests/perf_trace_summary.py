#!/usr/bin/env python3
# VJ_PERF_TRACE summary (#967 evenness): app/src/perf_trace.h writes <prefix>-emu.csv and <prefix>-ui.csv.
#
#   python3 app/tests/perf_trace_summary.py <prefix>...
#
# Per run: repeats and skips of the frames really shown (serial deltas between takes, after the 120 warm-up
# iterations), bursts (events more than 1 s apart), the emulation thread's wake-up lateness and frame cost, the
# spread of frame end - due (what makes a crossing of the take point ambiguous), where the take sits after the
# vblank (swap return), and how far the trim alone moved the grid's phase.
import csv, sys, statistics as st
def pct(v, p):
    v = sorted(v); return v[min(len(v)-1, int(p*len(v)))] if v else float('nan')
for pre in sys.argv[1:]:
    emu = list(csv.DictReader(open(pre + '-emu.csv')))
    ui = list(csv.DictReader(open(pre + '-ui.csv')))
    ui = ui[120:]   # perf warm-up
    t0 = int(ui[0]['take_ns']); t1 = int(ui[-1]['take_ns'])
    emu = [e for e in emu if t0 - 50e6 <= int(e['start_ns']) <= t1]
    # shown sequence: serial deltas between consecutive takes
    ser = [int(u['serial']) for u in ui]
    d = [b - a for a, b in zip(ser, ser[1:]) if a and b]
    rep = sum(1 for x in d if x == 0); skp = sum(x - 1 for x in d if x > 1)
    late = [(int(e['start_ns']) - int(e['due_ns'])) / 1e6 for e in emu]
    cost = [(int(e['end_ns']) - int(e['start_ns'])) / 1e6 for e in emu]
    fin = [(int(e['end_ns']) - int(e['due_ns'])) / 1e6 for e in emu]
    trim = [float(e['trim']) for e in emu]
    # take relative to the swap return before it (vblank proxy)
    tk = [(int(b['take_ns']) - int(a['swap1_ns'])) / 1e6 for a, b in zip(ui, ui[1:])]
    # trim phase: integral of (trim - 1) * period
    ph = 0; lo = hi = 0
    for x in trim:
        ph += (x - 1) * 16.6516; lo = min(lo, ph); hi = max(hi, ph)
    # bursts: events separated by > 1 s
    ev_t = [int(ui[i+1]['take_ns']) for i, x in enumerate(d) if x != 1]
    bursts = 0; last = None
    for t in ev_t:
        if last is None or t - last > 1e9: bursts += 1
        last = t
    print(f"{pre.split('/')[-1]:34s} shown-repeats {rep:3d} skips {skp:3d} bursts {bursts:2d} | "
          f"late p50 {pct(late,.5):.2f} p99 {pct(late,.99):.2f} max {max(late):.2f} | "
          f"cost p1 {pct(cost,.01):.2f} p50 {pct(cost,.5):.2f} p99 {pct(cost,.99):.2f} | "
          f"end-due p1-p99 spread {pct(fin,.99)-pct(fin,.01):.2f} ms | take-after-vblank p1 {pct(tk,.01):.2f} p50 {pct(tk,.5):.2f} p99 {pct(tk,.99):.2f} | "
          f"trim [{min(trim):.4f},{max(trim):.4f}] phase wander {hi-lo:.2f} ms")
