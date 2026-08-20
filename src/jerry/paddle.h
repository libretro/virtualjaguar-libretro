/*
 * src/jerry/paddle.h -- the $F17C00 "Paddle Interface": the 8-bit ADC
 * fitted to early Jaguar motherboards (#505).
 *
 * THIS IS NOT THE ANALOG CONTROLLER (#437).  Read that sentence twice
 * before editing either file.  TR10, section "Analogue Joystick and
 * 'Driving' Controllers": "Early versions of the Jaguar included an 8 bit
 * ADC on the motherboard.  This has been deleted -- analogue controllers
 * now require their own ADC chip."  #437's device is the REPLACEMENT: a
 * controller-side microcontroller answering the ordinary $F14000 row scan.
 * THIS file is the deleted part -- a converter on the console side, at its
 * own address, sampling pot lines that come in on the joystick connector.
 * The two share nothing but the phrase "analog stick".
 *
 * THE PART: an ADC0844 at U16, selected by JERRY's GPIO5 decode.  The
 * Jaguar TRM rev 8 register map labels $F17C00-$F17FFF "GPIO5" and the
 * schematic labels this block "Paddle Interface"; the connector pins it
 * digitises are named PAD0X / PAD0Y / PAD1X / PAD1Y, i.e. two axes per
 * joystick socket, four channels in all.
 *
 * WHAT A READ RETURNS, AND WHEN -- THE PROTOCOL
 * =============================================
 * There is no register-layout document for this block, so the protocol
 * below is pinned by TWO independent sources that agree:
 *
 *   (1) THE SOFTWARE.  Three ROMs in an 822-image sweep name $F17C00 as an
 *       instruction operand (#505).  BattleSphere's JERRY Timer 1 handler
 *       at $83E75C -- Gold's at $88059E is the same code -- is:
 *
 *           move.w  $F17C00.l, d0      ; completed conversion
 *           movea.l #$1F9314, a0
 *           move.w  $1F9310.l, d1      ; channel 0..3
 *           move.b  d0, (a0,d1.w)      ; store sample: LOW BYTE ONLY
 *           addq.w  #1, d1
 *           andi.w  #3, d1
 *           move.w  d1, $1F9310.l
 *           addq.w  #4, d1
 *           move.w  d1, $F17C00.l      ; start next conversion
 *
 *       and Club Drive at $811440 writes the same select idiom
 *       (`lsr.w #1,d3 / addi.w #4,d3 / move.w d3,$F17C00`) without ever
 *       reading the result back.  So: WRITE (channel | 4) to select a
 *       channel and start a conversion; READ to get the completed one;
 *       eight bits of result, in the LOW byte of the word.
 *
 *   (2) THE PART.  TI's ADC0844/ADC0848 datasheet (SNAS523): 8-bit
 *       resolution, 40 us typical conversion time, and the multiplexer
 *       address MA3..MA0 is latched off DATA BITS 3..0 by the write
 *       strobe.  Every row of its MUX Addressing table for SINGLE-ENDED
 *       inputs carries MA3 = 0 and MA2 = 1, with MA1:MA0 picking one of
 *       the four channels.  That is exactly the `channel | 4` all three
 *       ROMs write -- software and silicon corroborating each other, which
 *       is why this is a model rather than a guess.
 *
 * CHANNEL -> AXIS is the one inferred link and is labelled as such.  The
 * connector pin names (PAD0X, PAD0Y, PAD1X, PAD1Y) give the ORDER, and
 * BattleSphere consumes channels 2 and 3 behind a menu option worded
 * "2nd Controller: Analog Stick" -- so channels 0/1 are port 1's X/Y and
 * channels 2/3 are port 2's X/Y.  No pinout document we hold states the
 * mapping outright.
 *
 * TIMING IS COLLAPSED, deliberately and in the same shape as #437's
 * "answers instantly" simplification.  The conversion completes at the
 * write; a read returns whatever the last write converted.  The part
 * takes 40 us and the only known consumer reads once per JERRY Timer 1
 * tick -- milliseconds apart, thousands of conversion times -- so no
 * software that exists can observe the difference.  What the model DOES
 * reproduce is the one-behind pipelining the ROM depends on: the value a
 * read returns belongs to the channel selected by the PREVIOUS write.
 *
 * THE DEFAULT IS "NO ADC FITTED" AND THAT IS THE WHOLE SAFETY ARGUMENT
 * ===================================================================
 * A production Jaguar has no ADC on the board and reads $FF here; a board
 * WITH the ADC but nothing plugged into the pot lines reads $00 (both from
 * the JANALOG project, mdgames.de/janalog.html, already cited in
 * inputdev.h).  Three distinct states, and we reproduce all three:
 *
 *   no paddle device selected           -> $FF   (retail console)
 *   selected, but this channel's port
 *   has no paddle                       -> $00   (fitted, pin idle)
 *   selected                            -> live tuned axis, $80 centred
 *
 * Nothing is fitted until the user explicitly selects the device on a
 * port, so a session that does not opt in is bit-identical to one built
 * before this file existed -- including for BattleSphere and Club Drive,
 * which read/write the register unprompted.  Pinned by
 * test/tools/paddle_decode_test.c and by a frame_hash_ab A/B run.
 *
 * NO ENGAGEMENT LATCH, unlike #437, for two reasons.  The analog
 * controller needs one because it STEALS the $F14000 matrix from a pad the
 * frontend may actually be routing; the paddle steals nothing -- the pots
 * are separate connector pins and the digital pad on that port stays fully
 * live, which is also why INPUTDEV_PADDLE contributes nothing to the
 * matrix overlays.  And an engagement latch would actively break the one
 * consumer: BattleSphere's "Analog Joystick Calibrator" screen asks the
 * player to "Align Crosshairs with Joytick Centered", so an untouched
 * stick must read CENTRE ($80), not the $00 rail an un-engaged device
 * would sit at.
 */

#ifndef __PADDLE_H__
#define __PADDLE_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void PaddleInit(void);
void PaddleReset(void);      /* JERRYReset: converter state only        */
void PaddleShutdown(void);   /* retro_deinit: clear every static (iOS)  */

/* Fit/unfit the converter for one port's channel pair.  `port` is 0 for
 * Jaguar port 1 (channels 0/1) and 1 for port 2 (channels 2/3).  The ADC
 * itself is "fitted" while ANY port has a paddle attached, which is the
 * early-board reality: one converter, two sockets wired into it. */
void PaddleSetAttached(int port, int attached);
int  PaddleFitted(void);

/* Per-frame host feed, from update_input() in libretro.c via
 * InputDevFeedPaddle().  x and y are already-tuned 8-bit ADC counts
 * (128 = centre), because the tuning belongs to the shared axistune layer
 * that owns every analog source (#439) and not to this file. */
void PaddleFeed(int port, uint8_t x, uint8_t y);

/* Bus side, from JERRYRead/Write{Byte,Word} for $F17C00-$F17FFF.
 *
 * THE ADC'S EIGHT DATA LINES SIT ON THE LOW BYTE of the 16-bit bus.  That
 * single fact -- from the ROM's `move.w` / `move.b d0` pair, which keeps
 * the low half and discards the high -- derives every access width here:
 * a word read is $00xx, an odd-address byte read is xx, an even-address
 * byte read is $00, and only the low byte of a write reaches MA3..MA0.
 * GPIO5's chip select covers the whole $F17C00-$F17FFF range, so every
 * address in it decodes to the converter; no known software uses any
 * address but $F17C00 itself. */
uint16_t PaddleReadWord(uint32_t offset);
uint8_t  PaddleReadByte(uint32_t offset);
void     PaddleWriteWord(uint32_t offset, uint16_t data);
void     PaddleWriteByte(uint32_t offset, uint8_t data);

/* Savestate (trailing chunk, immediately after the input-device chunk).
 *
 * The latched MUX address and the completed conversion ARE what the next
 * $F17C00 read returns, so both are machine-visible and must survive a
 * rollback -- issue #400's lesson, the same one that put #437's latched
 * ADC bytes in the input-device chunk.  The live axis values are NOT
 * saved: update_input() re-feeds them from the frontend at the top of
 * every retro_run, before any emulation, so they are re-derived
 * deterministically on replay. Neither is the fitted flag, which is
 * option-derived and must not be fought by a stale state.
 *
 * No STATE_VERSION bump: this is a TRAILING chunk and retro_serialize
 * zero-fills the tail, so a v12 state written before this existed reads
 * MUX 0 / result 0 here -- inert unless a paddle is selected, and the
 * next conversion overwrites both. */
#define PADDLE_STATE_SIZE 2
size_t PaddleStateSave(uint8_t *buf);
size_t PaddleStateLoad(const uint8_t *buf);

#ifdef __cplusplus
}
#endif

#endif /* __PADDLE_H__ */
