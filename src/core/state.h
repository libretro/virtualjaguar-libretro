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

/* Save state format identifier and version */
#define STATE_MAGIC     0x564A5353  /* "VJSS" */
#define STATE_VERSION   3
/* Oldest layout retro_unserialize still accepts.  States between
 * STATE_MIN_VERSION and STATE_VERSION load by skipping the fields added
 * after them (see DACStateLoad); STATE_VERSION is always what we write. */
#define STATE_MIN_VERSION 2

/* Per-field version gates.  A module loader that has to skip a field an
 * older layout did not carry compares the header version against the
 * constant naming that field, never a bare literal. */
/* First version whose DAC block carries i2sNonZeroCount. */
#define STATE_VERSION_DAC_I2S_NONZEROCOUNT 3

/* Header flags */
#define STATE_FLAG_MEMTRACK  0x01
#define STATE_FLAG_CDROM     0x02

/* Fixed save state size (~2.4 MB).
 * Must never increase between retro_load_game() and retro_unload_game(). */
#define STATE_SIZE  0x260000  /* 2,490,368 bytes */

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

size_t TOMStateSave(uint8_t *buf);
size_t TOMStateLoad(const uint8_t *buf);

size_t CDROMStateSave(uint8_t *buf);
size_t CDROMStateLoad(const uint8_t *buf);

size_t JoystickStateSave(uint8_t *buf);
size_t JoystickStateLoad(const uint8_t *buf);

size_t MTStateSave(uint8_t *buf);
size_t MTStateLoad(const uint8_t *buf);

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
