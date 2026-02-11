#ifndef API_UTILS_H
#define API_UTILS_H

#include "cJSON.h"
#include <microhttpd.h>

// Helper struct for POST data
struct PostData {
  char *data;
  size_t size;
};

// Helper to send JSON response
enum MHD_Result send_json_response(struct MHD_Connection *connection,
                                   const char *json_str, int status_code);

// Helper to send error response
enum MHD_Result send_error_response(struct MHD_Connection *connection,
                                    const char *message, int status_code);

// Helper to send success response
enum MHD_Result send_success_response(struct MHD_Connection *connection,
                                      const char *message);

#endif
