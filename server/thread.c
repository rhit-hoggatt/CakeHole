#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ldns/ldns.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "apiHandler.h"
#include "cacheHandler.h"
#include "cacheSystem.h"
#include "dnssecHandler.h"
#include "runningAvgs.h"
#include "workQueue.h"

int adCacheEnabled;
pthread_mutex_t adCacheLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t upstream_lock = PTHREAD_MUTEX_INITIALIZER;

char *getUpstreamDNS() {
  pthread_mutex_lock(&upstream_lock);
  FILE *file = fopen("adlists/metadata/data.txt", "r");
  if (!file) {
    pthread_mutex_unlock(&upstream_lock);
    perror("Failed to open upstream DNS file");
    return NULL;
  }
  char line[256];
  char last_line[256] = {0};
  while (fgets(line, sizeof(line), file)) {
    strncpy(last_line, line, sizeof(last_line) - 1);
    last_line[sizeof(last_line) - 1] = '\0';
  }
  char *upstream_dns = NULL;
  if (strlen(last_line) > 0) {
    char *token = strtok(last_line, " \t\n");
    if (token && strcmp(token, "UPSTREAM") == 0) {
      token = strtok(NULL, " \t\n");
      if (token) {
        upstream_dns = strdup(token);
      } else {
        fprintf(stderr, "No IP found after UPSTREAM in last line\n");
      }
    } else {
      fprintf(stderr, "Last line does not start with UPSTREAM\n");
    }
  } else {
    fprintf(stderr, "File is empty or no lines found\n");
  }
  fclose(file);
  pthread_mutex_unlock(&upstream_lock);
  return upstream_dns;
}

int changeUpstreamDNS(const char *new_ip) {
  pthread_mutex_lock(&upstream_lock);
  FILE *file = fopen("adlists/metadata/data.txt", "r+");
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

int sendCachedValue(int sockfd, struct sockaddr_in client_addr,
                    socklen_t client_len, const char *ip_str_to_return,
                    ldns_pkt *original_query, struct timeval send_start,
                    struct timeval send_end) {
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
  if (!question_section_orig ||
      ldns_rr_list_rr_count(question_section_orig) == 0) {
    fprintf(stderr,
            "Error: Original query has no question section or it's empty.\n");
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
    fprintf(
        stderr,
        "Error: qname_orig is NULL after extracting from original query.\n");
    goto error;
  }

  response_question_rr = ldns_rr_new();
  if (!response_question_rr) {
    goto error;
  }

  qname_clone_for_question = ldns_rdf_clone(qname_orig);
  if (!qname_clone_for_question) {
    goto error;
  }

  ldns_rr_set_owner(response_question_rr, qname_clone_for_question);
  qname_clone_for_question = NULL;

  ldns_rr_set_type(response_question_rr, qtype_orig);
  ldns_rr_set_class(response_question_rr, qclass_orig);

  response_question_section = ldns_rr_list_new();
  if (!response_question_section) {
    goto error;
  }

  ldns_rr_list_push_rr(response_question_section, response_question_rr);
  response_question_rr = NULL;

  ldns_pkt_set_question(response_pkt, response_question_section);
  if (ldns_pkt_question(response_pkt)) {
    ldns_pkt_set_qdcount(
        response_pkt, ldns_rr_list_rr_count(ldns_pkt_question(response_pkt)));
  } else {
    ldns_pkt_set_qdcount(response_pkt, 0);
  }
  response_question_section = NULL;
  ldns_pkt_set_qr(response_pkt, true);
  ldns_pkt_set_aa(response_pkt, true); // Assuming authoritative for local cache
  ldns_pkt_set_rd(response_pkt,
                  ldns_pkt_rd(original_query)); // Copy Recursion Desired
  ldns_pkt_set_ra(response_pkt, true); // Recursion Available (server dependent)
  ldns_pkt_set_rcode(response_pkt, LDNS_RCODE_NOERROR);

  answer_rr = ldns_rr_new();
  if (!answer_rr) {
    goto error;
  }

  qname_clone_for_answer = ldns_rdf_clone(qname_orig);
  if (!qname_clone_for_answer) {
    goto error;
  }

  ldns_rr_set_owner(answer_rr, qname_clone_for_answer);
  qname_clone_for_answer = NULL; // Ownership transferred

  ldns_rr_set_type(answer_rr, LDNS_RR_TYPE_A); // Assuming A record response
  ldns_rr_set_class(answer_rr, LDNS_RR_CLASS_IN);
  ldns_rr_set_ttl(answer_rr,
                  315576000); // Approx 10 years for "permanent" cache

  rdf_ip = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_A, ip_str_to_return);
  if (!rdf_ip) {
    goto error;
  }

  if (!ldns_rr_push_rdf(answer_rr, rdf_ip)) {
    goto error;
  }
  rdf_ip = NULL; // Ownership transferred

  answer_section = ldns_rr_list_new();
  if (!answer_section) {
    goto error;
  }

  ldns_rr_list_push_rr(answer_section, answer_rr);
  answer_rr = NULL; // Ownership transferred

  ldns_pkt_set_answer(response_pkt, answer_section);
  // EXPLICITLY SET ANCOUNT
  if (ldns_pkt_answer(response_pkt)) {
    ldns_pkt_set_ancount(response_pkt,
                         ldns_rr_list_rr_count(ldns_pkt_answer(response_pkt)));
  } else {
    ldns_pkt_set_ancount(response_pkt, 0);
  }
  answer_section = NULL;

  if (ldns_pkt2wire(&response_wire, response_pkt, &response_size) !=
      LDNS_STATUS_OK) {
    fprintf(stderr,
            "Error: Failed to convert response packet to wire format.\n");
    goto error;
  }
  sent_bytes = sendto(sockfd, response_wire, response_size, 0,
                      (struct sockaddr *)&client_addr, client_len);
  if (sent_bytes < 0) {
    perror("Error: Failed to send response to client");
    goto error; // response_wire will be freed in error block
  } else if ((size_t)sent_bytes != response_size) {
    fprintf(stderr, "Warning: sendto sent %zd bytes, but expected %zu bytes.\n",
            sent_bytes, response_size);
    // Potentially problematic, but continue for now
  }

  // 9. Cleanup for success case
  LDNS_FREE(response_wire);
  response_wire = NULL;
  ldns_pkt_free(response_pkt);
  response_pkt = NULL;

  gettimeofday(&send_end, NULL);
  long seconds = send_end.tv_sec - send_start.tv_sec;
  long microseconds = send_end.tv_usec - send_start.tv_usec;
  double elapsed = seconds + microseconds * 1e-6;
  running_avgs_add_cached_query_response(elapsed);

  return sent_bytes;

error:
  // General error message, specific errors should be logged before goto
  fprintf(stderr, "Error encountered in sendCachedValue processing.\n");
  if (response_wire)
    LDNS_FREE(response_wire);

  // Cleanup for RDFs if they were allocated but not successfully attached
  if (qname_clone_for_question)
    ldns_rdf_deep_free(qname_clone_for_question);
  if (qname_clone_for_answer)
    ldns_rdf_deep_free(qname_clone_for_answer);
  if (rdf_ip)
    ldns_rdf_deep_free(rdf_ip);

  // Cleanup for RRs and RR lists if they were allocated but not successfully
  // attached
  if (response_question_rr)
    ldns_rr_free(response_question_rr);
  if (response_question_section)
    ldns_rr_list_free(response_question_section);
  if (answer_rr)
    ldns_rr_free(answer_rr);
  if (answer_section)
    ldns_rr_list_free(answer_section);

  // Finally, free the packet if it was allocated
  if (response_pkt)
    ldns_pkt_free(response_pkt);

  return -1;
}

int sendServFail(int sockfd, struct sockaddr_in client_addr,
                 socklen_t client_len, ldns_pkt *original_query) {
  if (!original_query)
    return -1;

  ldns_pkt *response_pkt = ldns_pkt_new();
  if (!response_pkt)
    return -1;

  ldns_pkt_set_id(response_pkt, ldns_pkt_id(original_query));
  ldns_pkt_set_qr(response_pkt, true);
  ldns_pkt_set_rd(response_pkt, ldns_pkt_rd(original_query));
  ldns_pkt_set_ra(response_pkt, true);
  ldns_pkt_set_rcode(response_pkt, LDNS_RCODE_SERVFAIL);

  // Copy question section from original query
  ldns_rr_list *question_orig = ldns_pkt_question(original_query);
  if (question_orig && ldns_rr_list_rr_count(question_orig) > 0) {
    ldns_rr *orig_rr = ldns_rr_list_rr(question_orig, 0);
    ldns_rr *q_rr = ldns_rr_new();
    if (q_rr) {
      ldns_rdf *qname_clone = ldns_rdf_clone(ldns_rr_owner(orig_rr));
      if (qname_clone) {
        ldns_rr_set_owner(q_rr, qname_clone);
        ldns_rr_set_type(q_rr, ldns_rr_get_type(orig_rr));
        ldns_rr_set_class(q_rr, ldns_rr_get_class(orig_rr));
        ldns_rr_list *q_section = ldns_rr_list_new();
        if (q_section) {
          ldns_rr_list_push_rr(q_section, q_rr);
          ldns_pkt_set_question(response_pkt, q_section);
          ldns_pkt_set_qdcount(response_pkt, 1);
        } else {
          ldns_rr_free(q_rr);
        }
      } else {
        ldns_rr_free(q_rr);
      }
    }
  }

  uint8_t *wire = NULL;
  size_t wire_size = 0;
  int result = -1;
  if (ldns_pkt2wire(&wire, response_pkt, &wire_size) == LDNS_STATUS_OK) {
    ssize_t sent = sendto(sockfd, wire, wire_size, 0,
                          (struct sockaddr *)&client_addr, client_len);
    if (sent >= 0)
      result = 0;
    LDNS_FREE(wire);
  }

  ldns_pkt_free(response_pkt);
  return result;
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

void *processDNS(void *arg) {
  int thread_num = *(int *)arg;

  if (thread_num == 0) {
    enableAdCache();
  }

  char *initial_upstream = getUpstreamDNS();
  printf("Upstream DNS: %s\n", initial_upstream ? initial_upstream : "(null)");
  free(initial_upstream);

  while (1) {
    ThreadArgs *args = dequeue();
    if (args == NULL) {
      continue;
    }
    addProcessedQuery();

    struct timeval send_start, send_end;
    gettimeofday(&send_start, NULL);

    int sockfd = args->sockfd;
    struct sockaddr_in client_addr = args->client_addr;
    socklen_t client_len = args->client_len;
    char *buffer = args->buffer;
    ssize_t n = args->n;

    // These will be cleaned up at the end via goto cleanup
    ldns_pkt *query_pkt = NULL;
    char *domain_str = NULL;
    int upstream_sock = -1;
    uint8_t *query_wire = NULL;
    char *upstream_dns_str = NULL;

    ldns_status status = ldns_wire2pkt(&query_pkt, (uint8_t *)buffer, n);
    if (status != LDNS_STATUS_OK) {
      fprintf(stderr, "Failed to parse DNS query: %s\n",
              ldns_get_errorstr_by_id(status));
      goto cleanup;
    }

    ldns_rr_list *question = ldns_pkt_question(query_pkt);
    if (question && ldns_rr_list_rr_count(question) > 0) {
      ldns_rr *rr = ldns_rr_list_rr(question, 0);
      ldns_rdf *domain = ldns_rr_owner(rr);
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

    if (domain_str) {
      // --- Check DNS cache (atomic: single lock, check + retrieve) ---
      if (CACHE_ENABLED) {
        struct timeval startCache, endCache;
        gettimeofday(&startCache, NULL);
        char cached_ip[16];
        if (get_from_cache_safe(domain_str, cached_ip, sizeof(cached_ip)) ==
            0) {
          gettimeofday(&endCache, NULL);
          long secondsCache = endCache.tv_sec - startCache.tv_sec;
          long microsecondsCache = endCache.tv_usec - startCache.tv_usec;
          double elapsedCache = secondsCache + microsecondsCache * 1e-6;
          running_avgs_add_cache_lookup(elapsedCache);
          addCacheHit();
          sendCachedValue(sockfd, client_addr, client_len, cached_ip, query_pkt,
                          send_start, send_end);
          goto cleanup;
        }
      }

      // --- Check adblock cache (atomic: single lock, check + retrieve) ---
      if (checkAdCacheEnabled()) {
        struct timeval start, end;
        gettimeofday(&start, NULL);
        char adblock_ip[16];
        if (get_from_adcache_safe(domain_str, adblock_ip, sizeof(adblock_ip)) ==
            0) {
          gettimeofday(&end, NULL);
          long seconds = end.tv_sec - start.tv_sec;
          long microseconds = end.tv_usec - start.tv_usec;
          double elapsed = seconds + microseconds * 1e-6;
          printf("Adcache lookup time: %.6f seconds\n", elapsed);
          addBlockedQuery();
          sendCachedValue(sockfd, client_addr, client_len, adblock_ip,
                          query_pkt, send_start, send_end);
          goto cleanup;
        }
      }
    }

    // --- Forward to upstream DNS ---
    upstream_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (upstream_sock < 0) {
      perror("Upstream socket creation failed");
      goto cleanup;
    }

    // Set receive timeout to prevent thread starvation
    struct timeval recv_timeout;
    recv_timeout.tv_sec = 3;
    recv_timeout.tv_usec = 0;
    setsockopt(upstream_sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout,
               sizeof(recv_timeout));

    struct sockaddr_in upstream_addr;
    memset(&upstream_addr, 0, sizeof(upstream_addr));
    upstream_addr.sin_family = AF_INET;
    upstream_addr.sin_port = htons(53);
    upstream_dns_str = getUpstreamDNS();
    if (!upstream_dns_str) {
      fprintf(stderr, "Failed to get upstream DNS address\n");
      sendServFail(sockfd, client_addr, client_len, query_pkt);
      goto cleanup;
    }
    inet_pton(AF_INET, upstream_dns_str, &upstream_addr.sin_addr);

    // If DNSSEC is enabled, prepare the query with DO bit + EDNS
    if (dnssec_is_enabled()) {
      dnssec_prepare_query(query_pkt);
    }

    size_t query_size;
    ldns_pkt2wire(&query_wire, query_pkt, &query_size);

    if (sendto(upstream_sock, query_wire, query_size, 0,
               (struct sockaddr *)&upstream_addr, sizeof(upstream_addr)) < 0) {
      perror("Failed to forward query to upstream server");
      sendServFail(sockfd, client_addr, client_len, query_pkt);
      goto cleanup;
    }

    // Buffer sized for DNSSEC responses (RRSIG records make them larger)
    char newBuffer[8192];
    ssize_t response_size =
        recvfrom(upstream_sock, newBuffer, sizeof(newBuffer), 0, NULL, NULL);
    if (response_size < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        fprintf(stderr, "Upstream DNS timeout for query: %s\n",
                domain_str ? domain_str : "(unknown)");
      } else {
        perror("Failed to receive response from upstream server");
      }
      sendServFail(sockfd, client_addr, client_len, query_pkt);
      goto cleanup;
    }

    // Handle TCP fallback for truncated DNSSEC responses
    ldns_pkt *tc_check_pkt = NULL;
    if (ldns_wire2pkt(&tc_check_pkt, (uint8_t *)newBuffer, response_size) ==
            LDNS_STATUS_OK &&
        ldns_pkt_tc(tc_check_pkt)) {
      ldns_pkt_free(tc_check_pkt);
      tc_check_pkt = NULL;

      // Response was truncated — retry over TCP
      int tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
      if (tcp_sock >= 0) {
        struct timeval tcp_timeout;
        tcp_timeout.tv_sec = 5;
        tcp_timeout.tv_usec = 0;
        setsockopt(tcp_sock, SOL_SOCKET, SO_RCVTIMEO, &tcp_timeout,
                   sizeof(tcp_timeout));
        setsockopt(tcp_sock, SOL_SOCKET, SO_SNDTIMEO, &tcp_timeout,
                   sizeof(tcp_timeout));

        if (connect(tcp_sock, (struct sockaddr *)&upstream_addr,
                    sizeof(upstream_addr)) == 0) {
          // TCP DNS uses 2-byte length prefix
          uint16_t tcp_len = htons((uint16_t)query_size);
          if (send(tcp_sock, &tcp_len, 2, 0) == 2 &&
              send(tcp_sock, query_wire, query_size, 0) ==
                  (ssize_t)query_size) {
            uint16_t resp_len;
            if (recv(tcp_sock, &resp_len, 2, MSG_WAITALL) == 2) {
              resp_len = ntohs(resp_len);
              if (resp_len <= sizeof(newBuffer)) {
                ssize_t tcp_received =
                    recv(tcp_sock, newBuffer, resp_len, MSG_WAITALL);
                if (tcp_received == resp_len) {
                  response_size = tcp_received;
                }
              }
            }
          }
        }
        close(tcp_sock);
      }
    } else {
      if (tc_check_pkt)
        ldns_pkt_free(tc_check_pkt);
    }

    // Parse upstream response and validate DNSSEC if enabled
    ldns_pkt *response_pkt = NULL;
    ldns_status response_status =
        ldns_wire2pkt(&response_pkt, (uint8_t *)newBuffer, response_size);
    if (response_status != LDNS_STATUS_OK) {
      fprintf(stderr, "Failed to parse upstream response: %s\n",
              ldns_get_errorstr_by_id(response_status));
    } else {
      // DNSSEC validation: check the response if enabled
      if (dnssec_is_enabled() && !dnssec_validate_response(response_pkt)) {
        fprintf(stderr, "DNSSEC validation failed for: %s\n",
                domain_str ? domain_str : "(unknown)");
        ldns_pkt_free(response_pkt);
        sendServFail(sockfd, client_addr, client_len, query_pkt);
        goto cleanup;
      }

      ldns_rr_list *answer_list = ldns_pkt_answer(response_pkt);
      if (answer_list && ldns_rr_list_rr_count(answer_list) > 0) {
        for (size_t i = 0; i < ldns_rr_list_rr_count(answer_list); i++) {
          ldns_rr *rr = ldns_rr_list_rr(answer_list, i);
          if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_A) {
            ldns_rdf *rdf_ip = ldns_rr_rdf(rr, 0);
            if (rdf_ip == NULL) {
              fprintf(stderr, "Invalid RDF IP object\n");
              continue;
            }

            char *ip_str = ldns_rdf2str(rdf_ip);
            if (ip_str == NULL || strlen(ip_str) == 0) {
              fprintf(stderr, "Invalid IP string from ldns_rdf2str\n");
              free(ip_str);
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
      ldns_pkt_free(response_pkt);
    }

    // Send response back to client
    if (sendto(sockfd, newBuffer, response_size, 0,
               (struct sockaddr *)&client_addr, client_len) < 0) {
      perror("Failed to send response to client");
    }

    gettimeofday(&send_end, NULL);
    long seconds = send_end.tv_sec - send_start.tv_sec;
    long microseconds = send_end.tv_usec - send_start.tv_usec;
    double elapsed = seconds + microseconds * 1e-6;
    running_avgs_add_query_response(elapsed);

  cleanup:
    if (upstream_sock >= 0)
      close(upstream_sock);
    if (query_wire)
      free(query_wire);
    if (query_pkt)
      ldns_pkt_free(query_pkt);
    if (domain_str)
      free(domain_str);
    if (upstream_dns_str)
      free(upstream_dns_str);
    free(args->buffer);
    free(args);
  }
}