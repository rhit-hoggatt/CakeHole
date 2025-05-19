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

#include "cacheHandler.h"
#include "cacheSystem.h"
#include "workQueue.h"
#include "thread.h"
#include "apiHandler.h"
#include "runningAvgs.h"

int main(int argc, char* argv[]) {
    if (argc != 1) {
        fprintf(stderr, "Usage: %s\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int log_fd = open("adlists/metadata/server_logs.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_fd < 0) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }
    if (dup2(log_fd, STDOUT_FILENO) < 0) {
        perror("Failed to redirect stdout to log file");
        close(log_fd);
        exit(EXIT_FAILURE);
    }
    if (dup2(log_fd, STDERR_FILENO) < 0) {
        perror("Failed to redirect stderr to log file");
        close(log_fd);
        exit(EXIT_FAILURE);
    }
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    int cache_init = init_cache_system();
    if (cache_init != 0) {
        fprintf(stderr, "Failed to initialize cache system\n");
        exit(EXIT_FAILURE);
    }

    running_avgs_init(500);

    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[512];

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    } 

    pthread_t api_thread;
    if (pthread_create(&api_thread, NULL, handleAPIs, NULL) != 0) {
        perror("Failed to create API thread");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    int THREAD_COUNT = getNumThreads();
    if (THREAD_COUNT <= 0) {
        fprintf(stderr, "Invalid number of threads\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    pthread_t threads[THREAD_COUNT];
    int thread_numbers[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; i++) {
        thread_numbers[i] = i;
        if (pthread_create(&threads[i], NULL, processDNS, &thread_numbers[i]) != 0) {
            perror("Failed to create thread");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }
    printf("Started %d request listeners\n", THREAD_COUNT);

    while(1) {
        ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)&client_addr, &client_len);
        if (n < 0) {
            perror("Receive failed");
            continue;
        }
        ThreadArgs* args = malloc(sizeof(ThreadArgs));
        args->sockfd = sockfd;
        args->client_addr = client_addr;
        args->client_len = client_len;
        args->buffer = malloc(n);
        args->n = n;
        memcpy(args->buffer, buffer, n);
        enqueue(args);
    }

    close(sockfd);
    return 0;
}