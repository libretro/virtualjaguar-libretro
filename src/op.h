//
// OBJECTP.H: Object Processor header file
//

#ifndef __OBJECTP_H__
#define __OBJECTP_H__

#include "types.h"

void OPInit(void);
void OPReset(void);
void OPDone(void);

uint64 OPLoadPhrase(uint32 offset);

void OPProcessList(int scanline, bool render);
uint32 OPGetListPointer(void);
void OPSetStatusRegister(uint32 data);
uint32 OPGetStatusRegister(void);
void OPSetCurrentObject(uint64 object);

//uint8 OPReadByte(uint32, uint32 who = UNKNOWN);
//uint16 OPReadWord(uint32, uint32 who = UNKNOWN);
//void OPWriteByte(uint32, uint8, uint32 who = UNKNOWN);
//void OPWriteWord(uint32, uint16, uint32 who = UNKNOWN);

#define OPFLAG_RELEASE		8					// Bus release bit
#define OPFLAG_TRANS		4					// Transparency bit
#define OPFLAG_RMW			2					// Read-Modify-Write bit
#define OPFLAG_REFLECT		1					// Horizontal mirror bit

// Exported variables

extern uint8 objectp_running;

#endif	// __OBJECTP_H__
