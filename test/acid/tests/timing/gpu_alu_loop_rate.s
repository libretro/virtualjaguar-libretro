;
; tests/timing/gpu_alu_loop_rate.s - GPU pure-ALU loop rate microbench
; (baseline for the render-bound-loop family, issue #401).
;
; WHAT IT MEASURES
;   How many iterations of a purely-internal GPU loop complete in a
;   fixed window of 60 video fields (one NTSC second).  The kernel
;   lives entirely in GPU local RAM and touches no external memory
;   except one *local* store per iteration to publish its counter, so
;   this is the "how fast does the emulated RISC core tick" baseline
;   that the load/store siblings (gpu_load_use_loop_rate.s,
;   gpu_store_ext_loop_rate.s) and the bug repro
;   (render_bound_loop_rate.s) are compared against.
;
; MEASUREMENT SCHEME (shared by the whole family)
;   The GPU free-runs the kernel, bumping an iteration counter and
;   storing it to GPU_COUNT (GPU local RAM) every pass.  The 68K
;   counts VC wraps -- the same field reference doom_update_gate_rate.s
;   and vblank_60hz_exact.s use -- and after WINDOW_FIELDS wraps stops
;   the GPU and reads the counter.  Results are published to fixed
;   main-RAM addresses for the harness:
;
;     $00080000  passes    = kernel iterations completed in the window
;     $00080004  window    = sysclk cycles per window (constant
;                            26590906 = 1 s of NTSC sysclk; 60 fields
;                            is ~1.0009 s, close enough for a scale)
;     $00080008  cyc/iter  = window / passes (integer divide, 68K side)
;
; PASS POLICY
;   ALWAYS PASSES unless the GPU never ran (counter still 0) -- this is
;   a measurement ROM.  Expected hardware numbers are TBD pending the
;   FPGA timing spec; thresholds come later.
;
; GPU KERNEL (hand-assembled 16-bit words, opcode<<10|reg1<<5|reg2;
; opcode indexes match src/tom/gpu.c gpu_opcode[]):
;
;   off  word   instruction
;   $00  $980A  movei #GPU_COUNT,r10      (movei=38; +2 imm words
;               $3F00,$00F0                lo,hi)
;   $06  $8C0B  moveq #0,r11              (moveq=35; iteration counter)
;   $08  $8C20  moveq #1,r0               operand regs r0..r7 = 1..8
;   $0A  $8C41  moveq #2,r1
;   $0C  $8C62  moveq #3,r2
;   $0E  $8C83  moveq #4,r3
;   $10  $8CA4  moveq #5,r4
;   $12  $8CC5  moveq #6,r5
;   $14  $8CE6  moveq #7,r6
;   $16  $8D07  moveq #8,r7
;   $18  $980C  movei #GLOOP,r12          (loop address for jump)
;               $301E,$00F0
;   $1E  GLOOP: 16 independent ALU ops -- the add/or/xor pattern
;               repeats 4x; consecutive instructions never share a
;               dest, so no register interlocks:
;        $0001  add  r0,r1                (add=0)
;        $2843  or   r2,r3                (or=10)
;        $2C85  xor  r4,r5                (xor=11)
;        $00C7  add  r6,r7
;        ... (x4 = 16 words, $1E..$3D)
;   $3E  $082B  addq  #1,r11              (addq=2)
;   $40  $BD4B  store r11,(r10)           (store=47; LOCAL publish)
;   $42  $D180  jump  T,(r12)             (jump=52; body too long for
;   $44  $E400  nop   (delay slot)         jr's 5-bit offset)
;
;   20 instructions per iteration.
;
; Detail codes:
;   2 = GPU never ran (counter still zero after the window)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

G_FLAGS         equ     GPU_BASE+$00
G_PC            equ     GPU_BASE+$10
G_CTRL          equ     GPU_BASE+$14
GO              equ     $00000001

GPU_COUNT       equ     GPU_RAM+$F00            ; counter, clear of code
GLOOP           equ     GPU_RAM+$1E             ; loop top (see listing)

RES_PASSES      equ     $00080000
RES_WINCYC      equ     $00080004
RES_CYCITER     equ     $00080008

VC_LINE_MASK    equ     $07FF                   ; VC bit 11 is the field flag
WINDOW_FIELDS   equ     60                      ; one NTSC second
WINDOW_CYCLES   equ     26590906                ; sysclk/s (NTSC)

                org     $802000
entry:
                ACID_INIT

                ;; Clear the GPU-side counter and the result block.
                move.l  #0,GPU_COUNT
                moveq   #0,d0
                move.l  d0,RES_PASSES.l
                move.l  d0,RES_WINCYC.l
                move.l  d0,RES_CYCITER.l

                ;; Build the GPU kernel in GPU_RAM.
                lea     GPU_RAM.l,a0
                ;; movei #GPU_COUNT,r10
                move.w  #$980A,(a0)+
                move.w  #(GPU_COUNT&$FFFF),(a0)+
                move.w  #((GPU_COUNT>>16)&$FFFF),(a0)+
                ;; moveq #0,r11 (counter)
                move.w  #$8C0B,(a0)+
                ;; operand registers r0..r7 = 1..8
                move.w  #$8C20,(a0)+            ; moveq #1,r0
                move.w  #$8C41,(a0)+            ; moveq #2,r1
                move.w  #$8C62,(a0)+            ; moveq #3,r2
                move.w  #$8C83,(a0)+            ; moveq #4,r3
                move.w  #$8CA4,(a0)+            ; moveq #5,r4
                move.w  #$8CC5,(a0)+            ; moveq #6,r5
                move.w  #$8CE6,(a0)+            ; moveq #7,r6
                move.w  #$8D07,(a0)+            ; moveq #8,r7
                ;; movei #GLOOP,r12
                move.w  #$980C,(a0)+
                move.w  #(GLOOP&$FFFF),(a0)+
                move.w  #((GLOOP>>16)&$FFFF),(a0)+
                ;; GLOOP: 4x (add r0,r1 / or r2,r3 / xor r4,r5 / add r6,r7)
                move.w  #$0001,(a0)+
                move.w  #$2843,(a0)+
                move.w  #$2C85,(a0)+
                move.w  #$00C7,(a0)+
                move.w  #$0001,(a0)+
                move.w  #$2843,(a0)+
                move.w  #$2C85,(a0)+
                move.w  #$00C7,(a0)+
                move.w  #$0001,(a0)+
                move.w  #$2843,(a0)+
                move.w  #$2C85,(a0)+
                move.w  #$00C7,(a0)+
                move.w  #$0001,(a0)+
                move.w  #$2843,(a0)+
                move.w  #$2C85,(a0)+
                move.w  #$00C7,(a0)+
                ;; addq #1,r11
                move.w  #$082B,(a0)+
                ;; store r11,(r10) -- local publish
                move.w  #$BD4B,(a0)+
                ;; jump T,(r12) / nop delay slot
                move.w  #$D180,(a0)+
                move.w  #$E400,(a0)+

                ;; Start the GPU.
                move.l  #0,G_FLAGS
                move.l  #GPU_RAM,G_PC
                move.l  #GO,G_CTRL

                ;; --- Fixed window: count WINDOW_FIELDS VC wraps.
                moveq   #0,d3                   ; fields seen
                move.w  TOM_VC,d1
                and.w   #VC_LINE_MASK,d1
.spin:
                move.w  TOM_VC,d0
                and.w   #VC_LINE_MASK,d0
                cmp.w   d1,d0
                bcc.s   .nowrap                 ; d0 >= d1: no wrap yet
                addq.l  #1,d3
.nowrap:        move.w  d0,d1
                cmp.l   #WINDOW_FIELDS,d3
                blt.s   .spin

                ;; Stop the GPU, read the published counter.
                move.l  #0,G_CTRL
                move.l  GPU_COUNT,d4

                ;; Publish raw numbers for the harness.
                move.l  d4,RES_PASSES.l
                move.l  #WINDOW_CYCLES,RES_WINCYC.l

                tst.l   d4
                beq     .never

                ;; cycles-per-iteration = WINDOW_CYCLES / passes
                move.l  #WINDOW_CYCLES,d0
                move.l  d4,d1
                bsr     div32
                move.l  d0,RES_CYCITER.l

                ACID_PASS

.never:
                ACID_FAIL #2,d4,#0

;
; div32 - unsigned 32/32 divide, shift-subtract restoring.
;   in:  d0 = dividend, d1 = divisor (nonzero)
;   out: d0 = quotient          clobbers d2/d3
;
div32:
                moveq   #0,d2                   ; remainder
                move.w  #31,d3
.dvloop:
                add.l   d0,d0                   ; MSB out -> X, quotient in
                addx.l  d2,d2                   ; remainder = rem<<1 | bit
                cmp.l   d1,d2
                bcs.s   .dvskip
                sub.l   d1,d2
                addq.l  #1,d0                   ; set quotient bit
.dvskip:
                dbra    d3,.dvloop
                rts
