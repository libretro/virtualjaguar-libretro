;
; tests/timing/vc_field_bit.s - VC bit 11 must toggle between fields.
;
; The Jaguar runs interlaced and toggles VC bit #11 between odd and
; even fields.  Polling VC long enough should show both states (i.e.
; we should see VC values both with and without bit 11 set).
;
; If bit 11 never sets, our HalflineCallback's "lowerField =
; !lowerField" never triggers and games that rely on field detection
; (some 480i homebrew, BIOS) misbehave.
;
; Detail codes:
;   1 = saw VC values but bit 11 never set within spin budget
;   2 = VC never read non-zero (test broken)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

VC              equ     $F00006
SPIN_LIMIT      equ     5000000
FIELD_BIT       equ     $0800

                org     $802000
entry:
                ACID_INIT

                moveq   #0,d2                   ; d2 = saw-bit-set flag
                moveq   #0,d4                   ; d4 = saw-any-vc
                move.l  #SPIN_LIMIT,d6

.spin:          move.w  VC,d3
                tst.w   d3
                beq.s   .skip
                moveq   #1,d4
.skip:          and.w   #FIELD_BIT,d3
                beq.s   .next
                moveq   #1,d2
                bra.s   .done
.next:          subq.l  #1,d6
                bne.s   .spin

.done:          tst.b   d2
                bne.s   .pass
                tst.b   d4
                beq.s   .vc_dead

                ACID_FAIL #1,#0,#FIELD_BIT

.vc_dead:       ACID_FAIL #2,#0,#1

.pass:          ACID_PASS
