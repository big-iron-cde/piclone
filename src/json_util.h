#ifndef JSON_UTIL_H
#define JSON_UTIL_H

#include "cJSON.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define HW_API_VERSION 1

/* Max raw bytes per upload_rom chunk (must match romulan/protocol_v1.py). */
#define UPLOAD_CHUNK_RAW_MAX 0x8000

bool json_parse_frame(const char *text, cJSON **out_root);
bool json_check_version(cJSON *root, const char *req_id);

cJSON *json_error_response(const char *req_id, const char *error, const char *detail);

bool json_send_object(cJSON *obj);
bool json_send_error(const char *req_id, const char *error, const char *detail);

const char *json_get_string(cJSON *root, const char *key);
bool json_get_bool(cJSON *root, const char *key, bool default_val);
uint32_t json_get_uint(cJSON *root, const char *key, uint32_t default_val);
float json_get_float(cJSON *root, const char *key, float default_val);

const char *json_id_from_root(cJSON *root);

void json_attach_id(cJSON *resp, const char *req_id);

/* Decode base64 into dst; returns bytes written or -1 on error. */
int json_base64_decode(const char *src, uint8_t *dst, size_t dst_max);

#endif
