//
// Memory Track cartridge emulation
//
// by James Hammons
// (C) 2016 Underground Software
//
// The Memory Track is just a large(-ish) EEPROM, holding 128K. We emulate the
// Atmel part, since it seems to be easier to deal with than the AMD part. The
// way it works is the 68K checks in its R/W functions to see if the MT is
// inserted, and, if so, call the R/W functions here.
//
// NOTE (2026): the two claims that used to follow here -- that the part is
// selected by switching the ROM width to 32-bit, and that it reads/writes a
// single byte into a long space in the $8xxxxx ROM window -- were both wrong,
// and between them made the device unreachable for CD content (see #258 and
// the MiSTer notes below).  The ROM-width test has no hardware counterpart,
// and the NVRAM is a plain 16-bit window at $900000.
//
// JLH = James Hammons <jlhamm@acm.org>
//
// Who  When        What
// ---  ----------  -----------------------------------------------------------
// JLH  06/12/2016  Created this file ;-)
//

#include "memtrack.h"

#include <stdlib.h>
#include <string.h>

/*
 * Address map and detection protocol corrected in 2026 against the MiSTer
 * Atari Jaguar core (https://github.com/MiSTer-devel/Jaguar_MiSTer), which is
 * the only open-source implementation known to work -- the Jaguar TRM and the
 * official SDK document none of this.  Jaguar.sv carries the 68K detection
 * routine in comments; the relevant facts, with thanks to its authors:
 *
 *   - Unlock:  $AA -> $815554, $55 -> $80AAA8, then a command to $815554.
 *              $90 = ID mode ("override memtrack flash in place of cart rom"),
 *              $F0 = undo.
 *   - In ID mode, $800000 reads the manufacturer ID and $800004 the device ID
 *              (ATMEL $1F / AT29C010 $D5, or AMD $01 / AM29F010 $20).
 *   - $80AAA8 reads back $0055 after the unlock write (the "ROMULATOR" probe).
 *   - "Actual save data is at 9XXXXXX" -- the NVRAM window is $900000, NOT the
 *              $8xxxxx ROM window.
 *   - MiSTer gates purely on presence (`memtrack = (forced || cd_loaded) &&
 *              memtrak_bios_exists`); there is no MEMCON1/ROMWIDTH condition
 *              on hardware.
 *
 * Everything the part does not explicitly claim must read through to the
 * normal cart ROM / CD BIOS, or a disc-based game loses its BIOS at $800000.
 */

enum { MT_NONE, MT_PROD_ID, MT_RESET, MT_WRITE_ENABLE };
enum { MT_IDLE, MT_PHASE1, MT_PHASE2 };

uint8_t mtMem[0x20000];
uint8_t mtCommand = MT_NONE;
uint8_t mtState = MT_IDLE;
/* Set once the unlock write hits $80AAA8; makes that address read back $0055
 * (MiSTer's memtrack_override1). */
static uint8_t mtOverride1 = 0;
void (*mt_dirty_cb)(void) = NULL;
static int mt_initialized = 0;

// Private function prototypes
void MTStateMachine(uint8_t reg, uint16_t data);


void MTInit(void)
{
	/* On first init (power-on), fill with 0xFF (blank NVRAM state).
	 * The frontend will overwrite this via RETRO_MEMORY_SAVE_RAM
	 * before the first retro_run() if a save file exists. */
	if (!mt_initialized)
	{
		memset(mtMem, 0xFF, 0x20000);
		mt_initialized = 1;
	}
}


void MTReset(void)
{
	/* Preserve Memory Track contents across soft resets.
	 * Only reset the command state machine. */
	mtCommand = MT_NONE;
	mtState = MT_IDLE;
	mtOverride1 = 0;
}


void MTDone(void)
{
	mt_initialized = 0;
}


/* The part only answers inside its own windows.  Anything else in cart space
 * belongs to the cartridge ROM or the CD BIOS. */
bool MTClaimsRead(uint32_t addr)
{
	if (addr >= MT_DATA_BASE && addr < MT_DATA_END)
		return true;
	/* ID reads are valid only while the unlock sequence has selected ID
	 * mode -- outside it, $800000/$800004 are ordinary ROM. */
	if (mtCommand == MT_PROD_ID && (addr == MT_ID_MANUF || addr == MT_ID_DEVICE))
		return true;
	if (mtOverride1 && addr == MT_CMD_UNLOCK2)
		return true;

	return false;
}


bool MTClaimsWrite(uint32_t addr)
{
	if (addr >= MT_DATA_BASE && addr < MT_DATA_END)
		return true;
	/* Command writes are always accepted: they are how the part is woken up
	 * in the first place, and cart ROM is not writable anyway. */
	if (addr == MT_CMD_UNLOCK1 || addr == MT_CMD_UNLOCK2)
		return true;

	return false;
}


uint16_t MTReadWord(uint32_t addr)
{
	if (addr >= MT_DATA_BASE && addr < MT_DATA_END)
	{
		uint32_t off = (addr - MT_DATA_BASE) & (MT_MEM_SIZE - 2);
		return (uint16_t)((mtMem[off] << 8) | mtMem[off + 1]);
	}

	if (mtOverride1 && addr == MT_CMD_UNLOCK2)
		return 0x0055;

	if (mtCommand == MT_PROD_ID)
	{
		if (addr == MT_ID_MANUF)
			return 0x001F;		/* ATMEL manufacturer ID */
		if (addr == MT_ID_DEVICE)
			return 0x00D5;		/* AT29C010 device ID */
	}

	return 0;
}


/* Byte accessors.  The 68K reaches the device with move.b as readily as
 * move.w -- the original NVM BIOS's Getbyte/Putbyte are byte operations --
 * so these must exist or byte-granular drivers silently read cart ROM and
 * write nowhere. */
uint8_t MTReadByte(uint32_t addr)
{
	uint16_t w = MTReadWord(addr & ~1u);
	return (addr & 1) ? (uint8_t)(w & 0xFF) : (uint8_t)(w >> 8);
}


void MTWriteByte(uint32_t addr, uint8_t data)
{
	if (addr >= MT_DATA_BASE && addr < MT_DATA_END)
	{
		if (mtCommand == MT_WRITE_ENABLE)
		{
			mtMem[(addr - MT_DATA_BASE) & (MT_MEM_SIZE - 1)] = data;
			if (mt_dirty_cb)
				mt_dirty_cb();
		}
		return;
	}

	/* A byte write to a command address carries the command in the low
	 * byte; feed the state machine the same way a word write would. */
	if (addr == MT_CMD_UNLOCK1 || addr == MT_CMD_UNLOCK2)
		MTWriteWord(addr & ~1u, (uint16_t)data);
}


uint32_t MTReadLong(uint32_t addr)
{
	return ((uint32_t)MTReadWord(addr) << 16) | MTReadWord(addr + 2);
}


void MTWriteWord(uint32_t addr, uint16_t data)
{
	if (addr >= MT_DATA_BASE && addr < MT_DATA_END)
	{
		/* Writes only land once the unlock sequence has armed the part. */
		if (mtCommand == MT_WRITE_ENABLE)
		{
			uint32_t off = (addr - MT_DATA_BASE) & (MT_MEM_SIZE - 2);
			mtMem[off]     = (uint8_t)(data >> 8);
			mtMem[off + 1] = (uint8_t)(data & 0xFF);
			if (mt_dirty_cb)
				mt_dirty_cb();
		}
		return;
	}

	switch (addr)
	{
	case MT_CMD_UNLOCK1:
		MTStateMachine(0, data);
		break;
	case MT_CMD_UNLOCK2:
		MTStateMachine(1, data);
		/* MiSTer latches this so the address reads back $0055 afterwards
		 * (the ROMULATOR probe); it is never cleared by a later command. */
		if (data == 0x0055)
			mtOverride1 = 1;
		break;
	}
}


void MTWriteLong(uint32_t addr, uint32_t data)
{
	// Strip off lower 3 bits of the passed in address
	addr &= 0xFFFFFC;

	MTWriteWord(addr + 0, data & 0xFFFF);
	MTWriteWord(addr + 2, data >> 16);
}


void MTStateMachine(uint8_t reg, uint16_t data)
{
	switch (mtState)
	{
	case MT_IDLE:
		if ((reg == 0) && (data == 0xAA))
			mtState = MT_PHASE1;

		break;
	case MT_PHASE1:
		if ((reg == 1) && (data == 0x55))
			mtState = MT_PHASE2;
		else
			mtState = MT_IDLE;

		break;
	case MT_PHASE2:
		if (reg == 0)
		{
			if (data == 0x90)		// Product ID
				mtCommand = MT_PROD_ID;
			else if (data == 0xF0)	// Reset
				mtCommand = MT_NONE;
			else if (data == 0xA0)	// Write enagle
				mtCommand = MT_WRITE_ENABLE;
			else
				mtCommand = MT_NONE;
		}

		mtState = MT_IDLE;
		break;
	}
}

#include "state.h"

size_t MTStateSave(uint8_t *buf)
{
	uint8_t *start = buf;

	STATE_SAVE_BUF(buf, mtMem, sizeof(mtMem));
	STATE_SAVE_VAR(buf, mtCommand);
	STATE_SAVE_VAR(buf, mtState);
	STATE_SAVE_VAR(buf, mtOverride1);

	return (size_t)(buf - start);
}

size_t MTStateLoad(const uint8_t *buf, uint32_t version)
{
	const uint8_t *start = buf;

	STATE_LOAD_BUF(buf, mtMem, sizeof(mtMem));
	STATE_LOAD_VAR(buf, mtCommand);
	STATE_LOAD_VAR(buf, mtState);
	/* Pre-v7 states have no override flag; leave it clear so the part
	 * simply re-probes rather than reading back a stale $0055. */
	if (version >= STATE_VERSION_MEMTRACK_OVERRIDE)
		STATE_LOAD_VAR(buf, mtOverride1);
	else
		mtOverride1 = 0;

	return (size_t)(buf - start);
}
