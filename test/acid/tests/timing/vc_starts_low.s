;
; tests/timing/vc_starts_low.s - VC must be in valid range right after boot.
;
; Sample TOM VC ($F00006) immediately after entry; mask off the
; lower-field bit ($0800); confirm the residual is < 525 (one valid
; NTSC frame's worth of halflines).  Catches "VC didn't get reset
; on cart boot" bugs where the counter is sitting on garbage left
; over from a prior frame loop.
;
; Detail codes:
;   1 = observed VC (after $7FF mask) >= 525
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

VC              equ     $F00006
VC_MAX          equ     525

                org     $802000
entry:
                ACID_INIT

                move.w  VC,d5
                and.l   #$7FF,d5                ; strip field bit
                cmp.l   #VC_MAX,d5
                bge.s   .too_big

                ACID_PASS

.too_big:       ACID_FAIL #1,d5,#VC_MAX
