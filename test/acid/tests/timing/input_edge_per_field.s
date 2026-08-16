;
; tests/timing/input_edge_per_field.s - one physical press must read as
; one press edge, and as exactly as many held fields as it was held.
;
; WHY THIS ROM EXISTS
; -------------------
; Reported symptom: a short D-pad tap registers as more than one press
; -- Doom's menu cursor jumps two or three entries, Hover Strike's ship
; accelerates faster than the tap should allow -- unless the tap is
; extremely quick.
;
; Exactly two mechanisms produce that, and they need different fixes, so
; the first job is to tell them apart:
;
;   (a) INPUT over-sampling.  The pad is latched more than once per
;       field, or one continuous press reads as several press edges.
;       That is a core input bug and breaks every title that
;       edge-detects.
;
;   (b) LOOP rate.  Input is clean, but the title's tick loop iterates
;       more often per second than on hardware, so its own auto-repeat
;       threshold is crossed sooner in wall-clock terms.  Jaguar Doom's
;       menu is this shape: M_Ticker (m_main.c) repeats on
;       `movecount == 6`, and M_Drawer -- unlike the gameplay path --
;       never calls I_Update(), so the menu loop carries NO 3-tick gate
;       and runs at whatever rate the renderer allows.
;
; This ROM isolates (a).  The pad is sampled in the VI handler, exactly
; once per field -- the same place and cadence Jaguar Doom samples it
; (init.s `Frame:`, which reads $81FE then bumps _ticcount) -- and the
; handler maintains:
;
;   EDGES  press transitions (not-held -> held)
;   HELD   fields the button was observed held
;
; The host drives it: hold one button for a known number of fields, then
; read the published counters.  For a single continuous N-field press a
; correct machine must report EDGES == 1 and HELD == N.  EDGES > 1 means
; the core manufactures press events (mechanism a).  EDGES == 1 while
; games still misbehave means input is clean and the fault is (b), a
; timing-accuracy problem that cannot be fixed in the input path.
;
; Detail codes:
;   1 = EDGES exceeds HELD -- impossible on hardware; more press
;       transitions than fields held.  observed = EDGES, expected = HELD.
;   2 = HELD nonzero but EDGES zero -- edge detection never fired.
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

;; Published results (below ACID_BASE, clear of the vector table).
RES_EDGES       equ     $00000800
RES_HELD        equ     $00000804
RES_FIELDS      equ     $00000808
RES_READY       equ     $0000080C               ; $5A5A5A5A when finished
;; ISR working state.
CUR_FIELDS      equ     $00000810
CUR_EDGES       equ     $00000814
CUR_HELD        equ     $00000818
PREV_HELD       equ     $0000081C

;; irq_ack_handler() returns vector 64 ($100) for ALL hardware IRQs.
HW_IRQ_VECTOR   equ     $00000100

JOYSTICK        equ     $F14000

;; ~5 s at NTSC: long enough for the host to press well inside it.
WINDOW_FIELDS   equ     300

                org     $802000
entry:
                ACID_INIT

                moveq   #0,d0
                move.l  d0,RES_EDGES.l
                move.l  d0,RES_HELD.l
                move.l  d0,RES_FIELDS.l
                move.l  d0,RES_READY.l
                move.l  d0,CUR_FIELDS.l
                move.l  d0,CUR_EDGES.l
                move.l  d0,CUR_HELD.l
                move.l  d0,PREV_HELD.l

                ;; Install the VI handler at vector 64.
                lea     irq_handler(pc),a0
                move.l  a0,HW_IRQ_VECTOR.l

                ;; Clear pending TOM IRQs (high byte = clear bits).
                move.w  #$1F00,TOM_INT1

                ;; Fire VI once per field, near the top of the frame.
                move.w  #2,TOM_VI

                ;; Enable IRQ_VIDEO (low byte = enable mask).
                move.w  #IRQ_VIDEO_MASK,TOM_INT1

                ;; Allow IPL=2 in 68K SR (supervisor, mask=0).
                move.w  #$2000,sr

                ;; Spin until the ISR has counted the whole window.
.wait:          move.l  CUR_FIELDS.l,d0
                cmp.l   #WINDOW_FIELDS,d0
                blt.s   .wait

                ;; Mask interrupts so the reads are stable.
                move.w  #$2700,sr

                move.l  CUR_EDGES.l,d4
                move.l  CUR_HELD.l,d5
                move.l  CUR_FIELDS.l,d3

                move.l  d4,RES_EDGES.l
                move.l  d5,RES_HELD.l
                move.l  d3,RES_FIELDS.l
                move.l  #$5A5A5A5A,RES_READY.l

                ;; Self-checks that need no host knowledge.
                tst.l   d5
                beq.s   .pass                   ; never pressed: nothing to judge
                tst.l   d4
                beq     .no_edge
                cmp.l   d5,d4
                bgt     .too_many_edges
.pass:
                ACID_PASS

.too_many_edges:
                ACID_FAIL #1,d4,d5

.no_edge:
                ACID_FAIL #2,d4,d5

;; ---------------------------------------------------------------
;; VI handler: sample the pad once per field, exactly where Jaguar
;; Doom samples it.
;; ---------------------------------------------------------------
irq_handler:
                movem.l d0-d2,-(sp)

                addq.l  #1,CUR_FIELDS.l

                ;; Byte-for-byte the read Jaguar Doom's Frame: handler
                ;; performs (init.s): select column $81FE, long-read the
                ;; port, mask the unused bits, rotate the nibble into
                ;; place.  After the ror the layout is
                ;;   d0 = xxAPxxxx RLDUxxxx xxxxxxxx xxxxxxxx
                ;; so the D-pad sits in bits 23-20.
                move.w  #$81FE,JOYSTICK
                move.l  JOYSTICK,d0
                or.l    #$F0FFFFFC,d0
                ror.l   #4,d0

                ;; Inputs are active LOW; invert so held reads as 1.
                not.l   d0
                and.l   #$00F00000,d0           ; RLDU field

                tst.l   d0
                beq.s   .released

                addq.l  #1,CUR_HELD.l
                move.l  PREV_HELD.l,d1
                tst.l   d1
                bne.s   .done
                addq.l  #1,CUR_EDGES.l          ; rising edge
                moveq   #1,d2
                move.l  d2,PREV_HELD.l
                bra.s   .done
.released:
                moveq   #0,d2
                move.l  d2,PREV_HELD.l
.done:
                ;; Re-clear video pending bit so the next field can fire.
                move.w  #$0100,TOM_INT1         ; clear IRQ_VIDEO pending
                move.w  #IRQ_VIDEO_MASK,TOM_INT1 ; re-enable
                movem.l (sp)+,d0-d2
                rte
