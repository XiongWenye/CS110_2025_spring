#include "queue.h"
#include <stdlib.h>
#include <stdio.h>

Queue *queue_create(void)
{
  Queue *queue = malloc(sizeof(Queue));
  if (queue == NULL)
  {
    fprintf(stderr, "Error: malloc failed\n");
    exit(1);
  }
  queue->size = 0;
  queue->capacity = QUEUE_INITIAL_CAPACITY;
  queue->data = malloc(sizeof(double) * queue->capacity);
  if (queue->data == NULL)
  {
    fprintf(stderr, "Error: malloc failed\n");
    exit(1);
  }
  return queue;
}

void push(Queue *queue, double element)
{
  if (queue == NULL)
  {
    fprintf(stderr, "Error: queue is NULL\n");
    exit(1);
  }
  if (queue->size == queue->capacity)
  {
    int capacity = queue->capacity * 2;

    queue->data = realloc(queue->data, sizeof(double) * capacity);
    if (queue->data == NULL)
    {
      fprintf(stderr, "Error: realloc failed\n");
      exit(1);
    }
    queue->capacity = capacity;
  }

  int insert_index = queue->size % queue->capacity;
  queue->data[insert_index] = element;
  queue->size++;
}

double back(Queue *queue)
{
  if (queue == NULL)
  {
    fprintf(stderr, "Error: queue is NULL\n");
    exit(1);
  }
  if (queue->size == 0)
  {
    fprintf(stderr, "Error: queue is empty\n");
    exit(1);
  }
  return queue->data[queue->size - 1];
}

void queue_free(Queue *queue)
{
  if (queue == NULL)
  {
    fprintf(stderr, "Error: queue is NULL\n");
    exit(1);
  }
  free(queue->data);
  free(queue);
}

