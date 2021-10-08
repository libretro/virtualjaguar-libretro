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
      eventListJERRY[i].valid = false;
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
   double time;
   unsigned i;
   if (type == EVENT_MAIN)
   {
      time      = eventList[0].eventTime;
      nextEvent = 0;

      for(i = 1; i < EVENT_LIST_SIZE; i++)
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
      time           = eventListJERRY[0].eventTime;
      nextEventJERRY = 0;

      for(i = 1; i < EVENT_LIST_SIZE; i++)
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
