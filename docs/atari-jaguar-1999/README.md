# Atari Jaguar Hardware & Developer Documentation (1999 release)

After Hasbro Interactive acquired the Atari brand in 1998, it released the
Jaguar's patents into the public domain in 1999 and declared it an open
platform. The official developer documentation that Atari shipped to licensed
developers in the mid-1990s is now freely redistributable. This directory
mirrors that documentation in both original PDF and Markdown form for easy
in-editor reference while working on the emulator.

## Source

PDFs were pulled verbatim from the [`cubanismo/jaguar-sdk`][1] GitHub
repository, which preserves the scanned originals as released by Atari /
Hasbro. The two `Technical Reference v*.pdf` files come from
[`hillsoftware.com/files/atari/jaguar/`][2] and are the more polished,
typeset Tom & Jerry reference manuals (revision 8 from 28 February 2001 and
revision 10) maintained by Brennan / Dunn / Mathieson.

[1]: https://github.com/cubanismo/jaguar-sdk/tree/master/jaguar/docs/dev
[2]: https://hillsoftware.com/files/atari/jaguar/

## What's checked in

Only the converted **Markdown** files are committed (~2 MB total). The
source PDFs (~73 MB) are `.gitignore`d — they're trivially re-downloadable
from the upstream mirrors, and keeping them out of the repo keeps clones
fast.

To regenerate the Markdown from scratch:

```sh
./fetch-pdfs.sh                               # pulls 20 PDFs (~73 MB)
python3 -m venv .venv
.venv/bin/pip install pymupdf4llm
.venv/bin/python .convert.py                  # writes one .md per .pdf
```

The conversion uses [`pymupdf4llm`][3], which falls back to Tesseract OCR
for image-only pages.

[3]: https://pymupdf.readthedocs.io/en/latest/pymupdf4llm/

### Quality notes

The numbered files (`00 - Index.pdf` … `17 - DB - The Atari Debugger.pdf`)
are scans of the printed Atari developer binders from 1995. pymupdf4llm
falls back to Tesseract OCR for these, so the resulting Markdown is rough:
column flow can be wrong, register tables may collapse into prose, and OCR
mis-reads are common (`F00000` may render as `FOOO0O`, etc.). Treat them as
a searchable index — when in doubt, open the original PDF.

`Technical Reference v8.md` and `Technical Reference v10.md` were produced
from typeset (text-layer) PDFs, so the Markdown is faithful and is the
preferred reference for register layouts, opcode encodings, and timing.

## Contents

| File                                    | Topic                                                      |
| --------------------------------------- | ---------------------------------------------------------- |
| `00 - Index.*`                          | Master index of the developer binder set                   |
| `01 - Getting Started.*`                | Hardware setup, dev kit overview                           |
| `02 - Technical Overview.*`             | High-level system architecture                             |
| `03 - Software Reference.*`             | Software reference manual (Tom & Jerry programming model)  |
| `04 - Technical Reference.*`            | Hardware register reference (1995 release notes form)      |
| `05 - Hardware Bugs & Warnings.*`       | Errata for production silicon                              |
| `06 - Jaguar CD-ROM.*`                  | CD-ROM peripheral programming guide                        |
| `07 - The Jaguar Voice Modem.*`         | JagLink / Voice Modem peripheral                           |
| `08 - Jaguar Workshop Series.*`         | Programming workshop materials                             |
| `09 - Sample Programs.*`                | Annotated sample code listings                             |
| `10 - Libraries.*`                      | Standard library reference (jaglib, etc.)                  |
| `11 - QSound for Jaguar.*`              | QSound 3D audio API                                        |
| `12 - Cinepak for Jaguar.*`             | Cinepak video codec API                                    |
| `13 - Tools.*`                          | rdbjag debugger / loader / cart utilities                  |
| `14 - Appendices.*`                     | Misc. appendices (cart format, file types, etc.)           |
| `15 - Madmac Macro Assembler.*`         | MADMAC 68K/RISC assembler manual                           |
| `16 - ALN Linker.*`                     | ALN linker manual                                          |
| `17 - DB - The Atari Debugger.*`        | Source-level debugger manual                               |
| `Technical Reference v8.*`              | **Tom & Jerry hardware reference, rev 8 (2001)**           |
| `Technical Reference v10.*`             | Tom & Jerry hardware reference, rev 10                     |

## Why this is checked in

This project's emulator core (`virtualjaguar-libretro`) is constantly being
poked at hardware-register granularity to chase bugs in OP/GPU/DSP/JERRY
behaviour. Having greppable references next to `src/op.c`, `src/tom.c`,
`src/gpu.c` etc. saves a lot of context switching.

`Technical Reference v8.md` is the document you almost always want — it's
the cleanest, most authoritative source for register layouts, opcode
encodings, blitter modes, and timing. The numbered binder files (`00`–`17`)
are valuable for cross-checking, but their OCR quality varies because the
upstream PDFs are scans of printed pages.
