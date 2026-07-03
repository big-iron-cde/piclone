#include "json_util.h"
#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *json_id_from_root(cJSON *root) {
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (cJSON_IsString(id) && id->valuestring) {
        return id->valuestring;
    }
    return NULL;
}

bool json_parse_frame(const char *text, cJSON **out_root) {
    cJSON *root = cJSON_Parse(text);
    if (!root) {
        *out_root = NULL;
        return false;
    }
    *out_root = root;
    return true;
}

bool json_check_version(cJSON *root, const char *req_id) {
    (void)req_id;
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "v");
    if (!cJSON_IsNumber(v) || v->valueint != HW_API_VERSION) {
        return false;
    }
    return true;
}

cJSON *json_error_response(const char *req_id, const char *error, const char *detail) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "v", HW_API_VERSION);
    if (req_id) {
        cJSON_AddStringToObject(obj, "id", req_id);
    }
    cJSON_AddBoolToObject(obj, "ok", false);
    cJSON_AddStringToObject(obj, "error", error);
    if (detail) {
        cJSON_AddStringToObject(obj, "detail", detail);
    }
    return obj;
}

bool json_send_object(cJSON *obj) {
    char *text = cJSON_PrintUnformatted(obj);
    if (!text) {
        return false;
    }
    bool ok = proto_send_frame((const uint8_t *)text, strlen(text));
    cJSON_free(text);
    return ok;
}

bool json_send_error(const char *req_id, const char *error, const char *detail) {
    cJSON *obj = json_error_response(req_id, error, detail);
    bool ok = json_send_object(obj);
    cJSON_Delete(obj);
    return ok;
}

const char *json_get_string(cJSON *root, const char *key) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item) && item->valuestring) {
        return item->valuestring;
    }
    return NULL;
}

bool json_get_bool(cJSON *root, const char *key, bool default_val) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return default_val;
}

uint32_t json_get_uint(cJSON *root, const char *key, uint32_t default_val) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNumber(item) && item->valueint >= 0) {
        return (uint32_t)item->valueint;
    }
    return default_val;
}

void json_attach_id(cJSON *resp, const char *req_id) {
    if (req_id) {
        cJSON_AddStringToObject(resp, "id", req_id);
    }
}

static int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int json_base64_decode(const char *src, uint8_t *dst, size_t dst_max) {
    size_t out_len = 0;
    uint32_t acc = 0;
    int bits = 0;

    for (const char *p = src; *p; p++) {
        if (*p == '=') {
            break;
        }
        int v = b64_value(*p);
        if (v < 0) {
            continue;
        }
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out_len >= dst_max) {
                return -1;
            }
            dst[out_len++] = (uint8_t)((acc >> bits) & 0xFFu);
        }
    }
    return (int)out_len;
}
