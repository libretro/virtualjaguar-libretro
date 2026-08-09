#ifndef _VIRTUALJAGUAR_JAGGD_H
#define _VIRTUALJAGUAR_JAGGD_H

/*
 * jaggd.h: Jaguar GameDrive (JagGD) flash cartridge emulation
 *
 * Register/protocol/ABI ground truth: docs/jgd-interface-notes.md
 * (synthesized from RetroHQ's published gdbios_bindings.s, JagStudio
 * v1.11 and BigPEmu behavior).  Issue #312.
 */

#include <stdint.h>
#include <stddef.h>

/* Core option modes (virtualjaguar_jgd) */
#define JGD_MODE_DISABLED 0
#define JGD_MODE_AUTO     1   /* enable when the image exceeds the 6 MB cart window */
#define JGD_MODE_ENABLED  2

/* 16 MB of onboard SDRAM presented as cartridge ROM. */
#define JGD_ROM_SIZE      0x1000000
/* Auto mode threshold: the usable flat cart window ($800000-$DFFEFF). */
#define JGD_AUTO_THRESHOLD 0x5FFF00

/* SPI response FIFO: sized for the largest response we serve (the
 * GDBIOS blob plus its u16 size prefix). */
#define JGD_FIFO_SIZE     512

/* GD registers in JERRY's GPIO2 decode range (gdbios_bindings.s). */
#define JGD_REG_FIRST     0xF16000
#define JGD_REG_LAST      0xF16007
#define JGD_ASIC_SPI_STATUS 0xF16002
#define JGD_ASIC_SPI_DATA   0xF16004
/* Emulator-defined backdoor the served GDBIOS blob pokes for banking /
 * write enable.  Inside the GPIO2 range we already own; nothing else
 * decodes it.  See JGDControlWriteWord for the op encoding. */
#define JGD_BACKDOOR        0xF16006

/* Banking state.  jgdActive gates every hook; when it is 0 the cart
 * read path and the JERRY register windows behave exactly as before
 * (reads of $F16002 fall into the EEPROM catch-all and return 0, which
 * reproduces the stock-console "GD absent" hang in GD-locked titles). */
extern uint8_t  jgdActive;
extern uint8_t  jgdWriteEnabled;
extern uint8_t  jgdPage[6];
extern uint8_t *jgdROM;          /* 16 MB when active, NULL otherwise */

/* Hot-path helpers for the cart read dispatch in jaguar.c: one table
 * lookup + shift, same cost class as the MEMTRACK_PRESENT() gate. */
#define JGD_BANKING() (jgdActive)
#define JGDReadROM8(off) \
   (jgdROM[((uint32_t)jgdPage[(uint32_t)(off) >> 20] << 20) \
           | ((uint32_t)(off) & 0xFFFFF)])

void JGDSetMode(int mode);
int  JGDGetMode(void);

/* Called for every JST_ROM cartridge load; decides activation from the
 * mode + image size and keeps the full (up to 16 MB) image. */
void JGDLoadROM(const uint8_t *buffer, uint32_t size);
/* Drop the image + deactivate (fresh content load boundary). */
void JGDUnload(void);
/* Console reset: identity pages, write-protect, idle SPI engine.
 * Keeps the loaded image and the active flag. */
void JGDReset(void);
/* Full teardown incl. statics (iOS cannot dlclose; retro_deinit path). */
void JGDDone(void);

/* GD write path for ROMWriteEnable'd cart space (offset = addr - $800000). */
void JGDWriteROM8(uint32_t off, uint8_t v);

/* JERRY register window ($F16000-$F16007), called only when jgdActive. */
uint16_t JGDControlReadWord(uint32_t offset);
uint8_t  JGDControlReadByte(uint32_t offset);
void     JGDControlWriteWord(uint32_t offset, uint16_t data);
void     JGDControlWriteByte(uint32_t offset, uint8_t data);

/* Savestate (fixed-size chunk; all-zero when inactive). */
size_t JGDStateSave(uint8_t *buf);
size_t JGDStateLoad(const uint8_t *buf);

#endif
