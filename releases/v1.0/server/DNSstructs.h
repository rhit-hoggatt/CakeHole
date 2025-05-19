#ifndef DNSSTRUCTS_H
#define DNSSTRUCTS_H

#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define PORT 53
#define CACHE_ENABLED 1
#define QUEUE_SIZE 10000

typedef struct {
  int sockfd;
  struct sockaddr_in client_addr;
  socklen_t client_len;
  char* buffer;
  ssize_t n;
} ThreadArgs;

typedef struct {
  ThreadArgs* items[QUEUE_SIZE];
  int front;
  int rear;
  int count;
  pthread_mutex_t lock;
  pthread_cond_t not_empty;
  pthread_cond_t not_full;
} ThreadArgsQueue;

#endif