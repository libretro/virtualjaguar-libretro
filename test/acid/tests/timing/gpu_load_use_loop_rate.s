;
; tests/timing/gpu_load_use_loop_rate.s - GPU external-load + use loop
; rate (render-bound-loop family, issue #401).
;
; WHAT IT MEASURES
;   Iterations per 60-field window of a GPU loop whose body is a LOAD
;   from EXTERNAL main RAM immediately followed by a USE of the loaded
;   register -- the load-result interlock plus the external-bus access
;   path.  On real hardware the external load dominates the loop cost
;   (DRAM access + bus arbitration); an emulator that charges too few
;   cycles for external GPU accesses shows a much higher rate here
;   relative to the pure-ALU baseline (gpu_alu_loop_rate.s) than
;   hardware does.  That ratio is the point of this family.
;
; MEASUREMENT SCHEME
;   Identical to gpu_alu_loop_rate.s: GPU free-runs, publishing its
;   iteration counter to GPU_COUNT (GPU local RAM) each pass; the 68K
;   counts WINDOW_FIELDS VC wraps, stops the GPU, reads the counter,
;   and publishes:
;
;     $00080000  passes    = loop iterations completed in the window
;     $00080004  window    = 26590906 (sysclk cycles, ~1 s NTSC)
;     $00080008  cyc/iter  = window / passes
;
; PASS POLICY
;   ALWAYS PASSES unless the GPU never ran (counter still 0).
;   Expected hardware numbers are TBD pending the FPGA timing spec.
;
; GPU KERNEL (words are opcode<<10|reg1<<5|reg2, opcode indexes from
; src/tom/gpu.c gpu_opcode[]):
;
;   off  word   instruction
;   $00  $9800  movei #EXT_SRC,r0         (movei=38)
;               $0000,$0009               ; EXT_SRC = $00090000
;   $06  $980A  movei #GPU_COUNT,r10
;               $3F00,$00F0
;   $0C  $8C0B  moveq #0,r11              (iteration counter)
;   $0E  $8C02  moveq #0,r2               (use-side accumulator)
;   $10  GLOOP:
;        $A401  load  (r0),r1             (load=41; EXTERNAL read)
;   $12  $0022  add   r1,r2               (add=0; immediate use of r1)
;   $14  $082B  addq  #1,r11              (addq=2)
;   $16  $BD4B  store r11,(r10)           (store=47; LOCAL publish)
;   $18  $D760  jr    T,GLOOP             (jr=53; offset -5 words:
;   $1A  $E400  nop   (delay slot)         $D400|($1B<<5)|0)
;
;   6 instructions per iteration.
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
EXT_SRC         equ     $00090000               ; external load target

RES_PASSES      equ     $00080000
RES_WINCYC      equ     $00080004
RES_CYCITER     equ     $00080008

VC_LINE_MASK    equ     $07FF
WINDOW_FIELDS   equ     60
WINDOW_CYCLES   equ     26590906

                org     $802000
entry:
                ACID_INIT

                ;; Known data at the external load address.
                move.l  #$01020304,EXT_SRC.l

                ;; Clear the GPU-side counter and the result block.
                move.l  #0,GPU_COUNT
                moveq   #0,d0
                move.l  d0,RES_PASSES.l
                move.l  d0,RES_WINCYC.l
                move.l  d0,RES_CYCITER.l

                ;; Build the GPU kernel in GPU_RAM.
                lea     GPU_RAM.l,a0
                ;; movei #EXT_SRC,r0
                move.w  #$9800,(a0)+
                move.w  #(EXT_SRC&$FFFF),(a0)+
                move.w  #((EXT_SRC>>16)&$FFFF),(a0)+
                ;; movei #GPU_COUNT,r10
                move.w  #$980A,(a0)+
                move.w  #(GPU_COUNT&$FFFF),(a0)+
                move.w  #((GPU_COUNT>>16)&$FFFF),(a0)+
                ;; moveq #0,r11 / moveq #0,r2
                move.w  #$8C0B,(a0)+
                move.w  #$8C02,(a0)+
                ;; GLOOP: load (r0),r1 / add r1,r2 (load-use pair)
                move.w  #$A401,(a0)+
                move.w  #$0022,(a0)+
                ;; addq #1,r11 / store r11,(r10)
                move.w  #$082B,(a0)+
                move.w  #$BD4B,(a0)+
                ;; jr T,GLOOP (-5 words) / nop delay slot
                move.w  #$D760,(a0)+
                move.w  #$E400,(a0)+

                ;; Start the GPU.
                move.l  #0,G_FLAGS
                move.l  #GPU_RAM,G_PC
                move.l  #GO,G_CTRL

                ;; --- Fixed window: count WINDOW_FIELDS VC wraps.
                moveq   #0,d3
                move.w  TOM_VC,d1
                and.w   #VC_LINE_MASK,d1
.spin:
                move.w  TOM_VC,d0
                and.w   #VC_LINE_MASK,d0
                cmp.w   d1,d0
                bcc.s   .nowrap
                addq.l  #1,d3
.nowrap:        move.w  d0,d1
                cmp.l   #WINDOW_FIELDS,d3
                blt.s   .spin

                ;; Stop the GPU, read the published counter.
                move.l  #0,G_CTRL
                move.l  GPU_COUNT,d4

                move.l  d4,RES_PASSES.l
                move.l  #WINDOW_CYCLES,RES_WINCYC.l

                tst.l   d4
                beq     .never

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
