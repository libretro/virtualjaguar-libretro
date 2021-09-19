//
// log.h: Logfile support
//

#ifndef __LOG_H__
#define __LOG_H__

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int LogInit(const char *);
void LogDone(void);
void WriteLog(const char * text, ...);

#ifdef __cplusplus
}
#endif

#endif	// __LOG_H__
