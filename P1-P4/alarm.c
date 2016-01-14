#include <stdio.h>
#include <stdlib.h>

#include "interrupts.h"
#include "alarm.h"
#include "minithread.h"
#include "queue.h"

//External variable for the number of interrupts
extern long long int nInterrupts;
//Alarm priority queue
queue_t *alarm_queue = NULL;

/*
 * Alarm structure - Contains alarm end time, alarm handler function 
 * and the argument to that function which is basically the
 * thread_t pointer for now
 */
struct alarm {
  long long int end;
  alarm_handler_t call_back;
  void *arg;
};

/*
 * The actual alarm handler function, wakes up the sleeping thread
 */
static void alarm_call_back(void *arg)
{
  assert(arg);
  minithread_start((minithread_t *) arg);
}

/*
 * Alarm system initialize function to initialize the system alarm queue
 */
void alarm_system_initialize()
{
  alarm_queue = queue_new();
}

/* see alarm.h */
alarm_id
register_alarm(int delay, alarm_handler_t alarm, void *arg)
{
  long long int del = delay / PERIOD;

  // if the delay entered is not a multiple of PERIOD (quantum value)
  // add one to it, since the thread should sleep for atleast the value
  // of delay milliseconds
  if (delay % PERIOD != 0) {
    del = del + 1;
  }

  alarm_t *newAlarm = (alarm_t *) malloc(sizeof(alarm_t));

  if (!newAlarm) {      //If malloc fails
    return NULL;
  }

  newAlarm->end = nInterrupts + del;    //Set end time to the current number of interrupts plus the delay
  newAlarm->call_back = alarm;
  newAlarm->arg = arg;
  interrupt_level_t old_level = set_interrupt_level(DISABLED);
  queue_insert_sorted(alarm_queue, newAlarm, newAlarm->end);    //Insert the alarm into the priority queue
  set_interrupt_level(old_level);
  return newAlarm;
}

/* see alarm.h */
int
deregister_alarm(alarm_id alarm)
{
  assert(alarm);
  alarm_t *a = (alarm_t *) alarm;   //Type cast it into an alarm_t variable
  
  if (a->end <= nInterrupts) {      //If the alarm went off, return 1 after deleting it from the queue
  	interrupt_level_t old_level = set_interrupt_level(DISABLED);
    queue_delete(alarm_queue, a);   
    set_interrupt_level(old_level);
    free (a);
    a = NULL;
    return 1;
  }
  else {                            //Alarm did not go off, return 0 after deleting it from the queue
    interrupt_level_t old_level = set_interrupt_level(DISABLED);
    queue_delete(alarm_queue, a);
    set_interrupt_level(old_level);
    free (a);
    a = NULL;
    return 0;
  }
}

/*
 * Return the next alarm that will go off. Return NULL if no such alarm exists
 */
alarm_t* get_next_alarm()
{
  interrupt_level_t old_level = set_interrupt_level(DISABLED);	
  alarm_t *next = (alarm_t *) queue_front(alarm_queue);
  set_interrupt_level(old_level);
  
  if (next && next->end == nInterrupts) {   //If the alarm is not NULL and it is supposed to go off at this time, return it
    return next;
  }

  return NULL;    //Otherwise return NULL
}

/*
 * Return a new alarm handler
 */
alarm_handler_t get_new_alarm_handler()
{
  alarm_handler_t a = alarm_call_back;
  return a;
}

/*
 * Call the alarm handler of the specified alarm
 */
void call_handler(alarm_t *alarm)
{
  assert(alarm);
  alarm->call_back(alarm->arg);
}

/*
** vim: ts=4 sw=4 et cindent
*/
