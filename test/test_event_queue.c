#include <stdio.h>
#include <stdlib.h>

#include "src/core/event.h"

static int halfline_calls;
static int tompit_calls;
static int jerry_pit1_calls;
static int jerry_pit2_calls;

static int nearly_equal(double actual, double expected)
{
   double diff = actual - expected;
   if (diff < 0.0)
      diff = -diff;

   return diff < 0.000001;
}

static void assert_double(const char *label, double actual, double expected)
{
   if (!nearly_equal(actual, expected))
   {
      fprintf(stderr, "%s: expected %.6f, got %.6f\n", label, expected, actual);
      exit(1);
   }
}

static void assert_int(const char *label, int actual, int expected)
{
   if (actual != expected)
   {
      fprintf(stderr, "%s: expected %d, got %d\n", label, expected, actual);
      exit(1);
   }
}

void HalflineCallback(void)
{
   halfline_calls++;
}

void TOMPITCallback(void)
{
   tompit_calls++;
}

void JERRYPIT1Callback(void)
{
   jerry_pit1_calls++;
}

void JERRYPIT2Callback(void)
{
   jerry_pit2_calls++;
}

void JERRYI2SCallback(void)
{
}

void DSPSampleCallback(void)
{
}

static void reset_counts(void)
{
   halfline_calls = 0;
   tompit_calls = 0;
   jerry_pit1_calls = 0;
   jerry_pit2_calls = 0;
}

static void test_main_queue_due_now_clamp(void)
{
   InitializeEventList();
   reset_counts();

   SetCallbackTime(HalflineCallback, 10.0, EVENT_MAIN);
   SetCallbackTime(TOMPITCallback, 20.0, EVENT_MAIN);
   SubtractEventTimes(12.0, EVENT_MAIN);

   assert_double("main due-now time", GetTimeToNextEvent(EVENT_MAIN), 0.0);
   HandleNextEvent(EVENT_MAIN);
   assert_int("main due-now callback", halfline_calls, 1);
   assert_int("main later callback before due", tompit_calls, 0);

   assert_double("main later event preserved", GetTimeToNextEvent(EVENT_MAIN), 8.0);
   HandleNextEvent(EVENT_MAIN);
   assert_int("main later callback", tompit_calls, 1);
}

static void test_jerry_queue_due_now_clamp(void)
{
   InitializeEventList();
   reset_counts();

   SetCallbackTime(JERRYPIT1Callback, 10.0, EVENT_JERRY);
   SetCallbackTime(JERRYPIT2Callback, 20.0, EVENT_JERRY);
   SubtractEventTimes(12.0, EVENT_JERRY);

   assert_double("jerry due-now time", GetTimeToNextEvent(EVENT_JERRY), 0.0);
   HandleNextEvent(EVENT_JERRY);
   assert_int("jerry due-now callback", jerry_pit1_calls, 1);
   assert_int("jerry later callback before due", jerry_pit2_calls, 0);

   assert_double("jerry later event preserved", GetTimeToNextEvent(EVENT_JERRY), 8.0);
   HandleNextEvent(EVENT_JERRY);
   assert_int("jerry later callback", jerry_pit2_calls, 1);
}

static void test_schedule_negative_as_due_now(void)
{
   InitializeEventList();
   reset_counts();

   SetCallbackTime(HalflineCallback, -1.0, EVENT_MAIN);
   assert_double("negative schedule clamp", GetTimeToNextEvent(EVENT_MAIN), 0.0);
   HandleNextEvent(EVENT_MAIN);
   assert_int("negative schedule callback", halfline_calls, 1);

   SetCallbackTime(TOMPITCallback, 10.0, EVENT_MAIN);
   AdjustCallbackTime(TOMPITCallback, -5.0);
   assert_double("negative adjust clamp", GetTimeToNextEvent(EVENT_MAIN), 0.0);
   HandleNextEvent(EVENT_MAIN);
   assert_int("negative adjust callback", tompit_calls, 1);
}

/* RemoveCallback must drop a pending slot without firing it. */
static void test_remove_callback(void)
{
   InitializeEventList();
   reset_counts();

   SetCallbackTime(HalflineCallback, 10.0, EVENT_MAIN);
   SetCallbackTime(TOMPITCallback, 20.0, EVENT_MAIN);
   RemoveCallback(TOMPITCallback);

   assert_double("after remove next MAIN", GetTimeToNextEvent(EVENT_MAIN), 10.0);
   HandleNextEvent(EVENT_MAIN);
   assert_int("halfline after pit removed", halfline_calls, 1);
   assert_int("pit never ran", tompit_calls, 0);
}

/* AdjustCallbackTime replaces the delay for an already-scheduled callback. */
static void test_adjust_callback_reschedule(void)
{
   InitializeEventList();
   reset_counts();

   SetCallbackTime(HalflineCallback, 100.0, EVENT_MAIN);
   AdjustCallbackTime(HalflineCallback, 33.0);
   assert_double("adjust MAIN time", GetTimeToNextEvent(EVENT_MAIN), 33.0);
}

/* When two MAIN events share the same due time, the lower slot index wins.
 * (unordered list minimum uses strict < so first minimum is kept.)
 * Caller must run GetTimeToNextEvent before each HandleNextEvent (matches
 * JaguarExecuteNew interleave). */
static void test_main_tie_break_lower_index(void)
{
   InitializeEventList();
   reset_counts();

   SetCallbackTime(HalflineCallback, 7.0, EVENT_MAIN);
   SetCallbackTime(TOMPITCallback, 7.0, EVENT_MAIN);

   (void)GetTimeToNextEvent(EVENT_MAIN);
   HandleNextEvent(EVENT_MAIN);
   assert_int("first tie: halfline first", halfline_calls, 1);
   assert_int("first tie: pit not yet", tompit_calls, 0);

   (void)GetTimeToNextEvent(EVENT_MAIN);
   HandleNextEvent(EVENT_MAIN);
   assert_int("second tie: pit", tompit_calls, 1);
}

/* Order of insertion flips which callback shares slot index 0. */
static void test_main_tie_break_order_matters(void)
{
   InitializeEventList();
   reset_counts();

   SetCallbackTime(TOMPITCallback, 4.0, EVENT_MAIN);
   SetCallbackTime(HalflineCallback, 4.0, EVENT_MAIN);

   (void)GetTimeToNextEvent(EVENT_MAIN);
   HandleNextEvent(EVENT_MAIN);
   assert_int("pit scheduled first wins tie", tompit_calls, 1);
   assert_int("halfline not yet", halfline_calls, 0);

   (void)GetTimeToNextEvent(EVENT_MAIN);
   HandleNextEvent(EVENT_MAIN);
   assert_int("halfline second", halfline_calls, 1);
}

static void test_main_subtract_does_not_touch_jerry(void)
{
   InitializeEventList();
   reset_counts();

   SetCallbackTime(JERRYPIT1Callback, 50.0, EVENT_JERRY);
   SubtractEventTimes(12.0, EVENT_MAIN);
   assert_double("jerry time after MAIN subtract", GetTimeToNextEvent(EVENT_JERRY), 50.0);
}

/* SetCallbackTime always uses a fresh slot — it does NOT replace an existing
 * entry for the same callback. Two SetCallbackTime calls register two
 * independent firings; callers that want to reschedule must use
 * AdjustCallbackTime (or RemoveCallback first). This is intentional per the
 * "no checking, no nada" comment in event.c and is what JaguarExecuteNew /
 * JERRYI2SCallback rely on. */
static void test_schedule_does_not_replace_existing(void)
{
   InitializeEventList();
   reset_counts();

   SetCallbackTime(HalflineCallback, 10.0, EVENT_MAIN);
   SetCallbackTime(HalflineCallback, 30.0, EVENT_MAIN);

   (void)GetTimeToNextEvent(EVENT_MAIN);
   HandleNextEvent(EVENT_MAIN);
   assert_int("first scheduled fires first", halfline_calls, 1);

   /* The second copy survives. After firing the 10.0 event, the elapsed
    * time was subtracted from every slot, so the second entry now reports
    * 30 - 10 = 20.0. */
   assert_double("duplicate copy still pending", GetTimeToNextEvent(EVENT_MAIN), 20.0);
   HandleNextEvent(EVENT_MAIN);
   assert_int("duplicate fires after first", halfline_calls, 2);
}

/* AdjustCallbackTime on a callback that was never scheduled is a no-op:
 * it must not create a phantom entry in either queue. */
static void test_adjust_unknown_is_noop(void)
{
   InitializeEventList();
   reset_counts();

   SetCallbackTime(TOMPITCallback, 5.0, EVENT_MAIN);
   AdjustCallbackTime(HalflineCallback, 1.0);

   (void)GetTimeToNextEvent(EVENT_MAIN);
   HandleNextEvent(EVENT_MAIN);
   assert_int("registered cb fired", tompit_calls, 1);
   assert_int("unknown adjust did not register", halfline_calls, 0);
}

/* AdjustCallbackTime targets the JERRY queue too, without disturbing MAIN. */
static void test_adjust_jerry_only_touches_jerry(void)
{
   InitializeEventList();
   reset_counts();

   SetCallbackTime(HalflineCallback, 50.0, EVENT_MAIN);
   SetCallbackTime(JERRYPIT1Callback, 100.0, EVENT_JERRY);
   AdjustCallbackTime(JERRYPIT1Callback, 25.0);

   assert_double("MAIN unaffected", GetTimeToNextEvent(EVENT_MAIN), 50.0);
   assert_double("JERRY adjusted in place", GetTimeToNextEvent(EVENT_JERRY), 25.0);
}

/* JaguarExecuteNew dispatcher cross-queue contract:
 *   if (timeToJerryEvent < timeToMainEvent) { handle JERRY; SubtractEventTimes(elapsed, EVENT_MAIN) }
 *   else                                    { handle MAIN;  SubtractEventTimes(elapsed, EVENT_JERRY) }
 *
 * Two consequences pinned by the tests below:
 *   1. Strict `<` means equal due-times go to MAIN (JERRY only wins when strictly sooner).
 *   2. Whichever queue fires, the OTHER queue's pending events are decremented by the
 *      elapsed time so both queues stay on the same wall clock. */
static void test_dispatch_tie_goes_to_main(void)
{
   double tMain;
   double tJerry;
   double elapsed;

   InitializeEventList();
   reset_counts();

   /* Both queues have an event at t=10. Tie must resolve to MAIN. */
   SetCallbackTime(HalflineCallback, 10.0, EVENT_MAIN);
   SetCallbackTime(JERRYPIT1Callback, 10.0, EVENT_JERRY);

   tMain  = GetTimeToNextEvent(EVENT_MAIN);
   tJerry = GetTimeToNextEvent(EVENT_JERRY);
   assert_double("tie: MAIN at 10", tMain, 10.0);
   assert_double("tie: JERRY at 10", tJerry, 10.0);

   /* Mirror JaguarExecuteNew's branch exactly. */
   if (tJerry < tMain)
   {
      elapsed = tJerry;
      SubtractEventTimes(elapsed, EVENT_MAIN);
      HandleNextEvent(EVENT_JERRY);
   }
   else
   {
      elapsed = tMain;
      SubtractEventTimes(elapsed, EVENT_JERRY);
      HandleNextEvent(EVENT_MAIN);
   }

   assert_int("tie: MAIN fired", halfline_calls, 1);
   assert_int("tie: JERRY did not fire", jerry_pit1_calls, 0);
   /* JERRY's slot was decremented by the elapsed 10 wall-time, so it is now due. */
   assert_double("tie: JERRY now due-now", GetTimeToNextEvent(EVENT_JERRY), 0.0);

   /* Second iteration: JERRY (0) is strictly less than MAIN's next event (none -> large
    * sentinel). JERRY must fire, MAIN must remain untouched. */
   tMain  = GetTimeToNextEvent(EVENT_MAIN);
   tJerry = GetTimeToNextEvent(EVENT_JERRY);
   if (tJerry < tMain)
   {
      elapsed = tJerry;
      SubtractEventTimes(elapsed, EVENT_MAIN);
      HandleNextEvent(EVENT_JERRY);
   }
   else
   {
      elapsed = tMain;
      SubtractEventTimes(elapsed, EVENT_JERRY);
      HandleNextEvent(EVENT_MAIN);
   }
   assert_int("after tie: JERRY fired second", jerry_pit1_calls, 1);
   assert_int("after tie: MAIN unchanged", halfline_calls, 1);
}

/* When MAIN < JERRY, MAIN fires first and JERRY's pending event must be
 * decremented by the elapsed time so both queues stay on the same wall clock. */
static void test_dispatch_main_before_jerry_decrements_jerry(void)
{
   double tMain;
   double tJerry;
   double elapsed;

   InitializeEventList();
   reset_counts();

   SetCallbackTime(HalflineCallback, 5.0, EVENT_MAIN);
   SetCallbackTime(JERRYPIT1Callback, 7.0, EVENT_JERRY);

   tMain  = GetTimeToNextEvent(EVENT_MAIN);
   tJerry = GetTimeToNextEvent(EVENT_JERRY);
   assert_double("pre: MAIN 5", tMain, 5.0);
   assert_double("pre: JERRY 7", tJerry, 7.0);

   if (tJerry < tMain)
   {
      elapsed = tJerry;
      SubtractEventTimes(elapsed, EVENT_MAIN);
      HandleNextEvent(EVENT_JERRY);
   }
   else
   {
      elapsed = tMain;
      SubtractEventTimes(elapsed, EVENT_JERRY);
      HandleNextEvent(EVENT_MAIN);
   }

   assert_int("MAIN fired (5 < 7)", halfline_calls, 1);
   assert_int("JERRY did not fire yet", jerry_pit1_calls, 0);
   /* JERRY 7 - elapsed 5 = 2: dispatcher cross-decrement keeps clocks aligned. */
   assert_double("JERRY decremented by elapsed", GetTimeToNextEvent(EVENT_JERRY), 2.0);

   /* Step again: JERRY (2) < MAIN (none), so JERRY fires. */
   tMain  = GetTimeToNextEvent(EVENT_MAIN);
   tJerry = GetTimeToNextEvent(EVENT_JERRY);
   if (tJerry < tMain)
   {
      elapsed = tJerry;
      SubtractEventTimes(elapsed, EVENT_MAIN);
      HandleNextEvent(EVENT_JERRY);
   }
   else
   {
      elapsed = tMain;
      SubtractEventTimes(elapsed, EVENT_JERRY);
      HandleNextEvent(EVENT_MAIN);
   }
   assert_int("JERRY fired second", jerry_pit1_calls, 1);
   assert_int("MAIN still 1 firing", halfline_calls, 1);
}

int main(void)
{
   test_main_queue_due_now_clamp();
   test_jerry_queue_due_now_clamp();
   test_schedule_negative_as_due_now();
   test_remove_callback();
   test_adjust_callback_reschedule();
   test_main_tie_break_lower_index();
   test_main_tie_break_order_matters();
   test_main_subtract_does_not_touch_jerry();
   test_schedule_does_not_replace_existing();
   test_adjust_unknown_is_noop();
   test_adjust_jerry_only_touches_jerry();
   test_dispatch_tie_goes_to_main();
   test_dispatch_main_before_jerry_decrements_jerry();

   printf("event queue tests passed\n");
   return 0;
}
