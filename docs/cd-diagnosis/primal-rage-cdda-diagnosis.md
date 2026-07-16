# Primal Rage CD-audio silence — RESOLVED (2026-07-16)

**Root cause:** the boot-stub TOC at $2C00 left bytes [5..7] of every
track entry (track duration as MSF) zeroed.  Primal Rage's music player
(RAM $3A2D6, transient overlay) computes its DSP playback countdown from
exactly those bytes — `[5]*4500 + [6]*75 + [7]` sectors — and stores it
to the synth-DSP's sector counter at $F1B278.  With duration 0 the DSP's
per-588-sample countdown ($F1B23A) underflowed on the first sector,
set the done flag at $F1B27C, and the 68K service loop ($3A3CC) sent
mailbox cmd 2 (mix OFF) within ~13 ms of cmd 1 (mix ON).  The mix gate
(bank-0 r20) was never the problem — the 68K opened it every attempt and
immediately closed it because the track "ended".

**Fix:** `CDIntfGetTrackDuration()` (lengthLBA minus pregap, as MSF) now
fills TOC bytes [5..7] in both TOC writers (jagcd_bios.c boot stub,
jagcd_hle.c).  Also reverted the Pause/Pause-Release DSA response to
$0400: the game's own CD driver ($BE42) masks the response high byte and
treats only $04xx (error-status, code $00 = none) as success — $0400 is
the Philips-protocol completion ack, not an error.

**Verified:** headless BIOS-mode run — mailbox cmd 1 arrives, no cmd 2
follows, LRXD reads go from 5 to 900 000+ with live waveform data, and
per-second host audio RMS holds 1 000–4 500 from music start (was 0).
Old "68K enable-gate" hypothesis below is obsolete: Primal Rage never
sets INT1 C_JERENA at all (all INT1 writes are $100/$101); its CD flow
is poll-driven, no 68K CD interrupt needed.

---

Historical notes (pre-resolution):

**State (2026-07-15, late):** the entire playback mechanism is decoded
end-to-end; the remaining unknown is the 68K's precondition for enabling
the DSP's CD-audio mix. Device symptom: no CD music; the game re-seeks its
music track every ~430 ticks, 148+ times (seek storm), no error screen.
Related device symptom in other titles: "loop or skip scenes between FMVs"
— likely the same missing CD-audio path (games waiting on / timing out of
audio cues).

## How Jaguar CD-DA actually works (authoritative: "06 - Jaguar CD-ROM.pdf")

- p.7: for DSP-based CD access, "install a DSP I2S interrupt handler, call
  **CD_jeri** appropriately, and set **SMODE to $14**" (slave; boot ROM
  default is $15). "To play Red Book audio you need a very simple interrupt
  handler that reads the incoming data from the CD and outputs it to the
  DACs (see INOUT.DAS)."
- p.8: "call CD_read with the 'Just Seek' bit set (D0 bit 31) and the
  timecode of your track. Audio will be played by your interrupt handler."
  A **CD_ack** may follow only if Just Seek is set — it waits for
  completion, err_flag semantics.
- CD-DA is DIGITAL through Jerry: BUTCH I2S → Jerry SSI (slave) → DSP reads
  LRXD/RRXD ($F1A148/$F1A14C read-side) → mixes → writes LTXD/RTXD. There
  is no analog bypass.

## Primal Rage's implementation (decoded from DSP RAM dump)

- SMODE toggles $15↔$14 observed (matches the doc's CD_jeri flow).
- DSP I2S ISR (vector $F1B010 → body `$F1B200`): mixes synth buffer
  ((r19)) with CD input — `load (r18)=LRXD, (r18+4)=RRXD`, volume-scale
  via (r17), add — **gated on bank-0 r20**; also a per-588-samples
  (one sector) countdown (r21/r22/r23) used as a position/segment tracker;
  outputs via `store (r18)=LTXD`.
- DSP main loop (`$F1B0AC`): command mailbox at **$F1B274** (r8):
  cmd 1 → `moveta 1,r20` (CD mix ON), cmd 2 → OFF, cmd 3 → reset r21,
  cmd 4 → exit; DSP acks by clearing the mailbox.

## Measured emulation-side facts (instrumented runs)

- Our slave-mode JERRYI2SCallback DOES deliver CD samples to lrxd/rrxd at
  44.1 kHz and raises DSPIRQ_SSI (~350K+/8s emulated; `butchReady=1`).
- The DSP SSI ISR DOES dispatch (~680K entries; I2S enable set in D_FLAGS).
- **LRXD is read only 3 times ever** → r20 stays 0 → CD mix never enabled.
- **The mailbox $F1B274 is never written** by the 68K (watchpoint, 2400
  frames headless) → the 68K never decides "playback started".
- Headless, the game DOES seek to the music track (Goto 5:33:20 → block
  24845 after `$1501` Set Mode audio + `$7001` Set DAC mode) and consumes
  the `$0100` seek response — then idles without enabling the mix.
  On device (with input) it enters the storm on a different track
  (6:08:08 → 27458): Goto triplet every ~430 ticks, response NEVER read,
  `dsaIRQs` mostly blocked by `globalDisabled` (BUTCH bit 0 clear ~82% of
  ticks during that phase).

## Prime suspects for the missing 68K precondition (next steps)

1. **JERRY external-interrupt delivery to the 68K.** BUTCHExec currently
   routes BUTCH IRQs to GPU IRQ1 only and deliberately skips
   `m68k_set_irq(2)` (blind delivery corrupted boot). The hardware-correct
   gate is JINTCTRL: deliver 68K IPL2 when
   `TOMIRQEnabled(IRQ_DSP) && JERRYIRQEnabled(IRQ2_EXTERNAL)`, mirroring
   jerry.c's timer callbacks. A patch was written and tested — it did NOT
   change the 2400-frame headless behavior (mailbox still never written),
   so it was reverted pending real evidence; NOTE headless never reaches
   the device's storm state (needs input), so this is untested against the
   actual failure. Re-test on device or with an input-scripted run.
2. **CD_mute/CD_umute** ($5100/$51FF): CD_umute "functions only in audio
   mode" — never seen in any trace. If the BIOS unmute path is broken
   emulation-side, the 68K may wait on it. Check the BIOS's mute handling
   and whether $51xx commands get responses.
3. **CD_ack/err_flag semantics for just-seek in CD_jeri mode** — what does
   the BIOS's CD_ack poll? (BUTCH+2 bit 13 sets correctly emulation-side.)
   Disassemble the 68K around the storm's Goto sender (BIOS service band;
   the caller retries every ~430 ticks).

## Tooling notes for whoever continues

- DSP RAM dump + disassembler: scratchpad `dlair_wedge.c` probe dumps
  `dspram.bin`/`gpuram.bin`/`mainram.bin`; the throwaway RISC disassembler
  used in this diagnosis is a ~40-line python (opcode table order = gpu.c
  `gpu_opcode[64]`; MOVEI imm is low-word-first).
- Watchpoints: temp fprintf hooks in `m68k_write_memory_8/16/32` and
  `JaguarWriteLong` (git checkout to revert; always `make clean` — the
  user's iOS builds share the checkout and poison `.o` files).
- The headless runs sit in attract mode (no input); the device storm state
  was reached by menu navigation. `cd_visual_verify` audio RMS ~980 for PR
  is the game's own DSP synth (SFX), not CD audio — do not read it as
  CD-DA working.
