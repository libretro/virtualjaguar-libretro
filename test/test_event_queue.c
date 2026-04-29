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

int main(void)
{
   test_main_queue_due_now_clamp();
   test_jerry_queue_due_now_clamp();
   test_schedule_negative_as_due_now();

   printf("event queue tests passed\n");
   return 0;
}
