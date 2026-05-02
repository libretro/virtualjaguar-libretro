;
; acid_test.s - pass/fail signature macros.
;
; The host runner reads four 32-bit words at RAM offset $100000..$10000F:
;
;   $100000 ACID_RESULT  $12345678 = pass
;                        $DEADBEEF = fail
;                        $00000000 = not-yet-run
;   $100004 ACID_DETAIL  test-specific code
;   $100008 ACID_OBSERVED value the test got
;   $10000C ACID_EXPECTED value the test expected
;
; The signature lives at $100000 (1 MB into main RAM) to stay well
; clear of:
;   $0..$3FF       68K exception vector table (filled by HLE BIOS init)
;   $400..$1FFF    BIOS workspace + stack (cart-mode SSP=$4000 grows down)
;   $4000..$103FF  typical RAM-loaded executable region
;   $802000+       cart code

ACID_BASE       equ     $100000
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
                move.l  d0,ACID_RESULT.l
                bra.s   .acid_halt\@
.acid_halt\@:   bra.s   .acid_halt\@
                endm

;
; ACID_FAIL - mark this test as failing and halt.
; Args (any addressing mode that move.l accepts):
;   detail   : 32-bit value -- include `#` for immediate, omit for register
;   observed : ditto
;   expected : ditto
; Clobbers d0.
;
; Examples:
;   ACID_FAIL #5,#$DEAD,#$BEEF       ; all immediates
;   ACID_FAIL d3,d5,d4               ; all from registers
;   ACID_FAIL #1,d2,#0               ; mixed
;
ACID_FAIL       macro   detail,observed,expected
                move.l  \1,d0
                move.l  d0,ACID_DETAIL.l
                move.l  \2,d0
                move.l  d0,ACID_OBSERVED.l
                move.l  \3,d0
                move.l  d0,ACID_EXPECTED.l
                move.l  #ACID_FAIL_MAGIC,d0
                move.l  d0,ACID_RESULT.l
                bra.s   .acid_halt\@
.acid_halt\@:   bra.s   .acid_halt\@
                endm

;
; ACID_INIT - clear the signature block to NOT-RUN-YET.  Call once
; near the top of your test before doing any real work.
; Clobbers d0/a0.
;
ACID_INIT       macro
                lea     ACID_BASE.l,a0
                moveq   #0,d0
                move.l  d0,(a0)+
                move.l  d0,(a0)+
                move.l  d0,(a0)+
                move.l  d0,(a0)+
                endm
