; tools/jaguar-toolchain/smoke/hellogpu.s
;
; Minimal lyxass smoke source: assembles to prove lyxass works.  Not a
; real GPU program -- a self-branch plus its delay-slot nop is the whole
; payload.
;
; Assemble with (see .github/workflows/jaguar-toolchain.yml):
;   lyxass -o hellogpu.o hellogpu.s
;
; *** DO NOT MOVE `.run` BELOW THE CODE. ***
;
; lyxass only emits bytes while `Global.genesis > 0` (see the guard at the
; top of writeByte/writeWordBig/... in lyxass.c), and `.run` is what sets
; that flag (p_run in pseudo.c).  Anything assembled *before* `.run` is
; silently discarded: lyxass still exits 0, still writes an output file,
; and that file is a 12-byte BS94 header whose code-length field reads
; zero.  A `test -s` on it passes.  That is why the CI workflow asserts
; the output is larger than the 12-byte header, not merely non-empty.
;
; With no operand, `.run` uses the current pc -- i.e. the `.org` address
; below -- so it must sit after `.org` and before the first instruction.
;
; Expected output: 16 bytes.
;   4253 3934              "BS94" magic
;   00F0 3000              run address
;   0000 0004              code length
;   D7E0                   jr start   (opcode 53, cc 0 = always, offset -1)
;   E400                   nop        (opcode 57)

        .gpu
        .org    $F03000
        .run
start:
        jr      start
        nop
        .end
