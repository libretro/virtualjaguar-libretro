;
; copy_simple.s - first acid test: trivial blitter copy round-trip.
;
; What it does:
;   1. Fill 8 longwords at $4000 with a known pattern (0xAABBCCDD,...).
;   2. Program the blitter to copy 8 phrases ($4000 -> $5000) in
;      16-bit pixel mode, no compositing, no Z-buffer, no gouraud.
;   3. Wait for blitter to finish (poll BUSY in B_CMD).
;   4. Verify each longword at $5000 matches the source.
;   5. ACID_PASS or ACID_FAIL with the offset of the first mismatch.
;
; This is the simplest possible blitter exercise and should pass on
; both fast and accurate blitter modes.  If it FAILS, something
; basic is broken.
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

;
; Blitter register addresses (TOM, $F02200..)
;
A1_BASE         equ     $F02200
A1_FLAGS        equ     $F02204
A1_PIXEL        equ     $F02214
A2_BASE         equ     $F02218
A2_FLAGS        equ     $F0221C
A2_PIXEL        equ     $F02228
B_CMD           equ     $F02238
B_COUNT         equ     $F0223C
B_SRCD          equ     $F02240
B_DSTD          equ     $F02248
B_PATD          equ     $F02250

                org     $802000
entry:
                ACID_INIT

                ;; Fill source buffer at $4000 with 8 longwords.
                lea     $4000.w,a0
                move.l  #$AABBCCDD,(a0)+
                move.l  #$11223344,(a0)+
                move.l  #$DEADBEEF,(a0)+
                move.l  #$CAFEBABE,(a0)+
                move.l  #$0BADF00D,(a0)+
                move.l  #$FACEFEED,(a0)+
                move.l  #$F00DBEEF,(a0)+
                move.l  #$DEADC0DE,(a0)+

                ;; Clear destination at $5000 so we can tell if the
                ;; blitter actually wrote anything.
                lea     $5000.w,a0
                moveq   #7,d0
.zerodest:      clr.l   (a0)+
                dbra    d0,.zerodest

                ;; Program the blitter.
                ;; A1 (dest) = $5000, A2 (src) = $4000.
                ;; FLAGS: 16bpp pixsize=4, phrase mode (xadd=phrase=00).
                ;; Width: 1 phrase wide (m=0,e=2 -> 4 pixels), 1 line.
                ;;
                ;; A2_FLAGS / A1_FLAGS layout (16bpp + phrase):
                ;;   bit 11..14 e=0010 (=2)
                ;;   bit  9..10 m=00
                ;;   bit  6.. 8 zoffs=0
                ;;   bit  3.. 5 pixsize=4 (16bpp)
                ;;   bit  0.. 1 pitch=00 (1 phrase)
                ;;   bit 16..17 xadd=00 (phrase)
                ;; = $00001020
                move.l  #$5000,A1_BASE
                move.l  #$00001020,A1_FLAGS
                move.l  #0,A1_PIXEL
                move.l  #$4000,A2_BASE
                move.l  #$00001020,A2_FLAGS
                move.l  #0,A2_PIXEL

                ;; Inner=4 pixels, outer=1 line: $00010004
                move.l  #$00010004,B_COUNT

                ;; B_CMD: SRCEN=1, no others.  $00000001 = SRCEN.
                ;; LFU = "src" (pass through) needs ity bits (cmd>>14)&15
                ;; = 0xC = "S" (just copy source) -> bit 14|15 = $C000.
                move.l  #$0001C000,B_CMD

                ;; Spin until blitter completes.
.wait_blit:     move.l  B_CMD,d0
                btst    #0,d0           ; bit 0 = busy/start.  Some
                bne.s   .wait_blit      ; emulators clear it on done.

                ;; Compare 8 longwords src vs dest.
                lea     $4000.w,a0
                lea     $5000.w,a1
                moveq   #7,d2           ; loop counter (0..7)
                moveq   #0,d3           ; word index
.compare:       move.l  (a0)+,d4
                move.l  (a1)+,d5
                cmp.l   d4,d5
                bne.s   .mismatch
                addq.l  #1,d3
                dbra    d2,.compare

                ;; All 8 longwords matched.
                ACID_PASS

.mismatch:
                ;; d3 = first mismatched longword index, d5 = observed,
                ;; d4 = expected.
                ACID_FAIL d3,d5,d4
