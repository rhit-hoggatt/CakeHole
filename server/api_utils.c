#include "api_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


enum MHD_Result send_json_response(struct MHD_Connection *connection,
                                   const char *json_str, int status_code) {
  struct MHD_Response *response = MHD_create_response_from_buffer(
      strlen(json_str), (void *)json_str, MHD_RESPMEM_MUST_COPY);
  if (!response) {
    return MHD_NO;
  }
  MHD_add_response_header(response, "Content-Type", "application/json");
  enum MHD_Result ret = MHD_queue_response(connection, status_code, response);
  MHD_destroy_response(response);
  return ret;
}

enum MHD_Result send_error_response(struct MHD_Connection *connection,
                                    const char *message, int status_code) {
  cJSON *json = cJSON_CreateObject();
  cJSON_AddStringToObject(json, "error", message);
  char *json_str = cJSON_PrintUnformatted(json);
  enum MHD_Result ret = send_json_response(connection, json_str, status_code);
  free(json_str);
  cJSON_Delete(json);
  return ret;
}

enum MHD_Result send_success_response(struct MHD_Connection *connection,
                                      const char *message) {
  cJSON *json = cJSON_CreateObject();
  cJSON_AddStringToObject(json, "status", message);
  char *json_str = cJSON_PrintUnformatted(json);
  enum MHD_Result ret = send_json_response(connection, json_str, MHD_HTTP_OK);
  free(json_str);
  cJSON_Delete(json);
  return ret;
}
