//
// Log handler
//
// Originally by David Raingeard (Cal2)
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Caz (BeOS)
// Cleanups/new stuff by James Hammons
// (C) 2010 Underground Software
//
// JLH = James Hammons <jlhamm@acm.org>
//
// Who  When        What
// ---  ----------  -------------------------------------------------------------
// JLH  01/16/2010  Created this log ;-)
// JLH  07/11/2011  Instead of dumping out on max log file size being reached, we
//                  now just silently ignore any more output. 10 megs ought to be
//                  enough for anybody. ;-) Except when it isn't. :-P
//

#include "log.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>

int LogInit(const char * path)
{
	return 1;
}

void LogDone(void)
{
}

//
// This logger is used mainly to ensure that text gets written to the log file
// even if the program crashes. The performance hit is acceptable in this case!
//
void WriteLog(const char * text, ...)
{
}
