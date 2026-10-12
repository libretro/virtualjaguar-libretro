#!/usr/bin/env python3
"""Measurement-only patch for test/tools/netlink_sim.c: lets a host process
drive jlink.c's clock and sleep (adds JLinkSimSetClock) and observe the
byte-level link events (adds JLinkSimSetTrace).  Apply to a THROWAWAY
worktree only -- it must never be committed to src/.
Usage: netlink_sim_clockpatch.py <worktree>   (idempotent)

JLinkSimSetTrace(fn): fn(kind, value) is called at
  1 = byte handed to the transport (JLinkWireSendByte)   value = byte
  2 = byte arrived in the RX ring (JLinkRingPush)         value = byte
  3 = game wrote ASIDATA (UARTWriteWord)                  value = byte
  4 = game read ASIDATA with RBF set (UARTReadWord)       value = byte
  5 = JLinkAwaitReply entered a wait                      value = 0
  6 = JLinkAwaitReply returned                            value = 1 reply, 0 not
netlink_sim --btrace FILE logs "inst virtual_us kind value" per event."""
import sys

root = sys.argv[1]


def patch(path, edits):
    s = open(path).read()
    for old, new in edits:
        if new in s:
            continue            # already applied
        if old not in s:
            sys.exit('clockpatch: anchor not found in %s: %r' % (path, old[:50]))
        s = s.replace(old, new, 1)
    open(path, 'w').write(s)


hook = '''#include <time.h>
#include <sys/time.h>
#define JLINK_HAVE_WAIT 1
static long long (*vjSimNow)(void) = 0;
static void (*vjSimSleep)(int) = 0;
void JLinkSimSetClock(long long (*n)(void), void (*sl)(int)) { vjSimNow = n; vjSimSleep = sl; }
void (*vjSimTrace)(int, int) = 0;
void JLinkSimSetTrace(void (*t)(int, int)) { vjSimTrace = t; }
'''
patch(root + '/src/jerry/jlink.c', [
    ('#include <time.h>\n#include <sys/time.h>\n#define JLINK_HAVE_WAIT 1\n',
     hook),
    ('static long long JLinkNowUsec(void)\n{\n   /* CLOCK_MONOTONIC',
     'static long long JLinkNowUsec(void)\n{\n   if (vjSimNow) return vjSimNow();\n   /* CLOCK_MONOTONIC'),
    ('static void JLinkSleepUsec(int usec)\n{\n   /* nanosleep',
     'static void JLinkSleepUsec(int usec)\n{\n   if (vjSimSleep) { vjSimSleep(usec); return; }\n   /* nanosleep'),
    ('   jlinkTxTotal++;\n   if (jlinkMode == JLINK_MODE_LOOPBACK)',
     '   jlinkTxTotal++;\n   if (vjSimTrace) vjSimTrace(1, b);\n   if (jlinkMode == JLINK_MODE_LOOPBACK)'),
    ('   jlinkRing[tail] = b;\n   jlinkCount++;\n',
     '   jlinkRing[tail] = b;\n   jlinkCount++;\n   if (vjSimTrace) vjSimTrace(2, b);\n'),
    ('   start = JLinkNowUsec();\n',
     '   start = JLinkNowUsec();\n   if (vjSimTrace) vjSimTrace(5, 0);\n'),
    ('   now = JLinkNowUsec();\n   jlinkWaitBudgetUsec -= (int)(now - start);',
     '   now = JLinkNowUsec();\n   if (vjSimTrace) vjSimTrace(6, JLinkDeliverable() > 0);\n   jlinkWaitBudgetUsec -= (int)(now - start);'),
])
patch(root + '/src/jerry/uart.c', [
    ('static uint16_t asiCtrl;',
     'extern void (*vjSimTrace)(int, int);\nstatic uint16_t asiCtrl;'),
    ('         v = uartRxData;\n',
     '         if (vjSimTrace && uartRxFull) vjSimTrace(4, uartRxData);\n         v = uartRxData;\n'),
    ('         if (!uartTxBusy)\n         {\n            uartTxShift = ',
     '         if (vjSimTrace) vjSimTrace(3, data & 0xFF);\n         if (!uartTxBusy)\n         {\n            uartTxShift = '),
])
