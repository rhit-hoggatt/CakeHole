#ifndef APIHANDLER_H
#define APIHANDLER_H

#include <stdint.h>
#include <pthread.h>

extern uint32_t totalQueriesProcessed;
extern pthread_mutex_t total_queries_lock; 

extern uint32_t totalQueriesBlocked;
extern pthread_mutex_t total_queries_blocked_lock;

extern uint32_t totalValsInCache;
extern pthread_mutex_t total_vals_in_cache_lock;

extern uint32_t totalCacheHits;
extern pthread_mutex_t total_cache_hits_lock;

// Function declarations
int addProcessedQuery();
int addBlockedQuery();
int updateCacheSize(uint32_t size);
int addCacheHit();
int addToQueue();
int removeFromQueue();
int checkAdlistStatus(const char* filename);
int getNumThreads();
int setNumThreads(int numThreads);

void printProcessedQueries();
void printBlockedQueries();
void printCacheCapacity();
void printCacheSize();
void printCacheHits();

void* handleAPIs(void* arg);

#endif // APIHANDLER_H