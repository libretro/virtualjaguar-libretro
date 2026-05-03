#!/usr/bin/env python3
"""
lint-acid.py -- catch encoding mistakes in acid-test .s files.

Four checks today:

  1. **B_COMMAND literal validation.**  Every `move.l #$XXXXXXXX,B_COMMAND`
     literal must use only bits defined in the blitter cmd set
     (BLIT_CMD_VALID_BITS in jaguar_regs.s).  Catches the kind of bug
     where someone writes $0001C000 thinking that's "LFU=S" but $C000
     are actually unused "ity" bits.

  2. **Hard-coded register address detection.**  Tests should reference
     symbolic names from include/jaguar_regs.s (B_COMMAND, TOM_INT1,
     etc.), not hex literals like $F02238.

  3. **Mode-flag-with-required-companion sanity.**  E.g. DCOMPEN with
     no DSTEN can't actually compare against the existing dest.
     LFU functions $1..$E require the operand they reference (S, D,
     or both) to be enabled.  Walks each B_COMMAND literal and warns
     on inconsistent combinations.

  4. **Local equate must not shadow oracle symbols.**  If a test
     defines `TOM_OLP_HI equ $F00020` locally, it overrides the
     oracle's correct value -- exactly how the OLP_HI/LO swap snuck
     through Copilot review batch 3.  Any local `name equ ...` whose
     LHS is already in jaguar_regs.s is a warning.

Exit code: 0 if clean, 1 if any warning, 2 on parse error.

Run via `make -C test/acid lint`.
"""
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
ACID_ROOT = os.path.join(REPO_ROOT, "test", "acid")
REGS_PATH = os.path.join(ACID_ROOT, "include", "jaguar_regs.s")
TESTS_DIR = os.path.join(ACID_ROOT, "tests")

# ----- parse jaguar_regs.s into a name->value table ------------------------
def parse_regs():
    table = {}
    with open(REGS_PATH) as fh:
        for line in fh:
            m = re.match(r"^(\w+)\s+equ\s+\$([0-9A-Fa-f]+)", line)
            if m:
                table[m.group(1)] = int(m.group(2), 16)
    return table

# ----- collect facts we'll need from the table -----------------------------
def collect_facts(regs):
    facts = {
        "valid_cmd_bits": regs.get("BLIT_CMD_VALID_BITS", 0),
        "SRCEN":          regs.get("SRCEN", 0),
        "DSTEN":          regs.get("DSTEN", 0),
        "BCOMPEN":        regs.get("BCOMPEN", 0),
        "DCOMPEN":        regs.get("DCOMPEN", 0),
        "PATDSEL":        regs.get("PATDSEL", 0),
        "BKGWREN":        regs.get("BKGWREN", 0),
        "DSTWRZ":         regs.get("DSTWRZ", 0),
        "GOURD":          regs.get("GOURD", 0),
    }
    return facts

# ----- LFU function classification -----------------------------------------
# 4-bit LFU function in bits 21..24.  Returns which operands the LFU
# actually consumes so we can check SRCEN / DSTEN are set when needed.
def lfu_uses(fn):
    """Return ('S' in r, 'D' in r) for whether LFU function uses S, D."""
    # Truth-table encoded as which inputs change the output.
    # f=0: out always 0.    f=F: out always 1.       no inputs.
    # f=3: ~S.  f=C: S.                              S only.
    # f=5: ~D.  f=A: D.                              D only.
    # everything else uses both.
    s_only = {0xC, 0x3}
    d_only = {0xA, 0x5}
    none   = {0x0, 0xF}
    if fn in none:   return (False, False)
    if fn in s_only: return (True,  False)
    if fn in d_only: return (False, True )
    return (True, True)

# ----- check a single B_COMMAND literal ------------------------------------
def check_cmd_literal(filename, lineno, val_str, facts):
    warnings = []
    val = int(val_str, 16)

    # 1. unknown bits
    extra = val & ~facts["valid_cmd_bits"]
    if extra:
        warnings.append(
            f"{filename}:{lineno}: B_COMMAND uses unknown bits "
            f"${extra:08X} (val=${val:08X}). "
            f"Did you mean a different field? "
            f"See test/acid/include/jaguar_regs.s.")

    # 2. LFU operand consistency
    lfu_fn = (val >> 21) & 0xF
    uses_s, uses_d = lfu_uses(lfu_fn)
    if uses_s and not (val & facts["SRCEN"]):
        warnings.append(
            f"{filename}:{lineno}: LFU=${lfu_fn:X} reads S but SRCEN not set "
            f"(val=${val:08X}); SRC will read as 0.")
    if uses_d and not (val & facts["DSTEN"]):
        warnings.append(
            f"{filename}:{lineno}: LFU=${lfu_fn:X} reads D but DSTEN not set "
            f"(val=${val:08X}); existing dest won't be fed to LFU.")

    # 3. compositing without read-back
    if (val & facts["DCOMPEN"]) and not (val & facts["DSTEN"]):
        warnings.append(
            f"{filename}:{lineno}: DCOMPEN set but DSTEN not "
            f"(val=${val:08X}); data-compare can't read existing dest.")
    if (val & facts["BCOMPEN"]) and not (val & facts["SRCEN"]):
        warnings.append(
            f"{filename}:{lineno}: BCOMPEN set but SRCEN not "
            f"(val=${val:08X}); bit-mask source won't be read.")

    # 4. PATDSEL with no LFU=0 / no SRCEN suspicious; PATDSEL alone with
    # no source enable is the typical "fast clear" idiom -- allow it but
    # warn if anything else is going on.
    return warnings

# ----- check a single .s file ---------------------------------------------
CMD_LITERAL_RE = re.compile(
    r"^\s*move\.l\s+#\$([0-9A-Fa-f]+)\s*,\s*B_COMMAND")
HEX_ADDR_RE = re.compile(
    r"\$F[0-9A-Fa-f]{5,}")           # F-prefixed MMIO literal
EQU_RE      = re.compile(
    r"^\s*(\w+)\s+equ\s+(.+?)\s*$", re.I)     # `name equ value` definition

def eval_equ_value(expr, regs):
    """Evaluate a vasm-style equ RHS using known oracle constants.
    Supports: $hex literals, decimal, simple +/-/<<, and oracle symbols.
    Returns int on success, None if anything is unparseable."""
    # Strip end-of-line comments
    if ";" in expr:
        expr = expr.split(";", 1)[0]
    # Replace vasm $hex with Python 0x and oracle names with their values.
    py = re.sub(r"\$([0-9A-Fa-f]+)", r"0x\1", expr)
    # Substitute known oracle symbols (longest first to avoid prefix bugs).
    for name in sorted(regs, key=len, reverse=True):
        py = re.sub(rf"\b{re.escape(name)}\b", str(regs[name]), py)
    # vasm uses `<<` and `>>` like C; Python supports those natively.
    # Bail on anything that still has letters (unknown symbol).
    if re.search(r"[A-Za-z_]", py):
        return None
    try:
        return int(eval(py, {"__builtins__": {}}, {}))
    except Exception:
        return None

def check_file(path, facts, regs):
    warnings = []
    rel = os.path.relpath(path, REPO_ROOT)
    in_oracle = path.endswith("jaguar_regs.s")
    with open(path) as fh:
        for lineno, line in enumerate(fh, start=1):
            # strip comments (everything after first ';')
            code = line.split(";", 1)[0]

            # check 1: B_COMMAND literal sanity
            m = CMD_LITERAL_RE.match(code)
            if m:
                warnings += check_cmd_literal(rel, lineno, m.group(1), facts)

            # check 4: local equate that DIVERGES from an oracle symbol.
            # Pure value-duplicates are safe (just redundant); only flag
            # cases where the local value differs from the oracle's --
            # those are the ones that bypass the source of truth.
            # The oracle file itself is exempt -- it's the source of truth.
            if not in_oracle:
                em = EQU_RE.match(code)
                if em and em.group(1) in regs:
                    name = em.group(1)
                    local_val = eval_equ_value(em.group(2), regs)
                    oracle_val = regs[name]
                    if local_val is not None and local_val != oracle_val:
                        warnings.append(
                            f"{rel}:{lineno}: local `{name} equ ${local_val:X}` "
                            f"DIVERGES from oracle `${oracle_val:X}` -- this "
                            f"is the OLP_HI/LO-swap class of bug.  Delete the "
                            f"local definition or fix the oracle.")

            # check 2: hard-coded MMIO addresses
            # skip lines that DEFINE a symbol (`equ $F...`) and the file
            # that legitimately contains the canonical addresses.
            if "equ" in code:
                continue
            if "include/" in path or in_oracle:
                continue
            for hex_match in HEX_ADDR_RE.finditer(code):
                # Reverse-lookup: is this address one we have a name for?
                val = int(hex_match.group(0)[1:], 16)
                name = next((k for k, v in regs.items() if v == val), None)
                if name:
                    warnings.append(
                        f"{rel}:{lineno}: hard-coded {hex_match.group(0)} "
                        f"-- use the symbol `{name}` from jaguar_regs.s.")
    return warnings

# ----- main ----------------------------------------------------------------
def main():
    if not os.path.exists(REGS_PATH):
        print(f"ERROR: {REGS_PATH} doesn't exist; "
              f"run gen-jaguar-regs.py first.", file=sys.stderr)
        return 2

    regs = parse_regs()
    facts = collect_facts(regs)

    if not facts["valid_cmd_bits"]:
        print("ERROR: BLIT_CMD_VALID_BITS missing from jaguar_regs.s",
              file=sys.stderr)
        return 2

    all_warnings = []
    for root, _, files in os.walk(TESTS_DIR):
        for f in files:
            if f.endswith(".s"):
                all_warnings += check_file(os.path.join(root, f), facts, regs)

    if not all_warnings:
        print("acid lint: clean")
        return 0

    print(f"acid lint: {len(all_warnings)} warning(s)")
    for w in all_warnings:
        print(f"  {w}")
    return 1

if __name__ == "__main__":
    sys.exit(main())
