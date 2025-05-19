#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h> // For thread safety

typedef struct {
    char ip[16];
    char url[256];
    uint32_t timeToLive;
} IPUrlPair;

typedef struct HashNode {
    IPUrlPair pair;
    struct HashNode *next;
} HashNode;

// Define the structure for the hash map
typedef struct HashMap {
    HashNode **buckets;      // Array of pointers to HashNodes (the buckets)
    int capacity;            // Current capacity of the bucket array
    int size;                // Current number of elements in the hash map
    pthread_mutex_t lock;    // Mutex for thread-safe operations
} HashMap;

/**
 * @brief Creates a new hash map.
 * @param initial_capacity The initial number of buckets in the hash map.
 * @return A pointer to the newly created HashMap, or NULL on failure.
 */
HashMap* createHashMap(int initial_capacity);

/**
 * @brief Frees all memory associated with the hash map.
 * @param map A pointer to the HashMap to be freed.
 */
void freeHashMap(HashMap* map);

/**
 * @brief Adds or updates an IPUrlPair in the hash map.
 * If the URL already exists, its IP and TTL are updated. Otherwise, a new entry is added.
 * This function is thread-safe.
 * @param map A pointer to the HashMap.
 * @param element The IPUrlPair to add or update.
 * @param new_node_count_increment Pointer to an integer that will be set to 1 if a new node was added,
 * 0 if an existing node was updated or if an error occurred.
 * @return 0 if a new node was added, 1 if an existing node was updated, -1 on error (e.g., memory allocation failure).
 */
int addHashMap(HashMap* map, IPUrlPair element, int* new_node_count_increment);

/**
 * @brief Finds an IPUrlPair in the hash map by its URL.
 * This function is thread-safe.
 * @param map A pointer to the HashMap.
 * @param url The URL to search for.
 * @return A pointer to the found IPUrlPair, or NULL if the URL is not found.
 * The returned pointer is to the internal data and should not be freed by the caller.
 */
IPUrlPair* findHashMap(HashMap* map, const char* url);

/**
 * @brief Removes an element from the hash map by its URL.
 * This function is thread-safe.
 * @param map A pointer to the HashMap.
 * @param url The URL of the element to remove.
 * @return true if the element was successfully removed, false otherwise.
 */
bool removeHashMapElement(HashMap* map, const char* url);

/**
 * @brief Gets the current number of elements in the hash map.
 * This function is thread-safe.
 * @param map A pointer to the HashMap.
 * @return The number of elements.
 */
int getHashMapSize(HashMap* map);

/**
 * @brief Checks if the hash map is empty.
 * This function is thread-safe.
 * @param map A pointer to the HashMap.
 * @return true if the hash map is empty, false otherwise.
 */
bool isHashMapEmpty(HashMap* map);

/**
 * @brief Prints the contents of the hash map (for debugging purposes).
 * This function is thread-safe.
 * @param map A pointer to the HashMap.
 */
void printHashMap(HashMap* map);

/**
 * @brief Removes expired or invalid entries from the hash map.
 * An entry is considered invalid if its IP is not a valid IPv4 address or if its TTL has expired.
 * This function is thread-safe.
 * @param map A pointer to the HashMap.
 * @return The number of elements removed.
 */
int cleanHashMap(HashMap* map);

/**
 * @brief Removes all elements from the hash map, making it empty.
 * This function is thread-safe.
 * @param map A pointer to the HashMap.
 */
void wipeHashMap(HashMap* map);

#endif // HASHMAP_H
