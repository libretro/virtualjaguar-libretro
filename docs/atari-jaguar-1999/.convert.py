#!/usr/bin/env python3
"""Convert every PDF in this directory to a sibling .md via pymupdf4llm."""
from __future__ import annotations

import os
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

import pymupdf4llm

HERE = Path(__file__).resolve().parent


def convert(pdf: Path) -> tuple[Path, int, float]:
    t0 = time.time()
    md = pymupdf4llm.to_markdown(str(pdf), show_progress=False)
    out = pdf.with_suffix(".md")
    out.write_text(md, encoding="utf-8")
    return out, len(md), time.time() - t0


def main() -> int:
    pdfs = sorted(p for p in HERE.glob("*.pdf"))
    if not pdfs:
        print("no PDFs found", file=sys.stderr)
        return 1

    workers = min(os.cpu_count() or 4, 8)
    print(f">> converting {len(pdfs)} PDFs with {workers} workers", flush=True)

    with ProcessPoolExecutor(max_workers=workers) as ex:
        futs = {ex.submit(convert, p): p for p in pdfs}
        for f in as_completed(futs):
            src = futs[f]
            try:
                out, size, dt = f.result()
                print(f"  [{dt:5.1f}s] {src.name}  ->  {out.name} ({size:,} chars)", flush=True)
            except Exception as exc:
                print(f"  !! {src.name}: {type(exc).__name__}: {exc}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
