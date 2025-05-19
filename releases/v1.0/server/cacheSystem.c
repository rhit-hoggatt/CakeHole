#include <stdio.h>
#include <dirent.h>
#include <sys/types.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include "DNSstructs.h"
#include "cacheHandler.h"
#include "apiHandler.h"

ArrayList* cache_list = NULL;
ArrayList* adlist = NULL;

uint32_t numAdDomains;

pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t adlist_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t adDomains_mutex = PTHREAD_MUTEX_INITIALIZER;

int init_cache_system() {
    numAdDomains = 0;
    cache_list = createArrayList();
    adlist = createArrayList();
    if (cache_list == NULL || adlist == NULL) {
        fprintf(stderr, "Failed to create cache list\n");
        return -1;
    }

    return 0;
}

uint32_t getDomainsInAdlist() {
    pthread_mutex_lock(&adDomains_mutex);
    uint32_t count = numAdDomains;
    pthread_mutex_unlock(&adDomains_mutex);
    return count;
}

int is_in_cache(const char* domain) {
    pthread_mutex_lock(&cache_mutex);
    int result = find(cache_list, domain) != NULL;
    pthread_mutex_unlock(&cache_mutex);
    return result;
}

int is_in_adcache(const char* domain) {
    pthread_mutex_lock(&adlist_mutex);
    int result = find(adlist, domain) != NULL;
    pthread_mutex_unlock(&adlist_mutex);
    return result;
}

int remove_from_cache(const char* domain) {
    pthread_mutex_lock(&cache_mutex);
    removeElement(cache_list, domain);
    pthread_mutex_unlock(&cache_mutex);
    return 0;
}

int checkAndRemoveExpiredCache() {
    pthread_mutex_lock(&cache_mutex);
    pthread_mutex_lock(&adlist_mutex);
    int check = cleanList(cache_list);
    printf("\nCache size after cleanup: %d\n\n", getListSize(cache_list));
    updateCacheSize(getListSize(cache_list));
    pthread_mutex_unlock(&adlist_mutex);
    pthread_mutex_unlock(&cache_mutex);
    return check;
}

int add_to_cache(const char* domain, const char* ip, uint32_t timeToLive) {
    if (is_in_cache(domain)) {
        return -1;
    }
    pthread_mutex_lock(&cache_mutex);

    // Allocate memory for the IPUrlPair
    IPUrlPair* pair = malloc(sizeof(IPUrlPair));
    if (pair == NULL) {
        fprintf(stderr, "Failed to allocate memory for cache entry\n");
        pthread_mutex_unlock(&cache_mutex);
        return -1;
    }

    // Copy the IP and domain into the pair
    snprintf(pair->ip, sizeof(pair->ip), "%s", ip);
    snprintf(pair->url, sizeof(pair->url), "%s", domain);
    pair->timeToLive = timeToLive;

    int count;
    add(cache_list, *pair, &count);
    if (count == 0) {
        free(pair);
        pthread_mutex_unlock(&cache_mutex);
        return -1;
    }

    // Free the allocated memory for the pair after adding it to the cache
    free(pair);

    pthread_mutex_unlock(&cache_mutex);
    return 0;
}

int add_to_adcache(const char* domain, const char* ip) {
    pthread_mutex_lock(&adlist_mutex);

    IPUrlPair pair;
    snprintf(pair.ip, sizeof(pair.ip), "%s", ip);
    snprintf(pair.url, sizeof(pair.url), "%s", domain);

    int count;
    add(adlist, pair, &count);
    if (count == 0) {
        pthread_mutex_unlock(&adlist_mutex);
        return -1;
    }
    pthread_mutex_unlock(&adlist_mutex);
    return 0;
}

char* get_from_cache(const char* domain) {
    pthread_mutex_lock(&cache_mutex);
    IPUrlPair* pair = find(cache_list, domain);
    char* result = pair != NULL ? pair->ip : NULL;
    pthread_mutex_unlock(&cache_mutex);
    return result;
}

char* get_from_adcache(const char* domain) {
    pthread_mutex_lock(&adlist_mutex);
    IPUrlPair* pair = find(adlist, domain);
    char* result = pair != NULL ? pair->ip : NULL;
    pthread_mutex_unlock(&adlist_mutex);
    return result;
}

int wipeAdcache() {
    pthread_mutex_lock(&adlist_mutex);
    wipeList(adlist);
    pthread_mutex_unlock(&adlist_mutex);
    pthread_mutex_lock(&adDomains_mutex);
    numAdDomains = 0;
    pthread_mutex_unlock(&adDomains_mutex);
    return 0;
}

int isValidDomain(const char* domain) {
    if (domain == NULL || strlen(domain) == 0 || strlen(domain) > 253) {
        return 0; // Invalid if null, empty, or exceeds max length
    }

    const char* ptr = domain;
    int labelLength = 0;

    while (*ptr) {
        if (*ptr == '.') {
            if (labelLength == 0 || labelLength > 63) {
                return 0; // Invalid if label is empty or exceeds max length
            }
            labelLength = 0; // Reset for the next label
        } else if (!((*ptr >= 'a' && *ptr <= 'z') || (*ptr >= 'A' && *ptr <= 'Z') || 
                     (*ptr >= '0' && *ptr <= '9') || *ptr == '-')) {
            return 0; // Invalid if contains invalid characters
        } else {
            if (labelLength == 0 && *ptr == '-') {
                return 0; // Invalid if label starts with a hyphen
            }
            labelLength++;
        }
        ptr++;
    }

    if (labelLength == 0 || labelLength > 63 || domain[strlen(domain) - 1] == '-') {
        return 0; // Invalid if last label is empty, too long, or ends with a hyphen
    }

    return 1; // Valid domain
}

int isValidIP(const char* ip) {
    struct sockaddr_in sa;
    int result = inet_pton(AF_INET, ip, &(sa.sin_addr));
    return result != 0; // Returns 1 if valid, 0 if invalid
}

void cleanInput(char* input, char* output, size_t outputSize) {
    // Remove protocol (e.g., "http://", "https://")
    char* start = strstr(input, "://");
    if (start) {
        start += 3; // Skip "://"
    } else {
        start = input;
    }

    // Copy up to the first '/' or end of string
    size_t i = 0;
    while (*start && *start != '/' && i < outputSize - 1) {
        output[i++] = *start++;
    }
    output[i] = '\0';

    // Remove trailing dot, if present
    size_t len = strlen(output);
    if (len > 0 && output[len - 1] == '.') {
        output[len - 1] = '\0';
    }
}

char* getLocalDNSEntries() {
    FILE* file = fopen("adlists/metadata/localDNS.txt", "r");
    if (file == NULL) {
        fprintf(stderr, "Failed to open localDNS.txt for reading\n");
        return NULL;
    }

    size_t bufsize = 4096;
    size_t used = 0;
    char* entries = malloc(bufsize);
    if (entries == NULL) {
        fprintf(stderr, "Failed to allocate memory for entries\n");
        fclose(file);
        return NULL;
    }
    entries[0] = '\0';

    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        size_t linelen = strlen(line);
        if (used + linelen + 1 > bufsize) {
            bufsize = (used + linelen + 1) * 2;
            char* new_entries = realloc(entries, bufsize);
            if (new_entries == NULL) {
                fprintf(stderr, "Failed to reallocate memory for entries\n");
                free(entries);
                fclose(file);
                return NULL;
            }
            entries = new_entries;
        }
        strcpy(entries + used, line);
        used += linelen;
    }

    fclose(file);
    return entries;
}

int add_addlists() {
    DIR* dir = opendir("adlists/listdata");
    if (dir == NULL) {
        fprintf(stderr, "Failed to open adlists directory\n");
        return -1;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "adlists/listdata/%s", entry->d_name);
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        int adlistCheck = checkAdlistStatus(filepath);
        if (adlistCheck != 1) {
            printf("Adlist %s is not enabled, skipping...\n", filepath);
            continue;
        }

        FILE* file = fopen(filepath, "r");
        if (file == NULL) {
            fprintf(stderr, "Failed to open file: %s\n", filepath);
            closedir(dir);
            return -1;
        } else {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            printf("Reading file: %s\n", filepath);
        }

        char line[1024];
        while (fgets(line, sizeof(line), file)) {
            // Trim leading and trailing whitespace
            char* trimmedLine = strtok(line, "\n\r");
            if (!trimmedLine || trimmedLine[0] == '#') {
                // Skip empty lines or comments
                continue;
            }

            char domain[1024] = {0};
            char ip[1024] = {0};

            // Try to parse the line
            int tokensRead = sscanf(trimmedLine, "%1023s %1023s", ip, domain);

            if (tokensRead == 1) {
                // If only one token is read, treat it as a domain and assign IP "0.0.0.0"
                snprintf(domain, sizeof(domain), "%s", ip);
                snprintf(ip, sizeof(ip), "0.0.0.0");
            } else if (tokensRead != 2) {
                // If the line doesn't match the expected format, skip it
                printf("Skipping invalid line: %s\n", trimmedLine);
                continue;
            }

            // Check if the domain and IP are swapped
            if (isValidIP(domain) && isValidDomain(ip)) {
                // Swap them if necessary
                char temp[1024];
                snprintf(temp, sizeof(temp), "%s", domain);
                snprintf(domain, sizeof(domain), "%s", ip);
                snprintf(ip, sizeof(ip), "%s", temp);
            }

            char cleanedDomain[1024];
            cleanInput(domain, cleanedDomain, sizeof(cleanedDomain));

            if (!isValidDomain(cleanedDomain) || !isValidIP(ip)) {
                printf("Invalid domain or IP: %s -> %s\n", domain, ip);
                continue;
            }

            int adCheck = add_to_adcache(cleanedDomain, ip);
            if (adCheck != -1) {
                pthread_mutex_lock(&adDomains_mutex);
                numAdDomains++;
                pthread_mutex_unlock(&adDomains_mutex);
            } 
        }

        fclose(file);
    }
    closedir(dir);

    return 0;
}

int reloadLocalDNSCache() {
    FILE* file = fopen("adlists/metadata/localDNS.txt", "r");
    if (file == NULL) {
        fprintf(stderr, "Failed to open localDNS.txt for reading\n");
        return -1;
    }

    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        char ip[256] = {0};
        char domain[768] = {0};
        if (sscanf(line, "%255s %767s", ip, domain) == 2) {
            if (!isValidIP(ip) || !isValidDomain(domain)) {
                printf("Invalid entry in localDNS.txt: %s\n", line);
                continue;
            }
            remove_from_cache(domain);
            uint32_t timeToLive = 0;
            int adCheck = add_to_cache(domain, ip, timeToLive);
            if (adCheck != -1) {
                pthread_mutex_lock(&adDomains_mutex);
                numAdDomains++;
                pthread_mutex_unlock(&adDomains_mutex);
            } 
        }
    }

    fclose(file);
    return 0;
}

int addLocalEntry(const char* ip, const char* url, const char* name) {
    FILE* file = fopen("adlists/metadata/localDNS.txt", "a");
    if (file == NULL) {
        fprintf(stderr, "Failed to open localDNS.txt for appending\n");
        return -1;
    }
    if (!isValidIP(ip) || !isValidDomain(url)) {
        fprintf(stderr, "Invalid IP or domain: %s -> %s\n", ip, url);
        fclose(file);
        return -1;
    }
    fprintf(file, "%s %s %s\n", ip, url, name);
    fclose(file);

    reloadLocalDNSCache();

    return 0;
}

int removeLocalEntry(const char* url) {
    FILE* file = fopen("adlists/metadata/localDNS.txt", "r");
    if (file == NULL) {
        fprintf(stderr, "Failed to open localDNS.txt for reading\n");
        return -1;
    }

    FILE* tempFile = fopen("adlists/metadata/temp.txt", "w");
    if (tempFile == NULL) {
        fprintf(stderr, "Failed to open temp file for writing\n");
        fclose(file);
        return -1;
    }

    char line[1024];
    int found = 0;
    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, url)) {
            found = 1;
        } else {
            fprintf(tempFile, "%s", line);
        }
    }

    fclose(file);
    fclose(tempFile);

    if (found) {
        remove("adlists/metadata/localDNS.txt");
        rename("adlists/metadata/temp.txt", "adlists/metadata/localDNS.txt");
    } else {
        remove("adlists/metadata/temp.txt");
    }

    reloadLocalDNSCache();

    return found ? 0 : -1;
}

void printCache() {
    pthread_mutex_lock(&cache_mutex);
    printf("Cache contents:\n");
    printArrayList(cache_list);
    pthread_mutex_unlock(&cache_mutex);
}