/*
 * minithread.c:
 *      This file provides a few function headers for the procedures that
 *      you are required to implement for the minithread assignment.
 *
 *      EXCEPT WHERE NOTED YOUR IMPLEMENTATION MUST CONFORM TO THE
 *      NAMING AND TYPING OF THESE PROCEDURES.
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "interrupts.h"
#include "minithread.h"
#include "queue.h"
#include "synch.h"
#include "alarm.h"
#include "multilevel_queue.h"
#include "network.h"
#include "minimsg.h"
#include "miniheader.h"
#include "minisocket.h"
/*
 * A minithread should be defined either in this file or in a private
 * header file.  Minithreads have a stack pointer with to make procedure
 * calls, a stackbase which points to the bottom of the procedure
 * call stack, the ability to be enqueueed and dequeued, and any other state
 * that you feel they must have.
 */

#define MAX_LEVELS 4
/* 
 * Minithread states 
 */
enum status {
  RUNNABLE,
  RUNNING,
  WAITING,
  ZOMBIE
};

static int minithreads_count = 0;
long long int nInterrupts = 0;
static minithread_t *scheduler_thread_create();
static void minithread_free(minithread_t *t);
static int clean_stopped_threads(int *arg);
static int final_proc(int *arg);
static void yield_running_thread();
static void stop_running_thread();
static void implement_scheduler();
void clock_handler(void* arg);

const int level_max_quanta[MAX_LEVELS] = {80, 40, 24, 16};
const int level_quantum_value[MAX_LEVELS] = {1, 2, 4, 8};
static int curr_level = 0;
static int curr_level_quanta = 0;
extern miniport_t *unbound_ports[MAX_PORTS];
extern minisocket_t *ports[N_PORTS];
/*
 * Minithread structure
 */
struct minithread {
  int id;
  enum status s;
  int level;
  int quanta;
  stack_pointer_t base;
  stack_pointer_t top;
};

multilevel_queue_t *runnable_queue = NULL;
queue_t *stopped_queue = NULL;
minithread_t *running_thread = NULL;
minithread_t *scheduler_thread = NULL;
minithread_t *reaper_thread = NULL;
typedef void (*clock_handler_t)(void *);

/*
 *  Create and schedule a new thread of control so
 *  that it starts executing inside proc_t with
 *  initial argument arg.
 */

minithread_t*
minithread_fork(proc_t proc, arg_t arg) {
  minithread_t *mthread = minithread_create(proc, arg);
  if (!mthread) {
    return NULL;
  }
  minithread_start(mthread);
  return mthread;
}

/*
 * Create and return a new thread. Like minithread_fork,
 * only returned thread is not scheduled for execution.
 */
minithread_t*
minithread_create(proc_t proc, arg_t arg) {
  minithread_t *mthread = (minithread_t *) malloc(sizeof(minithread_t));
  if (!mthread) {
    return NULL;
  }
  mthread->id = minithreads_count++;
  mthread->level = 0;
  mthread->quanta = 0;
  mthread->base = NULL;
  mthread->top = NULL;
  minithread_allocate_stack(&mthread->base, &mthread->top);
  minithread_initialize_stack(&mthread->top, proc, arg, final_proc, NULL);
  return mthread;
}

/*
 * Return identity (minithread_t) of caller thread.
 */
minithread_t*
minithread_self() {
  return running_thread;
}

/*
 * Return thread identifier of caller thread, for debugging.
 */
int
minithread_id() {
  if (running_thread) {
    return running_thread->id;
  }
  return 0;
}

/*
 * Block the calling thread.
 */
void
minithread_stop() {
  // the runnable queue, running_thread->quanta and curr_level being critical section
  // i.e. they can be modified inside interrupt handler as well, therefore, interrupts
  // are disabled in this section.
  interrupt_level_t old_level = set_interrupt_level(DISABLED);
  // if a thread is stopped in the middle of a quantum,
  // it is counted as one quantum completed for that thread
  // therefore if it is also checked for have completed quanta
  // for that level and pushed to next level.
  running_thread->quanta++;
  if (running_thread->quanta == level_quantum_value[curr_level]) {
    running_thread->quanta = 0;
    running_thread->level = (curr_level+1 == MAX_LEVELS ) ? curr_level : curr_level + 1;
  }
  stop_running_thread();
  set_interrupt_level(old_level);
}

/*
 * Make thread t runnable.
 */
void
minithread_start(minithread_t *t) {
  t->s = RUNNABLE;
  if (runnable_queue) {
    // the runnable queue, being critical section i.e. can be modified
    // inside interrupt handler as well, therefore, interrupts
    // are disabled in this section.
    interrupt_level_t old_level = set_interrupt_level(DISABLED);
    multilevel_queue_enqueue(runnable_queue, t->level, t);
    set_interrupt_level(old_level);
  } 
}

/*
 * Forces the caller to relinquish the processor and be put to the end of
 * the ready queue.  Allows another thread to run.
 */
void
minithread_yield() {
  // the runnable queue, running_thread->quanta and curr_level being critical section
  // i.e. they can be modified inside interrupt handler as well, therefore, interrupts
  // are disabled in this section.
  interrupt_level_t old_level = set_interrupt_level(DISABLED);
  // if a thread yields in the middle of a quantum,
  // it is counted as one quantum completed for that thread
  // therefore if it is also checked for have completed quanta
  // for that level and pushed to next level.  running_thread->quanta++;
  if (running_thread->quanta == level_quantum_value[curr_level]) {
    running_thread->quanta = 0;
    running_thread->level = (curr_level+1 == MAX_LEVELS ) ? curr_level : curr_level + 1;
  }
  yield_running_thread();
  set_interrupt_level(old_level);
}

 /*
 * this function sends the received packet to the corresponding port
 * if port not found, drops the packet i.e. frees it and returns
 */
void network_handler(void *a)
{
  if (!a) {
    return;
  }
  int old_level = set_interrupt_level(DISABLED);  
  network_interrupt_arg_t *arg = (network_interrupt_arg_t *) a;
  if (arg->size < sizeof(mini_header_t))
  {
    free(a);
    set_interrupt_level(old_level);
    return;
  }

  mini_header_t *header = (mini_header_t *) (arg->buffer);

  //Handle UDP Packet
  if (header->protocol-'0' == PROTOCOL_MINIDATAGRAM) {
    handle_udp_packet(arg);
    set_interrupt_level(old_level);
    return;
  }
  
  //Handle TCP Packet
  else if (header->protocol-'0' == PROTOCOL_MINISTREAM)
  {

    minisocket_handle_tcp_packet(arg);

    set_interrupt_level(old_level);
    return;
  }

  free(arg);
  set_interrupt_level(old_level);
  return;  
}

/*
 * Initialize the system to run the first minithread at
 * mainproc(mainarg).  This procedure should be called from your
 * main program with the callback procedure and argument specified
 * as arguments.
 */
void
minithread_system_initialize(proc_t mainproc, arg_t mainarg) {
  runnable_queue = multilevel_queue_new(MAX_LEVELS);
  
  stopped_queue = queue_new();
  scheduler_thread = scheduler_thread_create();
  assert(scheduler_thread);
  running_thread = scheduler_thread;
  int res = network_initialize((network_handler_t) network_handler);
  assert(res == 0);
  alarm_system_initialize();  
  minimsg_initialize();
  minisocket_initialize();
  reaper_thread = minithread_create(clean_stopped_threads, NULL);
  minithread_fork(mainproc, mainarg);
  interrupt_level_t prev_level = set_interrupt_level(ENABLED);
  minithread_clock_init(PERIOD * MILLISECOND, clock_handler);
  while (1) {
    if (!multilevel_queue_is_empty(runnable_queue)) {
      minithread_yield();
    }
  }
  set_interrupt_level(prev_level);
  multilevel_queue_free(runnable_queue);
  queue_free(stopped_queue);
}


static minithread_t *scheduler_thread_create() {
  minithread_t *thread = (minithread_t *) malloc(sizeof(minithread_t));
  thread->id = minithreads_count++;
  thread->level = 0;
  thread->base = (stack_pointer_t) malloc(sizeof(stack_pointer_t));
  thread->top = (stack_pointer_t) malloc(sizeof(stack_pointer_t));
  return thread;
}

static void minithread_free(minithread_t *t) {
  assert(t);
  minithread_free_stack(t->base);
  free(t);
  t = NULL;
}

static int clean_stopped_threads(int *arg) {
  while (1) {
    // disable interrupts in this section, so that interrupt handler
    // cannot disrupt the deletion of threads.
    interrupt_level_t old_level = set_interrupt_level(DISABLED);
    assert(stopped_queue);
    minithread_t *t = NULL;
    while (queue_length(stopped_queue)) {
      queue_dequeue(stopped_queue, (void **)(&t));
      assert(t);
      minithread_free(t);
    }
    stop_running_thread();
    set_interrupt_level(old_level);
  }
  return 0;
}

static int final_proc(int *arg) {
  interrupt_level_t old_level = set_interrupt_level(DISABLED);
  // switch to the reaper thread after completion of a thread
  // if the running thread is already reaper, switch to the next runnable thread
  // reaper thread will free the elements of stopped queue which contains all finished threads.
  if (running_thread != reaper_thread) {
    queue_append(stopped_queue, running_thread);
  }
  if (queue_length(stopped_queue)) {
    minithread_t *temp = running_thread;
    running_thread = reaper_thread;
    minithread_switch(&temp->top, &reaper_thread->top);
  }
  else {
    stop_running_thread();
  }
  set_interrupt_level(old_level);
  return 0;  
}

static void yield_running_thread()
{
  minithread_t *first_runnable = NULL;
  minithread_t *temp = running_thread;
  int res = multilevel_queue_dequeue(runnable_queue, curr_level, (void **)(&first_runnable));
  if (running_thread != scheduler_thread) {
    multilevel_queue_enqueue(runnable_queue, running_thread->level, running_thread);
  }
  if (res != -1) {
    // if the level of dequeued thread is different from current running, update the curr_level
    // and also the curr_level_quanta is reset to 0, since we are switching to the next level
    if (curr_level != first_runnable->level) {
      curr_level = first_runnable->level;
      curr_level_quanta = 0;
    }
    if (running_thread != first_runnable) {
      running_thread = first_runnable;
      minithread_switch(&temp->top, &running_thread->top);
    }
  }
  else {
    running_thread = scheduler_thread;
    minithread_switch(&temp->top, &running_thread->top);
  }
}

static void stop_running_thread()
{
  minithread_t *first_runnable = NULL;
  minithread_t *temp = running_thread;
  int res = multilevel_queue_dequeue(runnable_queue, curr_level, (void **)(&first_runnable));
  if (res != -1) {
    // if the level of dequeued thread is different from current running, update the curr_level
    // and also the curr_level_quanta is reset to 0, since we are switching to the next level
    if (curr_level != first_runnable->level) {
      curr_level = first_runnable->level;
      curr_level_quanta = 0;
    }
    if (running_thread != first_runnable) {
      running_thread = first_runnable;
      minithread_switch(&temp->top, &running_thread->top);
    }
  }
  else {
    running_thread = scheduler_thread;
    minithread_switch(&temp->top, &running_thread->top);
  }
}



/*
 * This is the clock interrupt handling routine.
 * You have to call minithread_clock_init with this
 * function as parameter in minithread_system_initialize
 */
void 
clock_handler(void* arg)
{
  
  interrupt_level_t old_level = set_interrupt_level(DISABLED);
  nInterrupts++;
  // get_next_alarm always runs in O(1)
  alarm_t *next_alarm = get_next_alarm();
  while (next_alarm) {
    call_handler(next_alarm);
    // since next_alarm is the first element, deregister also runs in O(1)
    deregister_alarm(next_alarm);
    next_alarm = get_next_alarm();
  }
  implement_scheduler();
  set_interrupt_level(old_level);
}

static void implement_scheduler() {
  curr_level_quanta++;
  running_thread->quanta++;
  int schedule_next = 0;
  // check if the quanta run on the current level have reached maximum allowed for
  // that level, if yes switch to next level.
  if (curr_level_quanta == level_max_quanta[curr_level]) {
    // if the running thread has exhausted the max allowed quanta for a thread on
    // that level, put that thread to next level and switch to next runnable thread
    if (running_thread->quanta == level_quantum_value[curr_level]) {
      running_thread->quanta = 0;
      running_thread->level = (curr_level+1 == MAX_LEVELS ) ? curr_level : curr_level + 1;
    }
    curr_level = (curr_level + 1) % MAX_LEVELS;
    curr_level_quanta = 0;
    schedule_next = 1;
  }
  // the level max quanta has not reached but
  // if the running thread has exhausted the max allowed quanta for a thread on
  // that level, put that thread to next level and switch to next runnable thread
  else if (running_thread->quanta == level_quantum_value[curr_level]) {
    running_thread->quanta = 0;
    running_thread->level = (curr_level+1 == MAX_LEVELS ) ? curr_level : curr_level + 1;
    schedule_next = 1;
  }
  if (schedule_next) {
    yield_running_thread();
  }
}

/*
 * sleep with timeout in milliseconds
 */
void 
minithread_sleep_with_timeout(int delay)
{
  interrupt_level_t l = set_interrupt_level(DISABLED);
  register_alarm(delay, get_new_alarm_handler(), minithread_self());
  set_interrupt_level(l);
  minithread_stop();
}
