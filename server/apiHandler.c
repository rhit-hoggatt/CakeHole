#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <ldns/ldns.h>
#include <microhttpd.h>
#include <time.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "cacheSystem.h"
#include "thread.h"
#include "runningAvgs.h"

#define SALT_SIZE 16
#define HASH_SIZE 64

uint32_t totalQueriesProcessed;
pthread_mutex_t total_queries_lock = PTHREAD_MUTEX_INITIALIZER;
uint32_t totalQueriesBlocked;
pthread_mutex_t total_queries_blocked_lock = PTHREAD_MUTEX_INITIALIZER;
uint32_t totalValsInCache;
pthread_mutex_t total_vals_in_cache_lock = PTHREAD_MUTEX_INITIALIZER;
uint32_t totalCacheHits;
pthread_mutex_t total_cache_hits_lock = PTHREAD_MUTEX_INITIALIZER;
uint32_t queriesInQueue;
pthread_mutex_t qiqLock = PTHREAD_MUTEX_INITIALIZER;
uint32_t totalCacheSize;
pthread_mutex_t total_cache_size_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t logFileLock = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t adlistFileLock = PTHREAD_MUTEX_INITIALIZER;

int addProcessedQuery() {
    pthread_mutex_lock(&total_queries_lock);
    totalQueriesProcessed++;
    pthread_mutex_unlock(&total_queries_lock);
    return totalQueriesProcessed;
}
int addBlockedQuery() {
    pthread_mutex_lock(&total_queries_blocked_lock);
    totalQueriesBlocked++;
    pthread_mutex_unlock(&total_queries_blocked_lock);
    return totalQueriesBlocked;
}
int updateCacheSize(uint32_t size) {
    pthread_mutex_lock(&total_vals_in_cache_lock);
    totalValsInCache = size;
    pthread_mutex_unlock(&total_vals_in_cache_lock);
    return totalValsInCache;
}
int addCacheHit() {
    pthread_mutex_lock(&total_cache_hits_lock);
    totalCacheHits++;
    pthread_mutex_unlock(&total_cache_hits_lock);
    return totalCacheHits;
} 
int addToQueue() {
    pthread_mutex_lock(&qiqLock);
    queriesInQueue++;
    pthread_mutex_unlock(&qiqLock);
    return queriesInQueue;
}
int removeFromQueue() {
    pthread_mutex_lock(&qiqLock);
    queriesInQueue--;
    pthread_mutex_unlock(&qiqLock);
    return queriesInQueue;
}

void printProcessedQueries() {
    pthread_mutex_lock(&total_queries_lock);
    printf("Total queries processed: %d\n", totalQueriesProcessed);
    pthread_mutex_unlock(&total_queries_lock);
}
void printBlockedQueries() {
    pthread_mutex_lock(&total_queries_blocked_lock);
    printf("Total queries blocked: %d\n", totalQueriesBlocked);
    pthread_mutex_unlock(&total_queries_blocked_lock);
}
void printCacheCapacity() {
    pthread_mutex_lock(&total_vals_in_cache_lock);
    printf("Total values in cache: %d\n", totalValsInCache);
    pthread_mutex_unlock(&total_vals_in_cache_lock);
}
void printCacheHits() {
    pthread_mutex_lock(&total_cache_hits_lock);
    printf("Total cache hits: %d\n", totalCacheHits);
    pthread_mutex_unlock(&total_cache_hits_lock);
}
void printValInQueue() {
    pthread_mutex_lock(&qiqLock);
    printf("Queries in queue: %d\n", queriesInQueue);
    pthread_mutex_unlock(&qiqLock);
}

int getNumThreads() {
    int numThreads = sysconf(_SC_NPROCESSORS_ONLN);

    FILE* file = fopen("adlists/metadata/data.txt", "r");
    if (!file) {
        perror("Failed to open data.txt");
        return numThreads;
    }
    char line[256];
    // Skip the first line (login info)
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return numThreads;
    }
    // Read the second line (thread info)
    if (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "THREADS %d", &numThreads) != 1) {
            return numThreads;
        }
    }
    fclose(file);
    return numThreads;
}

int setNumThreads(int numThreads) {
    FILE* file = fopen("adlists/metadata/data.txt", "r+");
    if (!file) {
        perror("Failed to open data.txt");
        return -1;
    }

    char line[256];
    // Skip the first line (login info)
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return -1;
    }
    // Read the second line (thread info)
    if (fgets(line, sizeof(line), file)) {
        fseek(file, -strlen(line), SEEK_CUR);
        fprintf(file, "THREADS %d\n", numThreads);
    }
    fclose(file);
    return 0;
}

typedef enum MHD_Result (*ApiHandler)(struct MHD_Connection* connection);

typedef struct {
    const char* endpoint;
    ApiHandler handler;
} ApiEndpoint;

int checkAdlistStatus(const char* filename) {
    pthread_mutex_lock(&adlistFileLock);
    FILE* file = fopen("adlists/metadata/lists.txt", "r");
    if (!file) {
        perror("Failed to open adlist file");
        pthread_mutex_unlock(&adlistFileLock);
        return -1;
    }

    char line[256];
    const char* fileName = strrchr(filename, '/');
    if (fileName) {
        fileName++; // Move past the '/'
    } else {
        fileName = filename; // No '/' found, use the whole filename
    }

    int status = -1; // Default to not found
    while (fgets(line, sizeof(line), file)) {
        char url[256], currentStatus[16];
        if (sscanf(line, "%255s %15s", url, currentStatus) == 2) {
            const char* currentFileName = strrchr(url, '/');
            if (currentFileName) {
                currentFileName++; // Move past the '/'
            } else {
                currentFileName = url; // No '/' found, use the whole URL
            }
            if (strcmp(currentFileName, fileName) == 0) {
                if (strcmp(currentStatus, "enabled") == 0) {
                    status = 1;
                } else if (strcmp(currentStatus, "disabled") == 0) {
                    status = 0;
                }
                break;
            }
        }
    }

    fclose(file);
    pthread_mutex_unlock(&adlistFileLock);
    return status;
}

int resetAdlists() {
    if (wipeAdcache() != 0) {
        fprintf(stderr, "Failed to wipe adcache\n");
        return -1;
    }
    if (add_addlists() != 0) {
        fprintf(stderr, "Failed to add adlists\n");
        return -1;
    }
    return 0;
}

static enum MHD_Result handleGetTotalNumOfQueries(struct MHD_Connection* connection) {
    char response[1024];
    pthread_mutex_lock(&total_queries_lock);
    uint32_t totalQueriesProcessedCopy = totalQueriesProcessed;
    pthread_mutex_unlock(&total_queries_lock);
    pthread_mutex_lock(&total_queries_blocked_lock);
    uint32_t totalQueriesBlockedCopy = totalQueriesBlocked;
    pthread_mutex_unlock(&total_queries_blocked_lock);
    pthread_mutex_lock(&total_vals_in_cache_lock);
    uint32_t totalValsInCacheCopy = totalValsInCache;
    pthread_mutex_unlock(&total_vals_in_cache_lock);
    pthread_mutex_lock(&total_cache_hits_lock);
    uint32_t totalCacheHitsCopy = totalCacheHits;
    pthread_mutex_unlock(&total_cache_hits_lock);
    pthread_mutex_lock(&qiqLock);
    uint32_t queriesInQueueCopy = queriesInQueue;
    pthread_mutex_unlock(&qiqLock);
    snprintf(response, sizeof(response),
        "{\"processed\": %d, \"blocked\": %d, \"cache\": %d, \"hits\": %d, \"queue\": %d}",
        totalQueriesProcessedCopy, totalQueriesBlockedCopy, totalValsInCacheCopy, totalCacheHitsCopy, queriesInQueueCopy);
    struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
    return MHD_queue_response(connection, MHD_HTTP_OK, resp);
}

int loadAdlistsFromFile() {
    if (system("rm -rf adlists/listdata/*") != 0) {
        perror("Failed to remove old adlist files");
        return -1;
    }

    FILE* file = fopen("adlists/metadata/lists.txt", "r");
    if (!file) {
        perror("Failed to open adlist file");
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        char url[256], status[16];
        if (sscanf(line, "%255s %15s", url, status) == 2) {
            char command[512];
            snprintf(command, sizeof(command), "wget -q -P adlists/listdata/ %s", url);
            int ret = system(command);
            if (ret != 0) {
                fprintf(stderr, "Failed to download %s\n", url);
            }
        }
    }

    if (resetAdlists() != 0) {
        fprintf(stderr, "Failed to reset adlists\n");
        fclose(file);
        return -1;
    }

    fclose(file);
    return 0;
}

int changeAdlistStatus(const char* url, int status) {
    pthread_mutex_lock(&adlistFileLock);
    FILE* file = fopen("adlists/metadata/lists.txt", "r");
    if (!file) {
        perror("Failed to open adlist file");
        pthread_mutex_unlock(&adlistFileLock);
        return -1;
    }

    char line[256];
    bool found = false;
    FILE* tempFile = fopen("adlists/metadata/temp.txt", "w");
    if (!tempFile) {
        perror("Failed to open temporary file");
        fclose(file);
        pthread_mutex_unlock(&adlistFileLock);
        return -1;
    }

    while (fgets(line, sizeof(line), file)) {
        char existingUrl[256], currentStatus[16];
        if (sscanf(line, "%255s %15s", existingUrl, currentStatus) == 2) {
            if (strcmp(existingUrl, url) == 0) {
                found = true;
                if (status == 0 && strcmp(currentStatus, "disabled") == 0) {
                    fprintf(tempFile, "%s enabled\n", existingUrl);
                } else if (status == 1 && strcmp(currentStatus, "enabled") == 0) {
                    fprintf(tempFile, "%s disabled\n", existingUrl);
                } else {
                    fprintf(tempFile, "%s %s\n", existingUrl, currentStatus);
                }
            } else {
                fprintf(tempFile, "%s", line);
            }
        } else {
            fprintf(tempFile, "%s", line);
        }
    }

    fclose(file);
    fclose(tempFile);

    if (!found) {
        remove("adlists/metadata/temp.txt");
        pthread_mutex_unlock(&adlistFileLock);
        return -1;
    }

    remove("adlists/metadata/lists.txt");
    rename("adlists/metadata/temp.txt", "adlists/metadata/lists.txt");

    pthread_mutex_unlock(&adlistFileLock);

    if (resetAdlists() != 0) {
        fprintf(stderr, "Failed to reset adlists after changing status\n");
        return -1;
    }
    return 0;
}

int addAdlistFile(const char* url) {
    pthread_mutex_lock(&adlistFileLock);
    FILE* file = fopen("adlists/metadata/lists.txt", "a");
    if (!file) {
        perror("Failed to open adlist file");
        pthread_mutex_unlock(&adlistFileLock);
        return -1;
    }

    // Always write the new entry on a new line
    fprintf(file, "%s enabled\n", url);
    fclose(file);
    pthread_mutex_unlock(&adlistFileLock);

    if (resetAdlists() != 0) {
        fprintf(stderr, "Failed to reset adlists after adding new entry\n");
        return -1;
    }

    if (loadAdlistsFromFile() != 0) {
        fprintf(stderr, "Failed to load adlists from file after adding new entry\n");
        return -1;
    }

    return 0;
}

int removeLinesFromFile(const char* filepath, const char* stringToRemove) {
    // This function remains the same as in the previous version
    pthread_mutex_lock(&adlistFileLock);

    FILE* file = fopen(filepath, "r");
    if (!file) {
        perror("Failed to open file for reading");
        pthread_mutex_unlock(&adlistFileLock);
        return -1;
    }

    FILE* tempFile = fopen("temp.txt", "w"); // Use a simple "temp.txt" in the current directory
    if (!tempFile) {
        perror("Failed to open temporary file for writing");
        fclose(file);
        pthread_mutex_unlock(&adlistFileLock);
        return -1;
    }

    char line[256]; // Or a size appropriate for your expected line lengths
    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, stringToRemove) == NULL) {
            fputs(line, tempFile);
        }
    }

    fclose(file);
    fclose(tempFile);

    if (remove(filepath) != 0) {
        perror("Failed to remove original file");
        pthread_mutex_unlock(&adlistFileLock);
        return -1; // Consider this a failure
    }
    if (rename("temp.txt", filepath) != 0) {
        perror("Failed to rename temporary file");
        pthread_mutex_unlock(&adlistFileLock);
        return -1;  // Consider this a failure
    }

    pthread_mutex_unlock(&adlistFileLock);
    return 0;
}

int removeAdlistFile(const char* url) {
    int result = 0; // Track the overall result

    // Remove the line from lists.txt
    if (removeLinesFromFile("adlists/metadata/lists.txt", url) != 0) {
        fprintf(stderr, "Failed to remove line from adlist file\n");
        result = -1; // Set the result to failure
    }

    // Extract the file name from the URL
    const char* fileName = strrchr(url, '/');
    if (fileName) {
        fileName++; // Move past the '/'
        char filePath[512];
        snprintf(filePath, sizeof(filePath), "adlists/listdata/%s", fileName);

        // Remove the file
        if (remove(filePath) != 0) {
            perror("Failed to remove file from adlists/listdata");
        }
    }

    // Reset and load adlists
    if (resetAdlists() != 0) {
        fprintf(stderr, "Failed to reset adlists after removing entry\n");
        result = -1;
    }

    if (loadAdlistsFromFile() != 0) {
        fprintf(stderr, "Failed to load adlists from file after removing entry\n");
        result = -1;
    }

    return result; // Return the overall result
}

char* getAllAdlists() {
    pthread_mutex_lock(&adlistFileLock);
    FILE* file = fopen("adlists/metadata/lists.txt", "r");
    if (!file) {
        perror("Failed to open adlist file");
        pthread_mutex_unlock(&adlistFileLock);
        return NULL;
    }

    size_t bufferSize = 1024;
    char* adlists = malloc(bufferSize);
    if (!adlists) {
        perror("Failed to allocate memory for adlists");
        fclose(file);
        pthread_mutex_unlock(&adlistFileLock);
        return NULL;
    }
    adlists[0] = '\0';

    char line[256];
    bool first = true;
    while (fgets(line, sizeof(line), file)) {
        char url[256], status[16];
        if (sscanf(line, "%255s %15s", url, status) == 2) {
            size_t currentLength = strlen(adlists);
            size_t lineLength = strlen(url) + strlen(status) + 2; // Include space and null terminator

            if (currentLength + lineLength + 1 > bufferSize) {
                bufferSize *= 2;
                char* newAdlists = realloc(adlists, bufferSize);
                if (!newAdlists) {
                    perror("Failed to reallocate memory for adlists");
                    free(adlists);
                    fclose(file);
                    pthread_mutex_unlock(&adlistFileLock);
                    return NULL;
                }
                adlists = newAdlists;
            }

            if (!first) {
                strcat(adlists, ",");
            }
            strcat(adlists, url);
            strcat(adlists, " ");
            strcat(adlists, status);
            first = false;
        }
    }

    fclose(file);
    pthread_mutex_unlock(&adlistFileLock);
    return adlists;
}

void checkAndCleanServerLogs() {
    pthread_mutex_lock(&logFileLock);

    FILE* logFile = fopen("adlists/metadata/server_logs.txt", "r");
    if (!logFile) {
        perror("Failed to open log file");
        pthread_mutex_unlock(&logFileLock);
        return;
    }

    // Count the number of lines in the file
    int lineCount = 0;
    char line[1024];
    while (fgets(line, sizeof(line), logFile)) {
        lineCount++;
    }
    rewind(logFile); // Reset file pointer to the beginning

    // If the file has more than 500 lines, remove the oldest lines
    if (lineCount > 500) {
        FILE* tempFile = fopen("adlists/metadata/temp_logs.txt", "w");
        if (!tempFile) {
            perror("Failed to open temporary log file");
            fclose(logFile);
            pthread_mutex_unlock(&logFileLock);
            return;
        }

        // Seek to the correct position to start writing the last 500 lines
        long startPosition = 0;
        if (lineCount > 500) {
            startPosition = lineCount - 500;
        }

        // Rewind to the beginning
        rewind(logFile);

        // Skip to the correct position
        for (int i = 0; i < startPosition; ++i) {
            if (fgets(line, sizeof(line), logFile) == NULL) {
                break; // Handle unexpected end of file
            }
        }

        // Copy the last 500 lines to the temporary file
        while (fgets(line, sizeof(line), logFile)) {
            fputs(line, tempFile);
        }

        fclose(tempFile);

        // Close and reopen the log file for writing
        fclose(logFile);
        logFile = fopen("adlists/metadata/server_logs.txt", "w");
        if (!logFile) {
            perror("Failed to reopen log file for writing");
            pthread_mutex_unlock(&logFileLock);
            return;
        }

        // Copy the contents of the temporary file back to the original file
        tempFile = fopen("adlists/metadata/temp_logs.txt", "r");
        if (!tempFile) {
            perror("Failed to open temporary log file for reading");
            fclose(logFile);
            pthread_mutex_unlock(&logFileLock);
            return;
        }

        while (fgets(line, sizeof(line), tempFile)) {
            fputs(line, logFile);
        }

        fclose(tempFile);
        fclose(logFile);
    }

    pthread_mutex_unlock(&logFileLock);
}

char* getTerminalOutput() {    
    pthread_mutex_lock(&logFileLock);

    FILE* logFile = fopen("adlists/metadata/server_logs.txt", "r");
    if (!logFile) {
        perror("Failed to open log file");
        pthread_mutex_unlock(&logFileLock);
        return NULL;
    }

    // Determine the size of the file
    fseek(logFile, 0, SEEK_END);
    long fileSize = ftell(logFile);
    rewind(logFile);

    // Allocate memory for the log content
    char* logContent = malloc(fileSize + 1);
    if (!logContent) {
        perror("Failed to allocate memory for log content");
        fclose(logFile);
        pthread_mutex_unlock(&logFileLock);
        return NULL;
    }

    // Read the file into the buffer
    fread(logContent, 1, fileSize, logFile);
    logContent[fileSize] = '\0'; // Null-terminate the string

    fclose(logFile);
    pthread_mutex_unlock(&logFileLock);
    return logContent;
}

int generateSalt(unsigned char* salt, size_t size) {
    if (RAND_bytes(salt, size) != 1) {
        perror("Failed to generate salt");
        return -1;
    }
    return 0;
}

int hashPassword(const char* password, const unsigned char* salt, size_t saltSize, unsigned char* hash) {
    if (!password || !salt || !hash) {
        fprintf(stderr, "Invalid input to hashPassword\n");
        return -1;
    }

    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (!context) {
        perror("Failed to create EVP_MD_CTX");
        return -1;
    }

    if (EVP_DigestInit_ex(context, EVP_sha512(), NULL) != 1 ||
        EVP_DigestUpdate(context, salt, saltSize) != 1 ||
        EVP_DigestUpdate(context, password, strlen(password)) != 1 ||
        EVP_DigestFinal_ex(context, hash, NULL) != 1) {
        perror("Failed to hash password");
        EVP_MD_CTX_free(context);
        return -1;
    }

    EVP_MD_CTX_free(context);
    return 0;
}

void printHex(const unsigned char* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        printf("%02x", data[i]);
    }
    printf("\n");
}

int handleLoginPassData(const char* user, const char* pass) {
    FILE* file = fopen("adlists/metadata/data.txt", "r");
    if (!file) {
        perror("Failed to open data file");
        return -1;
    }

    // Read the entire file into memory
    char** lines = NULL;
    size_t numLines = 0;
    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        char* copy = strdup(line);
        if (!copy) {
            perror("strdup failed");
            fclose(file);
            // Free any previously allocated lines
            for (size_t i = 0; i < numLines; ++i) free(lines[i]);
            free(lines);
            return -1;
        }
        char** newLines = realloc(lines, (numLines + 1) * sizeof(char*));
        if (!newLines) {
            perror("realloc failed");
            free(copy);
            fclose(file);
            for (size_t i = 0; i < numLines; ++i) free(lines[i]);
            free(lines);
            return -1;
        }
        lines = newLines;
        lines[numLines++] = copy;
    }
    fclose(file);

    // Check if the first line is empty or only whitespace
    int firstLineEmpty = 1;
    if (numLines > 0) {
        char* p = lines[0];
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        if (*p != '\0') firstLineEmpty = 0;
    }

    if (numLines == 0 || firstLineEmpty) {
        // First line is empty, set login data
        unsigned char salt[SALT_SIZE];
        unsigned char hash[HASH_SIZE];

        if (generateSalt(salt, SALT_SIZE) != 0) {
            for (size_t i = 0; i < numLines; ++i) free(lines[i]);
            free(lines);
            return -1;
        }
        if (hashPassword(pass, salt, SALT_SIZE, hash) != 0) {
            for (size_t i = 0; i < numLines; ++i) free(lines[i]);
            free(lines);
            return -1;
        }

        // Prepare the new first line
        char firstLine[1024];
        int offset = snprintf(firstLine, sizeof(firstLine), "%s ", user);
        for (size_t i = 0; i < SALT_SIZE; i++)
            offset += snprintf(firstLine + offset, sizeof(firstLine) - offset, "%02x", salt[i]);
        offset += snprintf(firstLine + offset, sizeof(firstLine) - offset, " ");
        for (size_t i = 0; i < HASH_SIZE; i++)
            offset += snprintf(firstLine + offset, sizeof(firstLine) - offset, "%02x", hash[i]);
        snprintf(firstLine + offset, sizeof(firstLine) - offset, "\n");

        // Replace or add the first line
        if (numLines == 0) {
            char** newLines = realloc(lines, sizeof(char*));
            if (!newLines) {
                perror("realloc failed");
                free(lines);
                return -1;
            }
            lines = newLines;
            lines[0] = strdup(firstLine);
            numLines = 1;
        } else {
            free(lines[0]);
            lines[0] = strdup(firstLine);
        }

        // Write all lines back to the file
        file = fopen("adlists/metadata/data.txt", "w");
        if (!file) {
            perror("Failed to open data file for writing");
            for (size_t i = 0; i < numLines; ++i) free(lines[i]);
            free(lines);
            return -1;
        }
        for (size_t i = 0; i < numLines; ++i) fputs(lines[i], file);
        fclose(file);

        for (size_t i = 0; i < numLines; ++i) free(lines[i]);
        free(lines);
        return 0;
    } else {
        // First line is not empty, check credentials as before
        char storedUser[256], storedSaltHex[SALT_SIZE * 2 + 1], storedHashHex[HASH_SIZE * 2 + 1];
        if (sscanf(lines[0], "%255s %32s %128s", storedUser, storedSaltHex, storedHashHex) == 3) {
            if (strcmp(user, storedUser) == 0) {
                unsigned char storedSalt[SALT_SIZE];
                unsigned char storedHash[HASH_SIZE];
                for (size_t i = 0; i < SALT_SIZE; i++)
                    sscanf(&storedSaltHex[i * 2], "%2hhx", &storedSalt[i]);
                for (size_t i = 0; i < HASH_SIZE; i++)
                    sscanf(&storedHashHex[i * 2], "%2hhx", &storedHash[i]);

                unsigned char computedHash[HASH_SIZE];
                if (hashPassword(pass, storedSalt, SALT_SIZE, computedHash) != 0) {
                    for (size_t i = 0; i < numLines; ++i) free(lines[i]);
                    free(lines);
                    return -1;
                }

                if (memcmp(computedHash, storedHash, HASH_SIZE) == 0) {
                    for (size_t i = 0; i < numLines; ++i) free(lines[i]);
                    free(lines);
                    return 0; // Valid credentials
                } else {
                    for (size_t i = 0; i < numLines; ++i) free(lines[i]);
                    free(lines);
                    return -1; // Invalid password
                }
            }
        }
    }

    for (size_t i = 0; i < numLines; ++i) free(lines[i]);
    free(lines);
    return -1;
}

int addLocalDNSToCache(const char* ip, const char* url, const char* name) {
    int check = addLocalEntry(ip, url, name);
    if (check != 0) {
        fprintf(stderr, "Failed to add local DNS entry\n");
        return -1;
    }
    return 0;
}

int setNumThreadsInFile(int numThreads) {
    FILE* file = fopen("adlists/metadata/data.txt", "r");
    if (!file) {
        perror("Failed to open data.txt");
        return -1;
    }

    // Read all lines into memory
    char* lines[3] = {NULL, NULL, NULL};
    char buffer[256];
    int i = 0;
    while (i < 3 && fgets(buffer, sizeof(buffer), file)) {
        lines[i] = strdup(buffer);
        i++;
    }
    fclose(file);

    if (i < 2) { // Not enough lines
        for (int j = 0; j < i; ++j) free(lines[j]);
        return -1;
    }

    // Replace the second line with the new THREADS line
    char threadsLine[64];
    snprintf(threadsLine, sizeof(threadsLine), "THREADS %d\n", numThreads);
    free(lines[1]);
    lines[1] = strdup(threadsLine);

    // Write all lines back
    file = fopen("adlists/metadata/data.txt", "w");
    if (!file) {
        perror("Failed to open data.txt for writing");
        for (int j = 0; j < 3; ++j) free(lines[j]);
        return -1;
    }
    for (int j = 0; j < i; ++j) {
        fputs(lines[j], file);
        free(lines[j]);
    }
    fclose(file);
    return 0;
}

static enum MHD_Result handleGetAdlists(struct MHD_Connection* connection) {
    char* adlists = getAllAdlists();
    if (!adlists) {
        const char* response = "{\"error\": \"Failed to retrieve adlists\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
    }

    struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(adlists), (uint8_t*)adlists, MHD_RESPMEM_MUST_COPY);
    free(adlists);
    return MHD_queue_response(connection, MHD_HTTP_OK, resp);
}

static enum MHD_Result handleAddAdlist(struct MHD_Connection* connection) {
    const char* url = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "url");
    if (!url) {
        const char* response = "{\"error\": \"Missing URL parameter\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);
    }

    if (addAdlistFile(url) == 0) {
        const char* response = "{\"status\": \"Adlist added\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_OK, resp);
    } else {
        const char* response = "{\"error\": \"Failed to add adlist\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
    }
}

static enum MHD_Result handleRemoveAdlist(struct MHD_Connection* connection) {
    const char* url = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "url");
    if (!url) {
        const char* response = "{\"error\": \"Missing URL parameter\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);
    }

    if (removeAdlistFile(url) == 0) {
        const char* response = "{\"status\": \"Adlist removed\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_OK, resp);
    } else {
        const char* response = "{\"error\": \"Failed to remove adlist\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
    }
}

static enum MHD_Result handleEnableAdlist(struct MHD_Connection* connection) {
    const char* url = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "url");
    if (!url) {
        const char* response = "{\"error\": \"Missing URL parameter\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);
    }

    if (changeAdlistStatus(url, 0) == 0) {
        const char* response = "{\"status\": \"Adlist enabled\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_OK, resp);
    } else {
        const char* response = "{\"error\": \"Failed to enable adlist\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
    }
}

static enum MHD_Result handleDisableAdlist(struct MHD_Connection* connection) {
    const char* url = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "url");
    if (!url) {
        const char* response = "{\"error\": \"Missing URL parameter\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);
    }

    if (changeAdlistStatus(url, 1) == 0) {
        const char* response = "{\"status\": \"Adlist disabled\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_OK, resp);
    } else {
        const char* response = "{\"error\": \"Failed to disable adlist\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
    }
}

static enum MHD_Result handleDomainsInAdlist(struct MHD_Connection* connection) {
    char response[1024];
    uint32_t domainsInAdlist = getDomainsInAdlist();
    snprintf(response, sizeof(response), "{\"domainsInAdlist\": %d}", domainsInAdlist);
    struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
    return MHD_queue_response(connection, MHD_HTTP_OK, resp);
}

static enum MHD_Result enableAdCacheCall(struct MHD_Connection* connection) {
    enableAdCache();
    const char* response = "{\"status\": \"Ad cache enabled\"}";
    struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
    return MHD_queue_response(connection, MHD_HTTP_OK, resp);
}

static enum MHD_Result disableAdCacheCall(struct MHD_Connection* connection) {
    disableAdCache();
    const char* response = "{\"status\": \"Ad cache disabled\"}";
    struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
    return MHD_queue_response(connection, MHD_HTTP_OK, resp);
}

static enum MHD_Result handleReloadAdlists(struct MHD_Connection* connection) {
    if (loadAdlistsFromFile() == 0) {
        const char* response = "{\"status\": \"Adlists reloaded\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_OK, resp);
    } else {
        const char* response = "{\"error\": \"Failed to reload adlists\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
    }
}

static enum MHD_Result handleTerminalOutput(struct MHD_Connection* connection) {
    char* output = getTerminalOutput();
    if (!output) {
        const char* response = "{\"error\": \"Failed to retrieve terminal output\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
    }

    struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(output), (uint8_t*)output, MHD_RESPMEM_MUST_COPY);
    free(output);
    return MHD_queue_response(connection, MHD_HTTP_OK, resp);
}

static enum MHD_Result handleRestartDNS(struct MHD_Connection* connection) {
    const char* response = "{\"status\": \"Restarting DNS server\"}";
    struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
    MHD_queue_response(connection, MHD_HTTP_OK, resp);

    // Close the current program
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);

    // Restart the program
    char* const args[] = { "./server", NULL };
    execv(args[0], args);

    // If execv fails
    perror("Failed to restart DNS server");
    exit(EXIT_FAILURE);
}

static enum MHD_Result handleLogin(struct MHD_Connection* connection) {
    const char* username = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "username");
    const char* password = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "password");

    if (!username || !password) {
        const char* response = "{\"error\": \"Missing username or password\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);
    }

    if (handleLoginPassData(username, password) == 0) {
        const char* response = "{\"status\": \"Login successful\"}";
        printf("Login successful for user: %s\n", username);
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_OK, resp);
    } else {
        const char* response = "{\"error\": \"Invalid credentials\"}";
        printf("Invalid credentials for user: %s\n", username);
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_UNAUTHORIZED, resp);
    }
}

static enum MHD_Result handleAddLocalDomain(struct MHD_Connection* connection) {
    const char* domain = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "domain");
    const char* ip = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "ip");
    const char* name = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "name");
    if (!domain || !ip) {
        const char* response = "{\"error\": \"Missing domain or IP parameter\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);
    }
    
    if (addLocalDNSToCache(ip, domain, name) == 0) {
        const char* response = "{\"status\": \"Local domain added\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_OK, resp);
    } else {
        const char* response = "{\"error\": \"Failed to add local domain\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
    }
}

static enum MHD_Result handleGetLocalDNSEntries(struct MHD_Connection* connection) {
    char* localDNSEntries = getLocalDNSEntries();
    if (!localDNSEntries) {
        const char* response = "{\"error\": \"Failed to retrieve local DNS entries\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
    }

    struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(localDNSEntries), (uint8_t*)localDNSEntries, MHD_RESPMEM_MUST_COPY);
    free(localDNSEntries);
    return MHD_queue_response(connection, MHD_HTTP_OK, resp);
}

static enum MHD_Result handleRemoveLocalDomain(struct MHD_Connection* connection) {
    const char* domain = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "domain");
    if (!domain) {
        const char* response = "{\"error\": \"Missing domain parameter\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);
    }

    if (removeLocalEntry(domain) == 0) {
        const char* response = "{\"status\": \"Local domain removed\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_OK, resp);
    } else {
        const char* response = "{\"error\": \"Failed to remove local domain\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
    }
}

static enum MHD_Result handleGetNumThreads(struct MHD_Connection* connection) {
    char response[256];
    uint32_t numThreads = getNumThreads();
    snprintf(response, sizeof(response), "{\"numThreads\": %d}", numThreads);
    struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
    return MHD_queue_response(connection, MHD_HTTP_OK, resp);
}

static enum MHD_Result handleGetAvgCacheLookupTime(struct MHD_Connection* connection) {
    char response[256];
    double avgCacheLookupTime = running_avgs_get_cache_lookup();
    printf("Average Cache Lookup Time: %f\n", avgCacheLookupTime);
    snprintf(response, sizeof(response), "{\"avgCacheLookupTime\": %.99f}", avgCacheLookupTime);
    struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
    return MHD_queue_response(connection, MHD_HTTP_OK, resp);
}

static enum MHD_Result handleGetAvgCacheResponseTime(struct MHD_Connection* connection) {
    char response[256];
    double avgCacheResponseTime = running_avgs_get_cached_query_response();
    snprintf(response, sizeof(response), "{\"avgCacheResponseTime\": %.5f}", avgCacheResponseTime);
    struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
    return MHD_queue_response(connection, MHD_HTTP_OK, resp);
}

static enum MHD_Result handleGetAvgNCResponseTime(struct MHD_Connection* connection) {
    char response[256];
    double avgAvgNCResponseTime = running_avgs_get_query_response();
    snprintf(response, sizeof(response), "{\"avgAvgNCResponseTime\": %.5f}", avgAvgNCResponseTime);
    struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
    return MHD_queue_response(connection, MHD_HTTP_OK, resp);
}

static enum MHD_Result handleSetNumThreads(struct MHD_Connection* connection) {
    const char* numThreadsStr = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "numThreads");
    if (!numThreadsStr) {
        const char* response = "{\"error\": \"Missing numThreads parameter\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);
    }

    int numThreads = atoi(numThreadsStr);
    if (setNumThreadsInFile(numThreads) == 0) {
        const char* response = "{\"status\": \"Number of threads set\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_OK, resp);
    } else {
        const char* response = "{\"error\": \"Failed to set number of threads\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
    }
}

static enum MHD_Result handleGetUpstreamDNS(struct MHD_Connection* connection) {
    char response[256];
    const char* upstreamDNS = getUpstreamDNS();
    if (upstreamDNS) {
        snprintf(response, sizeof(response), "{\"upstreamDNS\": \"%s\"}", upstreamDNS);
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_OK, resp);
    } else {
        const char* errorResponse = "{\"error\": \"Failed to retrieve upstream DNS\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(errorResponse), (uint8_t*)errorResponse, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
    }
}

static enum MHD_Result handleSetUpstreamDNS(struct MHD_Connection* connection) {
    const char* upstreamDNS = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "upstreamDNS");
    if (!upstreamDNS) {
        const char* response = "{\"error\": \"Missing upstreamDNS parameter\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);
    }

    if (changeUpstreamDNS(upstreamDNS) == 0) {
        const char* response = "{\"status\": \"Upstream DNS set\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_OK, resp);
    } else {
        const char* response = "{\"error\": \"Failed to set upstream DNS\"}";
        struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
        return MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, resp);
    }
}

ApiEndpoint apiEndpoints[] = {
    { "/numQueries", handleGetTotalNumOfQueries },
    { "/domainsInAdlist", handleDomainsInAdlist },
    { "/enableAdCache", enableAdCacheCall },
    { "/disableAdCache", disableAdCacheCall },
    { "/getAdlists", handleGetAdlists },
    { "/addAdlist", handleAddAdlist },
    { "/removeAdlist", handleRemoveAdlist },
    { "/enableAdlist", handleEnableAdlist },
    { "/disableAdlist", handleDisableAdlist },
    { "/reloadAdlists", handleReloadAdlists },
    { "/terminalOutput", handleTerminalOutput },
    { "/restartDNS", handleRestartDNS },
    { "/validateLogin", handleLogin },
    { "/addLocalDomain", handleAddLocalDomain },
    { "/getLocalDNSEntries", handleGetLocalDNSEntries },
    { "/removeLocalDomain", handleRemoveLocalDomain },
    { "/getNumThreads", handleGetNumThreads },
    { "/getAvgCacheLookupTime", handleGetAvgCacheLookupTime },
    { "/getAvgCacheResponseTime", handleGetAvgCacheResponseTime },
    { "/getAvgNonCachedResponseTime", handleGetAvgNCResponseTime },
    { "/setNumThreads", handleSetNumThreads },
    { "/getUpstreamDNS", handleGetUpstreamDNS },
    { "/setUpstreamDNS", handleSetUpstreamDNS },
    { NULL, NULL } // Sentinel value to mark the end of the table
};

static enum MHD_Result dispatchRequest(const char* url, struct MHD_Connection* connection) {
    for (int i = 0; apiEndpoints[i].endpoint != NULL; i++) {
        if (strcmp(url, apiEndpoints[i].endpoint) == 0) {
            return apiEndpoints[i].handler(connection);
        }
    }

    const char* response = "{\"error\": \"Unknown endpoint\"}";
    struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
    return MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, resp);
}

static enum MHD_Result answer_to_connection(void* cls, struct MHD_Connection* connection,
    const char* url, const char* method,
    const char* version, const char* upload_data,
    size_t* upload_data_size, void** con_cls) {
    (void)cls;
    (void)version;
    (void)upload_data;
    (void)upload_data_size;
    (void)con_cls;

    if (strcmp(method, "GET") == 0 || strcmp(method, "POST") == 0) {
        return dispatchRequest(url, connection);
    }

    const char* response = "{\"error\": \"Method not allowed\"}";
    struct MHD_Response* resp = MHD_create_response_from_buffer(strlen(response), (uint8_t*)response, MHD_RESPMEM_MUST_COPY);
    return MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, resp);
}

void* handleAPIs(void* arg) {
    (void)arg; // Unused parameter
    struct MHD_Daemon* daemon = MHD_start_daemon(
        MHD_USE_THREAD_PER_CONNECTION,
        8081, NULL, NULL,
        &answer_to_connection, NULL,
        MHD_OPTION_END
    );
    if (!daemon) {
        perror("Failed to start HTTP server");
        return NULL;
    }
    pthread_mutex_t waitMutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t waitCond = PTHREAD_COND_INITIALIZER;

    int localDNS = reloadLocalDNSCache();
    if (localDNS != 0) {
        fprintf(stderr, "Failed to reload local DNS cache\n");
        exit(EXIT_FAILURE);
    }

    int adlistsCheck = loadAdlistsFromFile();
    if (adlistsCheck != 0) {
        fprintf(stderr, "Failed to load adlists from file\n");
        exit(EXIT_FAILURE);
    }

    int adCheck = add_addlists();
    if (adCheck != 0) {
        fprintf(stderr, "Failed to add adlists\n");
        exit(EXIT_FAILURE);
    }

    while (1) {
        pthread_mutex_lock(&waitMutex);
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 5;

        int res = pthread_cond_timedwait(&waitCond, &waitMutex, &ts);
        if (res == ETIMEDOUT) {
            int removedVal = checkAndRemoveExpiredCache();
            printf("---------------------\n");
            printf("API Handler Stats:\n");
            printProcessedQueries();
            printBlockedQueries();
            printCacheCapacity();
            printCacheHits();
            printValInQueue();

            checkAndCleanServerLogs();

            printf("removed %d expired cache entries\n", removedVal);
            // printCache();
        }
        pthread_mutex_unlock(&waitMutex);
    }
    pthread_mutex_destroy(&waitMutex);
    pthread_cond_destroy(&waitCond);
    MHD_stop_daemon(daemon);
    return NULL;
}