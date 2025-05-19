#ifndef WORKQUEUE_H
#define WORKQUEUE_H

#include <pthread.h>
#include "DNSstructs.h"

void init_queue();
void enqueue(ThreadArgs* item);
ThreadArgs* dequeue();
void printQueueSize();

#endif // WORKQUEUE_H