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
// STILL TO DO:
//
// - Handling for an event that occurs NOW
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
static uint32_t numberOfEvents;


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

   numberOfEvents = 0;
}


// Set callback time in µs. This is fairly arbitrary, but works well enough for our purposes.
//We just slap the next event into the list in the first available slot, no checking, no nada...
void SetCallbackTime(void (* callback)(void), double time, int type/*= EVENT_MAIN*/)
{
   unsigned i;
   if (type == EVENT_MAIN)
   {
      for(i = 0; i < EVENT_LIST_SIZE; i++)
      {
         if (!eventList[i].valid)
         {
            eventList[i].timerCallback = callback;
            eventList[i].eventTime = time;
            eventList[i].eventType = type;
            eventList[i].valid = true;
            numberOfEvents++;

            return;
         }
      }
   }
   else
   {
      for(i = 0; i < EVENT_LIST_SIZE; i++)
      {
         if (!eventListJERRY[i].valid)
         {
            /* Found callback slot */
            eventListJERRY[i].timerCallback = callback;
            eventListJERRY[i].eventTime = time;
            eventListJERRY[i].eventType = type;
            eventListJERRY[i].valid = true;
            numberOfEvents++;

            return;
         }
      }
   }
}


void RemoveCallback(void (* callback)(void))
{
   unsigned i;

   for (i = 0; i < EVENT_LIST_SIZE; i++)
   {
      if (eventList[i].valid && eventList[i].timerCallback == callback)
      {
         eventList[i].valid = false;
         numberOfEvents--;

         return;
      }
      else if (eventListJERRY[i].valid && eventListJERRY[i].timerCallback == callback)
      {
         eventListJERRY[i].valid = false;
         numberOfEvents--;

         return;
      }
   }
}


void AdjustCallbackTime(void (* callback)(void), double time)
{
   unsigned i;
   for(i = 0; i < EVENT_LIST_SIZE; i++)
   {
      if (eventList[i].valid && eventList[i].timerCallback == callback)
      {
         eventList[i].eventTime = time;
         return;
      }
      else if (eventListJERRY[i].valid && eventListJERRY[i].timerCallback == callback)
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

      for(i = 0; i < EVENT_LIST_SIZE; i++)
      {
         if (eventList[i].valid && (eventList[i].eventTime < time))
         {
            time = eventList[i].eventTime;
            nextEvent = i;
         }
      }
   }
   else
   {
      nextEventJERRY = 0;

      for(i = 0; i < EVENT_LIST_SIZE; i++)
      {
         if (eventListJERRY[i].valid && (eventListJERRY[i].eventTime < time))
         {
            time = eventListJERRY[i].eventTime;
            nextEventJERRY = i;
         }
      }
   }

   return time;
}


void HandleNextEvent(int type/*= EVENT_MAIN*/)
{
   unsigned i;

   if (type == EVENT_MAIN)
   {
      double elapsedTime = eventList[nextEvent].eventTime;
      void (* event)(void) = eventList[nextEvent].timerCallback;

      for (i = 0; i < EVENT_LIST_SIZE; i++)
      {
         //We can skip the check & just subtract from everything, since the check is probably
         //just as heavy as the code after and we won't use the elapsed time from an invalid event anyway.
         //		if (eventList[i].valid)
         eventList[i].eventTime -= elapsedTime;
      }

      eventList[nextEvent].valid = false;			// Remove event from list...
      numberOfEvents--;

      (*event)();
   }
   else
   {
      double elapsedTime = eventListJERRY[nextEventJERRY].eventTime;
      void (* event)(void) = eventListJERRY[nextEventJERRY].timerCallback;

      for (i = 0; i < EVENT_LIST_SIZE; i++)
      {
         //We can skip the check & just subtract from everything, since the check is probably
         //just as heavy as the code after and we won't use the elapsed time from an invalid event anyway.
         //		if (eventList[i].valid)
         eventListJERRY[i].eventTime -= elapsedTime;
      }

      eventListJERRY[nextEventJERRY].valid = false;	// Remove event from list...
      numberOfEvents--;

      (*event)();
   }
}


void SubtractEventTimes(double elapsed, int type)
{
   unsigned i;
   if (type == EVENT_MAIN)
   {
      for (i = 0; i < EVENT_LIST_SIZE; i++)
         eventList[i].eventTime -= elapsed;
   }
   else
   {
      for (i = 0; i < EVENT_LIST_SIZE; i++)
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

typedef void (*event_callback_t)(void);

static const event_callback_t callback_registry[] = {
   NULL,                  /* 0 = invalid/empty */
   HalflineCallback,     /* 1 */
   TOMPITCallback,       /* 2 */
   JERRYPIT1Callback,    /* 3 */
   JERRYPIT2Callback,    /* 4 */
   JERRYI2SCallback,     /* 5 */
   DSPSampleCallback,    /* 6 */
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

   STATE_LOAD_VAR(buf, nextEvent);
   STATE_LOAD_VAR(buf, nextEventJERRY);
   STATE_LOAD_VAR(buf, numberOfEvents);

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

   return (size_t)(buf - start);
}
