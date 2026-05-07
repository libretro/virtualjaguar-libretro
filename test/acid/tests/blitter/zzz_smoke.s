;
; zzz_smoke.s - smoke test, no blitter, no logic.
; Just writes ACID_PASS_MAGIC to ACID_RESULT and halts.
; If THIS doesn't pass, the boot stub / 68K cold-start is broken.
; Filename starts with "zzz_" so `find` lists it last; runner reports
; in find order.
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

                org     $802000
entry:
                ACID_INIT
                ACID_PASS
