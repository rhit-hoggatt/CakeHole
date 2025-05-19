#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>

typedef struct {
    double *values;
    int size;
    int index;
    int count;
    double sum;
    pthread_mutex_t mutex;
} RollingAvg;

static RollingAvg avg_cache_lookup;
static RollingAvg avg_query_response;
static RollingAvg avg_cached_query_response;

static void rolling_avg_init(RollingAvg *ra, int window_size) {
    if (window_size <= 0) {
        ra->values = NULL;
        ra->size = 0;
        ra->index = 0;
        ra->count = 0;
        ra->sum = 0.0;
        pthread_mutex_init(&ra->mutex, NULL);
        return;
    }
    ra->values = (double *)calloc(window_size, sizeof(double));
    ra->size = window_size;
    ra->index = 0;
    ra->count = 0;
    ra->sum = 0.0;
    pthread_mutex_init(&ra->mutex, NULL);
}

static void rolling_avg_free(RollingAvg *ra) {
    free(ra->values);
    ra->values = NULL;
    pthread_mutex_destroy(&ra->mutex);
}

static void rolling_avg_add(RollingAvg *ra, double value) {
    pthread_mutex_lock(&ra->mutex);
    if (ra->size == 0) {
        pthread_mutex_unlock(&ra->mutex);
        return;
    }
    if (ra->count < ra->size) {
        ra->sum += value;
        ra->values[ra->index++] = value;
        ra->count++;
    } else {
        ra->index %= ra->size;
        ra->sum -= ra->values[ra->index];
        ra->sum += value;
        ra->values[ra->index++] = value;
    }
    pthread_mutex_unlock(&ra->mutex);
}

static double rolling_avg_get(const RollingAvg *ra) {
    double avg;
    pthread_mutex_lock((pthread_mutex_t *)&ra->mutex); // cast away const for mutex
    if (ra->count == 0)
        avg = 0.0;
    else
        avg = ra->sum / ra->count;
    pthread_mutex_unlock((pthread_mutex_t *)&ra->mutex);
    return avg;
}

void running_avgs_init(int window_size) {
    rolling_avg_init(&avg_cache_lookup, window_size);
    rolling_avg_init(&avg_query_response, window_size);
    rolling_avg_init(&avg_cached_query_response, window_size);
}

void running_avgs_free(void) {
    rolling_avg_free(&avg_cache_lookup);
    rolling_avg_free(&avg_query_response);
    rolling_avg_free(&avg_cached_query_response);
}

void running_avgs_add_cache_lookup(double value) {
    printf("Adding cache lookup: %f\n", value);
    rolling_avg_add(&avg_cache_lookup, value);
}

void running_avgs_add_query_response(double value) {
    printf("Adding query response: %f\n", value);
    rolling_avg_add(&avg_query_response, value);
}

void running_avgs_add_cached_query_response(double value) {
    printf("Adding cached query response: %f\n", value);
    rolling_avg_add(&avg_cached_query_response, value);
}

double running_avgs_get_cache_lookup(void) {
    printf("Cache Lookup Average: %f\n", rolling_avg_get(&avg_cache_lookup));
    return rolling_avg_get(&avg_cache_lookup);
}

double running_avgs_get_query_response(void) {
    printf("Query Response Average: %f\n", rolling_avg_get(&avg_query_response));
    return rolling_avg_get(&avg_query_response);
}

double running_avgs_get_cached_query_response(void) {
    printf("Cached Query Response Average: %f\n", rolling_avg_get(&avg_cached_query_response));
    return rolling_avg_get(&avg_cached_query_response);
}