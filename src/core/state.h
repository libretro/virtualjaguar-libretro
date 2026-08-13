/*
 * Save state serialization support for Virtual Jaguar
 *
 * Each hardware module provides StateSave/StateLoad functions that
 * serialize their internal (static) state into a flat byte buffer.
 * libretro.c orchestrates the overall save/load sequence.
 */

#ifndef STATE_H
#define STATE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Save state format identifier and version.
 * v1: original layout, written only by release v2.2.0 (savestate support
 *     first shipped there; the bump to v2 shipped in v2.3.0).
 * v2: DAC chunk gained the I2S resampler fields (i2sWritePos,
 *     i2sWriteCount, i2sPhase, i2sRateRatio).
 * v3: DAC chunk gained i2sNonZeroCount.
 * v4: CDROM chunk restructured — the two 2628-byte staging buffers
 *     (cdBuf2/cdBuf3) the pre-CD-support layout carried were dropped and
 *     replaced by the BUTCH/FIFO/DSA/SSI working set, and the chunk gained
 *     the DSA response queue + serial-delay counter.
 * v5: CDROM chunk gained the latched drive speed (DSA Set Mode $15nn).
 * v6: new UART chunk (JERRY async serial + jlink RX ring).
 * v7: Memory Track chunk gained the latched $80AAA8 override flag and
 *     the NVM BIOS dispatcher state (nvmbios.c); trailing bus-arbiter
 *     68K self-cost carry (symmetric DRAM timing).  One shared bump —
 *     all in-flight changes since the last release use v7.
 * v8: trailing Jaguar GameDrive chunk (bank pages + SPI mailbox engine,
 *     jaggd.c).  One shared bump — all in-flight changes since the
 *     v3.1.0 release use v8.  This is the newest RELEASED layout (v3.2.0).
 * v9: DAC block gained the I2S ring (dac.c).  develop only.
 * v10: trailing blitter busy-window counter (blitter timing model).
 *     develop only.
 * v11: trailing hi-res shadow-surface epoch (shadowfb.c).  The epoch is a
 *     modulo-256 age stamp that gates which supersampled blocks resolve,
 *     and ShadowHiresFrameTick() drops every cached block when it wraps.
 *     That clear is visible in the presented frame, so a state restored
 *     without it replays the wrap at a different frame and diverges —
 *     which is exactly the determinism we advertise as
 *     savestate_features = 3.  See issue #400. */
#define STATE_MAGIC     0x564A5353  /* "VJSS" */
#define STATE_VERSION   11
/* Oldest layout retro_unserialize still accepts.  States between
 * STATE_MIN_VERSION and STATE_VERSION load by reading each chunk in the
 * layout the header version names (see DACStateLoad, CDROMStateLoad);
 * STATE_VERSION is always what we write.
 *
 * Released cores wrote exactly four versions: 1 (v2.2.0), 2 (v2.3.0 and
 * v2.3.1), 3 (v2.3.2) and 7 (v3.0.0, v3.1.0).  Versions 4-6 existed only
 * on develop/nightlies.  All four released layouts load (issue #268). */
#define STATE_MIN_VERSION 1

/* Per-field version gates.  A module loader that has to skip a field an
 * older layout did not carry compares the header version against the
 * constant naming that field, never a bare literal. */
/* First version whose DAC block carries the I2S resampler fields
 * (i2sWritePos, i2sWriteCount, i2sPhase, i2sRateRatio). */
#define STATE_VERSION_DAC_I2S_RESAMPLER 2
/* First version whose DAC block carries i2sNonZeroCount. */
#define STATE_VERSION_DAC_I2S_NONZEROCOUNT 3
/* First version whose CDROM block carries the post-CD-support working set
 * (cdrom_eeprom_ram, the BUTCH/FIFO/DSA flags and the SSI head) in place
 * of the two cdBuf2/cdBuf3 staging buffers the original layout carried.
 * Every released core below this wrote the old shape; see
 * CDROMStateLoad. */
#define STATE_VERSION_CDROM_RESTRUCTURE 4
/* First version whose CDROM block carries the DSA response queue and
 * serial-delay counter. */
#define STATE_VERSION_CDROM_DSA_QUEUE 4
/* First version whose CDROM block carries the latched drive speed. */
#define STATE_VERSION_CDROM_DRIVE_SPEED 5
/* First version carrying the JERRY UART + jlink chunk. */
#define STATE_VERSION_JERRY_UART 6
/* First version whose Memory Track block carries the $80AAA8 override flag. */
#define STATE_VERSION_MEMTRACK_OVERRIDE 7
/* First version carrying the trailing bus-arbiter 68K carry (symmetric
 * DRAM self-cost). */
#define STATE_VERSION_BUS_ARBITER 7
/* First version whose DAC block carries the I2S hardware registers
 * themselves (LTXD/RTXD/SCLK/SMODE at $F1A148-$F1A157, plus the dual
 * read-side registers LRXD/RRXD/SSTAT).
 *
 * These sit in jagMemSpace, which no STATE_SAVE_BUF covers: the state
 * saves jaguarMainRAM (the low 2 MB of jagMemSpace), tomRam8, and
 * jerry_ram_8 — and jerry_ram_8 is a separate array in jerry.c, not the
 * $F10000 window of jagMemSpace.  So every DAC register was restored as
 * whatever the previous run happened to leave behind.  DACPrepareFrame
 * seeds the resampler's interpolation endpoints from LTXD/RTXD, which
 * made the first frame after a rollback differ (see
 * test/tools/test_runahead_determinism.c). */
#define STATE_VERSION_DAC_REGISTERS 8
/* First version carrying the trailing Jaguar GameDrive chunk.  Older
 * states load with the GD at its reset state (identity pages, write
 * protect, idle SPI) — see JGDStateLoad / the caller's else branch. */
#define STATE_VERSION_JAGGD 8

/* v9: the I2S resample ring contents.  The ring and its cursors now
 * persist across frames (the per-frame reset was half of the 60 Hz
 * audio step), so replay determinism requires the ring data itself in
 * the state -- the cursors alone reference content the load cannot
 * reconstruct. */
#define STATE_VERSION_DAC_I2S_RING 9

/* v10: blitter bus-time busy window (virtualjaguar_blitter_timing).
 * One uint32 appended after the bus-arbiter fields.  Older states load
 * with the window closed -- at most one pending blit's stall is lost,
 * self-corrects within a field. */
#define STATE_VERSION_BLITTER_TIMING 10
/* First version whose trailer carries the hi-res shadow-surface epoch
 * (shadowfb.c).  Issue #400: without it, the epoch wrap's cache clear
 * replays at a different frame after a rollback and the picture diverges. */
#define STATE_VERSION_HIRES_EPOCH 11

/* Header flags */
#define STATE_FLAG_MEMTRACK  0x01
#define STATE_FLAG_CDROM     0x02

/* Fixed save state size (~2.4 MB).
 * Must never increase between retro_load_game() and retro_unload_game(). */
/* v9 grew the payload by the 64 KB I2S resample ring
 * (STATE_VERSION_DAC_I2S_RING).  States written by v8-and-older cores
 * are STATE_SIZE_V8 bytes; retro_unserialize sizes its floor check by
 * the version the state itself declares, so those still load. */
#define STATE_SIZE     0x280000  /* 2,621,440 bytes */
#define STATE_SIZE_V8  0x260000  /* 2,490,368 bytes -- all pre-v9 layouts */

/* Helper macros for sequential buffer writes/reads.
 * These advance the buffer pointer automatically. */
#define STATE_SAVE_VAR(buf, var) \
   do { memcpy((buf), &(var), sizeof(var)); (buf) += sizeof(var); } while (0)
#define STATE_LOAD_VAR(buf, var) \
   do { memcpy(&(var), (buf), sizeof(var)); (buf) += sizeof(var); } while (0)

#define STATE_SAVE_BUF(buf, src, len) \
   do { memcpy((buf), (src), (len)); (buf) += (len); } while (0)
#define STATE_LOAD_BUF(buf, dst, len) \
   do { memcpy((dst), (buf), (len)); (buf) += (len); } while (0)

/* Module save/load functions — each returns bytes written/read */
size_t GPUStateSave(uint8_t *buf);
size_t GPUStateLoad(const uint8_t *buf);

size_t DSPStateSave(uint8_t *buf);
size_t DSPStateLoad(const uint8_t *buf);

size_t BlitterStateSave(uint8_t *buf);
size_t BlitterStateLoad(const uint8_t *buf);

size_t EventStateSave(uint8_t *buf);
size_t EventStateLoad(const uint8_t *buf);

size_t EepromStateSave(uint8_t *buf);
size_t EepromStateLoad(const uint8_t *buf);

size_t JERRYStateSave(uint8_t *buf);
size_t JERRYStateLoad(const uint8_t *buf);

/* UART chunk is wholly new in v6; the caller gates on
 * STATE_VERSION_JERRY_UART before invoking the loader. */
size_t UARTStateSave(uint8_t *buf);
size_t UARTStateLoad(const uint8_t *buf, uint32_t stateVersion);

size_t TOMStateSave(uint8_t *buf);
size_t TOMStateLoad(const uint8_t *buf);

size_t CDROMStateSave(uint8_t *buf);
size_t CDROMStateLoad(const uint8_t *buf, uint32_t stateVersion);

size_t JoystickStateSave(uint8_t *buf);
size_t JoystickStateLoad(const uint8_t *buf);

size_t MTStateSave(uint8_t *buf);
size_t MTStateLoad(const uint8_t *buf, uint32_t version);
size_t NVMBiosStateSave(uint8_t *buf);
size_t NVMBiosStateLoad(const uint8_t *buf);

size_t DACStateSave(uint8_t *buf);
/* stateVersion is the version read from the state header: fields added in
 * later versions are skipped for older states (see STATE_MIN_VERSION). */
size_t DACStateLoad(const uint8_t *buf, uint32_t stateVersion);

size_t M68KStateSave(uint8_t *buf);
size_t M68KStateLoad(const uint8_t *buf);

#ifdef __cplusplus
}
#endif

#endif /* STATE_H */
