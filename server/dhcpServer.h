#ifndef DHCP_SERVER_H
#define DHCP_SERVER_H

#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>


// DHCP Ports
#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

// DHCP Message Types
#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_DECLINE 4
#define DHCP_ACK 5
#define DHCP_NAK 6
#define DHCP_RELEASE 7
#define DHCP_INFORM 8

// DHCP Options
#define DHCP_OPT_PAD 0
#define DHCP_OPT_SUBNET_MASK 1
#define DHCP_OPT_ROUTER 3
#define DHCP_OPT_DNS 6
#define DHCP_OPT_HOSTNAME 12
#define DHCP_OPT_DOMAIN_NAME 15
#define DHCP_OPT_BROADCAST 28
#define DHCP_OPT_REQUESTED_IP 50
#define DHCP_OPT_LEASE_TIME 51
#define DHCP_OPT_MSG_TYPE 53
#define DHCP_OPT_SERVER_ID 54
#define DHCP_OPT_PARAM_LIST 55
#define DHCP_OPT_RENEWAL_TIME 58
#define DHCP_OPT_REBIND_TIME 59
#define DHCP_OPT_END 255

// DHCP Message structure (RFC 2131)
#define DHCP_CHADDR_LEN 16
#define DHCP_SNAME_LEN 64
#define DHCP_FILE_LEN 128
#define DHCP_OPTIONS_LEN 312

typedef struct __attribute__((packed)) {
  uint8_t op;      // Message op code: 1 = BOOTREQUEST, 2 = BOOTREPLY
  uint8_t htype;   // Hardware address type: 1 = Ethernet
  uint8_t hlen;    // Hardware address length: 6 for Ethernet
  uint8_t hops;    // Hops
  uint32_t xid;    // Transaction ID
  uint16_t secs;   // Seconds elapsed
  uint16_t flags;  // Flags
  uint32_t ciaddr; // Client IP address
  uint32_t yiaddr; // 'Your' IP address (assigned)
  uint32_t siaddr; // Server IP address
  uint32_t giaddr; // Gateway IP address
  uint8_t chaddr[DHCP_CHADDR_LEN];   // Client hardware address
  uint8_t sname[DHCP_SNAME_LEN];     // Server host name
  uint8_t file[DHCP_FILE_LEN];       // Boot file name
  uint8_t options[DHCP_OPTIONS_LEN]; // Options (variable length)
} DHCPMessage;

// Static lease entry
#define MAX_HOSTNAME_LEN 64
#define MAC_ADDR_LEN 6

typedef struct {
  uint8_t mac[MAC_ADDR_LEN];
  uint32_t ip;
  char hostname[MAX_HOSTNAME_LEN];
  bool is_static; // true = static assignment, false = dynamic
  time_t expiry;  // Lease expiry time (0 for static)
} DHCPLease;

// DHCP Server Configuration
typedef struct {
  bool enabled;
  uint32_t range_start; // Start of IP pool
  uint32_t range_end;   // End of IP pool
  uint32_t subnet_mask; // Subnet mask
  uint32_t gateway;     // Default gateway
  uint32_t dns_server;  // DNS server (usually this server)
  uint32_t server_ip;   // This server's IP
  uint32_t lease_time;  // Lease duration in seconds
  char domain_name[64]; // Domain name (optional)
} DHCPConfig;

// Maximum number of leases
#define MAX_DHCP_LEASES 256

// Global DHCP state
extern DHCPConfig dhcp_config;
extern DHCPLease dhcp_leases[MAX_DHCP_LEASES];
extern int dhcp_lease_count;
extern pthread_mutex_t dhcp_mutex;

// Server control functions
int dhcp_server_init(void);
int dhcp_server_start(void);
int dhcp_server_stop(void);
bool dhcp_is_enabled(void);
void dhcp_set_enabled(bool enabled);

// Lease management
int dhcp_add_static_lease(const uint8_t *mac, uint32_t ip,
                          const char *hostname);
int dhcp_remove_static_lease(const uint8_t *mac);
DHCPLease *dhcp_find_lease_by_mac(const uint8_t *mac);
DHCPLease *dhcp_find_lease_by_ip(uint32_t ip);
char *dhcp_get_leases_json(void);

// Configuration
int dhcp_load_config(void);
int dhcp_save_config(void);
char *dhcp_get_settings_json(void);
int dhcp_set_settings(uint32_t range_start, uint32_t range_end,
                      uint32_t subnet_mask, uint32_t gateway,
                      uint32_t dns_server, uint32_t lease_time);

// Utility functions
int mac_str_to_bytes(const char *mac_str, uint8_t *mac);
void mac_bytes_to_str(const uint8_t *mac, char *mac_str);
uint32_t ip_str_to_uint(const char *ip_str);
void ip_uint_to_str(uint32_t ip, char *ip_str);

// DHCP server thread
void *dhcp_server_thread(void *arg);

#endif // DHCP_SERVER_H
