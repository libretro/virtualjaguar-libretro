;
; tests/timing/hc_advance.s - HC has the half-line bit (0x0400) AND a
; bounded phase counter in the low bits.
;
; Per src/tom/tom.c:1042-1056, HC reads return:
;   (hc_register & 0x0400) | (phase & 0x03FF)
; where:
;   * bit 0x0400 toggles per halfline (even halfline -> 0,
;                                       odd  halfline -> 1)
;   * phase is a small counter [0, (HP+1)/2) that increments on every
;     HC read and wraps at HP/2 (~422 NTSC)
;
; Tightened assertion (the loose previous test only required HC to
; change at all):
;   1. We must observe at least one sample with bit 0x0400 SET.
;   2. We must observe at least one sample with bit 0x0400 CLEAR.
;   3. Every sample's low 10 bits must be < 1024 (which is implied by
;      the 0x03FF mask anyway), and our peak phase value must be
;      below MAX_PHASE = 1024 (sanity bound).
; If condition 1 or 2 never observed -> halfline timing is dead;
; if condition 3 fails -> HC layout is wrong.
;
; Detail codes:
;   1 = never observed bit 0x0400 SET
;   2 = never observed bit 0x0400 CLEAR
;   3 = a sample's low 10 bits exceeded MAX_PHASE (HC layout wrong)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

LOOP_ITERS      equ     50000
HALF_BIT        equ     $0400
PHASE_MASK      equ     $03FF
MAX_PHASE       equ     $0400           ; phase MUST be < this

                org     $802000
entry:
                ACID_INIT

                moveq   #0,d6                   ; saw HALF_BIT set
                moveq   #0,d7                   ; saw HALF_BIT clear
                move.l  #LOOP_ITERS,d2

.spin:          move.w  TOM_HC,d3
                ;; Sanity: low 10 bits < MAX_PHASE.
                move.w  d3,d4
                and.w   #PHASE_MASK,d4
                cmp.w   #MAX_PHASE,d4
                bge     .badphase
                ;; Track HALF_BIT presence across samples.
                move.w  d3,d4
                and.w   #HALF_BIT,d4
                bne.s   .seenset
                moveq   #1,d7                   ; saw clear
                bra.s   .check
.seenset:       moveq   #1,d6                   ; saw set
.check:         tst.b   d6
                beq.s   .next
                tst.b   d7
                bne     .ok
.next:          subq.l  #1,d2
                bne.s   .spin

                ;; Spun out -- diagnose.
                tst.b   d6
                beq.s   .noset
                ACID_FAIL #2,d3,#0              ; never saw HALF_BIT clear
.noset:         ACID_FAIL #1,d3,#HALF_BIT       ; never saw HALF_BIT set

.badphase:      ACID_FAIL #3,d4,#MAX_PHASE

.ok:            ACID_PASS
