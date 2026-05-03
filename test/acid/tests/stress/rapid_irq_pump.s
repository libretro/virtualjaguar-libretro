;
; tests/stress/rapid_irq_pump.s - sustained TOM video IRQ delivery.
;
; Modelled on tests/irq/vblank_delivery.s but instead of stopping at
; the first IRQ it spin-waits for the counter to reach 60.  Stress-
; tests the IRQ ack path: if anything fails to clear pending or the
; autovector dispatch is broken, the counter will stall and the
; spin budget will run out.
;
; Companion to vblank_delivery.s -- if that test is NOT-RUN-YET, this
; one will too: VBlank delivery is a known gap in the emulator and
; this test exists to gate that we ever fix it.
;
; Detail codes:
;   1 = IRQ counter never reached 60 within the spin budget
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

TOM_INT1        equ     $F000E0                 ; interrupt mask + clear
TOM_VI          equ     $F0004E                 ; vertical interrupt position
HW_IRQ_VECTOR   equ     $00000100               ; vector 64 (irq_ack returns 64)
IRQ_COUNTER     equ     $00000800
SPIN_LIMIT      equ     20000000

                org     $802000
entry:
                ACID_INIT

                moveq   #0,d0
                move.l  d0,IRQ_COUNTER.l

                ;; Install handler at vector 64.
                lea     irq_handler(pc),a0
                move.l  a0,HW_IRQ_VECTOR.l

                ;; Idle TOM, then arm video IRQ at scanline 2.
                ;; TOM_INT1: HIGH byte = clear pending, LOW byte = enable
                ;; (per src/tom/tom.c).  IRQ_VIDEO=0 -> $01.
                move.w  #$1F00,TOM_INT1         ; clear all pending
                move.w  #0,TOM_INT1             ; idle
                move.w  #2,TOM_VI
                move.w  #$0001,TOM_INT1         ; enable video IRQ

                ;; Drop interrupt mask: supervisor, IPL=0.
                move.w  #$2000,sr

                ;; Spin until counter >= 60 or budget exhausted.
                move.l  #SPIN_LIMIT,d2
.spin:
                move.l  IRQ_COUNTER.l,d6
                cmp.l   #60,d6
                bge.s   .done
                subq.l  #1,d2
                bne.s   .spin

                ACID_FAIL #1,d6,#60

.done:
                ACID_PASS

;
; IRQ handler -- bump counter, ack pending video bit, return.
;
irq_handler:
                addq.l  #1,IRQ_COUNTER.l
                ;; Clear pending VIDEO bit (HIGH byte) and re-enable
                ;; mask (LOW byte): $0101.
                move.w  #$0101,TOM_INT1
                rte
