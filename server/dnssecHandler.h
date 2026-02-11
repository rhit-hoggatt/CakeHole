#ifndef DNSSEC_HANDLER_H
#define DNSSEC_HANDLER_H

#include <ldns/ldns.h>

// Initialize DNSSEC subsystem — loads config from disk
int dnssec_init();

// Check if DNSSEC validation is currently enabled
int dnssec_is_enabled();

// Enable or disable DNSSEC validation (thread-safe)
void dnssec_set_enabled(int enabled);

// Save current DNSSEC config to disk
int dnssec_save_config();

// Validate an upstream DNS response using the AD flag.
// Returns 1 if the response is acceptable, 0 if it should be rejected.
// For non-DNSSEC domains (no RRSIG/DNSKEY in answer), always returns 1.
int dnssec_validate_response(ldns_pkt *response_pkt);

// Prepare a query packet for DNSSEC-aware forwarding.
// Sets the DO bit, CD bit, and ensures EDNS is present.
void dnssec_prepare_query(ldns_pkt *query_pkt);

// Stats
void dnssec_add_validated();
void dnssec_add_failed();
uint32_t dnssec_get_validated_count();
uint32_t dnssec_get_failed_count();

#endif // DNSSEC_HANDLER_H
