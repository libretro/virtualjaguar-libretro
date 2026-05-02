;
; acid_test.s - pass/fail signature macros.
;
; The host runner reads four 32-bit words at RAM offset $100..$10F:
;
;   $100 ACID_RESULT     $12345678 = pass
;                        $DEADBEEF = fail
;                        $00000000 = not-yet-run
;   $104 ACID_DETAIL     test-specific code
;   $108 ACID_OBSERVED   value the test got
;   $10C ACID_EXPECTED   value the test expected
;

ACID_BASE       equ     $100
ACID_RESULT     equ     ACID_BASE+0
ACID_DETAIL     equ     ACID_BASE+4
ACID_OBSERVED   equ     ACID_BASE+8
ACID_EXPECTED   equ     ACID_BASE+12

ACID_PASS_MAGIC equ     $12345678
ACID_FAIL_MAGIC equ     $DEADBEEF

;
; ACID_PASS - mark this test as passing and halt.
; Clobbers d0/d1.
;
ACID_PASS       macro
                move.l  #ACID_PASS_MAGIC,d0
                move.l  d0,ACID_RESULT.w
                bra.s   .acid_halt\@
.acid_halt\@:   bra.s   .acid_halt\@
                endm

;
; ACID_FAIL - mark this test as failing and halt.
; Args:
;   detail  : 32-bit code (typically a sub-test ID)
;   observed: 32-bit value the test actually saw
;   expected: 32-bit value the test wanted
; Clobbers d0/d1.
;
ACID_FAIL       macro   detail,observed,expected
                move.l  #(\1),d0
                move.l  d0,ACID_DETAIL.w
                move.l  #(\2),d0
                move.l  d0,ACID_OBSERVED.w
                move.l  #(\3),d0
                move.l  d0,ACID_EXPECTED.w
                move.l  #ACID_FAIL_MAGIC,d0
                move.l  d0,ACID_RESULT.w
                bra.s   .acid_halt\@
.acid_halt\@:   bra.s   .acid_halt\@
                endm

;
; ACID_INIT - clear the signature block to NOT-RUN-YET.  Call once
; near the top of your test before doing any real work.
; Clobbers d0/a0.
;
ACID_INIT       macro
                lea     ACID_BASE.w,a0
                moveq   #0,d0
                move.l  d0,(a0)+
                move.l  d0,(a0)+
                move.l  d0,(a0)+
                move.l  d0,(a0)+
                endm
