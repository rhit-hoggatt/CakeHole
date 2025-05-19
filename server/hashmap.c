#include "hashmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h> // For inet_pton

#define MAX_LOAD_FACTOR 0.75 // Trigger resize when size/capacity > MAX_LOAD_FACTOR
#define RESIZE_FACTOR 2      // Factor by which to increase capacity during resize

static unsigned long hashFunction(const char* str, int capacity) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash % capacity;
}

// --- Helper function to create a new HashNode ---
static HashNode* createHashNode(IPUrlPair element) {
    HashNode* newNode = (HashNode*)malloc(sizeof(HashNode));
    if (newNode == NULL) {
        perror("Failed to allocate memory for HashNode");
        return NULL;
    }
    newNode->pair = element; // Struct copy
    newNode->next = NULL;
    return newNode;
}

static bool resizeHashMap(HashMap* map) {
    if (map == NULL) return false;

    int old_capacity = map->capacity;
    int new_capacity = old_capacity * RESIZE_FACTOR;
    if (new_capacity <= old_capacity) { // Overflow or no change
        new_capacity = old_capacity + 1; // Ensure capacity increases
    }

    HashNode** new_buckets = (HashNode**)calloc(new_capacity, sizeof(HashNode*));
    if (new_buckets == NULL) {
        perror("Failed to allocate memory for resizing hash map buckets");
        return false; // Resize failed
    }

    // Rehash all existing elements into the new buckets
    for (int i = 0; i < old_capacity; i++) {
        HashNode* current = map->buckets[i];
        while (current != NULL) {
            HashNode* next = current->next; // Save next node
            unsigned long new_index = hashFunction(current->pair.url, new_capacity);

            // Insert into new bucket (at the head)
            current->next = new_buckets[new_index];
            new_buckets[new_index] = current;

            current = next;
        }
    }

    free(map->buckets); // Free the old bucket array
    map->buckets = new_buckets;
    map->capacity = new_capacity;

    // printf("HashMap resized to capacity: %d\n", new_capacity);
    return true;
}


// --- Public HashMap Functions ---

HashMap* createHashMap(int initial_capacity) {
    if (initial_capacity <= 0) {
        initial_capacity = 16; // Default initial capacity if invalid is provided
    }
    HashMap* map = (HashMap*)malloc(sizeof(HashMap));
    if (map == NULL) {
        perror("Failed to allocate memory for HashMap");
        return NULL;
    }

    map->buckets = (HashNode**)calloc(initial_capacity, sizeof(HashNode*));
    if (map->buckets == NULL) {
        perror("Failed to allocate memory for HashMap buckets");
        free(map);
        return NULL;
    }

    map->capacity = initial_capacity;
    map->size = 0;

    if (pthread_mutex_init(&map->lock, NULL) != 0) {
        perror("Failed to initialize mutex for HashMap");
        free(map->buckets);
        free(map);
        return NULL;
    }
    return map;
}

void freeHashMap(HashMap* map) {
    if (map == NULL) return;

    pthread_mutex_lock(&map->lock);
    for (int i = 0; i < map->capacity; i++) {
        HashNode* current = map->buckets[i];
        while (current != NULL) {
            HashNode* temp = current;
            current = current->next;
            free(temp);
        }
        map->buckets[i] = NULL; // Not strictly necessary as buckets array is freed next
    }
    free(map->buckets);
    pthread_mutex_unlock(&map->lock);
    pthread_mutex_destroy(&map->lock);
    free(map);
}

int addHashMap(HashMap* map, IPUrlPair element, int* new_node_count_increment) {
    if (map == NULL || element.url == NULL || strlen(element.url) == 0) {
        if (new_node_count_increment) *new_node_count_increment = 0;
        return -1; // Invalid arguments
    }

    pthread_mutex_lock(&map->lock);

    // Check load factor and resize if necessary
    if ((double)map->size / map->capacity > MAX_LOAD_FACTOR) {
        if (!resizeHashMap(map)) {
            // Resize failed, cannot add element reliably
            pthread_mutex_unlock(&map->lock);
            if (new_node_count_increment) *new_node_count_increment = 0;
            fprintf(stderr, "HashMap resize failed. Element not added: %s\n", element.url);
            return -1;
        }
    }

    unsigned long index = hashFunction(element.url, map->capacity);
    HashNode* current = map->buckets[index];
    HashNode* prev = NULL;

    // Search for existing URL in the chain
    while (current != NULL) {
        if (strcmp(current->pair.url, element.url) == 0) {
            // URL found, update IP and TTL
            strcpy(current->pair.ip, element.ip);
            current->pair.timeToLive = element.timeToLive;
            pthread_mutex_unlock(&map->lock);
            if (new_node_count_increment) *new_node_count_increment = 0; // Existing node updated
            return 1; // Updated existing node
        }
        prev = current;
        current = current->next;
    }

    // URL not found, create and add new node
    HashNode* newNode = createHashNode(element);
    if (newNode == NULL) {
        pthread_mutex_unlock(&map->lock);
        if (new_node_count_increment) *new_node_count_increment = 0;
        return -1; // Memory allocation failed for new node
    }

    if (prev == NULL) { // Bucket was empty
        map->buckets[index] = newNode;
    } else { // Add to end of chain (or head, for simplicity here, add to head)
        newNode->next = map->buckets[index]; // Add to head of the list for this bucket
        map->buckets[index] = newNode;
    }
    
    map->size++;
    pthread_mutex_unlock(&map->lock);
    if (new_node_count_increment) *new_node_count_increment = 1; // New node added
    return 0; // New node added
}

IPUrlPair* findHashMap(HashMap* map, const char* url) {
    if (map == NULL || url == NULL) return NULL;

    pthread_mutex_lock(&map->lock);
    unsigned long index = hashFunction(url, map->capacity);
    HashNode* current = map->buckets[index];

    while (current != NULL) {
        if (strcmp(current->pair.url, url) == 0) {
            IPUrlPair* result = &current->pair;
            pthread_mutex_unlock(&map->lock);
            return result;
        }
        current = current->next;
    }

    pthread_mutex_unlock(&map->lock);
    return NULL; // Not found
}

bool removeHashMapElement(HashMap* map, const char* url) {
    if (map == NULL || url == NULL) return false;

    pthread_mutex_lock(&map->lock);
    unsigned long index = hashFunction(url, map->capacity);
    HashNode* current = map->buckets[index];
    HashNode* prev = NULL;

    while (current != NULL) {
        if (strcmp(current->pair.url, url) == 0) {
            if (prev == NULL) { // Node to remove is the head of the list
                map->buckets[index] = current->next;
            } else {
                prev->next = current->next;
            }
            free(current);
            map->size--;
            pthread_mutex_unlock(&map->lock);
            return true;
        }
        prev = current;
        current = current->next;
    }

    pthread_mutex_unlock(&map->lock);
    return false; // Not found
}

int getHashMapSize(HashMap* map) {
    if (map == NULL) return 0;
    pthread_mutex_lock(&map->lock);
    int current_size = map->size;
    pthread_mutex_unlock(&map->lock);
    return current_size;
}

bool isHashMapEmpty(HashMap* map) {
    if (map == NULL) return true; // Or handle as an error
    pthread_mutex_lock(&map->lock);
    bool empty = (map->size == 0);
    pthread_mutex_unlock(&map->lock);
    return empty;
}

void printHashMap(HashMap* map) {
    if (map == NULL) {
        printf("HashMap is NULL.\n");
        return;
    }

    pthread_mutex_lock(&map->lock);
    printf("HashMap Contents (Size: %d, Capacity: %d):\n", map->size, map->capacity);
    uint32_t current_time_sec = time(NULL);
    for (int i = 0; i < map->capacity; i++) {
        if (map->buckets[i] != NULL) {
            printf("Bucket %d:\n", i);
            HashNode* current = map->buckets[i];
            while (current != NULL) {
                printf("  { ip: \"%s\", url: \"%s\", ttl: %u",
                       current->pair.ip, current->pair.url, current->pair.timeToLive);
                if (current_time_sec > current->pair.timeToLive) {
                    printf(", expired: true");
                }
                printf(" }\n");
                current = current->next;
            }
        }
    }
    pthread_mutex_unlock(&map->lock);
}

int cleanHashMap(HashMap* map) {
    if (map == NULL) return 0;

    pthread_mutex_lock(&map->lock);
    int removed_count = 0;
    uint32_t current_time_sec = time(NULL);
    struct in_addr addr_validator; // For inet_pton

    for (int i = 0; i < map->capacity; i++) {
        HashNode* current = map->buckets[i];
        HashNode* prev = NULL;
        while (current != NULL) {
            bool should_remove = false;
            // Check for invalid IP or expired TTL
            if (inet_pton(AF_INET, current->pair.ip, &addr_validator) != 1 || // Invalid IP format
                (current->pair.timeToLive != 0 && current_time_sec > current->pair.timeToLive)) { // TTL expired (0 means never expires for adblock lists)
                should_remove = true;
            }

            if (should_remove) {
                HashNode* node_to_remove = current;
                if (prev == NULL) { // Head of the list for this bucket
                    map->buckets[i] = current->next;
                } else {
                    prev->next = current->next;
                }
                current = current->next; // Move to next before freeing
                free(node_to_remove);
                map->size--;
                removed_count++;
                // Do not advance prev, as current's predecessor is still prev
            } else {
                prev = current;
                current = current->next;
            }
        }
    }
    pthread_mutex_unlock(&map->lock);
    return removed_count;
}

void wipeHashMap(HashMap* map) {
    if (map == NULL) return;

    pthread_mutex_lock(&map->lock);
    for (int i = 0; i < map->capacity; i++) {
        HashNode* current = map->buckets[i];
        while (current != NULL) {
            HashNode* temp = current;
            current = current->next;
            free(temp);
        }
        map->buckets[i] = NULL;
    }
    map->size = 0;
    pthread_mutex_unlock(&map->lock);
}
