/*
 * Multilevel queue manipulation functions  
 */
#include "multilevel_queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

struct multilevel_queue {
  queue_t **level_queues;
  int n_levels;
};

/*
 * Returns an empty multilevel queue with number_of_levels levels.
 * Returns NULL on error.
 */
multilevel_queue_t* multilevel_queue_new(int number_of_levels)
{
  assert(number_of_levels > 0);
  multilevel_queue_t *mlq = (multilevel_queue_t *) malloc (sizeof (multilevel_queue_t));
  if (!mlq) {
    return NULL;
  }
  mlq->n_levels = number_of_levels;
  mlq->level_queues = (queue_t **) malloc (mlq->n_levels * sizeof(queue_t*));
  for (int i = 0; i < mlq->n_levels; i++) {
    mlq->level_queues[i] = queue_new();
  }
  return mlq;
}

/*
 * Appends a void* to the multilevel queue at the specified level.
 * Return 0 (success) or -1 (failure).
 */
int multilevel_queue_enqueue(multilevel_queue_t* queue, int level, void* item)
{
  if (!queue || level > queue->n_levels) {
    return -1;
  }
  queue_t *level_q = queue->level_queues[level];
  if (!level_q) {
    return -1;
  }
  queue_append(level_q, item);
  return 0;
}

/*
 * Dequeue and return the first void* from the multilevel queue starting at the specified level. 
 * Levels wrap around so as long as there is something in the multilevel queue an item should be returned.
 * Return the level that the item was located on and that item.
 * If the multilevel queue is empty, return -1 (failure) with a NULL item.
 */
int multilevel_queue_dequeue(multilevel_queue_t* queue, int level, void** item)
{
  assert (queue && level < queue->n_levels);
  int res = -1;
  // perform a circular search on the level queues to find an element
  // i.e. if no thread id found at one level, search on the next level
  for (int i = 0; i < queue->n_levels; i++) {
    res = queue_dequeue(queue->level_queues[(level + i)%queue->n_levels], item);
    if (res != -1) {
      return res;
    }
  }
  return -1;
}

/* 
 * Free the queue and return 0 (success) or -1 (failure).
 * Do not free the queue nodes; this is the responsibility of the programmer.
 */
int multilevel_queue_free(multilevel_queue_t* queue)
{
  assert(queue);
  for (int i = 0; i < queue->n_levels; i++) {
    if (queue_free(queue->level_queues[i]) == -1) {
      return -1;
    }
  }
  // free all level queues
  free (queue->level_queues);
  free (queue);
  return 0;
}

int multilevel_queue_is_empty(multilevel_queue_t* queue)
{
  assert(queue);
  for (int i = 0; i < queue->n_levels; i++) {
    if (queue->level_queues[i] && queue_length(queue->level_queues[i]) > 0) {
      return 0;
    }
  }
  return 1;
}
