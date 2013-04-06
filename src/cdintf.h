//
// CDINTF.H: OS agnostic CDROM access funcions
//
// by James L. Hammons
//

#ifndef __CDINTF_H__
#define __CDINTF_H__

#include "types.h"

bool CDIntfInit(void);
void CDIntfDone(void);
bool CDIntfReadBlock(uint32, uint8 *);
uint32 CDIntfGetNumSessions(void);
void CDIntfSelectDrive(uint32);
uint32 CDIntfGetCurrentDrive(void);
const uint8 * CDIntfGetDriveName(uint32);
uint8 CDIntfGetSessionInfo(uint32, uint32);
uint8 CDIntfGetTrackInfo(uint32, uint32);

#endif	// __CDINTF_H__
