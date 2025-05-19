#ifndef THREAD_H
#define THREAD_H

#include <ldns/ldns.h>

void* processDNS(void* arg);
void enableAdCache();
void disableAdCache();
int changeUpstreamDNS(const char* new_ip);
char* getUpstreamDNS();

#endif // THREAD_H