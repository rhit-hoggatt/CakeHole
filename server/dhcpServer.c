#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "dhcpServer.h"

// Global DHCP state
DHCPConfig dhcp_config = {.enabled = false,
                          .range_start = 0,
                          .range_end = 0,
                          .subnet_mask = 0,
                          .gateway = 0,
                          .dns_server = 0,
                          .server_ip = 0,
                          .lease_time = 86400, // 24 hours default
                          .domain_name = ""};

DHCPLease dhcp_leases[MAX_DHCP_LEASES];
int dhcp_lease_count = 0;
pthread_mutex_t dhcp_mutex = PTHREAD_MUTEX_INITIALIZER;

static int dhcp_socket = -1;
static pthread_t dhcp_thread;
static volatile bool dhcp_running = false;

// DHCP Magic Cookie
static const uint8_t DHCP_MAGIC_COOKIE[] = {99, 130, 83, 99};

// ============================================================================
// Utility Functions
// ============================================================================

int mac_str_to_bytes(const char *mac_str, uint8_t *mac) {
  if (!mac_str || !mac)
    return -1;

  unsigned int values[6];
  if (sscanf(mac_str, "%x:%x:%x:%x:%x:%x", &values[0], &values[1], &values[2],
             &values[3], &values[4], &values[5]) != 6) {
    // Try alternate format with dashes
    if (sscanf(mac_str, "%x-%x-%x-%x-%x-%x", &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) != 6) {
      return -1;
    }
  }

  for (int i = 0; i < 6; i++) {
    if (values[i] > 255)
      return -1;
    mac[i] = (uint8_t)values[i];
  }
  return 0;
}

void mac_bytes_to_str(const uint8_t *mac, char *mac_str) {
  sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
          mac[3], mac[4], mac[5]);
}

uint32_t ip_str_to_uint(const char *ip_str) {
  struct in_addr addr;
  if (inet_pton(AF_INET, ip_str, &addr) != 1) {
    return 0;
  }
  return ntohl(addr.s_addr);
}

void ip_uint_to_str(uint32_t ip, char *ip_str) {
  struct in_addr addr;
  addr.s_addr = htonl(ip);
  inet_ntop(AF_INET, &addr, ip_str, INET_ADDRSTRLEN);
}

// ============================================================================
// Configuration Persistence
// ============================================================================

#define DHCP_CONFIG_FILE "adlists/metadata/dhcp_config.txt"
#define DHCP_LEASES_FILE "adlists/metadata/dhcp_leases.txt"

int dhcp_load_config(void) {
  pthread_mutex_lock(&dhcp_mutex);

  // Load config
  FILE *file = fopen(DHCP_CONFIG_FILE, "r");
  if (file) {
    char line[256];
    while (fgets(line, sizeof(line), file)) {
      char key[64], value[128];
      if (sscanf(line, "%63s %127s", key, value) == 2) {
        if (strcmp(key, "ENABLED") == 0) {
          dhcp_config.enabled = (atoi(value) != 0);
        } else if (strcmp(key, "RANGE_START") == 0) {
          dhcp_config.range_start = ip_str_to_uint(value);
        } else if (strcmp(key, "RANGE_END") == 0) {
          dhcp_config.range_end = ip_str_to_uint(value);
        } else if (strcmp(key, "SUBNET_MASK") == 0) {
          dhcp_config.subnet_mask = ip_str_to_uint(value);
        } else if (strcmp(key, "GATEWAY") == 0) {
          dhcp_config.gateway = ip_str_to_uint(value);
        } else if (strcmp(key, "DNS_SERVER") == 0) {
          dhcp_config.dns_server = ip_str_to_uint(value);
        } else if (strcmp(key, "SERVER_IP") == 0) {
          dhcp_config.server_ip = ip_str_to_uint(value);
        } else if (strcmp(key, "LEASE_TIME") == 0) {
          dhcp_config.lease_time = (uint32_t)atol(value);
        } else if (strcmp(key, "DOMAIN_NAME") == 0) {
          strncpy(dhcp_config.domain_name, value,
                  sizeof(dhcp_config.domain_name) - 1);
        }
      }
    }
    fclose(file);
  }

  // Load static leases
  dhcp_lease_count = 0;
  file = fopen(DHCP_LEASES_FILE, "r");
  if (file) {
    char line[256];
    while (fgets(line, sizeof(line), file) &&
           dhcp_lease_count < MAX_DHCP_LEASES) {
      char mac_str[32], ip_str[32], hostname[MAX_HOSTNAME_LEN];
      hostname[0] = '\0';

      int fields =
          sscanf(line, "%31s %31s %63[^\n]", mac_str, ip_str, hostname);
      if (fields >= 2) {
        DHCPLease *lease = &dhcp_leases[dhcp_lease_count];
        if (mac_str_to_bytes(mac_str, lease->mac) == 0) {
          lease->ip = ip_str_to_uint(ip_str);
          if (fields >= 3) {
            strncpy(lease->hostname, hostname, MAX_HOSTNAME_LEN - 1);
            lease->hostname[MAX_HOSTNAME_LEN - 1] = '\0';
          } else {
            lease->hostname[0] = '\0';
          }
          lease->is_static = true;
          lease->expiry = 0;
          dhcp_lease_count++;
        }
      }
    }
    fclose(file);
  }

  pthread_mutex_unlock(&dhcp_mutex);

  // Auto-detect server IP if not configured
  if (dhcp_config.server_ip == 0) {
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == 0) {
      for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
          continue;
        if (ifa->ifa_addr->sa_family != AF_INET)
          continue;
        // Skip loopback
        if (ifa->ifa_flags & IFF_LOOPBACK)
          continue;
        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        dhcp_config.server_ip = ntohl(sa->sin_addr.s_addr);
        char ip_buf[INET_ADDRSTRLEN];
        ip_uint_to_str(dhcp_config.server_ip, ip_buf);
        printf("DHCP: Auto-detected server IP: %s (interface %s)\n", ip_buf,
               ifa->ifa_name);
        break;
      }
      freeifaddrs(ifaddr);
    }
    if (dhcp_config.server_ip == 0) {
      printf("DHCP: WARNING - Could not auto-detect server IP!\n");
    }
  }

  printf("DHCP: Loaded config (enabled=%d, leases=%d)\n", dhcp_config.enabled,
         dhcp_lease_count);
  return 0;
}

// Internal save function - caller MUST hold dhcp_mutex
static int dhcp_save_config_locked(void) {
  // Save config
  FILE *file = fopen(DHCP_CONFIG_FILE, "w");
  if (!file) {
    perror("Failed to open DHCP config file for writing");
    return -1;
  }

  char ip_buf[INET_ADDRSTRLEN];

  fprintf(file, "ENABLED %d\n", dhcp_config.enabled ? 1 : 0);

  ip_uint_to_str(dhcp_config.range_start, ip_buf);
  fprintf(file, "RANGE_START %s\n", ip_buf);

  ip_uint_to_str(dhcp_config.range_end, ip_buf);
  fprintf(file, "RANGE_END %s\n", ip_buf);

  ip_uint_to_str(dhcp_config.subnet_mask, ip_buf);
  fprintf(file, "SUBNET_MASK %s\n", ip_buf);

  ip_uint_to_str(dhcp_config.gateway, ip_buf);
  fprintf(file, "GATEWAY %s\n", ip_buf);

  ip_uint_to_str(dhcp_config.dns_server, ip_buf);
  fprintf(file, "DNS_SERVER %s\n", ip_buf);

  ip_uint_to_str(dhcp_config.server_ip, ip_buf);
  fprintf(file, "SERVER_IP %s\n", ip_buf);

  fprintf(file, "LEASE_TIME %u\n", dhcp_config.lease_time);

  if (dhcp_config.domain_name[0]) {
    fprintf(file, "DOMAIN_NAME %s\n", dhcp_config.domain_name);
  }

  fclose(file);

  // Save static leases
  file = fopen(DHCP_LEASES_FILE, "w");
  if (!file) {
    perror("Failed to open DHCP leases file for writing");
    return -1;
  }

  for (int i = 0; i < dhcp_lease_count; i++) {
    if (dhcp_leases[i].is_static) {
      char mac_str[32];
      mac_bytes_to_str(dhcp_leases[i].mac, mac_str);
      ip_uint_to_str(dhcp_leases[i].ip, ip_buf);

      if (dhcp_leases[i].hostname[0]) {
        fprintf(file, "%s %s %s\n", mac_str, ip_buf, dhcp_leases[i].hostname);
      } else {
        fprintf(file, "%s %s\n", mac_str, ip_buf);
      }
    }
  }

  fclose(file);
  return 0;
}

int dhcp_save_config(void) {
  pthread_mutex_lock(&dhcp_mutex);
  int result = dhcp_save_config_locked();
  pthread_mutex_unlock(&dhcp_mutex);
  return result;
}

// ============================================================================
// Lease Management
// ============================================================================

DHCPLease *dhcp_find_lease_by_mac(const uint8_t *mac) {
  for (int i = 0; i < dhcp_lease_count; i++) {
    if (memcmp(dhcp_leases[i].mac, mac, MAC_ADDR_LEN) == 0) {
      return &dhcp_leases[i];
    }
  }
  return NULL;
}

DHCPLease *dhcp_find_lease_by_ip(uint32_t ip) {
  for (int i = 0; i < dhcp_lease_count; i++) {
    if (dhcp_leases[i].ip == ip) {
      return &dhcp_leases[i];
    }
  }
  return NULL;
}

int dhcp_add_static_lease(const uint8_t *mac, uint32_t ip,
                          const char *hostname) {
  pthread_mutex_lock(&dhcp_mutex);

  // Check if MAC already exists
  DHCPLease *existing = dhcp_find_lease_by_mac(mac);
  if (existing) {
    // Update existing lease
    existing->ip = ip;
    existing->is_static = true;
    existing->expiry = 0;
    if (hostname) {
      strncpy(existing->hostname, hostname, MAX_HOSTNAME_LEN - 1);
      existing->hostname[MAX_HOSTNAME_LEN - 1] = '\0';
    }
    dhcp_save_config_locked();
    pthread_mutex_unlock(&dhcp_mutex);
    return 0;
  }

  // Add new lease
  if (dhcp_lease_count >= MAX_DHCP_LEASES) {
    pthread_mutex_unlock(&dhcp_mutex);
    return -1;
  }

  DHCPLease *lease = &dhcp_leases[dhcp_lease_count];
  memcpy(lease->mac, mac, MAC_ADDR_LEN);
  lease->ip = ip;
  lease->is_static = true;
  lease->expiry = 0;
  if (hostname) {
    strncpy(lease->hostname, hostname, MAX_HOSTNAME_LEN - 1);
    lease->hostname[MAX_HOSTNAME_LEN - 1] = '\0';
  } else {
    lease->hostname[0] = '\0';
  }

  dhcp_lease_count++;
  dhcp_save_config_locked();
  pthread_mutex_unlock(&dhcp_mutex);

  return 0;
}

int dhcp_remove_static_lease(const uint8_t *mac) {
  pthread_mutex_lock(&dhcp_mutex);

  for (int i = 0; i < dhcp_lease_count; i++) {
    if (memcmp(dhcp_leases[i].mac, mac, MAC_ADDR_LEN) == 0) {
      // Shift remaining leases
      for (int j = i; j < dhcp_lease_count - 1; j++) {
        dhcp_leases[j] = dhcp_leases[j + 1];
      }
      dhcp_lease_count--;
      dhcp_save_config_locked();
      pthread_mutex_unlock(&dhcp_mutex);
      return 0;
    }
  }

  pthread_mutex_unlock(&dhcp_mutex);
  return -1;
}

char *dhcp_get_leases_json(void) {
  pthread_mutex_lock(&dhcp_mutex);

  // Estimate buffer size
  size_t buf_size = 256 + (dhcp_lease_count * 256);
  char *json = malloc(buf_size);
  if (!json) {
    pthread_mutex_unlock(&dhcp_mutex);
    return NULL;
  }

  strcpy(json, "[");

  for (int i = 0; i < dhcp_lease_count; i++) {
    char mac_str[32], ip_str[32];
    mac_bytes_to_str(dhcp_leases[i].mac, mac_str);
    ip_uint_to_str(dhcp_leases[i].ip, ip_str);

    char entry[256];
    snprintf(entry, sizeof(entry),
             "%s{\"mac\":\"%s\",\"ip\":\"%s\",\"name\":\"%s\",\"static\":%s}",
             i > 0 ? "," : "", mac_str, ip_str, dhcp_leases[i].hostname,
             dhcp_leases[i].is_static ? "true" : "false");
    strcat(json, entry);
  }

  strcat(json, "]");
  pthread_mutex_unlock(&dhcp_mutex);
  return json;
}

char *dhcp_get_settings_json(void) {
  pthread_mutex_lock(&dhcp_mutex);

  char *json = malloc(1024);
  if (!json) {
    pthread_mutex_unlock(&dhcp_mutex);
    return NULL;
  }

  char range_start[INET_ADDRSTRLEN], range_end[INET_ADDRSTRLEN];
  char subnet[INET_ADDRSTRLEN], gateway[INET_ADDRSTRLEN];
  char dns[INET_ADDRSTRLEN], server_ip[INET_ADDRSTRLEN];

  ip_uint_to_str(dhcp_config.range_start, range_start);
  ip_uint_to_str(dhcp_config.range_end, range_end);
  ip_uint_to_str(dhcp_config.subnet_mask, subnet);
  ip_uint_to_str(dhcp_config.gateway, gateway);
  ip_uint_to_str(dhcp_config.dns_server, dns);
  ip_uint_to_str(dhcp_config.server_ip, server_ip);

  snprintf(json, 1024,
           "{\"enabled\":%s,\"rangeStart\":\"%s\",\"rangeEnd\":\"%s\","
           "\"subnetMask\":\"%s\",\"gateway\":\"%s\",\"dnsServer\":\"%s\","
           "\"serverIp\":\"%s\",\"leaseTime\":%u,\"domainName\":\"%s\"}",
           dhcp_config.enabled ? "true" : "false", range_start, range_end,
           subnet, gateway, dns, server_ip, dhcp_config.lease_time,
           dhcp_config.domain_name);

  pthread_mutex_unlock(&dhcp_mutex);
  return json;
}

int dhcp_set_settings(uint32_t range_start, uint32_t range_end,
                      uint32_t subnet_mask, uint32_t gateway,
                      uint32_t dns_server, uint32_t lease_time,
                      uint32_t server_ip) {
  pthread_mutex_lock(&dhcp_mutex);

  dhcp_config.range_start = range_start;
  dhcp_config.range_end = range_end;
  dhcp_config.subnet_mask = subnet_mask;
  dhcp_config.gateway = gateway;
  dhcp_config.dns_server = dns_server;
  dhcp_config.lease_time = lease_time;
  if (server_ip != 0) {
    dhcp_config.server_ip = server_ip;
  }

  pthread_mutex_unlock(&dhcp_mutex);
  return dhcp_save_config();
}

// ============================================================================
// DHCP Protocol Handling
// ============================================================================

static uint8_t get_dhcp_message_type(const DHCPMessage *msg) {
  // Find message type option
  const uint8_t *opt = msg->options + 4; // Skip magic cookie
  while (*opt != DHCP_OPT_END && opt < msg->options + DHCP_OPTIONS_LEN) {
    if (*opt == DHCP_OPT_PAD) {
      opt++;
      continue;
    }
    uint8_t opt_type = *opt++;
    uint8_t opt_len = *opt++;

    if (opt_type == DHCP_OPT_MSG_TYPE && opt_len >= 1) {
      return *opt;
    }
    opt += opt_len;
  }
  return 0;
}

static uint32_t get_requested_ip(const DHCPMessage *msg) {
  const uint8_t *opt = msg->options + 4;
  while (*opt != DHCP_OPT_END && opt < msg->options + DHCP_OPTIONS_LEN) {
    if (*opt == DHCP_OPT_PAD) {
      opt++;
      continue;
    }
    uint8_t opt_type = *opt++;
    uint8_t opt_len = *opt++;

    if (opt_type == DHCP_OPT_REQUESTED_IP && opt_len == 4) {
      return ntohl(*(uint32_t *)opt);
    }
    opt += opt_len;
  }
  return 0;
}

static uint32_t allocate_ip_for_mac(const uint8_t *mac) {
  // First check for existing/static lease
  DHCPLease *lease = dhcp_find_lease_by_mac(mac);
  if (lease) {
    return lease->ip;
  }

  // Allocate from pool
  for (uint32_t ip = dhcp_config.range_start; ip <= dhcp_config.range_end;
       ip++) {
    if (!dhcp_find_lease_by_ip(ip)) {
      return ip;
    }
  }

  return 0; // No available IP
}

static int build_dhcp_response(DHCPMessage *response,
                               const DHCPMessage *request, uint8_t msg_type,
                               uint32_t offered_ip) {
  memset(response, 0, sizeof(DHCPMessage));

  response->op = 2; // BOOTREPLY
  response->htype = 1;
  response->hlen = 6;
  response->hops = 0;
  response->xid = request->xid;
  response->secs = 0;
  response->flags = request->flags;
  response->ciaddr = 0;
  response->yiaddr = htonl(offered_ip);
  response->siaddr = htonl(dhcp_config.server_ip);
  response->giaddr = request->giaddr;
  memcpy(response->chaddr, request->chaddr, DHCP_CHADDR_LEN);

  // Build options
  uint8_t *opt = response->options;

  // Magic cookie
  memcpy(opt, DHCP_MAGIC_COOKIE, 4);
  opt += 4;

  // Message type
  *opt++ = DHCP_OPT_MSG_TYPE;
  *opt++ = 1;
  *opt++ = msg_type;

  // Server identifier
  *opt++ = DHCP_OPT_SERVER_ID;
  *opt++ = 4;
  *(uint32_t *)opt = htonl(dhcp_config.server_ip);
  opt += 4;

  // Lease time
  *opt++ = DHCP_OPT_LEASE_TIME;
  *opt++ = 4;
  *(uint32_t *)opt = htonl(dhcp_config.lease_time);
  opt += 4;

  // Subnet mask
  *opt++ = DHCP_OPT_SUBNET_MASK;
  *opt++ = 4;
  *(uint32_t *)opt = htonl(dhcp_config.subnet_mask);
  opt += 4;

  // Router (gateway)
  *opt++ = DHCP_OPT_ROUTER;
  *opt++ = 4;
  *(uint32_t *)opt = htonl(dhcp_config.gateway);
  opt += 4;

  // DNS server
  *opt++ = DHCP_OPT_DNS;
  *opt++ = 4;
  *(uint32_t *)opt = htonl(dhcp_config.dns_server);
  opt += 4;

  // Renewal time (T1) - 50% of lease time
  *opt++ = DHCP_OPT_RENEWAL_TIME;
  *opt++ = 4;
  *(uint32_t *)opt = htonl(dhcp_config.lease_time / 2);
  opt += 4;

  // Rebind time (T2) - 87.5% of lease time
  *opt++ = DHCP_OPT_REBIND_TIME;
  *opt++ = 4;
  *(uint32_t *)opt = htonl((dhcp_config.lease_time * 7) / 8);
  opt += 4;

  // End option
  *opt++ = DHCP_OPT_END;

  return (opt - response->options) + offsetof(DHCPMessage, options);
}

static void handle_dhcp_discover(int sock, const DHCPMessage *request,
                                 struct sockaddr_in *client_addr) {
  (void)client_addr; // Unused parameter
  pthread_mutex_lock(&dhcp_mutex);

  uint32_t offered_ip = allocate_ip_for_mac(request->chaddr);
  if (offered_ip == 0) {
    printf("DHCP: No available IP for DISCOVER\n");
    pthread_mutex_unlock(&dhcp_mutex);
    return;
  }

  DHCPMessage response;
  int resp_len =
      build_dhcp_response(&response, request, DHCP_OFFER, offered_ip);

  pthread_mutex_unlock(&dhcp_mutex);

  // Send to broadcast or unicast based on flags
  struct sockaddr_in dest;
  memset(&dest, 0, sizeof(dest));
  dest.sin_family = AF_INET;
  dest.sin_port = htons(DHCP_CLIENT_PORT);

  if (request->flags & htons(0x8000)) {
    dest.sin_addr.s_addr = INADDR_BROADCAST;
  } else if (request->giaddr != 0) {
    dest.sin_addr.s_addr = request->giaddr;
  } else {
    dest.sin_addr.s_addr = INADDR_BROADCAST;
  }

  char ip_str[INET_ADDRSTRLEN];
  ip_uint_to_str(offered_ip, ip_str);
  printf("DHCP: Sending OFFER for %s\n", ip_str);

  sendto(sock, &response, resp_len, 0, (struct sockaddr *)&dest, sizeof(dest));
}

static void handle_dhcp_request(int sock, const DHCPMessage *request,
                                struct sockaddr_in *client_addr) {
  (void)client_addr; // Unused parameter
  pthread_mutex_lock(&dhcp_mutex);

  uint32_t requested_ip = get_requested_ip(request);
  if (requested_ip == 0) {
    requested_ip = ntohl(request->ciaddr);
  }

  // Check if this request is valid
  uint32_t expected_ip = allocate_ip_for_mac(request->chaddr);

  uint8_t response_type;
  if (requested_ip == expected_ip ||
      (expected_ip != 0 && requested_ip >= dhcp_config.range_start &&
       requested_ip <= dhcp_config.range_end)) {

    // Valid request - create/update lease
    DHCPLease *lease = dhcp_find_lease_by_mac(request->chaddr);
    if (lease) {
      lease->expiry = time(NULL) + dhcp_config.lease_time;
    } else {
      // Add dynamic lease
      if (dhcp_lease_count < MAX_DHCP_LEASES) {
        lease = &dhcp_leases[dhcp_lease_count++];
        memcpy(lease->mac, request->chaddr, MAC_ADDR_LEN);
        lease->ip = requested_ip;
        lease->is_static = false;
        lease->expiry = time(NULL) + dhcp_config.lease_time;
        lease->hostname[0] = '\0';
      }
    }
    response_type = DHCP_ACK;
  } else {
    response_type = DHCP_NAK;
    requested_ip = 0;
  }

  DHCPMessage response;
  int resp_len =
      build_dhcp_response(&response, request, response_type,
                          response_type == DHCP_ACK ? requested_ip : 0);

  pthread_mutex_unlock(&dhcp_mutex);

  struct sockaddr_in dest;
  memset(&dest, 0, sizeof(dest));
  dest.sin_family = AF_INET;
  dest.sin_port = htons(DHCP_CLIENT_PORT);

  if (request->flags & htons(0x8000) || response_type == DHCP_NAK) {
    dest.sin_addr.s_addr = INADDR_BROADCAST;
  } else if (request->ciaddr != 0) {
    dest.sin_addr.s_addr = request->ciaddr;
  } else {
    dest.sin_addr.s_addr = INADDR_BROADCAST;
  }

  char ip_str[INET_ADDRSTRLEN];
  ip_uint_to_str(requested_ip, ip_str);
  printf("DHCP: Sending %s for %s\n", response_type == DHCP_ACK ? "ACK" : "NAK",
         ip_str);

  sendto(sock, &response, resp_len, 0, (struct sockaddr *)&dest, sizeof(dest));
}

static void handle_dhcp_release(const DHCPMessage *request) {
  pthread_mutex_lock(&dhcp_mutex);

  // Find and remove non-static lease
  for (int i = 0; i < dhcp_lease_count; i++) {
    if (memcmp(dhcp_leases[i].mac, request->chaddr, MAC_ADDR_LEN) == 0 &&
        !dhcp_leases[i].is_static) {
      for (int j = i; j < dhcp_lease_count - 1; j++) {
        dhcp_leases[j] = dhcp_leases[j + 1];
      }
      dhcp_lease_count--;
      break;
    }
  }

  pthread_mutex_unlock(&dhcp_mutex);
  printf("DHCP: Processed RELEASE\n");
}

// ============================================================================
// Server Control
// ============================================================================

void *dhcp_server_thread(void *arg) {
  (void)arg;

  uint8_t buffer[1024];
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);

  printf("DHCP: Server thread started\n");

  while (dhcp_running) {
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(dhcp_socket, &readfds);
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    int ret = select(dhcp_socket + 1, &readfds, NULL, NULL, &tv);
    if (ret < 0) {
      if (errno == EINTR)
        continue;
      perror("DHCP select error");
      break;
    }

    if (ret == 0)
      continue; // Timeout

    ssize_t n = recvfrom(dhcp_socket, buffer, sizeof(buffer), 0,
                         (struct sockaddr *)&client_addr, &client_len);

    if (n < (ssize_t)sizeof(DHCPMessage) - DHCP_OPTIONS_LEN + 4) {
      continue; // Too small
    }

    DHCPMessage *request = (DHCPMessage *)buffer;

    // Verify magic cookie
    if (memcmp(request->options, DHCP_MAGIC_COOKIE, 4) != 0) {
      continue;
    }

    // Only handle requests (op == 1)
    if (request->op != 1)
      continue;

    uint8_t msg_type = get_dhcp_message_type(request);

    switch (msg_type) {
    case DHCP_DISCOVER:
      printf("DHCP: Received DISCOVER\n");
      handle_dhcp_discover(dhcp_socket, request, &client_addr);
      break;
    case DHCP_REQUEST:
      printf("DHCP: Received REQUEST\n");
      handle_dhcp_request(dhcp_socket, request, &client_addr);
      break;
    case DHCP_RELEASE:
      printf("DHCP: Received RELEASE\n");
      handle_dhcp_release(request);
      break;
    case DHCP_DECLINE:
      printf("DHCP: Received DECLINE (ignored)\n");
      break;
    case DHCP_INFORM:
      printf("DHCP: Received INFORM (ignored)\n");
      break;
    default:
      printf("DHCP: Unknown message type %d\n", msg_type);
    }
  }

  printf("DHCP: Server thread stopped\n");
  return NULL;
}

int dhcp_server_init(void) { return dhcp_load_config(); }

int dhcp_server_start(void) {
  if (dhcp_running) {
    return 0; // Already running
  }

  // Create UDP socket
  dhcp_socket = socket(AF_INET, SOCK_DGRAM, 0);
  if (dhcp_socket < 0) {
    perror("DHCP: Failed to create socket");
    return -1;
  }

  // Enable broadcast
  int broadcast = 1;
  if (setsockopt(dhcp_socket, SOL_SOCKET, SO_BROADCAST, &broadcast,
                 sizeof(broadcast)) < 0) {
    perror("DHCP: Failed to set SO_BROADCAST");
    close(dhcp_socket);
    dhcp_socket = -1;
    return -1;
  }

  // Reuse address
  int reuse = 1;
  setsockopt(dhcp_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  // Bind to DHCP server port
  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(DHCP_SERVER_PORT);

  if (bind(dhcp_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("DHCP: Failed to bind socket (need root?)");
    close(dhcp_socket);
    dhcp_socket = -1;
    return -1;
  }

  dhcp_running = true;

  if (pthread_create(&dhcp_thread, NULL, dhcp_server_thread, NULL) != 0) {
    perror("DHCP: Failed to create server thread");
    dhcp_running = false;
    close(dhcp_socket);
    dhcp_socket = -1;
    return -1;
  }

  printf("DHCP: Server started on port %d\n", DHCP_SERVER_PORT);
  return 0;
}

int dhcp_server_stop(void) {
  if (!dhcp_running) {
    return 0;
  }

  dhcp_running = false;

  // Wait for thread to finish
  pthread_join(dhcp_thread, NULL);

  if (dhcp_socket >= 0) {
    close(dhcp_socket);
    dhcp_socket = -1;
  }

  printf("DHCP: Server stopped\n");
  return 0;
}

bool dhcp_is_enabled(void) {
  pthread_mutex_lock(&dhcp_mutex);
  bool enabled = dhcp_config.enabled;
  pthread_mutex_unlock(&dhcp_mutex);
  return enabled;
}

void dhcp_set_enabled(bool enabled) {
  pthread_mutex_lock(&dhcp_mutex);
  dhcp_config.enabled = enabled;
  pthread_mutex_unlock(&dhcp_mutex);

  dhcp_save_config();

  if (enabled) {
    dhcp_server_start();
  } else {
    dhcp_server_stop();
  }
}
