#ifndef CACHESYSTEM_H
#define CACHESYSTEM_H

#include "DNSstructs.h"
#include "cacheHandler.h"

extern ArrayList* cache_list;
int init_cache_system();
int add_to_cache(const char* domain, const char* ip, uint32_t timeToLive);
char* get_from_cache(const char* domain);
int is_in_cache(const char* domain);
int add_addlists();
int add_to_adcache(const char* domain, const char* ip);
int is_in_adcache(const char* domain);
char* get_from_adcache(const char* domain);
int checkAndRemoveExpiredCache();
void printCacheCapacity();
uint32_t getDomainsInAdlist();
void printCache();
int wipeAdcache();
int addLocalEntry(const char* ip, const char* url, const char* name);
int removeLocalEntry(const char* url);
char* getLocalDNSEntries();
int reloadLocalDNSCache();

#endif