#!/usr/bin/env python3
"""Measurement-only patch for test/tools/netlink_sim.c: lets a host process
drive jlink.c's clock and sleep (adds JLinkSimSetClock).  Apply to a THROWAWAY
worktree only -- it must never be committed to src/.
Usage: netlink_sim_clockpatch.py <worktree>   (idempotent)"""
import sys,re
p=sys.argv[1]+'/src/jerry/jlink.c'
s=open(p).read()
if 'JLinkSimSetClock' in s: sys.exit(0)
hook='''#include <time.h>
#include <sys/time.h>
#define JLINK_HAVE_WAIT 1
static long long (*vjSimNow)(void) = 0;
static void (*vjSimSleep)(int) = 0;
void JLinkSimSetClock(long long (*n)(void), void (*sl)(int)) { vjSimNow = n; vjSimSleep = sl; }
'''
s=s.replace('''#include <time.h>
#include <sys/time.h>
#define JLINK_HAVE_WAIT 1
''',hook,1)
s=s.replace('static long long JLinkNowUsec(void)\n{\n   /* CLOCK_MONOTONIC','static long long JLinkNowUsec(void)\n{\n   if (vjSimNow) return vjSimNow();\n   /* CLOCK_MONOTONIC',1)
s=s.replace('static long long JLinkNowUsec(void)\n{\n   struct timeval tv;','static long long JLinkNowUsec(void)\n{\n   struct timeval tv;\n   if (vjSimNow) return vjSimNow();',1)
s=s.replace('static void JLinkSleepUsec(int usec)\n{\n   /* nanosleep','static void JLinkSleepUsec(int usec)\n{\n   if (vjSimSleep) { vjSimSleep(usec); return; }\n   /* nanosleep',1)
assert s.count('vjSimNow()')==1 or s.count('vjSimNow()')==2
open(p,'w').write(s)
