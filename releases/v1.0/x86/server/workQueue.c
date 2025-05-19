#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>

#include "DNSstructs.h"
#include "cacheHandler.h"
#include "apiHandler.h"

ThreadArgsQueue queue;

void init_queue() {
    queue.front = 0;
    queue.rear = 0;
    queue.count = 0;
    pthread_mutex_init(&queue.lock, NULL);
    pthread_cond_init(&queue.not_empty, NULL);
    pthread_cond_init(&queue.not_full, NULL);
}

void enqueue(ThreadArgs* item) {
    pthread_mutex_lock(&queue.lock);

    while (queue.count == QUEUE_SIZE) {
        pthread_cond_wait(&queue.not_full, &queue.lock);
    }

    queue.items[queue.rear] = item;
    queue.rear = (queue.rear + 1) % QUEUE_SIZE;
    queue.count++;

    pthread_cond_signal(&queue.not_empty);
    pthread_mutex_unlock(&queue.lock);

    addToQueue();
}

ThreadArgs* dequeue() {
    pthread_mutex_lock(&queue.lock);

    while(queue.count == 0) {
        pthread_cond_wait(&queue.not_empty, &queue.lock);
    }

    ThreadArgs* item = queue.items[queue.front];
    queue.front = (queue.front + 1) % QUEUE_SIZE;
    queue.count--;

    pthread_cond_signal(&queue.not_full);
    pthread_mutex_unlock(&queue.lock);

    removeFromQueue();
    return item;
}