;
; tests/irq/vblank_delivery.s - VBlank IRQ must reach the 68K.
;
; Programs TOM to fire VBlank at VC == VDB (top of visible area),
; installs a level-2 autovector handler that bumps a counter, and
; spins waiting for the counter to advance.
;
; Background: irq_ack_handler() in our 68K core returns 64 for ALL
; hardware IRQs, so the actual landing vector is 64 (offset $100 in
; the vector table).  HLE BIOS init fills $100 with HLE_EXCEPT_HANDLER_RTE
; -- a plain RTE -- so without overriding it the IRQ handler does
; nothing.  We replace vector 64 with a handler that bumps d0 (saved
; in low RAM) and RTEs.
;
; Detail codes:
;   1 = VBlank IRQ never delivered within spin budget
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

;; TOM registers
TOM_INT1        equ     $F000E0         ; interrupt mask + clear bits
TOM_VI          equ     $F0004E         ; vertical interrupt position

;; Where we stash the IRQ-fired flag.  Out of the way of vectors,
;; below ACID_BASE.
IRQ_FIRED       equ     $00000800

;; 68K interrupt level-2 autovector lives at offset $68 ($1A * 4).
;; But our irq_ack_handler returns vector 64 ($100) for ALL hardware
;; IRQs -- so we patch that one.
HW_IRQ_VECTOR   equ     $00000100

SPIN_LIMIT      equ     5000000

                org     $802000
entry:
                ACID_INIT

                ;; Clear our flag.
                moveq   #0,d0
                move.l  d0,IRQ_FIRED.l

                ;; Install handler at vector 64.
                lea     irq_handler(pc),a0
                move.l  a0,HW_IRQ_VECTOR.l

                ;; Make sure no pending IRQs are latched in TOM.
                move.w  #$1F00,TOM_INT1         ; CLR_ALL clear bits
                move.w  #0,TOM_INT1             ; idle the mask

                ;; Configure VI to fire at scanline 1 (very top of
                ;; frame) so we see the IRQ ASAP.
                move.w  #2,TOM_VI               ; VC == 2 (halflines)

                ;; Enable just the video interrupt.
                ;; TOM_INT1 byte layout (per src/tom/tom.c:85, 1142-1146,
                ;; 1190-1194, 1244-1248): the LOW byte holds the enable
                ;; mask (read by TOMIRQEnabled via tomRam8[INT1+1]); the
                ;; HIGH byte is "clear pending" bits passed to
                ;; TOMClearPendingIRQs.  Big-endian word: high byte is
                ;; at offset $E0, low byte at $E1.
                ;; IRQ_VIDEO=0 -> enable bit $01.
                move.w  #$0001,TOM_INT1

                ;; Drop 68K interrupt mask to allow IPL=2.
                ;; SR bits 8..10 are I[2..0]; we want them all clear.
                move.w  #$2000,sr               ; supervisor, IPL=0

                ;; Spin until the handler bumps the flag.
                move.l  #SPIN_LIMIT,d2
.wait:          tst.l   IRQ_FIRED.l
                bne.s   .got_irq
                subq.l  #1,d2
                bne.s   .wait

                ACID_FAIL #1,IRQ_FIRED.l,#1

.got_irq:
                ACID_PASS

;
; IRQ handler -- bumps IRQ_FIRED and returns.
; Cooperates with whatever ack/clear logic the core provides;
; we don't poke TOM_INT1 here, the test ends after first delivery.
;
irq_handler:
                addq.l  #1,IRQ_FIRED.l
                rte
