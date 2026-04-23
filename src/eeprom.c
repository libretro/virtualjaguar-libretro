//
// Jaguar EEPROM handler
//
// by Cal2
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Caz (BeOS)
// Cleanups/enhancements by James Hammons
// (C) 2010 Underground Software
//
// JLH = James Hammons <jlhamm@acm.org>
//
// Who  When        What
// ---  ----------  -------------------------------------------------------------
// JLH  01/16/2010  Created this log ;-)
//

#include "eeprom.h"

#include <stdlib.h>
#include <boolean.h>
#include <string.h>								// For memset

uint16_t eeprom_ram[64];

/* Callback to sync the save buffer when EEPROM is modified.
 * Set by libretro.c to keep RETRO_MEMORY_SAVE_RAM up to date. */
void (*eeprom_dirty_cb)(void) = NULL;

// Private function prototypes
static void EEPROMSave(void);
static void eeprom_set_di(uint32_t state);
static void eeprom_set_cs(uint32_t state);
static uint32_t eeprom_get_do(void);

enum { EE_STATE_START = 1, EE_STATE_OP_A, EE_STATE_OP_B, EE_STATE_0, EE_STATE_1,
	EE_STATE_2, EE_STATE_3, EE_STATE_0_0, EE_READ_ADDRESS, EE_STATE_0_0_0,
	EE_STATE_0_0_1, EE_STATE_0_0_2, EE_STATE_0_0_3, EE_STATE_0_0_1_0, EE_READ_DATA,
	EE_STATE_BUSY, EE_STATE_1_0, EE_STATE_1_1, EE_STATE_2_0, EE_STATE_3_0 };

// Local global variables

static uint16_t jerry_ee_state = EE_STATE_START;
static uint16_t jerry_ee_op = 0;
static uint16_t jerry_ee_rstate = 0;
static uint16_t jerry_ee_address_data = 0;
static uint16_t jerry_ee_address_cnt = 6;
static uint16_t jerry_ee_data = 0;
static uint16_t jerry_ee_data_cnt = 16;
static uint16_t jerry_writes_enabled = 0;
static uint16_t jerry_ee_direct_jump = 0;


static bool eeprom_initialized = false;

void EepromInit(void)
{
   /* On first init (power-on), fill EEPROM with 0xFF (blank state).
    * The frontend will overwrite this via RETRO_MEMORY_SAVE_RAM
    * before the first retro_run() if a save file exists. */
   if (!eeprom_initialized)
   {
      memset(eeprom_ram, 0xFF, 64 * sizeof(uint16_t));
      eeprom_initialized = true;
   }
}


void EepromReset(void)
{
   /* Preserve EEPROM contents across soft resets — save data must
    * survive retro_reset(). Only the state machine is reset. */
   jerry_ee_state = EE_STATE_START;
   jerry_ee_op = 0;
   jerry_ee_rstate = 0;
   jerry_ee_address_data = 0;
   jerry_ee_address_cnt = 6;
   jerry_ee_data = 0;
   jerry_ee_data_cnt = 16;
   jerry_writes_enabled = 0;
   jerry_ee_direct_jump = 0;
}


void EepromDone(void)
{
   eeprom_initialized = false;
}


static void EEPROMSave(void)
{
   /* Notify libretro.c to sync the save buffer so frontends that
    * cache the RETRO_MEMORY_SAVE_RAM pointer see fresh data. */
   if (eeprom_dirty_cb)
      eeprom_dirty_cb();
}


uint8_t EepromReadByte(uint32_t offset)
{
	switch (offset)
	{
	case 0xF14001:
		return eeprom_get_do();
	case 0xF14801:
		break;
	case 0xF15001:
		eeprom_set_cs(1);
		break;
	}

	return 0x00;
}


uint16_t EepromReadWord(uint32_t offset)
{
	return ((uint16_t)EepromReadByte(offset + 0) << 8) | EepromReadByte(offset + 1);
}


void EepromWriteByte(uint32_t offset, uint8_t data)
{
	switch (offset)
	{
	case 0xF14001:
		break;
	case 0xF14801:
		eeprom_set_di(data & 0x01);
		break;
	case 0xF15001:
		eeprom_set_cs(1);
		break;
	}
}


void EepromWriteWord(uint32_t offset, uint16_t data)
{
	EepromWriteByte(offset + 0, (data >> 8) & 0xFF);
	EepromWriteByte(offset + 1, data & 0xFF);
}


/*
;
;   Commands specific to the National Semiconductor NM93C14
;
;
;  9-bit commands..
;			 876543210
eEWDS	equ	%100000000		;Erase/Write disable (default)
eWRAL	equ	%100010000		;Writes all registers
eERAL	equ	%100100000		;Erase all registers
eEWEN	equ	%100110000		;Erase/write Enable
eWRITE	equ	%101000000		;Write selected register
eREAD	equ	%110000000		;read from EEPROM
eERASE	equ	%111000000		;Erase selected register
*/


static void eeprom_set_di(uint32_t data)
{
   unsigned i;

	switch (jerry_ee_state)
	{
	case EE_STATE_START:
		jerry_ee_state = EE_STATE_OP_A;
		break;
	case EE_STATE_OP_A:
		jerry_ee_op = (data << 1);
		jerry_ee_state = EE_STATE_OP_B;
		break;
	case EE_STATE_OP_B:
		jerry_ee_op |= data;
		jerry_ee_direct_jump = 0;

		switch (jerry_ee_op)
		{
		// Opcode 00: eEWEN, eERAL, eWRAL, eEWNDS
		case 0: jerry_ee_state = EE_STATE_0; break;
		// Opcode 01: eWRITE (Write selected register)
		case 1: jerry_ee_state = EE_STATE_1; break;
		// Opcode 10: eREAD (Read from EEPROM)
		case 2: jerry_ee_state = EE_STATE_2; break;
		// Opcode 11: eERASE (Erase selected register)
		case 3: jerry_ee_state = EE_STATE_3; break;
		}

		eeprom_set_di(data);
		break;
	case EE_STATE_0:
		jerry_ee_rstate = EE_STATE_0_0;
		jerry_ee_state = EE_READ_ADDRESS;
		jerry_ee_direct_jump = 1;
		jerry_ee_address_cnt = 6;
		jerry_ee_address_data = 0;
		break;
	case EE_STATE_0_0:
		switch ((jerry_ee_address_data >> 4) & 0x03)
		{
		// Opcode 00 00: eEWDS (Erase/Write disable)
		case 0: jerry_ee_state=EE_STATE_0_0_0; break;
		// Opcode 00 01: eWRAL (Write all registers)
		case 1: jerry_ee_state=EE_STATE_0_0_1; break;
		// Opcode 00 10: eERAL (Erase all registers)
		case 2: jerry_ee_state=EE_STATE_0_0_2; break;
		// Opcode 00 11: eEWEN (Erase/Write enable)
		case 3: jerry_ee_state=EE_STATE_0_0_3; break;
		}

		eeprom_set_di(data);
		break;
	case EE_STATE_0_0_0:
		// writes disable (read only)
		jerry_writes_enabled = 0;
		jerry_ee_state = EE_STATE_START;
		break;
	case EE_STATE_0_0_1:
		// writes all
		jerry_ee_rstate = EE_STATE_0_0_1_0;
		jerry_ee_state = EE_READ_DATA;
		jerry_ee_data_cnt = 16;
		jerry_ee_data = 0;
		jerry_ee_direct_jump = 1;
		break;
	case EE_STATE_0_0_1_0:
		if (jerry_writes_enabled)
		{
			for(i = 0; i < 64; i++)
				eeprom_ram[i] = jerry_ee_data;

			EEPROMSave();
		}
		jerry_ee_state = EE_STATE_BUSY;
		break;
	case EE_STATE_0_0_2:
		// erase all
		if (jerry_writes_enabled)
			for(i = 0; i < 64; i++)
				eeprom_ram[i] = 0xFFFF;

		jerry_ee_state = EE_STATE_BUSY;
		break;
	case EE_STATE_0_0_3:
		// writes enable
		jerry_writes_enabled = 1;
		jerry_ee_state = EE_STATE_START;
		break;
	case EE_STATE_1:
		jerry_ee_rstate = EE_STATE_1_0;
		jerry_ee_state = EE_READ_ADDRESS;
		jerry_ee_address_cnt = 6;
		jerry_ee_address_data = 0;
		jerry_ee_direct_jump = 1;
		break;
	case EE_STATE_1_0:
		jerry_ee_rstate = EE_STATE_1_1;
		jerry_ee_state = EE_READ_DATA;
		jerry_ee_data_cnt = 16;
		jerry_ee_data = 0;
		jerry_ee_direct_jump = 1;
		break;
	case EE_STATE_1_1:
		if (jerry_writes_enabled)
		{
			eeprom_ram[jerry_ee_address_data] = jerry_ee_data;
			EEPROMSave();
		}

		jerry_ee_state = EE_STATE_BUSY;
		break;
	case EE_STATE_2:
		jerry_ee_rstate = EE_STATE_2_0;
		jerry_ee_state = EE_READ_ADDRESS;
		jerry_ee_address_cnt = 6;
		jerry_ee_address_data = 0;
		jerry_ee_data_cnt = 16;
		jerry_ee_data = 0;
		break;
	case EE_STATE_3:
		jerry_ee_rstate = EE_STATE_3_0;
		jerry_ee_state = EE_READ_ADDRESS;
		jerry_ee_address_cnt = 6;
		jerry_ee_address_data = 0;
		jerry_ee_direct_jump = 1;
		break;
	case EE_STATE_3_0:
		if (jerry_writes_enabled)
			eeprom_ram[jerry_ee_address_data] = 0xFFFF;

		jerry_ee_state = EE_STATE_BUSY;
		break;
	case EE_READ_DATA:
		jerry_ee_data <<= 1;
		jerry_ee_data |= data;
		jerry_ee_data_cnt--;

		if (!jerry_ee_data_cnt)
		{
			jerry_ee_state = jerry_ee_rstate;

			if (jerry_ee_direct_jump)
				eeprom_set_di(data);
		}

		break;
	case EE_READ_ADDRESS:
		jerry_ee_address_data <<= 1;
		jerry_ee_address_data |= data;
		jerry_ee_address_cnt--;

		if (!jerry_ee_address_cnt)
		{
			jerry_ee_state = jerry_ee_rstate;

			if (jerry_ee_direct_jump)
				eeprom_set_di(data);
		}

		break;
	default:
		jerry_ee_state = EE_STATE_OP_A;
	}
}


static void eeprom_set_cs(uint32_t state)
{
	jerry_ee_state = EE_STATE_START;
	jerry_ee_op = 0;
	jerry_ee_rstate = 0;
	jerry_ee_address_data = 0;
	jerry_ee_address_cnt = 6;
	jerry_ee_data = 0;
	jerry_ee_data_cnt = 16;
	jerry_writes_enabled = 1;
}


static uint32_t eeprom_get_do(void)
{
	uint16_t data = 1;

	switch (jerry_ee_state)
	{
	case EE_STATE_START:
		data = 1;
		break;
	case EE_STATE_BUSY:
		jerry_ee_state = EE_STATE_START;
		data = 0;
		break;
	case EE_STATE_2_0:
		jerry_ee_data_cnt--;
		data = (eeprom_ram[jerry_ee_address_data] >> jerry_ee_data_cnt) & 0x01;

		if (!jerry_ee_data_cnt)
			jerry_ee_state = EE_STATE_START;
		break;
	}

	return data;
}

#include "state.h"

size_t EepromStateSave(uint8_t *buf)
{
	uint8_t *start = buf;

	STATE_SAVE_VAR(buf, jerry_ee_state);
	STATE_SAVE_VAR(buf, jerry_ee_op);
	STATE_SAVE_VAR(buf, jerry_ee_rstate);
	STATE_SAVE_VAR(buf, jerry_ee_address_data);
	STATE_SAVE_VAR(buf, jerry_ee_address_cnt);
	STATE_SAVE_VAR(buf, jerry_ee_data);
	STATE_SAVE_VAR(buf, jerry_ee_data_cnt);
	STATE_SAVE_VAR(buf, jerry_writes_enabled);
	STATE_SAVE_VAR(buf, jerry_ee_direct_jump);

	/* EEPROM data arrays */
	STATE_SAVE_BUF(buf, eeprom_ram, sizeof(eeprom_ram));

	return (size_t)(buf - start);
}

size_t EepromStateLoad(const uint8_t *buf)
{
	const uint8_t *start = buf;

	STATE_LOAD_VAR(buf, jerry_ee_state);
	STATE_LOAD_VAR(buf, jerry_ee_op);
	STATE_LOAD_VAR(buf, jerry_ee_rstate);
	STATE_LOAD_VAR(buf, jerry_ee_address_data);
	STATE_LOAD_VAR(buf, jerry_ee_address_cnt);
	STATE_LOAD_VAR(buf, jerry_ee_data);
	STATE_LOAD_VAR(buf, jerry_ee_data_cnt);
	STATE_LOAD_VAR(buf, jerry_writes_enabled);
	STATE_LOAD_VAR(buf, jerry_ee_direct_jump);

	/* EEPROM data arrays */
	STATE_LOAD_BUF(buf, eeprom_ram, sizeof(eeprom_ram));

	return (size_t)(buf - start);
}

