#ifndef __ALARM_H__
#define __ALARM_H__ 1

/*
 * This is the alarm interface. You should implement the functions for these
 * prototypes, though you may have to modify some other files to do so.
 */


/* An alarm_handler_t is a function that will run within the interrupt handler.
 * It must not block, and it must not perform I/O or any other long-running
 * computations.
 */

typedef void (*alarm_handler_t)(void*);
typedef void *alarm_id;

typedef struct alarm alarm_t;

/* register an alarm to go off in "delay" milliseconds.  Returns a handle to
 * the alarm.
 */
alarm_id register_alarm(int delay, alarm_handler_t func, void *arg);

/* unregister an alarm.  Returns 0 if the alarm had not been executed, 1
 * otherwise.
 */
int deregister_alarm(alarm_id id);

/*
 * Return the next alarm that will go off. Return NULL if no such alarm exists
 */
alarm_t* get_next_alarm();

/*
 * Return a new alarm handler
 */
alarm_handler_t get_new_alarm_handler();

/*
 * Call the alarm handler of the specified alarm
 */
void call_handler(alarm_t *alarm);

/*
 * Alarm system initialize function to initialize the system alarm queue
 */
void alarm_system_initialize();

#endif
