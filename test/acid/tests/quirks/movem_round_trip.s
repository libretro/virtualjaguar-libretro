;
; tests/quirks/movem_round_trip.s - MOVEM.L D0-D7 round-trip on stack.
;
; MOVEM.L D0-D7,-(SP) pushes D0..D7 in REVERSE order (D7 first, D0
; last) per 68000 spec for the predecrement form.  MOVEM.L (SP)+,D0-D7
; pops in forward order (D0 first, D7 last).  After clobbering all
; eight regs in between, the post-pop values must EXACTLY match the
; pre-push values.
;
; This exercises the MOVEM register-mask + addressing-mode encoding
; in our 68K core, which has been a source of subtle bugs in past
; UAE-derived emulators.
;
; Detail codes:
;   0..7 = which Dn was wrong after restore (e.g. detail=3 -> D3
;     diverged; observed = post-restore Dn, expected = pre-push Dn)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

                org     $802000
entry:
                ACID_INIT

                ;; -------- step 1: load D0..D7 with distinct sentinels --------
                move.l  #$D0D0D000,d0
                move.l  #$D1D1D101,d1
                move.l  #$D2D2D202,d2
                move.l  #$D3D3D303,d3
                move.l  #$D4D4D404,d4
                move.l  #$D5D5D505,d5
                move.l  #$D6D6D606,d6
                move.l  #$D7D7D707,d7

                ;; -------- step 2: push all 8 to stack --------
                movem.l d0-d7,-(sp)

                ;; -------- step 3: clobber every Dn --------
                moveq   #-1,d0
                moveq   #-1,d1
                moveq   #-1,d2
                moveq   #-1,d3
                moveq   #-1,d4
                moveq   #-1,d5
                moveq   #-1,d6
                moveq   #-1,d7

                ;; -------- step 4: pop them all back --------
                movem.l (sp)+,d0-d7

                ;; -------- step 5: verify each Dn -- use a4 as scratch --------
                cmp.l   #$D0D0D000,d0
                bne     .bad_d0
                cmp.l   #$D1D1D101,d1
                bne     .bad_d1
                cmp.l   #$D2D2D202,d2
                bne     .bad_d2
                cmp.l   #$D3D3D303,d3
                bne     .bad_d3
                cmp.l   #$D4D4D404,d4
                bne     .bad_d4
                cmp.l   #$D5D5D505,d5
                bne     .bad_d5
                cmp.l   #$D6D6D606,d6
                bne     .bad_d6
                cmp.l   #$D7D7D707,d7
                bne     .bad_d7

                ACID_PASS

.bad_d0:        ACID_FAIL #0,d0,#$D0D0D000
.bad_d1:        ACID_FAIL #1,d1,#$D1D1D101
.bad_d2:        ACID_FAIL #2,d2,#$D2D2D202
.bad_d3:        ACID_FAIL #3,d3,#$D3D3D303
.bad_d4:        ACID_FAIL #4,d4,#$D4D4D404
.bad_d5:        ACID_FAIL #5,d5,#$D5D5D505
.bad_d6:        ACID_FAIL #6,d6,#$D6D6D606
.bad_d7:        ACID_FAIL #7,d7,#$D7D7D707
