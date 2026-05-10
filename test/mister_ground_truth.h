/*
 * mister_ground_truth.h — Expected hardware values extracted from MiSTer FPGA RTL.
 *
 * Source: /private/tmp/Jaguar_MiSTer/rtl/
 * These constants define the correct behavior according to real hardware
 * as implemented in the MiSTer Jaguar FPGA core.
 */

#ifndef MISTER_GROUND_TRUTH_H
#define MISTER_GROUND_TRUTH_H

#include <stdint.h>

/* ================================================================== */
/* GPU Control Register ($F02114) — from gpu_ctrl.v                    */
/* ================================================================== */

/* Write bits (ctrlwr): */
#define GPU_CTRL_GO         (1 << 0)   /* Start GPU execution */
#define GPU_CTRL_CPUINT     (1 << 1)   /* Trigger 68K interrupt */
#define GPU_CTRL_GPUIRQ0    (1 << 2)   /* Trigger GPU IRQ 0 (CPU->GPU) */
#define GPU_CTRL_SINGLE_STEP (1 << 3)  /* Single-step mode */
#define GPU_CTRL_SINGLE_GO  (1 << 4)   /* Single-step go (one instruction) */
#define GPU_CTRL_BUS_HOG    (1 << 11)  /* Bus hog mode */

/* Read bits (statrd): */
/* bit 0: go (GPU running) */
/* bit 3: single_stop (stopped in single-step) */
/* bit 11: bus_hog */
/* bit 13: always 1 (TOM version bit — gpu_ctrl.v line 136) */
/* bits 1-2, 4-10, 12, 14-15: always 0 */
#define GPU_CTRL_STAT_GO         (1 << 0)
#define GPU_CTRL_STAT_SINGLESTOP (1 << 3)
#define GPU_CTRL_STAT_BUSHOG     (1 << 11)
#define GPU_CTRL_STAT_VERSION    (1 << 13)  /* TOM always has this set */

/* Expected read-back after reset (only version bit set): */
#define GPU_CTRL_RESET_VALUE     0x00000000  /* go=0, no version bit in VJ impl */

/* ================================================================== */
/* GPU Flags Register ($F02100) — from interrupt.v                     */
/* ================================================================== */

/* Flag bits (low nibble): */
#define GPU_FLAGS_ZERO    (1 << 0)
#define GPU_FLAGS_CARRY   (1 << 1)
#define GPU_FLAGS_NEGA    (1 << 2)
#define GPU_FLAGS_IMASK   (1 << 3)   /* Set by ISR entry only, NOT writable */

/* INT_ENA bits (write to enable interrupts): */
#define GPU_FLAGS_INT_ENA0 (1 << 4)  /* CPU->GPU */
#define GPU_FLAGS_INT_ENA1 (1 << 5)  /* DSP */
#define GPU_FLAGS_INT_ENA2 (1 << 6)  /* PIT (timer) */
#define GPU_FLAGS_INT_ENA3 (1 << 7)  /* Object Processor */
#define GPU_FLAGS_INT_ENA4 (1 << 8)  /* Blitter */

/* INT_CLR bits (write to clear latched interrupts): */
#define GPU_FLAGS_INT_CLR0 (1 << 9)
#define GPU_FLAGS_INT_CLR1 (1 << 10)
#define GPU_FLAGS_INT_CLR2 (1 << 11)
#define GPU_FLAGS_INT_CLR3 (1 << 12)
#define GPU_FLAGS_INT_CLR4 (1 << 13)

/* On READ, bits 6-10 (in MiSTer) / 9-13 (our mapping) return ilatch state.
 * interrupt.v line 130: gpu_dout_out[10:6] on statrd = ilatch[4:0] */

/* ================================================================== */
/* GPU Interrupt Priority — from interrupt.v lines 139-161             */
/* ================================================================== */

/* Priority: higher number = higher priority.
 * GPU has 5 IRQ sources (0-4):
 *   IRQ 0: CPU->GPU (lowest)
 *   IRQ 1: DSP
 *   IRQ 2: Timer/PIT
 *   IRQ 3: Object Processor
 *   IRQ 4: Blitter (highest)
 *
 * DSP has 6 IRQ sources (0-5):
 *   IRQ 0: CPU->DSP (lowest)
 *   IRQ 1: SSI receive
 *   IRQ 2: Timer 0
 *   IRQ 3: Timer 1
 *   IRQ 4: External 0
 *   IRQ 5: External 1 (highest, Jerry only)
 */

/* ISR vector addresses: base + (irq_number * 16) */
#define GPU_ISR_BASE  0xF03000
#define DSP_ISR_BASE  0xF1B000
#define GPU_ISR_VECTOR(n) (GPU_ISR_BASE + ((n) * 16))
#define DSP_ISR_VECTOR(n) (DSP_ISR_BASE + ((n) * 16))

/* ISR entry microcode sequence (interrupt.v lines 259-268):
 * 0: SUBQT #4, R31        (0x1C9F)
 * 1: MOVE PC, R30         (0xCC1E)  -- actually MOVPC to R30
 * 2: STORE R30, (R31)     (0xBFFE)
 * 3: MOVEI <low>, R30     (MOVEI opcode)
 * 4: <ISR addr low word>
 * 5: <ISR addr high word>
 * 6: JUMP (R30)           (0xD3C0)
 * 7: NOP                  (0xE400)
 */

/* ================================================================== */
/* BUTCH CD Controller ($DFFF00) — from butch.v                        */
/* ================================================================== */

/* Register indices (butch_reg[0..11], each 32-bit at offset*4): */
#define BUTCH_BASE        0xDFFF00
#define BUTCH_INT_CTRL    (BUTCH_BASE + 0x00)  /* butch_reg[0] */
#define BUTCH_DSCNTRL     (BUTCH_BASE + 0x04)  /* butch_reg[1] */
#define BUTCH_DS_DATA     (BUTCH_BASE + 0x0A)  /* 16-bit access within reg[2] */
#define BUTCH_I2CNTRL     (BUTCH_BASE + 0x10)  /* butch_reg[4] */
#define BUTCH_SBCNTRL     (BUTCH_BASE + 0x14)  /* butch_reg[5] */
#define BUTCH_SUBDATA_A   (BUTCH_BASE + 0x18)  /* butch_reg[6] */
#define BUTCH_SUBDATA_B   (BUTCH_BASE + 0x1C)  /* butch_reg[7] */
#define BUTCH_SB_TIME     (BUTCH_BASE + 0x20)  /* butch_reg[8] */
#define BUTCH_I2SDAT1     (BUTCH_BASE + 0x24)  /* butch_reg[9] - FIFO */
#define BUTCH_I2SDAT2     (BUTCH_BASE + 0x28)  /* butch_reg[10] - FIFO */
#define BUTCH_EEPROM      (BUTCH_BASE + 0x2C)  /* butch_reg[11] */

/* BUTCH interrupt control bits (butch_reg[0]):
 * butch.v lines 83-95 */
#define BUTCH_INT_ENABLE   (1 << 0)   /* Master interrupt enable */
#define BUTCH_INT_FIFO_EN  (1 << 1)   /* FIFO half-full int enable */
#define BUTCH_INT_FRAME_EN (1 << 2)   /* Frame int enable */
#define BUTCH_INT_SUB_EN   (1 << 3)   /* Subcode int enable */
#define BUTCH_INT_TBUF_EN  (1 << 4)   /* TX buffer empty int enable */
#define BUTCH_INT_RBUF_EN  (1 << 5)   /* RX buffer full int enable */
#define BUTCH_INT_CRCERR   (1 << 6)   /* CRC error flag */
/* bits 7-8: reserved */
#define BUTCH_INT_FIFO_ST  (1 << 9)   /* FIFO half-full status */
#define BUTCH_INT_FRAME_ST (1 << 10)  /* Frame status */
#define BUTCH_INT_SUB_ST   (1 << 11)  /* Subcode status */
#define BUTCH_INT_TBUF_ST  (1 << 12)  /* TX buffer status */
#define BUTCH_INT_RBUF_ST  (1 << 13)  /* RX buffer status */
#define BUTCH_INT_CDERR    (1 << 14)  /* CD error */
/* bits 15-16: reserved */
#define BUTCH_INT_RESET    (1 << 17)  /* CD reset */
#define BUTCH_INT_BIOS     (1 << 18)  /* BIOS present */
#define BUTCH_INT_LIDRESET (1 << 19)  /* Open lid reset */
#define BUTCH_INT_KARTRESET (1 << 20) /* Cart pull reset */

/* eint (external interrupt to Jerry) logic:
 * butch.v line 83:
 *   eint = butch_reg[0][0] && (fifo_int || frame_int || sub_int || tbuf_int || rbuf_int)
 * where:
 *   fifo_int  = butch_reg[0][9]  && butch_reg[0][1]
 *   frame_int = butch_reg[0][10] && butch_reg[0][2]
 *   sub_int   = butch_reg[0][11] && butch_reg[0][3]
 *   tbuf_int  = butch_reg[0][12] && butch_reg[0][4]
 *   rbuf_int  = butch_reg[0][13] && butch_reg[0][5]
 */

/* I2S control (butch_reg[4]) — butch.v lines 228-232: */
#define BUTCH_I2S_DRIVE      (1 << 0)  /* i2s_drive */
#define BUTCH_I2S_JERRY      (1 << 1)  /* i2s_jerry (route to Jerry DAC) */
#define BUTCH_I2S_FIFO_EN    (1 << 2)  /* i2s_fifo_enabled */
#define BUTCH_I2S_16BIT      (1 << 3)  /* 16-bit mode */
#define BUTCH_I2S_FIFONEMPTY (1 << 4)  /* FIFO not empty (read-only status) */

/* FIFO: 16 entries deep, 32-bit wide.
 * butch.v line 295: fifo_half = (fifo_fill >= 8) */
#define BUTCH_FIFO_DEPTH     16
#define BUTCH_FIFO_HALF      8

/* DSA (Disc Servo Assembly) control — butch_reg[1]:
 * Enable bit at bit 16 */
#define BUTCH_DSA_ENABLE     (1 << 16)

/* EEPROM interface (butch_reg[11]) — butch.v line 302:
 * Note: active-low CS! eeprom_cs = !butch_reg[11][0] */
#define BUTCH_EE_CS   (1 << 0)  /* Chip select (active-low in hardware!) */
#define BUTCH_EE_CLK  (1 << 1)  /* Serial clock */
#define BUTCH_EE_DOUT (1 << 2)  /* Data out to EEPROM */
#define BUTCH_EE_DIN  (1 << 3)  /* Data in from EEPROM (read-only) */

/* ================================================================== */
/* DSA Command/Response — from butch.v lines 132-226                   */
/* ================================================================== */

/* DSA Commands: */
#define DSA_CMD_PLAY_TITLE    0x01
#define DSA_CMD_STOP          0x02
#define DSA_CMD_READ_TOC      0x03
#define DSA_CMD_PAUSE         0x04
#define DSA_CMD_PAUSE_RELEASE 0x05
#define DSA_CMD_SEARCH_FWD    0x06
#define DSA_CMD_SEARCH_BWD    0x07
#define DSA_CMD_SEARCH_REL    0x08
#define DSA_CMD_GET_LENGTH    0x09
#define DSA_CMD_GET_TIME      0x0D
#define DSA_CMD_GOTO_MIN      0x10
#define DSA_CMD_GOTO_SEC      0x11
#define DSA_CMD_GOTO_FRM      0x12
#define DSA_CMD_READ_LONG_TOC 0x14
#define DSA_CMD_SET_MODE      0x15
#define DSA_CMD_GET_ERROR     0x16
#define DSA_CMD_CLR_ERROR     0x17
#define DSA_CMD_SPIN_UP       0x18
#define DSA_CMD_PLAY_AB_MIN   0x20
#define DSA_CMD_PLAY_AB_SEC   0x21
#define DSA_CMD_PLAY_AB_FRM   0x22
#define DSA_CMD_STOP_AB_MIN   0x23
#define DSA_CMD_STOP_AB_SEC   0x24
#define DSA_CMD_STOP_AB_FRM   0x25
#define DSA_CMD_RELEASE_AB    0x26
#define DSA_CMD_GET_DISC_ID   0x30
#define DSA_CMD_GET_STATUS    0x50
#define DSA_CMD_SET_VOLUME    0x51
#define DSA_CMD_CLEAR_TOC     0x6A
#define DSA_CMD_SET_DAC       0x70

/* DSA Responses: */
#define DSA_RSP_FOUND         0x01
#define DSA_RSP_STOPPED       0x02
#define DSA_RSP_DISC_STATUS   0x03
#define DSA_RSP_ERROR         0x04
#define DSA_RSP_LENGTH_LSB    0x09
#define DSA_RSP_LENGTH_MSB    0x0A
#define DSA_RSP_ACT_TITLE     0x10
#define DSA_RSP_ACT_INDEX     0x11
#define DSA_RSP_ACT_MIN       0x12
#define DSA_RSP_ACT_SEC       0x13
#define DSA_RSP_ABS_MIN       0x14
#define DSA_RSP_ABS_SEC       0x15
#define DSA_RSP_ABS_FRM       0x16
#define DSA_RSP_MODE_STATUS   0x17
#define DSA_RSP_TOC_MIN_TRK   0x20
#define DSA_RSP_TOC_MAX_TRK   0x21
#define DSA_RSP_TOC_LO_MIN    0x22
#define DSA_RSP_TOC_LO_SEC    0x23
#define DSA_RSP_TOC_LO_FRM    0x24
#define DSA_RSP_AB_RELEASED   0x26
#define DSA_RSP_DISC_ID0      0x30
#define DSA_RSP_DISC_ID1      0x31
#define DSA_RSP_DISC_ID2      0x32
#define DSA_RSP_DISC_ID3      0x33
#define DSA_RSP_DISC_ID4      0x34
#define DSA_RSP_VOLUME        0x51
#define DSA_RSP_LONG_TOC_TRK  0x60
#define DSA_RSP_LONG_TOC_CA   0x61
#define DSA_RSP_LONG_TOC_MIN  0x62
#define DSA_RSP_LONG_TOC_SEC  0x63
#define DSA_RSP_LONG_TOC_FRM  0x64
#define DSA_RSP_TOC_CLEARED   0x6A
#define DSA_RSP_DAC_MODE      0x70
#define DSA_RSP_SERVO_VER     0xF0

/* DSA Error Codes: */
#define DSA_ERR_NONE          0x00
#define DSA_ERR_FOCUS         0x02  /* No disc */
#define DSA_ERR_SUBCODE       0x07
#define DSA_ERR_TOC           0x08
#define DSA_ERR_RADIAL        0x0A
#define DSA_ERR_SLEDGE        0x0C
#define DSA_ERR_MOTOR         0x0D
#define DSA_ERR_EMERGENCY     0x30
#define DSA_ERR_SEARCH_TIME   0x1F
#define DSA_ERR_SEARCH_BIN    0x20
#define DSA_ERR_SEARCH_IDX    0x21
#define DSA_ERR_SEARCH_TIME2  0x22
#define DSA_ERR_ILLEGAL_CMD   0x28
#define DSA_ERR_ILLEGAL_VAL   0x29
#define DSA_ERR_ILLEGAL_TIME  0x2A
#define DSA_ERR_COMMS         0x2B
#define DSA_ERR_TRAY          0x2C
#define DSA_ERR_HF_DETECT     0x2D

/* ================================================================== */
/* TOM Registers — from tom.v, iodec.v                                 */
/* ================================================================== */

/* TOM IRQ control ($F000E0) — 5 interrupt sources */
#define TOM_INT_VIDEO  0   /* Vertical blank */
#define TOM_INT_GPU    1   /* GPU done */
#define TOM_INT_OP     2   /* Object Processor */
#define TOM_INT_TIMER  3   /* PIT timer */
#define TOM_INT_JERRY  4   /* JERRY cascade */

/* INT1 register ($F000E0) layout:
 * Write: bits 0-4 = enable, bits 8-12 = clear
 * Read: bits 0-4 = enable state */

/* ================================================================== */
/* JERRY Registers — from j_jerry.v, j_jmisc.v                        */
/* ================================================================== */

/* JERRY timer (PIT) registers: */
#define JERRY_PIT1_PRESCALE  0xF10000  /* PIT1 prescaler (write) */
#define JERRY_PIT1_DIVIDER   0xF10004  /* PIT1 divider (write) */
#define JERRY_PIT2_PRESCALE  0xF10008  /* PIT2 prescaler (write) -- unverified */
#define JERRY_PIT2_DIVIDER   0xF1000C  /* PIT2 divider (write) -- unverified */

/* CLK registers: */
#define JERRY_CLK1           0xF10010
#define JERRY_CLK2           0xF10012
#define JERRY_CLK3           0xF10014

/* JERRY interrupt control: */
#define JERRY_INT_CTRL       0xF10020

/* JERRY IRQ bitmasks (from jerry.h IRQ2_ enum): */
#define JERRY_IRQ2_EXTERNAL  0x01
#define JERRY_IRQ2_DSP       0x02
#define JERRY_IRQ2_TIMER1    0x04
#define JERRY_IRQ2_TIMER2    0x08
#define JERRY_IRQ2_ASI       0x10
#define JERRY_IRQ2_SSI       0x20

/* ================================================================== */
/* Memory Map — from address decode logic                              */
/* ================================================================== */

#define JAGUAR_MAIN_RAM_START  0x000000
#define JAGUAR_MAIN_RAM_END    0x1FFFFF  /* 2MB */
#define JAGUAR_MAIN_RAM_SIZE   0x200000

#define JAGUAR_GPU_RAM_BASE    0xF03000
#define JAGUAR_GPU_RAM_SIZE    0x1000    /* 4KB */
#define JAGUAR_GPU_RAM_END     0xF03FFF

#define JAGUAR_DSP_RAM_BASE    0xF1B000
#define JAGUAR_DSP_RAM_SIZE    0x2000    /* 8KB */
#define JAGUAR_DSP_RAM_END     0xF1CFFF

#define JAGUAR_CART_ROM_START  0x800000
#define JAGUAR_CART_ROM_END    0xDFFEFF

#define JAGUAR_TOM_REG_BASE    0xF00000
#define JAGUAR_JERRY_REG_BASE  0xF10000

/* ================================================================== */
/* Blitter Registers ($F02200-$F022FF) — from dcontrol.v, blit.v       */
/* ================================================================== */

#define BLIT_A1_BASE    0xF02200
#define BLIT_A1_FLAGS   0xF02204
#define BLIT_A1_CLIP    0xF02208
#define BLIT_A1_PIXEL   0xF0220C
#define BLIT_A1_STEP    0xF02210
#define BLIT_A1_FSTEP   0xF02214
#define BLIT_A1_FPIXEL  0xF02218
#define BLIT_A1_INC     0xF0221C
#define BLIT_A1_FINC    0xF02220
#define BLIT_A2_BASE    0xF02224
#define BLIT_A2_FLAGS   0xF02228
#define BLIT_A2_MASK    0xF0222C
#define BLIT_A2_PIXEL   0xF02230
#define BLIT_A2_STEP    0xF02234
#define BLIT_B_CMD      0xF02238
#define BLIT_B_COUNT    0xF0223C
#define BLIT_B_SRCD     0xF02240
#define BLIT_B_DSTD     0xF02248
#define BLIT_B_DSTZ     0xF02250
#define BLIT_B_SRCZ1    0xF02258
#define BLIT_B_SRCZ2    0xF02260
#define BLIT_B_PATD     0xF02268
#define BLIT_B_IINC     0xF02270
#define BLIT_B_ZINC     0xF02274
#define BLIT_B_STOP     0xF02278
#define BLIT_B_I3       0xF0227C
#define BLIT_B_I2       0xF02280
#define BLIT_B_I1       0xF02284
#define BLIT_B_I0       0xF02288
#define BLIT_B_Z3       0xF0228C
#define BLIT_B_Z2       0xF02290
#define BLIT_B_Z1       0xF02294
#define BLIT_B_Z0       0xF02298

/* Blitter command bits (B_CMD): */
#define BLIT_SRCEN    (1 << 0)
#define BLIT_SRCENZ   (1 << 1)
#define BLIT_SRCENX   (1 << 2)
#define BLIT_DSTEN    (1 << 3)
#define BLIT_DSTENZ   (1 << 4)
#define BLIT_DSTWRZ   (1 << 5)
#define BLIT_CLIP_A1  (1 << 6)
#define BLIT_UPDA1F   (1 << 8)
#define BLIT_UPDA1    (1 << 9)
#define BLIT_UPDA2    (1 << 10)
#define BLIT_DSTA2    (1 << 11)
#define BLIT_GOURD    (1 << 12)  /* dcontrol.v: gpu_din[12] */
#define BLIT_GOURZ    (1 << 13)  /* dcontrol.v: gpu_din[13] */
#define BLIT_TOPBEN   (1 << 14)
#define BLIT_TOPNEN   (1 << 15)
#define BLIT_PATDSEL  (1 << 16)
#define BLIT_ADDDSEL  (1 << 17)

/* ================================================================== */
/* Caller type IDs (for who parameter in read/write functions)         */
/* ================================================================== */

#define CALLER_M68K    0
#define CALLER_GPU     1
#define CALLER_DSP     2
#define CALLER_TOM     3
#define CALLER_JERRY   4
#define CALLER_BLIT    5

#endif /* MISTER_GROUND_TRUTH_H */
