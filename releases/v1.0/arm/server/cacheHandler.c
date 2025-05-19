#include "cacheHandler.h" 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_INITIAL_CAPACITY 16384 

ArrayList* createArrayList() {
    return createHashMap(DEFAULT_INITIAL_CAPACITY);
}

uint32_t getListSize(ArrayList* list) {
    if (list == NULL) {
        return 0;
    }
    return (uint32_t)getHashMapSize(list);
}

void add(ArrayList* list, IPUrlPair element, int* new_node_count_increment) {
    if (list == NULL) {
        if (new_node_count_increment) *new_node_count_increment = 0;
        fprintf(stderr, "ArrayList (HashMap) is NULL, cannot add element.\n");
        return;
    }

    int result = addHashMap(list, element, new_node_count_increment);
    if (result == -1) {
        fprintf(stderr, "Failed to add element to ArrayList (HashMap): %s\n", element.url);
    }
}

void removeElement(ArrayList* list, const char* url) {
    if (list == NULL || url == NULL) {
        fprintf(stderr, "ArrayList (HashMap) or URL is NULL, cannot remove element.\n");
        return;
    }
    removeHashMapElement(list, url);

}

IPUrlPair* find(ArrayList* list, const char* url) {
    if (list == NULL || url == NULL) {
        return NULL;
    }
    return findHashMap(list, url);
}

void printArrayList(ArrayList* list) {
    if (list == NULL) {
        printf("ArrayList (HashMap) is NULL.\n");
        return;
    }
    printf("ArrayList (HashMap) Contents:\n"); // Wrapper print
    printHashMap(list);                       // Detailed print from HashMap implementation
}

int cleanList(ArrayList* list) { 
    if (list == NULL) {
        fprintf(stderr, "ArrayList (HashMap) is NULL, cannot clean.\n");
        return 0;
    }
    int removed_count = cleanHashMap(list);
    return removed_count;
}

void freeArrayList(ArrayList* list) {
    if (list == NULL) {
        return;
    }
    freeHashMap(list);
}

int size(ArrayList* list) {
    if (list == NULL) {
        return 0;
    }
    return getHashMapSize(list);
}

bool isEmpty(ArrayList* list) {
    if (list == NULL) {
        return true; 
    }
    return isHashMapEmpty(list);
}

/**
 * @brief Removes all elements from the ArrayList.
 */
int wipeList(ArrayList* list) {
    if (list == NULL) {
        return -1; 
    }
    wipeHashMap(list); 
    return 0; // Success
}