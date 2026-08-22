//
// System time handlers
//
// by James Hammons
// (C) 2010 Underground Software
//
// JLH = James Hammons <jlhamm@acm.org>
//
// Who  When        What
// ---  ----------  -------------------------------------------------------------
// JLH  01/16/2010  Created this log ;-)
//

//
#include <stdint.h>
#include <boolean.h>

#include "event.h"
#include "state.h"


#define EVENT_LIST_SIZE       32

// Now, a bit of weirdness: It seems that the number of lines displayed on the screen
// makes the effective refresh rate either 30 or 25 Hz!

// NOTE ABOUT TIMING SYSTEM DATA STRUCTURES:

// A queue won't work for this system because we can't guarantee that an event will go
// in with a time that is later than the ones already queued up. So we just use a simple
// list.

// Although if we used an insertion sort we could, but it wouldn't work for adjusting
// times... (For that, you would have to remove the event then reinsert it.)

struct Event
{
	bool valid;
	int eventType;
	double eventTime;
	void (* timerCallback)(void);
};

static struct Event eventList[EVENT_LIST_SIZE];
static struct Event eventListJERRY[EVENT_LIST_SIZE];
static uint32_t nextEvent;
static uint32_t nextEventJERRY;

/* Live-event counts, one per list. Packing invariant: every valid slot in
 * eventList[]/eventListJERRY[] occupies exactly indices [0, eventCount) /
 * [0, eventCountJERRY) -- contiguous, no holes. This lets every scan below
 * stop at the count instead of walking all EVENT_LIST_SIZE slots.
 * Replaces the old single `numberOfEvents`, which was summed across both
 * lists and couldn't bound either list's scan on its own. */
static uint32_t eventCount;
static uint32_t eventCountJERRY;


static double ClampDueEventTime(double time)
{
   return (time < 0.0 ? 0.0 : time);
}


void InitializeEventList(void)
{
   unsigned i;
   for(i = 0; i < EVENT_LIST_SIZE; i++)
   {
      eventList[i].valid = false;
      eventList[i].eventTime = 0.0;
      eventListJERRY[i].valid = false;
      eventListJERRY[i].eventTime = 0.0;
   }

   eventCount = 0;
   eventCountJERRY = 0;
}


// Set callback time in µs. This is fairly arbitrary, but works well enough for our purposes.
// The list is kept packed: a new event always lands at index `count` (the first slot past
// the live region) and `count` is incremented. If the list is already at capacity the event
// is silently dropped -- this matches the previous scan-and-give-up-at-32 behaviour.
void SetCallbackTime(void (* callback)(void), double time, int type/*= EVENT_MAIN*/)
{
   time = ClampDueEventTime(time);

   if (type == EVENT_MAIN)
   {
      if (eventCount < EVENT_LIST_SIZE)
      {
         unsigned i = eventCount;
         eventList[i].timerCallback = callback;
         eventList[i].eventTime = time;
         eventList[i].eventType = type;
         eventList[i].valid = true;
         eventCount++;
      }
   }
   else
   {
      if (eventCountJERRY < EVENT_LIST_SIZE)
      {
         unsigned i = eventCountJERRY;
         eventListJERRY[i].timerCallback = callback;
         eventListJERRY[i].eventTime = time;
         eventListJERRY[i].eventType = type;
         eventListJERRY[i].valid = true;
         eventCountJERRY++;
      }
   }
}


void RemoveCallback(void (* callback)(void))
{
   unsigned i;

   for (i = 0; i < eventCount; i++)
   {
      if (eventList[i].timerCallback == callback)
      {
         unsigned last = eventCount - 1;

         if (i != last)
            eventList[i] = eventList[last];

         eventList[last].valid = false;
         eventCount = last;

         return;
      }
   }

   for (i = 0; i < eventCountJERRY; i++)
   {
      if (eventListJERRY[i].timerCallback == callback)
      {
         unsigned last = eventCountJERRY - 1;

         if (i != last)
            eventListJERRY[i] = eventListJERRY[last];

         eventListJERRY[last].valid = false;
         eventCountJERRY = last;

         return;
      }
   }
}


void AdjustCallbackTime(void (* callback)(void), double time)
{
   unsigned i;
   time = ClampDueEventTime(time);

   for(i = 0; i < eventCount; i++)
   {
      if (eventList[i].timerCallback == callback)
      {
         eventList[i].eventTime = time;
         return;
      }
   }

   for(i = 0; i < eventCountJERRY; i++)
   {
      if (eventListJERRY[i].timerCallback == callback)
      {
         eventListJERRY[i].eventTime = time;
         return;
      }
   }
}


//
// Since our list is unordered WRT time, we have to search it to find the next event
// Returns time to next event & sets nextEvent to that event
//
double GetTimeToNextEvent(int type/*= EVENT_MAIN*/)
{
   double time = 1e30;
   unsigned i;
   if (type == EVENT_MAIN)
   {
      nextEvent = 0;

      for(i = 0; i < eventCount; i++)
      {
         if (eventList[i].eventTime < time)
         {
            time = eventList[i].eventTime;
            nextEvent = i;
         }
      }
   }
   else
   {
      nextEventJERRY = 0;

      for(i = 0; i < eventCountJERRY; i++)
      {
         if (eventListJERRY[i].eventTime < time)
         {
            time = eventListJERRY[i].eventTime;
            nextEventJERRY = i;
         }
      }
   }

   return ClampDueEventTime(time);
}


// If a caller invokes HandleNextEvent() when the relevant list is already
// empty (nothing left for the preceding GetTimeToNextEvent() to have
// found), there is nothing to dispatch: return without touching the list
// or calling any callback. Note this is a deliberate behaviour change from
// the pre-packing code, which had no such guard and would instead reread
// slot 0's leftover contents from whatever was last stored there and
// call it -- "harmless" only by accident, and which physical slot ends up
// with the leftover data is an artifact of the packing strategy (old:
// first-fit reuse; new: swap-last-into-hole), so the *particular* stale
// callback that gets redispatched is not something either implementation
// can be said to guarantee. A clean no-op is well-defined regardless of
// packing strategy -- but note the old code's stale re-fire was an
// accidental self-heal, not a safety net: for EVENT_MAIN specifically it
// almost always re-fires HalflineCallback (nothing in this codebase ever
// calls RemoveCallback(HalflineCallback), so slot 0's residue is
// HalflineCallback far more often than not), which re-arms the queue and
// keeps JaguarExecuteNew()'s `do { ... } while (!frameDone)` loop moving.
// If BOTH queues were ever empty at once, this no-op guard makes
// HandleNextEvent(EVENT_MAIN) do nothing, frameDone never gets set, and
// the loop spins forever -- the old code's accidental self-heal would
// have masked exactly that. Not reachable today: EVENT_MAIN (Halfline)
// and EVENT_JERRY (DSP/I2S) are perpetually self-rearmed by their own
// callbacks, so real dispatch never hits an empty queue; only synthetic
// over-dispatch in test harnesses (see test/test_uart_loopback.c's
// pump()) reaches this guard, and there JERRY's list is never actually
// down to zero valid MAIN-side events at the same time. Flagged here so a
// future change that stops re-arming one of these callbacks fails loudly
// (a hang, easy to notice) rather than being masked by lucky residue.
void HandleNextEvent(int type/*= EVENT_MAIN*/)
{
   if (type == EVENT_MAIN)
   {
      double elapsedTime;
      void (* event)(void);
      unsigned i;
      unsigned last;

      if (eventCount == 0)
         return;

      elapsedTime = ClampDueEventTime(eventList[nextEvent].eventTime);
      event = eventList[nextEvent].timerCallback;

      for (i = 0; i < eventCount; i++)
      {
         //We can skip the check & just subtract from everything, since the check is probably
         //just as heavy as the code after and we won't use the elapsed time from an invalid event anyway.
         eventList[i].eventTime -= elapsedTime;
      }

      // Remove the dispatched event: swap the last live slot into its place
      // (same move RemoveCallback makes) so [0, eventCount) stays packed.
      last = eventCount - 1;

      if (nextEvent != last)
         eventList[nextEvent] = eventList[last];

      eventList[last].valid = false;
      eventCount = last;

      (*event)();
   }
   else
   {
      double elapsedTime;
      void (* event)(void);
      unsigned i;
      unsigned last;

      if (eventCountJERRY == 0)
         return;

      elapsedTime = ClampDueEventTime(eventListJERRY[nextEventJERRY].eventTime);
      event = eventListJERRY[nextEventJERRY].timerCallback;

      for (i = 0; i < eventCountJERRY; i++)
      {
         //We can skip the check & just subtract from everything, since the check is probably
         //just as heavy as the code after and we won't use the elapsed time from an invalid event anyway.
         eventListJERRY[i].eventTime -= elapsedTime;
      }

      last = eventCountJERRY - 1;

      if (nextEventJERRY != last)
         eventListJERRY[nextEventJERRY] = eventListJERRY[last];

      eventListJERRY[last].valid = false;
      eventCountJERRY = last;

      (*event)();
   }
}


void SubtractEventTimes(double elapsed, int type)
{
   unsigned i;
   if (type == EVENT_MAIN)
   {
      for (i = 0; i < eventCount; i++)
         eventList[i].eventTime -= elapsed;
   }
   else
   {
      for (i = 0; i < eventCountJERRY; i++)
         eventListJERRY[i].eventTime -= elapsed;
   }
}


/* Callback registry for save state serialization.
 * Maps function pointers to integer IDs so events can be serialized. */

extern void HalflineCallback(void);
extern void TOMPITCallback(void);
extern void JERRYPIT1Callback(void);
extern void JERRYPIT2Callback(void);
extern void JERRYI2SCallback(void);
extern void DSPSampleCallback(void);
extern void GPUCPUINTCallback(void);
extern void UARTTXCallback(void);
extern void UARTRXCallback(void);

typedef void (*event_callback_t)(void);

static const event_callback_t callback_registry[] = {
   NULL,                  /* 0 = invalid/empty */
   HalflineCallback,     /* 1 */
   TOMPITCallback,       /* 2 */
   JERRYPIT1Callback,    /* 3 */
   JERRYPIT2Callback,    /* 4 */
   JERRYI2SCallback,     /* 5 */
   DSPSampleCallback,    /* 6 */
   GPUCPUINTCallback,    /* 7 */
   UARTTXCallback,       /* 8 */
   UARTRXCallback,       /* 9 */
};
#define CALLBACK_REGISTRY_SIZE (sizeof(callback_registry) / sizeof(callback_registry[0]))

static uint8_t callback_to_id(event_callback_t cb)
{
   unsigned i;
   if (!cb) return 0;
   for (i = 1; i < CALLBACK_REGISTRY_SIZE; i++)
   {
      if (callback_registry[i] == cb)
         return (uint8_t)i;
   }
   return 0; /* unknown callback — will be lost on load */
}

static event_callback_t id_to_callback(uint8_t id)
{
   if (id < CALLBACK_REGISTRY_SIZE)
      return callback_registry[id];
   return NULL;
}


size_t EventStateSave(uint8_t *buf)
{
   uint8_t *start = buf;
   unsigned i;
   /* On-disk field name/size is unchanged (numberOfEvents, uint32_t) even
    * though the runtime now keeps two per-list counts -- write their sum so
    * the byte layout, and thus the save-state format/version, don't move.
    * The physical per-slot valid/cb_id/etype/etime arrays below are still
    * serialized unconditionally and in full, same as before packing. */
   uint32_t numberOfEvents = eventCount + eventCountJERRY;

   STATE_SAVE_VAR(buf, nextEvent);
   STATE_SAVE_VAR(buf, nextEventJERRY);
   STATE_SAVE_VAR(buf, numberOfEvents);

   /* Save both event lists — serialize callback as ID */
   for (i = 0; i < EVENT_LIST_SIZE; i++)
   {
      uint8_t valid = eventList[i].valid ? 1 : 0;
      uint8_t cb_id = callback_to_id(eventList[i].timerCallback);
      int32_t etype = (int32_t)eventList[i].eventType;
      double etime  = eventList[i].eventTime;

      STATE_SAVE_VAR(buf, valid);
      STATE_SAVE_VAR(buf, cb_id);
      STATE_SAVE_VAR(buf, etype);
      STATE_SAVE_VAR(buf, etime);
   }

   for (i = 0; i < EVENT_LIST_SIZE; i++)
   {
      uint8_t valid = eventListJERRY[i].valid ? 1 : 0;
      uint8_t cb_id = callback_to_id(eventListJERRY[i].timerCallback);
      int32_t etype = (int32_t)eventListJERRY[i].eventType;
      double etime  = eventListJERRY[i].eventTime;

      STATE_SAVE_VAR(buf, valid);
      STATE_SAVE_VAR(buf, cb_id);
      STATE_SAVE_VAR(buf, etype);
      STATE_SAVE_VAR(buf, etime);
   }

   return (size_t)(buf - start);
}


size_t EventStateLoad(const uint8_t *buf)
{
   const uint8_t *start = buf;
   unsigned i;
   unsigned w;
   /* Loaded but intentionally unused: eventCount/eventCountJERRY are
    * recomputed below by counting the slots the per-slot loop below
    * actually marks valid (which already accounts for unresolved
    * callback ids), not by trusting this legacy combined total. It is
    * still read here so the buffer cursor lands in the same place. */
   uint32_t numberOfEvents;

   STATE_LOAD_VAR(buf, nextEvent);
   STATE_LOAD_VAR(buf, nextEventJERRY);
   STATE_LOAD_VAR(buf, numberOfEvents);
   (void)numberOfEvents;

   for (i = 0; i < EVENT_LIST_SIZE; i++)
   {
      uint8_t valid, cb_id;
      int32_t etype;
      double etime;

      STATE_LOAD_VAR(buf, valid);
      STATE_LOAD_VAR(buf, cb_id);
      STATE_LOAD_VAR(buf, etype);
      STATE_LOAD_VAR(buf, etime);

      eventList[i].timerCallback = id_to_callback(cb_id);
      /* Invalidate event if callback could not be resolved */
      eventList[i].valid = (valid && eventList[i].timerCallback) ? true : false;
      eventList[i].eventType = etype;
      eventList[i].eventTime = etime;
   }

   for (i = 0; i < EVENT_LIST_SIZE; i++)
   {
      uint8_t valid, cb_id;
      int32_t etype;
      double etime;

      STATE_LOAD_VAR(buf, valid);
      STATE_LOAD_VAR(buf, cb_id);
      STATE_LOAD_VAR(buf, etype);
      STATE_LOAD_VAR(buf, etime);

      eventListJERRY[i].timerCallback = id_to_callback(cb_id);
      /* Invalidate event if callback could not be resolved */
      eventListJERRY[i].valid = (valid && eventListJERRY[i].timerCallback) ? true : false;
      eventListJERRY[i].eventType = etype;
      eventListJERRY[i].eventTime = etime;
   }

   /* Compact both lists so the packed invariant ([0, count) all valid,
    * contiguous) holds regardless of how the physical array looked on
    * disk. A save written by this packed code is already contiguous, so
    * this is a no-op there; it also makes loading a state written by the
    * pre-packing code (which could leave holes after a RemoveCallback
    * with no immediately-following SetCallbackTime) safe, since scans
    * elsewhere in this file are now bounded by the count instead of
    * EVENT_LIST_SIZE.
    *
    * Index order is NOT purely cosmetic despite the "unordered WRT time"
    * file-header comment: GetTimeToNextEvent()'s strict `<` scan makes the
    * lowest surviving index win an exact eventTime tie, so reordering
    * could in principle change which of two simultaneously-due events
    * fires first. This compaction is safe regardless because it is
    * *stable* -- valid entries keep their relative order as they're
    * packed toward the front (e.g. valid slots at physical indices 3 and
    * 7 land at 0 and 1, in the same relative order), so the lowest-index
    * tie-break after compaction resolves identically to a full 0..31 scan
    * over the pre-compaction layout would have. */
   w = 0;
   for (i = 0; i < EVENT_LIST_SIZE; i++)
   {
      if (eventList[i].valid)
      {
         if (w != i)
            eventList[w] = eventList[i];
         w++;
      }
   }
   for (i = w; i < EVENT_LIST_SIZE; i++)
      eventList[i].valid = false;
   eventCount = w;

   w = 0;
   for (i = 0; i < EVENT_LIST_SIZE; i++)
   {
      if (eventListJERRY[i].valid)
      {
         if (w != i)
            eventListJERRY[w] = eventListJERRY[i];
         w++;
      }
   }
   for (i = w; i < EVENT_LIST_SIZE; i++)
      eventListJERRY[i].valid = false;
   eventCountJERRY = w;

   return (size_t)(buf - start);
}
