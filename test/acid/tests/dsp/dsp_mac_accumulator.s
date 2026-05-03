;
; tests/dsp/dsp_mac_accumulator.s - 40-bit MAC accumulator (placeholder).
;
; The Jaguar DSP's MAC accumulator is 40 bits wide -- not 32 like
; the GPU.  IMACN multiplies signed 16x16 -> 32 and accumulates into
; the 40-bit register.  The real test would do N multiply-accumulates
; that overflow a 32-bit accumulator, then RESMAC into a 68K-readable
; register, and verify the high bits weren't truncated.
;
; This file is a **deliberate FAIL placeholder**: until we land the
; real DSP MAC sequence (movei + imacn + resmac with proper register
; addressing), this test reports FAIL with detail=99 so it shows up
; in the failing column and reminds us the coverage is missing.
;
; Replacing this with a real test is on the follow-up list -- see
; PR #130 review for context.
;
; Detail codes:
;   99 = placeholder; real 40-bit MAC test not yet implemented
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

                org     $802000
entry:
                ACID_INIT
                ACID_FAIL #99,#0,#0
