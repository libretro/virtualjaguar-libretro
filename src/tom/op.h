//
// OBJECTP.H: Object Processor header file
//

#ifndef __OBJECTP_H__
#define __OBJECTP_H__

#include <stdint.h>

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

void OPInit(void);
void OPReset(void);
void OPDone(void);

uint64_t OPLoadPhrase(uint32_t offset);

void OPProcessList(int halfline, bool render);
void OPProcessListNoBitmaps(int halfline);
uint32_t OPGetListPointer(void);
void OPSetStatusRegister(uint32_t data);
uint32_t OPGetStatusRegister(void);
void OPSetCurrentObject(uint64_t object);
void OPNotifyOBFWrite(void);

/* App debug interface (src/debug/vjdebug.c): a one-shot capture of the
 * whole display list, taken at the top of the first OPProcessList() call
 * after it is armed that finds a list -- before the OP rewrites HEIGHT/DATA/
 * REMAINDER of any bitmap (it does so per halfline, so the list in RAM after
 * a frame is not the list the game built).  Both arms of every branch object
 * are followed, each object recorded once.  OPDbgSnapState is the only thing
 * the walk tests: nonzero only between an arm and the capture.
 * Host-transient: zero savestate fields, invisible to the emulated machine
 * (the reads use who == DEBUGGER, which the bus arbiter does not charge). */
struct vj_debug_op_obj;
extern int opDbgSnapState;
void OPDbgSnapArm(void);
int  OPDbgSnapGet(struct vj_debug_op_obj *o, int max, unsigned *halfline);
void OPDbgSnapReset(void);

#define OPFLAG_RELEASE		8					// Bus release bit
#define OPFLAG_TRANS		4					// Transparency bit
#define OPFLAG_RMW			2					// Read-Modify-Write bit
#define OPFLAG_REFLECT		1					// Horizontal mirror bit

#ifdef __cplusplus
}
#endif

#endif	// __OBJECTP_H__
