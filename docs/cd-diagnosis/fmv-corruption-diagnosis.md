# FMV corruption diagnosis — BrainDead 13 (and ReadySoft-engine titles)

2026-07-15, branch `feature/jaguar-cd-support` @ `52524d9`. User-reported on
device: "after the initial logo screens, the fmvs and playback have very
corrupted graphics, like pixel art with odd colors — scene recognizable but
very incorrect and incomplete." Reproduced headlessly (bios mode; requires
`--option virtualjaguar_cd_boot_mode=bios`, not just `--bios`).

## What is ruled OUT (measured, not inferred)

All evidence gathered with `test/tools/cd_visual_verify` + a scratch OP-list /
RAM dumper at the live FMV moment (frame ~916, first >26%-nonblack frame).

1. **Display mode / colorspace misinterpretation — ruled out.**
   VMODE = `$06C7` → MODE=RGB16, VARMOD **off** (bit 8), PWIDTH=4, BGEN=1.
   The live OP list is healthy: BRANCH/BRANCH/BRANCH → BITOBJ 320×216 @
   `$1E000`, depth=4 (16bpp), dwidth=iwidth=80 phrases → a plain fullscreen
   framebuffer. The dumped `$1E000` buffer was re-rendered offline as
   Jaguar-RGB16, CRY16, both byte-swapped, and RGB565 — **all five are
   speckled garbage** with the same macro shape. If the renderer were merely
   mis-decoding a colorspace, one interpretation would look clean. None does
   → the pixel VALUES in the framebuffer are wrong.

2. **CD data path / byte order — ruled out.**
   The ~1 MB compressed FMV stream lands at `$46000..$146000` (task-8 dest
   telemetry). Probes at +0x0/+0x10000/+0x40000/+0x80000/+0xC0000 all match
   Track 4 of the BIN **word-swapped** — and so does the *executing* 68K
   engine code at `$A000`/`$B2DC` (probed vs Track 3). Since that code runs
   correctly, "swapped vs BIN" is the CORRECT in-RAM byte order (the disc is
   mastered pre-swapped for the I2S path). The FMV compressed input is
   therefore byte-correct and contiguous in RAM.

3. **Logos are clean.** The BIOS cube and the disc-loaded "Licensed by
   ATARI" plate (same FIFO path) render pixel-perfect. Only DECODED FMV
   output is corrupt.

## Conclusion

Correct compressed input + garbage decoded output + intact macro structure
(block placement right, values wrong) ⇒ **the GPU FMV decompressor produces
wrong values** — an instruction-level GPU emulation bug exercised by the
ReadySoft decoder (Dragon's Lair / Space Ace share it; user reports "others"
corrupted too). The signature resembles a codebook/vector-quantization
decode where arithmetic or addressing goes wrong per block.

This is **independent of the lost-wakeup CPUINT fix** (`61aca48`): that fix
only delays interrupt delivery; the corruption is value-wise, deterministic,
and the input data is bit-correct. Pre-fix cores never reached FMV playback
(bios hung earlier; HLE parks at the licence plate — verified on the
pre-fix baseline build), so the corruption was previously unreachable, not
absent.

## Next steps (fresh session)

1. Dump GPU RAM at the live FMV moment and disassemble the decode loop
   (`python3 test/tools/disasm_gpu_isr.py`; handler entry `$F034EC`, cmd=3
   dispatch per task-8). Inventory the arithmetic ops used (SAT8/SAT16/
   SATS? MULT/IMULT/IMACN/RESMAC? MMULT? SH/SHA?) — then unit-test those
   gpu.c opcodes against the JTRM semantics (`docs/jtrm-gpu-dsp.md`).
   Suspects: saturation ops and multiply-accumulate edge cases (the DSP
   needed a 40-bit MAC fix once — `test_dsp_mac40`; the GPU may have an
   equivalent flaw), signed/unsigned shifts, byte/word loads from phrase
   buffers (LOADB/LOADW sign/alignment).
2. A known-good reference frame would pin the codec: BigPEmu screenshots of
   the same FMV moment, or decode the stream offline if the codec is
   identifiable (Cinepak-for-Jaguar was common in ReadySoft ports).
3. Repro command:
   ```bash
   make cd-visual CD_VISUAL_DISC="test/roms/private/BrainDead 13 (USA)/BrainDead 13 (USA)/BrainDead 13 (USA).cue" \
        CD_VISUAL_FLAGS="--bios --option virtualjaguar_cd_boot_mode=bios --frames 3000 --outdir /tmp/cdshots"
   ```
   The corrupted frame appears at the first >26%-nonblack window (~frame
   900-1300, run-dependent); logos before it must stay clean (regression
   canary).
