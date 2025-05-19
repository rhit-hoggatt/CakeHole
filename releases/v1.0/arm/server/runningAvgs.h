#ifndef RUNNING_AVGS_H
#define RUNNING_AVGS_H

void running_avgs_init(int window_size);
void running_avgs_free(void);
void running_avgs_add_cache_lookup(double value);
void running_avgs_add_query_response(double value);
double running_avgs_get_cache_lookup(void);
double running_avgs_get_query_response(void);
void running_avgs_add_cached_query_response(double value);
double running_avgs_get_cached_query_response(void);

#endif // RUNNING_AVGS_H