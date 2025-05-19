#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/in.h>
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
#include "apiHandler.h"
#include "runningAvgs.h"

int adCacheEnabled;
pthread_mutex_t adCacheLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t upstream_lock = PTHREAD_MUTEX_INITIALIZER;

char* getUpstreamDNS() {
    pthread_mutex_lock(&upstream_lock);
    FILE* file = fopen("adlists/metadata/data.txt", "r");
    if (!file) {
        pthread_mutex_unlock(&upstream_lock);
        perror("Failed to open upstream DNS file");
        return NULL;
    }
    char line[256];
    int line_num = 0;
    char* upstream_dns = NULL;
    while (fgets(line, sizeof(line), file)) {
        line_num++;
        if (line_num == 3) {
            // Expecting format: "UPSTREAM *ip here*"
            char* token = strtok(line, " \t\n");
            if (token && strcmp(token, "UPSTREAM") == 0) {
                token = strtok(NULL, " \t\n");
                if (token) {
                    upstream_dns = strdup(token);
                } else {
                    fprintf(stderr, "No IP found after UPSTREAM in line 3\n");
                }
            } else {
                fprintf(stderr, "Line 3 does not start with UPSTREAM\n");
            }
            break;
        }
    }
    if (line_num < 3) {
        fprintf(stderr, "File does not have at least 3 lines\n");
    }
    fclose(file);
    pthread_mutex_unlock(&upstream_lock);
    return upstream_dns;
}

int changeUpstreamDNS(const char* new_ip) {
    pthread_mutex_lock(&upstream_lock);
    FILE* file = fopen("adlists/metadata/data.txt", "r+");
    if (!file) {
        pthread_mutex_unlock(&upstream_lock);
        perror("Failed to open upstream DNS file");
        return -1;
    }
    char line[256];
    int line_num = 0;
    while (fgets(line, sizeof(line), file)) {
        line_num++;
        if (line_num == 3) {
            // Expecting format: "UPSTREAM *ip here*"
            fseek(file, -strlen(line), SEEK_CUR);
            fprintf(file, "UPSTREAM %s\n", new_ip);
            break;
        }
    }

    long pos = ftell(file);
    if (pos == -1L) {
        perror("ftell failed");
        fclose(file);
        pthread_mutex_unlock(&upstream_lock);
        return -1;
    }
    if (ftruncate(fileno(file), pos) != 0) {
        perror("ftruncate failed");
        fclose(file);
        pthread_mutex_unlock(&upstream_lock);
        return -1;
    }

    fclose(file);
    pthread_mutex_unlock(&upstream_lock);
    return 0;
}

int sendCachedValue(int sockfd, struct sockaddr_in client_addr, socklen_t client_len, const char* ip_str_to_return, ldns_pkt* original_query, struct timeval send_start, struct timeval send_end) {
    ldns_pkt *response_pkt = NULL;
    ldns_rr *answer_rr = NULL;
    ldns_rr_list *answer_section = NULL;
    ldns_rr_list *question_section_orig = NULL;
    ldns_rr *question_rr_orig = NULL;
    ldns_rdf *qname_orig = NULL;
    ldns_rr_type qtype_orig;
    ldns_rr_class qclass_orig;
    ldns_rr *response_question_rr = NULL;
    ldns_rr_list *response_question_section = NULL;
    ldns_rdf *qname_clone_for_question = NULL;
    ldns_rdf *qname_clone_for_answer = NULL;
    ldns_rdf *rdf_ip = NULL;

    uint8_t *response_wire = NULL;
    size_t response_size = 0;
    ssize_t sent_bytes = -1;

    if (!original_query) {
        fprintf(stderr, "Error: Original query is NULL in sendCachedValue.\n");
        return -1;
    }

    response_pkt = ldns_pkt_new();
    if (!response_pkt) {
        goto error;
    }

    ldns_pkt_set_id(response_pkt, ldns_pkt_id(original_query));

    struct timeval tv;
    gettimeofday(&tv, NULL);
    ldns_pkt_set_timestamp(response_pkt, tv);

    question_section_orig = ldns_pkt_question(original_query);
    if (!question_section_orig || ldns_rr_list_rr_count(question_section_orig) == 0) {
        fprintf(stderr, "Error: Original query has no question section or it's empty.\n");
        goto error;
    }
    question_rr_orig = ldns_rr_list_rr(question_section_orig, 0);
    if (!question_rr_orig) {
        fprintf(stderr, "Error: Failed to get question RR from original query.\n");
        goto error;
    }

    qname_orig = ldns_rr_owner(question_rr_orig);
    qtype_orig = ldns_rr_get_type(question_rr_orig);
    qclass_orig = ldns_rr_get_class(question_rr_orig);

    if (!qname_orig) { 
        fprintf(stderr, "Error: qname_orig is NULL after extracting from original query.\n");
        goto error;
    }

    response_question_rr = ldns_rr_new();
    if (!response_question_rr) { goto error; }

    qname_clone_for_question = ldns_rdf_clone(qname_orig);
    if (!qname_clone_for_question) { goto error; }

    ldns_rr_set_owner(response_question_rr, qname_clone_for_question);
    qname_clone_for_question = NULL; 

    ldns_rr_set_type(response_question_rr, qtype_orig);
    ldns_rr_set_class(response_question_rr, qclass_orig);

    response_question_section = ldns_rr_list_new();
    if (!response_question_section) { goto error; }

    ldns_rr_list_push_rr(response_question_section, response_question_rr);
    response_question_rr = NULL;

    ldns_pkt_set_question(response_pkt, response_question_section);
    if (ldns_pkt_question(response_pkt)) {
        ldns_pkt_set_qdcount(response_pkt, ldns_rr_list_rr_count(ldns_pkt_question(response_pkt)));
    } else {
        ldns_pkt_set_qdcount(response_pkt, 0);
    }
    response_question_section = NULL; 
    ldns_pkt_set_qr(response_pkt, true);
    ldns_pkt_set_aa(response_pkt, true); // Assuming authoritative for local cache
    ldns_pkt_set_rd(response_pkt, ldns_pkt_rd(original_query)); // Copy Recursion Desired
    ldns_pkt_set_ra(response_pkt, true); // Recursion Available (server dependent)
    ldns_pkt_set_rcode(response_pkt, LDNS_RCODE_NOERROR);

    answer_rr = ldns_rr_new();
    if (!answer_rr) { goto error; }

    qname_clone_for_answer = ldns_rdf_clone(qname_orig);
    if (!qname_clone_for_answer) { goto error; }

    ldns_rr_set_owner(answer_rr, qname_clone_for_answer);
    qname_clone_for_answer = NULL; // Ownership transferred

    ldns_rr_set_type(answer_rr, LDNS_RR_TYPE_A); // Assuming A record response
    ldns_rr_set_class(answer_rr, LDNS_RR_CLASS_IN);
    ldns_rr_set_ttl(answer_rr, 315576000); // Approx 10 years for "permanent" cache

    rdf_ip = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_A, ip_str_to_return);
    if (!rdf_ip) { goto error; }

    if (!ldns_rr_push_rdf(answer_rr, rdf_ip)) {
         goto error;
    }
    rdf_ip = NULL; // Ownership transferred

    answer_section = ldns_rr_list_new();
    if (!answer_section) { goto error; }

    ldns_rr_list_push_rr(answer_section, answer_rr);
    answer_rr = NULL; // Ownership transferred

    ldns_pkt_set_answer(response_pkt, answer_section);
    // EXPLICITLY SET ANCOUNT
    if (ldns_pkt_answer(response_pkt)) {
        ldns_pkt_set_ancount(response_pkt, ldns_rr_list_rr_count(ldns_pkt_answer(response_pkt)));
    } else {
        ldns_pkt_set_ancount(response_pkt, 0);
    }
    answer_section = NULL;

    if (ldns_pkt2wire(&response_wire, response_pkt, &response_size) != LDNS_STATUS_OK) {
        fprintf(stderr, "Error: Failed to convert response packet to wire format.\n");
        goto error;
    }
    sent_bytes = sendto(sockfd, response_wire, response_size, 0, (struct sockaddr*)&client_addr, client_len);
    if (sent_bytes < 0) {
        perror("Error: Failed to send response to client");
        goto error; // response_wire will be freed in error block
    } else if ((size_t)sent_bytes != response_size) {
        fprintf(stderr, "Warning: sendto sent %zd bytes, but expected %zu bytes.\n", sent_bytes, response_size);
        // Potentially problematic, but continue for now
    }

    // 9. Cleanup for success case
    LDNS_FREE(response_wire); response_wire = NULL;
    ldns_pkt_free(response_pkt); response_pkt = NULL;

    gettimeofday(&send_end, NULL);
    long seconds = send_end.tv_sec - send_start.tv_sec;
    long microseconds = send_end.tv_usec - send_start.tv_usec;
    double elapsed = seconds + microseconds * 1e-6;
    running_avgs_add_cached_query_response(elapsed);

    return sent_bytes;

error:
    // General error message, specific errors should be logged before goto
    fprintf(stderr, "Error encountered in sendCachedValue processing.\n");
    if (response_wire) LDNS_FREE(response_wire);

    // Cleanup for RDFs if they were allocated but not successfully attached
    if (qname_clone_for_question) ldns_rdf_deep_free(qname_clone_for_question);
    if (qname_clone_for_answer) ldns_rdf_deep_free(qname_clone_for_answer);
    if (rdf_ip) ldns_rdf_deep_free(rdf_ip);

    // Cleanup for RRs and RR lists if they were allocated but not successfully attached
    if (response_question_rr) ldns_rr_free(response_question_rr);
    if (response_question_section) ldns_rr_list_free(response_question_section);
    if (answer_rr) ldns_rr_free(answer_rr);
    if (answer_section) ldns_rr_list_free(answer_section);

    // Finally, free the packet if it was allocated
    if (response_pkt) ldns_pkt_free(response_pkt);

    return -1;
}

void enableAdCache() {
    pthread_mutex_lock(&adCacheLock);
    adCacheEnabled = 1;
    pthread_mutex_unlock(&adCacheLock);
}
void disableAdCache() {
    pthread_mutex_lock(&adCacheLock);
    adCacheEnabled = 0;
    pthread_mutex_unlock(&adCacheLock);
}

int checkAdCacheEnabled() {
    pthread_mutex_lock(&adCacheLock);
    int enabled = adCacheEnabled;
    pthread_mutex_unlock(&adCacheLock);
    return enabled;
}

void* processDNS(void* arg) {
    int thread_num = *(int*)arg;

    if (thread_num == 0) {
        enableAdCache();
    }

    printf("Upstream DNS: %s\n", getUpstreamDNS());

    while (1) {
        ThreadArgs* args = dequeue();
        if (args == NULL) {
            continue;
        }
        addProcessedQuery();

        struct timeval send_start, send_end;
        gettimeofday(&send_start, NULL);

        int sockfd = args->sockfd;
        struct sockaddr_in client_addr = args->client_addr;
        socklen_t client_len = args->client_len;
        char* buffer = args->buffer;
        ssize_t n = args->n;

        ldns_pkt* query_pkt;
        ldns_status status = ldns_wire2pkt(&query_pkt, (uint8_t*)buffer, n);
        if (status != LDNS_STATUS_OK) {
            fprintf(stderr, "Failed to parse DNS query: %s\n", ldns_get_errorstr_by_id(status));
            continue;
        }
        char* domain_str = NULL;
        ldns_rr_list* question = ldns_pkt_question(query_pkt);
        if (question && ldns_rr_list_rr_count(question) > 0) {
            ldns_rr* rr = ldns_rr_list_rr(question, 0);
            ldns_rdf* domain = ldns_rr_owner(rr);
            domain_str = ldns_rdf2str(domain);
            if (domain_str) {
                size_t len = strlen(domain_str);
                if (len > 0 && domain_str[len - 1] == '.') {
                    domain_str[len - 1] = '\0';
                }
            } else {
                fprintf(stderr, "Failed to convert domain to string\n");
            }
        } else {
            fprintf(stderr, "No question section in DNS query\n");
        }

        if(domain_str){
            struct timeval startCache, endCache;
            gettimeofday(&startCache, NULL);
            if (is_in_cache(domain_str) && CACHE_ENABLED) {
                gettimeofday(&endCache, NULL);
                long secondsCache = endCache.tv_sec - startCache.tv_sec;
                long microsecondsCache = endCache.tv_usec - startCache.tv_usec;
                double elapsedCache = secondsCache + microsecondsCache * 1e-6;
                running_avgs_add_cache_lookup(elapsedCache);
                char* ip = get_from_cache(domain_str);
                if (ip) {
                    addCacheHit();
                    sendCachedValue(sockfd, client_addr, client_len, ip, query_pkt, send_start, send_end);
                } else {
                    fprintf(stderr, "Failed to retrieve IP from cache for domain: %s\n", domain_str);
                } 
                continue;
            }

            struct timeval start, end;
            gettimeofday(&start, NULL);

            if (is_in_adcache(domain_str) && checkAdCacheEnabled()) {
                gettimeofday(&end, NULL);
                long seconds = end.tv_sec - start.tv_sec;
                long microseconds = end.tv_usec - start.tv_usec;
                double elapsed = seconds + microseconds * 1e-6;
                printf("Adcache lookup time: %.6f seconds\n", elapsed);

                char* ip = get_from_adcache(domain_str);
                if (ip) {
                    addBlockedQuery();
                    sendCachedValue(sockfd, client_addr, client_len, ip, query_pkt, send_start, send_end);
                } else {
                    fprintf(stderr, "Failed to retrieve IP from adblock cache for domain: %s\n", domain_str);
                }
                continue;
            }
        }
 
        int upstream_sock;
        struct sockaddr_in upstream_addr;
        upstream_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (upstream_sock < 0) {
            perror("Upstream socket creation failed");
            ldns_pkt_free(query_pkt);
            continue;
        }

        memset(&upstream_addr, 0, sizeof(upstream_addr));
        upstream_addr.sin_family = AF_INET;
        upstream_addr.sin_port = htons(53);
        inet_pton(AF_INET, getUpstreamDNS(), &upstream_addr.sin_addr);

        size_t query_size;
        uint8_t* query_wire;
        ldns_pkt2wire(&query_wire, query_pkt, &query_size);

        if (sendto(upstream_sock, query_wire, query_size, 0, (struct sockaddr*)&upstream_addr, sizeof(upstream_addr)) < 0) {
            perror("Failed to forward query to upstream server");
            free(query_wire);
            ldns_pkt_free(query_pkt);
            close(upstream_sock);
            continue;
        }

        char newBuffer[4096];
        ssize_t response_size = recvfrom(upstream_sock, newBuffer, sizeof(newBuffer), 0, NULL, NULL);
        if (response_size < 0) {
            perror("Failed to receive response from upstream server");
            free(query_wire);
            ldns_pkt_free(query_pkt);
            close(upstream_sock);
            continue;
        }

        ldns_pkt* response_pkt;
        ldns_status response_status = ldns_wire2pkt(&response_pkt, (uint8_t*)newBuffer, response_size);
        if (response_status != LDNS_STATUS_OK) {
            fprintf(stderr, "Failed to parse upstream response: %s\n", ldns_get_errorstr_by_id(response_status));
        } else {
            ldns_rr_list* answer_list = ldns_pkt_answer(response_pkt);
            if (answer_list && ldns_rr_list_rr_count(answer_list) > 0) {
                for (size_t i = 0; i < ldns_rr_list_rr_count(answer_list); i++) {
                    ldns_rr* rr = ldns_rr_list_rr(answer_list, i);
                    if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_A) {
                        ldns_rdf* rdf_ip = ldns_rr_rdf(rr, 0);
                        if (rdf_ip == NULL) {
                            fprintf(stderr, "Invalid RDF IP object\n");
                            continue;
                        }

                        char* ip_str = ldns_rdf2str(rdf_ip);
                        if (ip_str == NULL || strlen(ip_str) == 0) {
                            fprintf(stderr, "Invalid IP string from ldns_rdf2str\n");
                            continue;
                        }

                        struct in_addr addr;
                        if (inet_pton(AF_INET, ip_str, &addr) != 1) {
                            fprintf(stderr, "Invalid IP address: %s\n", ip_str);
                            free(ip_str);
                            continue;
                        }

                        uint32_t ttl = (uint32_t)ldns_rr_ttl(rr);
                        time_t current_time = time(NULL);
                        if (current_time == ((time_t)-1)) {
                            perror("Failed to get current time");
                            free(ip_str);
                            continue;
                        }

                        time_t expiration_time = current_time + ttl;

                        if (domain_str && CACHE_ENABLED) {
                            add_to_cache(domain_str, ip_str, expiration_time);
                        }

                        free(ip_str);
                    }
                }
            }
            free(domain_str);
            ldns_pkt_free(response_pkt);
        }

        close(upstream_sock);
        free(query_wire);
        ldns_pkt_free(query_pkt);

        // Send response back to client
        if (sendto(sockfd, newBuffer, response_size, 0, (struct sockaddr*)&client_addr, client_len) < 0) {
            perror("Failed to send response to client");
        }

        gettimeofday(&send_end, NULL);
        long seconds = send_end.tv_sec - send_start.tv_sec;
        long microseconds = send_end.tv_usec - send_start.tv_usec;
        double elapsed = seconds + microseconds * 1e-6;
        running_avgs_add_query_response(elapsed);
    }
}