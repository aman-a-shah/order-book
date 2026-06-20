#!/usr/bin/env python3
"""Render every PDF page to an image and report bottom-whitespace gaps (§11.3).

Usage:  python3 site/verify_pdf.py [path.pdf]
Flags a non-final page as a gap when >20% of its height below the last
non-white row is blank. Also dumps PNGs to site/_pages/ for eyeballing.
"""
import sys
from pathlib import Path
import fitz  # PyMuPDF

pdf = Path(sys.argv[1] if len(sys.argv) > 1 else "site/low-latency-order-book.pdf")
doc = fitz.open(pdf)
outdir = pdf.parent / "_pages"
outdir.mkdir(exist_ok=True)

print(f"{pdf.name}: {doc.page_count} pages")
gaps = []
for i, page in enumerate(doc):
    pm = page.get_pixmap(dpi=110)
    pm.save(outdir / f"p{i+1:02d}.png")
    h, w, n = pm.height, pm.width, pm.n
    samples = pm.samples
    # find last row with a non-white pixel (allow slight off-white)
    last_ink = 0
    for y in range(h):
        row = y * pm.stride
        ink = False
        for x in range(0, w, 3):  # subsample columns
            off = row + x * n
            if samples[off] < 245 or samples[off + 1] < 245 or samples[off + 2] < 245:
                ink = True
                break
        if ink:
            last_ink = y
    gap = 1 - (last_ink / h)
    final = i == doc.page_count - 1
    flag = "" if (final or gap <= 0.20) else "  <-- GAP"
    if not final and gap > 0.20:
        gaps.append((i + 1, gap))
    print(f"  page {i+1:2d}: bottom whitespace {gap*100:5.1f}%{flag}")

avg = sum(g for _, g in gaps) / len(gaps) if gaps else 0
print(f"interior gaps >20%: {len(gaps)}" + (f"  (avg {avg*100:.0f}%)" if gaps else ""))
doc.close()
