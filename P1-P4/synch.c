#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "defs.h"
#include "interrupts.h"
#include "synch.h"
#include "queue.h"
#include "minithread.h"
#include "interrupts.h"

/*
 * You must implement the procedures and types defined in this interface.
 */


/*
 * Semaphores. This implimentation allows the semaphore to be negative, since the test packages start 
 * with an initial semaphore value of 0
 */
struct semaphore {
  int count;
  queue_t *wait_list;
};

/*
 *  Allocate a new semaphore.
 */
semaphore_t* semaphore_create() {
  semaphore_t *sem = (semaphore_t *)malloc(sizeof(semaphore_t));
  assert(sem);
  return sem;
}

/*
 *  Deallocate a semaphore.
 */
void semaphore_destroy(semaphore_t *sem) {
  assert(sem);
  // since the wait_list of a semaphore is a critical section
  // disable interrupts before this
  interrupt_level_t old_level = set_interrupt_level(DISABLED);
  queue_free(sem->wait_list);
  free(sem);
  set_interrupt_level(old_level);
}

/*
 *  Initialize the semaphore data structure pointed at by
 *  sem with an initial value cnt.
 */
void semaphore_initialize(semaphore_t *sem, int cnt) {
  assert(sem);
  sem->count = cnt;
  sem->wait_list = queue_new();
  assert(sem->wait_list);
}

/*
 * P on the sempahore. If less than 0, then append itself to wait queue and yield
 */
void semaphore_P(semaphore_t *sem) {  
  assert(sem);
  // since the wait_list and count of a semaphore is a critical section
  // disable interrupts before this
  interrupt_level_t old_level = set_interrupt_level(DISABLED);
  sem->count--;
  if (sem->count < 0) {
    queue_append(sem->wait_list, minithread_self());
    minithread_stop();
  }
  set_interrupt_level(old_level);
}

/*
 * V on the sempahore. If less than or equal to 0, then wake up thread and start it
 */
void semaphore_V(semaphore_t *sem) {
  assert(sem);
  // since the wait_list and count of a semaphore is a critical section
  // disable interrupts before this
  interrupt_level_t old_level = set_interrupt_level(DISABLED);
  sem->count++;
  if (sem->count <= 0) {
    minithread_t* thread = NULL;
    queue_dequeue(sem->wait_list, (void**)(&thread));
    minithread_start(thread);
  }
  set_interrupt_level(old_level);
}

int semaphore_get_count(semaphore_t *sem) 
{
  assert(sem);
  return sem->count;
}
