#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ldns/ldns.h>

#include "dnssecHandler.h"

// ---------------------------------------------------------------------------
// DNSSEC state — thread-safe via mutex
// ---------------------------------------------------------------------------
static int dnssec_enabled = 0;
static pthread_mutex_t dnssec_lock = PTHREAD_MUTEX_INITIALIZER;

static uint32_t dnssec_validated = 0;
static uint32_t dnssec_failed = 0;
static pthread_mutex_t dnssec_stats_lock = PTHREAD_MUTEX_INITIALIZER;

#define DNSSEC_CONFIG_FILE "adlists/metadata/dnssec_config.txt"

// ---------------------------------------------------------------------------
// Config persistence
// ---------------------------------------------------------------------------

int dnssec_init() {
  FILE *f = fopen(DNSSEC_CONFIG_FILE, "r");
  if (!f) {
    // No config file yet — default to disabled, create the file
    dnssec_enabled = 0;
    return dnssec_save_config();
  }

  char line[64];
  if (fgets(line, sizeof(line), f)) {
    char key[32];
    int val = 0;
    if (sscanf(line, "%31s %d", key, &val) == 2 &&
        strcmp(key, "DNSSEC_ENABLED") == 0) {
      dnssec_enabled = (val != 0) ? 1 : 0;
    }
  }
  fclose(f);
  printf("DNSSEC validation: %s\n", dnssec_enabled ? "enabled" : "disabled");
  return 0;
}

int dnssec_save_config() {
  pthread_mutex_lock(&dnssec_lock);
  FILE *f = fopen(DNSSEC_CONFIG_FILE, "w");
  if (!f) {
    perror("Failed to save DNSSEC config");
    pthread_mutex_unlock(&dnssec_lock);
    return -1;
  }
  fprintf(f, "DNSSEC_ENABLED %d\n", dnssec_enabled);
  fclose(f);
  pthread_mutex_unlock(&dnssec_lock);
  return 0;
}

// ---------------------------------------------------------------------------
// Enable / disable
// ---------------------------------------------------------------------------

int dnssec_is_enabled() {
  pthread_mutex_lock(&dnssec_lock);
  int val = dnssec_enabled;
  pthread_mutex_unlock(&dnssec_lock);
  return val;
}

void dnssec_set_enabled(int enabled) {
  pthread_mutex_lock(&dnssec_lock);
  dnssec_enabled = enabled ? 1 : 0;
  pthread_mutex_unlock(&dnssec_lock);
  dnssec_save_config();
  printf("DNSSEC validation %s\n", enabled ? "enabled" : "disabled");
}

// ---------------------------------------------------------------------------
// Query preparation — add EDNS DO bit + CD bit
// ---------------------------------------------------------------------------

void dnssec_prepare_query(ldns_pkt *query_pkt) {
  if (!query_pkt)
    return;

  // Set the DNSSEC OK (DO) bit — tells upstream we want DNSSEC records
  ldns_pkt_set_edns_do(query_pkt, true);

  // Do NOT set CD (Checking Disabled) — we WANT the upstream to validate
  // and set the AD flag. This is the Pi-hole approach:
  //   DO=1, CD=0 → upstream validates and sets AD=1 if chain-of-trust is good
  //                 upstream returns SERVFAIL if validation fails
  ldns_pkt_set_cd(query_pkt, false);

  // Ensure EDNS0 is present with a buffer size large enough for DNSSEC
  // responses (RRSIG, DNSKEY, etc. make responses larger)
  ldns_pkt_set_edns_udp_size(query_pkt, 4096);
}

// ---------------------------------------------------------------------------
// Response validation
// ---------------------------------------------------------------------------

// Check if the answer section contains any DNSSEC-related RR types,
// indicating this is a DNSSEC-signed domain.
static int response_has_dnssec_records(ldns_pkt *response_pkt) {
  ldns_rr_list *answer = ldns_pkt_answer(response_pkt);
  if (!answer)
    return 0;

  for (size_t i = 0; i < ldns_rr_list_rr_count(answer); i++) {
    ldns_rr *rr = ldns_rr_list_rr(answer, i);
    ldns_rr_type type = ldns_rr_get_type(rr);
    if (type == LDNS_RR_TYPE_RRSIG || type == LDNS_RR_TYPE_DNSKEY ||
        type == LDNS_RR_TYPE_DS || type == LDNS_RR_TYPE_NSEC ||
        type == LDNS_RR_TYPE_NSEC3) {
      return 1;
    }
  }

  // Also check the authority section for NSEC/NSEC3/RRSIG
  ldns_rr_list *authority = ldns_pkt_authority(response_pkt);
  if (authority) {
    for (size_t i = 0; i < ldns_rr_list_rr_count(authority); i++) {
      ldns_rr *rr = ldns_rr_list_rr(authority, i);
      ldns_rr_type type = ldns_rr_get_type(rr);
      if (type == LDNS_RR_TYPE_RRSIG || type == LDNS_RR_TYPE_NSEC ||
          type == LDNS_RR_TYPE_NSEC3) {
        return 1;
      }
    }
  }

  return 0;
}

int dnssec_validate_response(ldns_pkt *response_pkt) {
  if (!response_pkt)
    return 0;

  // If upstream returned SERVFAIL, it may have failed its own validation
  if (ldns_pkt_get_rcode(response_pkt) == LDNS_RCODE_SERVFAIL) {
    fprintf(stderr, "DNSSEC: Upstream returned SERVFAIL (possible validation "
                    "failure)\n");
    dnssec_add_failed();
    return 0;
  }

  // Check for the AD (Authenticated Data) flag.
  // If set, the upstream resolver has validated the DNSSEC chain of trust.
  if (ldns_pkt_ad(response_pkt)) {
    dnssec_add_validated();
    return 1; // DNSSEC validated by upstream
  }

  // AD flag is NOT set. This can mean:
  //   1. The domain is not DNSSEC-signed (most common) — this is fine
  //   2. The upstream doesn't support DNSSEC — also fine, degrade gracefully
  //   3. The upstream couldn't validate it — concerning but we can't tell
  //
  // Strategy: If the response contains DNSSEC records (RRSIG, etc.) but
  // AD is not set, that's suspicious — the upstream had the data but didn't
  // validate it. However, we still allow it through because:
  //   - The upstream might have CD set (checking disabled)
  //   - We set CD=1 ourselves, which means the upstream won't set AD
  //
  // In proxy mode with CD=1, we actually need a different approach:
  // Since WE set CD=1, the upstream will NOT set AD. Instead, the upstream
  // returns the full DNSSEC data and we simply need to check that:
  //   1. RCODE is NOERROR (or NXDOMAIN)
  //   2. If DNSSEC records are present, the response wasn't tampered with
  //      (the upstream did see valid signatures even if it didn't set AD)
  //
  // For a simpler and more correct proxy approach, let's NOT set CD=1.
  // Instead, we only set the DO bit, and rely on the upstream's AD flag.
  // This is exactly what Pi-hole does.
  //
  // With DO=1 and CD=0:
  //   - If domain is DNSSEC-signed and valid → upstream sets AD=1
  //   - If domain is DNSSEC-signed but INVALID → upstream returns SERVFAIL
  //   - If domain is NOT DNSSEC-signed → upstream returns normally, AD=0
  //   - If upstream doesn't support DNSSEC → AD always 0, no SERVFAIL
  //
  // So: AD=0 with RCODE=NOERROR is perfectly fine — domain just isn't signed
  // or upstream doesn't support DNSSEC. Let it through.

  // No AD flag, no SERVFAIL — this is a normal non-DNSSEC response.
  // Allow it through without counting as validated or failed.
  return 1;
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------

void dnssec_add_validated() {
  pthread_mutex_lock(&dnssec_stats_lock);
  dnssec_validated++;
  pthread_mutex_unlock(&dnssec_stats_lock);
}

void dnssec_add_failed() {
  pthread_mutex_lock(&dnssec_stats_lock);
  dnssec_failed++;
  pthread_mutex_unlock(&dnssec_stats_lock);
}

uint32_t dnssec_get_validated_count() {
  pthread_mutex_lock(&dnssec_stats_lock);
  uint32_t val = dnssec_validated;
  pthread_mutex_unlock(&dnssec_stats_lock);
  return val;
}

uint32_t dnssec_get_failed_count() {
  pthread_mutex_lock(&dnssec_stats_lock);
  uint32_t val = dnssec_failed;
  pthread_mutex_unlock(&dnssec_stats_lock);
  return val;
}
