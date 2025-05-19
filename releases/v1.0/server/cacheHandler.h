#ifndef CACHEHANDLER_H // Changed from ARRAYLIST_H to CACHEHANDLER_H
#define CACHEHANDLER_H

#include <stdbool.h>
#include <stdint.h>
#include "hashmap.h" 
typedef HashMap ArrayList;


ArrayList* createArrayList();


void add(ArrayList* list, IPUrlPair element, int* new_node_count_increment);
void removeElement(ArrayList* list, const char* url);
IPUrlPair* find(ArrayList* list, const char* url);
int size(ArrayList* list);
bool isEmpty(ArrayList* list);
void printArrayList(ArrayList* list);
void freeArrayList(ArrayList* list);
int cleanList(ArrayList* list); 
uint32_t getListSize(ArrayList* list);
int wipeList(ArrayList* list);

#endif // CACHEHANDLER_H
