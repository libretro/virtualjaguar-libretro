; tools/jaguar-toolchain/smoke/hello68k.s
;
; Minimal rmac smoke source: assembles and links to prove rmac/rln work
; end to end.  Not a real ROM -- an infinite loop is the whole payload.
;
; Assemble/link with (see .github/workflows/jaguar-toolchain.yml):
;   rmac -fb -o hello68k.o hello68k.s
;   rln  -n -a 802000 x x -o hello68k.j64 hello68k.o
;
; Notes on the syntax, all learned from rmac/rln themselves rather than
; assumed:
;
;   * NO `.org` here.  rmac rejects it outright in a 68K section unless
;     you also pass -fr (absolute-address output):
;       "Error: .org permitted only in GPU/DSP/OP, 56001, 6502 and 68k
;        (with -fr switch) sections"
;     The address belongs on rln's command line instead (`-a <text>
;     <data> <bss>`), which is how a real Jaguar cart is built anyway.
;
;   * -fb selects BSD object format, which rmac's own usage text labels
;     "use this for Jaguar" and is what rln consumes.
;
;   * `rln -n` suppresses the file header, so the output is the raw
;     big-endian 68K image: the linked bytes start `60 FE` (bra.s to
;     self), padded out to rln's default phrase alignment.

        .68000
        .text
start:
        bra.s   start
        .end
