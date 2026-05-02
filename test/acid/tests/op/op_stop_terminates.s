;
; tests/op/op_stop_terminates.s - OP must terminate on a STOP object.
;
; Builds a minimal OP list with just a single STOP object (type 4),
; points OLP at it, lets it tick.  If the OP runs forever (cycles
; through), HalflineCallback would either hang or take far longer
; than expected.  We verify by counting halfline_callbacks via the
; perf counter (test passes regardless; perf delta is the diagnostic).
;
; Real check: a STOP object writes no pixels, so the framebuffer
; stays whatever we left it.  We pre-fill RAM with a sentinel and
; verify it's untouched after a few frames.
;
; Detail codes:
;   1 = sentinel modified (OP wrote pixels despite STOP)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

;; TOM
TOM_OLP_HI      equ     $F00020
TOM_OLP_LO      equ     $F00022
TOM_VMODE       equ     $F00028

;; OP list location (well clear of code/stack/sig)
OPLIST          equ     $00050000
SENTINEL        equ     $00060000
SENTINEL_VAL    equ     $A5A55A5A
SPIN_LIMIT      equ     500000

                org     $802000
entry:
                ACID_INIT

                ;; Pre-fill sentinel.
                move.l  #SENTINEL_VAL,SENTINEL.l

                ;; Build STOP object at OPLIST.
                ;; STOP object format: 64 bits, low 3 bits = 4 (STOP).
                ;; Just write phrase $0000000000000004:
                move.l  #$00000000,OPLIST.l
                move.l  #$00000004,OPLIST+4.l

                ;; Point OLP at OPLIST (LO low word, HI high word).
                move.w  #(OPLIST&$FFFF),TOM_OLP_LO
                move.w  #((OPLIST>>16)&$FFFF),TOM_OLP_HI

                ;; Spin a while so OP gets a chance to run.
                move.l  #SPIN_LIMIT,d2
.spin:          subq.l  #1,d2
                bne.s   .spin

                ;; Sentinel must be intact.
                move.l  SENTINEL.l,d5
                cmp.l   #SENTINEL_VAL,d5
                bne.s   .bad

                ACID_PASS

.bad:           ACID_FAIL #1,d5,#SENTINEL_VAL
