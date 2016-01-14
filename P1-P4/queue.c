/*****
 * Generic queue implementation.
 *
 */
#include "queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

/*
 * Queue node structure which contains the data, the priority
 * and a pointer to the next node
 */
typedef struct queue_node {
  void *data;
  long long int priority;
  struct queue_node *next;
} qnode;

struct queue {
  qnode *front;
  qnode *rear;
  int count;
};

/*
 * Create a new queue node with default priority 0
 */
qnode *create_qnode(void *ndata) {
  qnode *node = (qnode *) malloc(sizeof(qnode));
  assert(node);
  node->data = ndata;
  node->priority = 0;
  node->next = NULL;
  return node;
}

/*
 * Create a new queue node with priority p
 */
qnode *creat_qnode_priority(void *ndata, long long int p)
{
  qnode *node = (qnode *) malloc(sizeof(qnode));
  assert(node);
  node->data = ndata;
  node->priority = p;
  node->next = NULL;
  return node;  
}

/*
 * Return an empty queue.
 */
queue_t *queue_new() {
  queue_t *queue = (queue_t *) malloc(sizeof(queue_t));
  assert(queue);
  
  queue->front = NULL;
  queue->rear = NULL;
  queue->count = 0;
  return queue;
}

/*
 * Prepend a void* to a queue (both specifed as parameters).  Return
 * 0 (success) or -1 (failure).
 */
int
queue_prepend(queue_t *queue, void* item) {
  if(!queue){
    return -1;
  }
  
  qnode *n = create_qnode(item);
  if (!queue->front) {
    queue->front = n;
    queue->rear = n;    
  }
  else {
    n->next = queue->front;
    queue->front = n;
  }
  queue->count++;
  return 0;
}

/*
 * Append a void* to a queue (both specifed as parameters). Return
 * 0 (success) or -1 (failure).
 */
int
queue_append(queue_t *queue, void* item) {
  if(!queue){
    return -1;
  }

  qnode *n = create_qnode(item);
  if (queue->front == NULL) {
    queue->front = n;
    queue->rear = n;
  }
  else {
    queue->rear->next = n;
    queue->rear = n;
  }
  queue->count++;
  return 0;
}

/*
 * Dequeue and return the first void* from the queue or NULL if queue
 * is empty.  Return 0 (success) or -1 (failure).
 */
int
queue_dequeue(queue_t *queue, void **item) {
  assert(queue);

  if (!queue->front) {
    *item = NULL;
    return -1;
  }
  else {
    qnode *first = queue->front;
    queue->front = first->next;
    //printf("Just before failing\n");
    *item = first->data;
    free (first);
    first = NULL;
    // if there was only one element in the queue which has now been removed
    if (!queue->front) {
      queue->rear = NULL;
    }
    queue->count--;
  }
  return 0;
}

/*
 * Return the first element of the queue without dequeueing it
 */
void* queue_front(queue_t *queue)
{
  assert(queue);

  if(queue_length(queue) == 0)
    return NULL;
  else
    return queue->front->data;
}

/*
 * Iterate the function parameter over each element in the queue.  The
 * additional void* argument is passed to the function as its first
 * argument and the queue element is the second.  Return 0 (success)
 * or -1 (failure).
 */
int
queue_iterate(queue_t *queue, func_t f, void* item) {
  if (!queue) {
    return -1;
  }
  qnode *n = queue->front;
  while (n) {
    f(n, item);
    n = n->next; 
  }
  return 0;
}

/*
 * Free the queue and return 0 (success) or -1 (failure).
 */
int
queue_free (queue_t *queue) {
  assert (queue);
  // non-empty queue should error
  if (queue->front) {
    return -1;
  }
  free (queue);
  queue = NULL;
  return 0;
}

/*
 * Return the number of items in the queue.
 */
int
queue_length(const queue_t *queue) {
  assert(queue);
  return queue->count;
}

/*
 * Delete the specified item from the given queue.
 * Return -1 on error.
 */
int
queue_delete(queue_t *queue, void *item) {
  assert(queue && item);
  qnode *curr = queue->front;
  qnode *prev = NULL;
  while (curr) {
    if (curr->data == item) {
      if (curr == queue->front) { // the item to be deleted is first in queue
        queue->front = curr->next;
      }
      if (curr == queue->rear) { // the item to be deleted is last in queue
        queue->rear = prev;
      }      
      free (curr);
      queue->count--;
      return 0;
    }
    prev = curr;
    curr = curr->next;
  }

  return -1;
}

/*
 * Insert into the queue in a sorted manner
 */
int queue_insert_sorted(queue_t *queue, void *item, long long int p)
{
  assert(item && queue);
  qnode *n = creat_qnode_priority(item, p);

  if(queue_length(queue) == 0)    //If the queue is empty
  {
    queue->front = n;
    queue->rear = n;
    queue->count++;
  }
  else
  {
    qnode *curr = queue->front;
    qnode *prev = NULL;

    while (curr)
    {
      if (n->priority < curr->priority)   //If the priority is less than the current node's priority, place the element before it
      {
        if (!prev) {                      //If it has to be inserted in the front
          n->next = curr;
          queue->front = n;
          queue->count++;
          return 0;
        }
        
        prev->next = n;                   //If it has to be inserted in the middle of the list
        n->next = curr;
        queue->count++;
        return 0;
      }
      prev = curr;
      curr = curr->next;
    }

    prev->next = n;                       //If the element has to be inserted into the back of the queue
    queue->rear = n;
    queue->count++;
    
  } 
  return 0; 
}

qnode* queue_get_next(qnode* node)
{
  if(!node)
  {
    return NULL;
  }

  return node->next;
}

qnode* queue_get_front(queue_t *queue)
{
  assert(queue);
  return queue->front;
}

void* queue_get_data(qnode *node)
{
  if(!node)
    return NULL;

  return node->data;
}

