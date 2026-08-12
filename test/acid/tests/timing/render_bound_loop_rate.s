;
; tests/timing/render_bound_loop_rate.s - UNGATED render-bound loop
; rate: the issue #401 bug repro.
;
; THE BUG
;   Jaguar Doom's MiniLoop (d_main.c) has two pacing regimes.  The
;   in-game path is display-gated: I_Update() spins on the VI-driven
;   ticcount until 3 ticks have elapsed, so the game can never exceed
;   VI_rate/3 -- that GATED path is modeled by the sibling test
;   tests/timing/doom_update_gate_rate.s.  The menu / demo-screen path
;   (M_Drawer and friends) has NO tick gate: the 68K just kicks the
;   GPU render kernel, busy-waits on a "GPU finished" mailbox word in
;   main RAM, and immediately loops.  Its pace is bounded ONLY by how
;   long the GPU takes to render.  Our emulated GPU finishes such a
;   kernel 2-4x faster than real hardware (external texture loads and
;   framebuffer stores are charged far too few cycles), so those loops
;   run ~2x fast: Doom's menu auto-repeats, Hover Strike's ungated
;   loops, etc.  Same class in issue #401.
;
; WHAT IT MEASURES
;   The 68K performs exactly that ungated loop shape:
;
;       loop { request render (mailbox GO=1);
;              spin until GPU writes mailbox DONE=1;
;              passes++ }
;
;   for a fixed 60-field window and publishes passes-per-window.  The
;   GPU "render" kernel approximates a Doom column pass in miniature:
;   64 columns x 150 texels, each texel = LOADB from a main-RAM
;   "texture" + ALU + STOREB to a main-RAM "framebuffer" (~77k
;   instructions per pass, dominated on hardware by the external
;   byte accesses).  The GPU stays resident and handshakes through
;   main-RAM mailboxes -- the same shape as Doom's resident GPU
;   refresh with the 68K busy-waiting on `gpufinished`.
;
; RESULT LAYOUT
;     $00080000  passes    = ungated loop passes completed in window
;     $00080004  window    = 26590906 (sysclk cycles, ~1 s NTSC)
;     $00080008  cyc/pass  = window / passes
;
; PASS POLICY
;   ALWAYS PASSES unless the GPU never completed a single pass (that
;   means the kernel never ran -- wiring/encoding regression, not a
;   timing result).  Expected hardware numbers are TBD pending the
;   FPGA timing spec; once known, the harness compares the published
;   rate against hardware and the 2-4x inflation becomes a red bar.
;
; GPU KERNEL (words are opcode<<10|reg1<<5|reg2, opcode indexes from
; src/tom/gpu.c gpu_opcode[]):
;
;   off  word   instruction
;   $00  $980A  movei #MBOX_GO,r10        (movei=38)
;               $8000,$0008               ; $00088000
;   $06  $980B  movei #MBOX_DONE,r11
;               $8004,$0008               ; $00088004
;   $0C  $8C29  moveq #1,r9               (DONE token)
;   $0E  $8C06  moveq #0,r6               (ALU accumulator)
;   $10  $980C  movei #PASS_TOP,r12
;               $301C,$00F0
;   $16  $9808  movei #OUTER_TOP,r8
;               $303A,$00F0
;   $1C  PASS_TOP:
;        $A540  load  (r10),r0            (load=41; poll GO)
;   $1E  $7C00  cmpq  #0,r0               (cmpq=31)
;   $20  $D7A2  jr    EQ,PASS_TOP         (jr=53; offset -3, cond EQ=2)
;   $22  $E400  nop   (delay slot)
;   $24  $8C00  moveq #0,r0
;   $26  $BD40  store r0,(r10)            (store=47; GO=0, consume)
;   $28  $9801  movei #EXT_TEX,r1         ($00090000)
;   $2E  $9802  movei #EXT_FB,r2          ($000A0000)
;   $34  $9803  movei #64,r3              (columns)
;   $3A  OUTER_TOP:
;        $9804  movei #150,r4             (texels per column)
;   $40  INNER_TOP:
;        $9C25  loadb (r1),r5             (loadb=39; EXTERNAL texture)
;   $42  $00A6  add   r5,r6               (add=0; ALU on the texel)
;   $44  $B445  storeb r5,(r2)            (storeb=45; EXTERNAL fb)
;   $46  $0821  addq  #1,r1               (addq=2)
;   $48  $0822  addq  #1,r2
;   $4A  $1824  subq  #1,r4               (subq=6)
;   $4C  $D721  jr    NE,INNER_TOP        (offset -7, cond NE=1)
;   $4E  $E400  nop   (delay slot)
;   $50  $1823  subq  #1,r3
;   $52  $D101  jump  NE,(r8)             (jump=52; next column)
;   $54  $E400  nop   (delay slot)
;   $56  $BD69  store r9,(r11)            (DONE=1; pass complete)
;   $58  $D180  jump  T,(r12)             (back to GO poll)
;   $5A  $E400  nop   (delay slot)
;
; Detail codes:
;   2 = zero passes -- GPU never completed a render (kernel never ran)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

G_FLAGS         equ     GPU_BASE+$00
G_PC            equ     GPU_BASE+$10
G_CTRL          equ     GPU_BASE+$14
GO              equ     $00000001

PASS_TOP        equ     GPU_RAM+$1C             ; see kernel listing
OUTER_TOP       equ     GPU_RAM+$3A

MBOX_GO         equ     $00088000               ; 68K -> GPU "render!"
MBOX_DONE       equ     $00088004               ; GPU -> 68K "finished"
EXT_TEX         equ     $00090000               ; 9600-byte texture
EXT_FB          equ     $000A0000               ; 9600-byte framebuffer
TEX_LONGS       equ     2400                    ; 64*150 bytes / 4

RES_PASSES      equ     $00080000
RES_WINCYC      equ     $00080004
RES_CYCPASS     equ     $00080008

VC_LINE_MASK    equ     $07FF
WINDOW_FIELDS   equ     60
WINDOW_CYCLES   equ     26590906

                org     $802000
entry:
                ACID_INIT

                ;; Fill the texture region with a pattern.
                lea     EXT_TEX.l,a1
                move.w  #TEX_LONGS-1,d0
.texfill:       move.l  #$5A5A5A5A,(a1)+
                dbra    d0,.texfill

                ;; Clear mailboxes and the result block.
                moveq   #0,d0
                move.l  d0,MBOX_GO.l
                move.l  d0,MBOX_DONE.l
                move.l  d0,RES_PASSES.l
                move.l  d0,RES_WINCYC.l
                move.l  d0,RES_CYCPASS.l

                ;; Build the GPU kernel in GPU_RAM.
                lea     GPU_RAM.l,a0
                ;; movei #MBOX_GO,r10
                move.w  #$980A,(a0)+
                move.w  #(MBOX_GO&$FFFF),(a0)+
                move.w  #((MBOX_GO>>16)&$FFFF),(a0)+
                ;; movei #MBOX_DONE,r11
                move.w  #$980B,(a0)+
                move.w  #(MBOX_DONE&$FFFF),(a0)+
                move.w  #((MBOX_DONE>>16)&$FFFF),(a0)+
                ;; moveq #1,r9 / moveq #0,r6
                move.w  #$8C29,(a0)+
                move.w  #$8C06,(a0)+
                ;; movei #PASS_TOP,r12
                move.w  #$980C,(a0)+
                move.w  #(PASS_TOP&$FFFF),(a0)+
                move.w  #((PASS_TOP>>16)&$FFFF),(a0)+
                ;; movei #OUTER_TOP,r8
                move.w  #$9808,(a0)+
                move.w  #(OUTER_TOP&$FFFF),(a0)+
                move.w  #((OUTER_TOP>>16)&$FFFF),(a0)+
                ;; PASS_TOP: load (r10),r0 / cmpq #0,r0 / jr EQ,PASS_TOP / nop
                move.w  #$A540,(a0)+
                move.w  #$7C00,(a0)+
                move.w  #$D7A2,(a0)+
                move.w  #$E400,(a0)+
                ;; moveq #0,r0 / store r0,(r10)  -- consume the GO token
                move.w  #$8C00,(a0)+
                move.w  #$BD40,(a0)+
                ;; movei #EXT_TEX,r1
                move.w  #$9801,(a0)+
                move.w  #(EXT_TEX&$FFFF),(a0)+
                move.w  #((EXT_TEX>>16)&$FFFF),(a0)+
                ;; movei #EXT_FB,r2
                move.w  #$9802,(a0)+
                move.w  #(EXT_FB&$FFFF),(a0)+
                move.w  #((EXT_FB>>16)&$FFFF),(a0)+
                ;; movei #64,r3 (columns)
                move.w  #$9803,(a0)+
                move.w  #64,(a0)+
                move.w  #0,(a0)+
                ;; OUTER_TOP: movei #150,r4 (texels)
                move.w  #$9804,(a0)+
                move.w  #150,(a0)+
                move.w  #0,(a0)+
                ;; INNER_TOP: loadb/add/storeb/addq/addq/subq/jr NE/nop
                move.w  #$9C25,(a0)+
                move.w  #$00A6,(a0)+
                move.w  #$B445,(a0)+
                move.w  #$0821,(a0)+
                move.w  #$0822,(a0)+
                move.w  #$1824,(a0)+
                move.w  #$D721,(a0)+
                move.w  #$E400,(a0)+
                ;; subq #1,r3 / jump NE,(r8) / nop
                move.w  #$1823,(a0)+
                move.w  #$D101,(a0)+
                move.w  #$E400,(a0)+
                ;; store r9,(r11) -- DONE=1 / jump T,(r12) / nop
                move.w  #$BD69,(a0)+
                move.w  #$D180,(a0)+
                move.w  #$E400,(a0)+

                ;; Start the resident GPU kernel (it parks on the GO poll).
                move.l  #0,G_FLAGS
                move.l  #GPU_RAM,G_PC
                move.l  #GO,G_CTRL

                ;; --- The UNGATED MiniLoop, measured over a fixed window.
                ;; d3 = fields seen, d4 = passes completed
                moveq   #0,d3
                moveq   #0,d4
                move.w  TOM_VC,d1
                and.w   #VC_LINE_MASK,d1

                ;; First render request.
                move.l  #0,MBOX_DONE.l
                move.l  #1,MBOX_GO.l

.spin:
                ;; --- Doom-menu shape: did the GPU finish?  If so,
                ;; --- immediately request the next render.  NO tick gate.
                tst.l   MBOX_DONE.l
                beq.s   .not_done
                addq.l  #1,d4                   ; one ungated pass done
                move.l  #0,MBOX_DONE.l
                move.l  #1,MBOX_GO.l
.not_done:

                ;; --- Window bookkeeping: count VC wraps (fields).
                move.w  TOM_VC,d0
                and.w   #VC_LINE_MASK,d0
                cmp.w   d1,d0
                bcc.s   .nowrap
                addq.l  #1,d3
.nowrap:        move.w  d0,d1
                cmp.l   #WINDOW_FIELDS,d3
                blt.s   .spin

                ;; Stop the GPU.
                move.l  #0,G_CTRL

                move.l  d4,RES_PASSES.l
                move.l  #WINDOW_CYCLES,RES_WINCYC.l

                tst.l   d4
                beq     .never

                move.l  #WINDOW_CYCLES,d0
                move.l  d4,d1
                bsr     div32
                move.l  d0,RES_CYCPASS.l

                ACID_PASS

.never:
                ACID_FAIL #2,d4,#0

;
; div32 - unsigned 32/32 divide, shift-subtract restoring.
;   in:  d0 = dividend, d1 = divisor (nonzero)
;   out: d0 = quotient          clobbers d2/d3
;
div32:
                moveq   #0,d2
                move.w  #31,d3
.dvloop:
                add.l   d0,d0
                addx.l  d2,d2
                cmp.l   d1,d2
                bcs.s   .dvskip
                sub.l   d1,d2
                addq.l  #1,d0
.dvskip:
                dbra    d3,.dvloop
                rts
