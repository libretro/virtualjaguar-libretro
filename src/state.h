//
// state.h: Machine state save/load support
//
// by James L. Hammons
//

#ifndef __STATE_H__
#define __STATE_H__

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

bool SaveState(void);
bool LoadState(void);

#ifdef __cplusplus
}
#endif

#endif	// __STATE_H__
