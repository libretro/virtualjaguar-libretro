#!/usr/bin/env python3
"""contention_model.py -- timing-campaign S2 arithmetic (docs/timing-campaign-plan.md S2).

Reads a bus_census_probe per-halfline CSV (+ its frames CSV) and multiplies
the MEASURED traffic by a FIXED latency function (written down before the
numbers were looked at; see the S2 section of the plan for the provenance of
every term).  Nothing is fitted: each term has a low / central / high value
from its evidence, and the script reports all three.

Per-access expected wait for a master M arriving uniformly in a video line
of T sysclks (re-arbitration at every memory-cycle ack, JTRM v8 p.36):

  OP + refresh  (JTRM pri 6 and 2 beat GPU-normal 9 and DSP-normal 7; RELEASE
                 clear on every Doom object => OP holds the bus for its whole
                 per-line burst B; refresh is grouped after the OP's STOP,
                 JTRM p.24):                      w = B^2 / (2T)
                 low: per-object bursts (gaps between objects)
  DSP           (pri 7 beats GPU 9): residual of an in-flight DSP transfer
                                                 w = U_dsp * L_dsp / 2
  blitter       (normal pri 10 loses to GPU 9 / DSP 7 at the next ack):
                                                 w = U_blit * c / 2
  68K handoff   (pri 11, default owner; ARB.NET Idle->Request->Hold waits for
                 the 68000's BG): only when the bus is parked on the 68K:
                                                 w = P_idle * H
  GPU in flight (DSP only; GPU 9 loses to DSP 7 at the next ack):
                                                 w = U_gpu * D / 2
68K work stretch: the 68K (lowest priority, ~80% bus duty) loses every
cycle another master holds:   extra = f68 * (duty * U_others + n_acq * r)
Blit stretch: a blit is suspended while OP/DSP/GPU/refresh hold the bus.

Usage: contention_model.py census.csv frames.csv [--scale-gpu-spin-u U]
"""
import csv
import sys

FIELD = 442717.0           # sysclks per NTSC field (26.590906 MHz / 60.0544)

# ---- the fixed latency function: (low, central, high) ----------------------
P = {
    # 68K default-owner handoff, sysclks (gpu-timing-spec R19: ARB.NET:33-45
    # state machine + 68000 BR->BG/AS timing; bound 2..12, not simulated)
    "H":        (2.0, 6.0, 12.0),
    # 68K resync after each TOM-side tenure (BG negation -> next 68K cycle)
    "r68":      (0.0, 2.0, 4.0),
    # sysclks per refresh cycle: JTRM MEMCON1 table (DRAMSPEED 3) = 3; the
    # MEM.NET CBR walk (spec section 8) reads ~9 states
    "ref_clk":  (3.0, 3.0, 9.0),
    # blitter memory cycle (page hit 2 .. page miss 2+3)
    "blit_c":   (2.0, 3.5, 5.0),
    # DSP occupancy multiplier on its idle-bus memory price (JERRY's 16-bit
    # bus-interface hold time beyond the TOM memory cycle is unpinned)
    "dsp_hold": (1.0, 1.0, 2.0),
    # DSP burst length in transactions (a 32-bit access = 2 x 16-bit, the
    # second follows back-to-back and keeps winning against GPU-normal)
    "dsp_burst": (2.0, 2.0, 1.0),
    # hardware-rate bus utilisation of the GPU's main-RAM spin polls while
    # spinning (dl_adopt: 4 external loads/iteration at L = 7+D each plus
    # handoff; the emulated poll rate is unphysical and is NOT used)
    "u_gspin":  (0.25, 0.40, 0.60),
    # 68K bus duty during its work (bus cycles x 8 sysclks / clocks) --
    # measured per line from the census, clamped to 1
}
T = FIELD / 262.0          # one video line (2 halflines)
REF_PERIOD = 64.0 * (5 + 1)  # CLK / (64*(REFRATE+1)), REFRATE=5 measured


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    rows = csv.DictReader(open(sys.argv[1]))
    flips = sum(int(r["flip"]) for r in csv.DictReader(open(sys.argv[2])))

    # aggregate halflines into video lines (vc & 0x7FF) >> 1, in order
    lines = []
    cur = None
    key = None
    for r in rows:
        k = (int(r["field"]), (int(r["vc"]) & 0x7FF) >> 1)
        if k != key:
            if cur:
                lines.append(cur)
            cur = {c: 0.0 for c in r if c not in ("field", "hl", "vc")}
            key = k
        for c in cur:
            cur[c] += float(r[c])
    if cur:
        lines.append(cur)
    fields = len(lines) / 262.0

    out = {}
    for which, idx in (("low", 0), ("central", 1), ("high", 2)):
        H = P["H"][idx]; r68 = P["r68"][idx]; rc = P["ref_clk"][idx]
        bc = P["blit_c"][idx]; dh = P["dsp_hold"][idx]; db = P["dsp_burst"][idx]
        ug = P["u_gspin"][idx]
        tot = dict(gpu_wait=0.0, gpu_op=0.0, gpu_dsp=0.0, gpu_blit=0.0, gpu_ho=0.0,
                   dsp_wait=0.0, m68k_stretch=0.0, blit_stretch=0.0,
                   gpu_work_acc=0.0, dsp_acc=0.0, blit_occ=0.0, op_occ=0.0,
                   ref_occ=0.0, m68k_work=0.0)
        blit_debt = 0.0
        for L in lines:
            ref = (T / REF_PERIOD) * rc
            B = L["op_clk"] + ref
            # blit occupancy: emulated blits complete instantly; carry the
            # bus time forward at full bus rate in the time left by OP/refresh
            blit_debt += (L["blit_prd"] + L["blit_pwr"]) * bc
            room = max(0.0, T - B)
            ob = min(blit_debt, room)
            blit_debt -= ob
            Ub = ob / T
            dsp_occ = L["dsp_mem"] * dh
            Ud = dsp_occ / T
            gpu_cyc = max(1.0, L["gpu_cyc_total"])
            f_spin = max(0.0, (L["gpu_cyc_total"] - L["gpu_cyc_work"]) / gpu_cyc)
            gw_occ = L["gpu_mem_work"]
            Ugs = f_spin * ug
            Ug = gw_occ / T + Ugs
            U_tom = min(1.0, (B + dsp_occ + ob + gw_occ) / T + Ugs)
            p_idle = 1.0 - U_tom
            # OP wait: contiguous burst (central/high) or per-object (low)
            if which == "low" and L["op_objs"] > 0:
                bo = L["op_clk"] / L["op_objs"]
                w_op = (L["op_objs"] * bo * bo + ref * ref) / (2 * T)
            else:
                w_op = B * B / (2 * T)
            w_dsp = Ud * (db * L["dsp_mem"] / max(1.0, L["dsp_ext"]) * dh) / 2.0 if L["dsp_ext"] else 0.0
            w_blit = Ub * bc / 2.0
            w_ho = p_idle * H
            g = L["gpu_ext_work"]
            tot["gpu_op"] += g * w_op
            tot["gpu_dsp"] += g * w_dsp
            tot["gpu_blit"] += g * w_blit
            tot["gpu_ho"] += g * w_ho
            tot["gpu_wait"] += g * (w_op + w_dsp + w_blit + w_ho)
            # DSP accesses (bursts of db transactions pay one wait)
            nd = L["dsp_ext"] / db
            dmean = L["gpu_mem_work"] / max(1.0, g) if g else 3.3
            w_gin = Ug * dmean / 2.0
            tot["dsp_wait"] += nd * (w_op + w_gin + w_blit + w_ho)
            # 68K work stretch
            f68 = min(1.0, L["m68k_clk_work"] / T)
            duty = min(1.0, L["m68k_bus_work"] * 8.0 / max(1.0, L["m68k_clk_work"]))
            occ_oth = min(1.0, (B + dsp_occ + ob + gw_occ) / T + Ugs)
            n_acq = g + nd + L["op_objs"] + (1 if ob > 0 else 0)
            tot["m68k_stretch"] += f68 * (duty * occ_oth * T + n_acq * r68)
            # blit stretch by higher-priority holders in the same line
            tot["blit_stretch"] += ob * min(1.0, (B + dsp_occ + gw_occ) / T)
            tot["gpu_work_acc"] += g
            tot["dsp_acc"] += L["dsp_ext"]
            tot["blit_occ"] += ob
            tot["op_occ"] += L["op_clk"]
            tot["ref_occ"] += ref
            tot["m68k_work"] += L["m68k_clk_work"]
        out[which] = tot

    print("lines=%d fields=%.1f flips=%d fields/flip=%.3f" % (len(lines), fields, flips, fields / flips))
    c = out["central"]
    print("traffic per field: GPU work ext %.0f, DSP 16-bit %.0f, OP occ %.0f sysclks, "
          "refresh %.0f, blit occ (central c) %.0f, 68K work %.0f sysclks" % (
              c["gpu_work_acc"] / fields, c["dsp_acc"] / fields, c["op_occ"] / fields,
              c["ref_occ"] / fields, c["blit_occ"] / fields, c["m68k_work"] / fields))
    hdr = "%-28s %10s %10s %10s" % ("term (fields per flip)", "low", "central", "high")
    print(hdr)
    for k, label in (("gpu_op", "GPU work: OP+refresh wait"), ("gpu_dsp", "GPU work: DSP in flight"),
                     ("gpu_blit", "GPU work: blit cycle resid."), ("gpu_ho", "GPU work: 68K handoff"),
                     ("gpu_wait", "GPU work: total"), ("blit_stretch", "blit stretch"),
                     ("dsp_wait", "DSP: total wait"), ("m68k_stretch", "68K work stretch")):
        print("%-28s %10.3f %10.3f %10.3f" % (label, out["low"][k] / FIELD / flips,
              out["central"][k] / FIELD / flips, out["high"][k] / FIELD / flips))
    for which in ("low", "central", "high"):
        t = out[which]
        per = t["gpu_wait"] / max(1.0, t["gpu_work_acc"])
        perd = t["dsp_wait"] / max(1.0, t["dsp_acc"])
        g_chain = (t["gpu_wait"] + t["blit_stretch"]) / FIELD / flips
        d_chain = (t["dsp_wait"] + t["m68k_stretch"]) / FIELD / flips
        print("CHAIN %-7s gpu_wait/acc=%.2f dsp_wait/16b=%.2f  GPU-chain +%.3f  DSP/68K-chain +%.3f  "
              "serialized-sum +%.3f fields/flip" % (which, per, perd, g_chain, d_chain, g_chain + d_chain))
    return 0


if __name__ == "__main__":
    sys.exit(main())
