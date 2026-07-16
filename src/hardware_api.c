#include "hardware_api.h"
#include "protocol.h"
#include "json_util.h"
#include "phi2.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STP_OPCODE 0xDB
#define DATA_MASK  (0xFFu << 15)
#define CAPTURE_QUEUE_DEPTH 256

static hw_context_t hw;
static uint16_t last_addr;
static uint8_t last_data;
static uint8_t last_rw_report; /* 0=read, 1=write (protocol) */
static bool read_active;
static bool monitor_enabled;
static bool reset_asserted;
static uint32_t read_max_cycles;
static uint32_t read_cycle_count;

typedef struct {
    uint16_t addr;
    uint8_t data;
    uint8_t rw; /* protocol: 0 = read, 1 = write */
    uint32_t seq;
} capture_sample_t;

static capture_sample_t capture_queue[CAPTURE_QUEUE_DEPTH];
static uint16_t capture_q_head;
static uint16_t capture_q_tail;
static uint16_t capture_q_count;
static bool pending_done;
static bool pending_done_ok;
static const char *pending_done_reason;
static uint16_t pending_done_addr;

/* upload_rom state */
static bool upload_active;
static uint32_t upload_expected;
static uint32_t upload_received;
static uint32_t upload_next_offset;

/* Live RAM/bus peek (temporary LDA abs / STP stub). */
static bool     peek_active;
static uint16_t peek_target;
static bool     peek_seen_data;
static uint8_t  peek_data;
static bool     peek_done_stp;

static void capture_queue_clear(void) {
    capture_q_head = 0;
    capture_q_tail = 0;
    capture_q_count = 0;
    pending_done = false;
    pending_done_ok = false;
    pending_done_reason = NULL;
    pending_done_addr = 0;
}

static bool capture_queue_push(uint16_t addr, uint8_t data, uint8_t rw, uint32_t seq) {
    if (capture_q_count >= CAPTURE_QUEUE_DEPTH) {
        return false;
    }
    capture_queue[capture_q_tail].addr = addr;
    capture_queue[capture_q_tail].data = data;
    capture_queue[capture_q_tail].rw = rw;
    capture_queue[capture_q_tail].seq = seq;
    capture_q_tail = (uint16_t)((capture_q_tail + 1u) % CAPTURE_QUEUE_DEPTH);
    capture_q_count++;
    return true;
}

static bool capture_queue_pop(capture_sample_t *out) {
    if (capture_q_count == 0) {
        return false;
    }
    *out = capture_queue[capture_q_head];
    capture_q_head = (uint16_t)((capture_q_head + 1u) % CAPTURE_QUEUE_DEPTH);
    capture_q_count--;
    return true;
}

static void upload_reset_state(void) {
    upload_active = false;
    upload_expected = 0;
    upload_received = 0;
    upload_next_offset = 0;
}

static bool upload_is_complete(void) {
    return upload_received >= hw.rom_size;
}

static void upload_abort_restore(void) {
    upload_reset_state();
    *hw.rom_active = true;
    hw.reset_assert();
    reset_asserted = true;
    gpio_set_dir_in_masked(DATA_MASK);
}

static void cmd_reset(cJSON *root, const char *req_id) {
    bool assert_reset = json_get_bool(root, "assert", true);
    if (assert_reset) {
        hw.reset_assert();
        reset_asserted = true;
    } else {
        hw.reset_release();
        reset_asserted = false;
    }
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
    json_attach_id(resp, req_id);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "cmd", "reset");
    cJSON_AddBoolToObject(resp, "asserted", assert_reset);
    json_send_object(resp);
    cJSON_Delete(resp);
}

static void cmd_upload_rom(cJSON *root, const char *req_id) {
    const char *action = json_get_string(root, "action");
    if (!action) {
        json_send_error(req_id, "bad_request", "upload_rom requires action");
        return;
    }

    if (strcmp(action, "abort") == 0) {
        upload_abort_restore();
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
        json_attach_id(resp, req_id);
        cJSON_AddBoolToObject(resp, "ok", true);
        cJSON_AddStringToObject(resp, "cmd", "upload_rom");
        cJSON_AddStringToObject(resp, "action", "abort");
        cJSON_AddBoolToObject(resp, "reset_asserted", true);
        json_send_object(resp);
        cJSON_Delete(resp);
        return;
    }

    if (strcmp(action, "begin") == 0) {
        uint32_t size = json_get_uint(root, "size", 0);
        if (size != hw.rom_size) {
            json_send_error(req_id, "bad_size", "size must be 32768");
            return;
        }

        upload_reset_state();
        upload_active = true;
        upload_expected = size;
        upload_next_offset = 0;
        upload_received = 0;

        *hw.rom_active = false;
        gpio_set_dir_in_masked(DATA_MASK);
        hw.reset_assert();
        reset_asserted = true;

        cJSON *resp = cJSON_CreateObject();
        cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
        json_attach_id(resp, req_id);
        cJSON_AddBoolToObject(resp, "ok", true);
        cJSON_AddStringToObject(resp, "cmd", "upload_rom");
        cJSON_AddStringToObject(resp, "action", "begin");
        cJSON_AddNumberToObject(resp, "received", 0);
        cJSON_AddNumberToObject(resp, "expected", (double)upload_expected);
        json_send_object(resp);
        cJSON_Delete(resp);
        return;
    }

    if (!upload_active) {
        json_send_error(req_id, "upload_inactive", "call upload_rom begin first");
        return;
    }

    if (strcmp(action, "chunk") == 0) {
        uint32_t offset = json_get_uint(root, "offset", UINT32_MAX);
        const char *data_b64 = json_get_string(root, "data");
        if (!data_b64 || offset >= hw.rom_size) {
            json_send_error(req_id, "bad_offset", "invalid offset or missing data");
            return;
        }
        if (offset != upload_next_offset) {
            json_send_error(req_id, "bad_offset", "chunks must be sent in order");
            return;
        }

        size_t max_dec = hw.rom_size - offset;
        if (max_dec > UPLOAD_CHUNK_RAW_MAX) {
            max_dec = UPLOAD_CHUNK_RAW_MAX;
        }

        int decoded = json_base64_decode(data_b64, hw.rom_image + offset, max_dec);
        if (decoded <= 0) {
            json_send_error(req_id, "bad_base64", "could not decode chunk data");
            return;
        }
        if ((uint32_t)decoded > max_dec) {
            json_send_error(req_id, "bad_base64", "chunk too large");
            return;
        }

        upload_next_offset += (uint32_t)decoded;
        upload_received = upload_next_offset;

        cJSON *resp = cJSON_CreateObject();
        cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
        json_attach_id(resp, req_id);
        cJSON_AddBoolToObject(resp, "ok", true);
        cJSON_AddStringToObject(resp, "cmd", "upload_rom");
        cJSON_AddStringToObject(resp, "action", "chunk");
        cJSON_AddNumberToObject(resp, "offset", (double)offset);
        cJSON_AddNumberToObject(resp, "received", (double)upload_received);
        json_send_object(resp);
        cJSON_Delete(resp);
        return;
    }

    if (strcmp(action, "commit") == 0) {
        if (!upload_is_complete()) {
            char detail[64];
            snprintf(detail, sizeof(detail), "received %lu of %lu bytes",
                     (unsigned long)upload_received, (unsigned long)upload_expected);
            json_send_error(req_id, "incomplete", detail);
            return;
        }

        *hw.rom_active = true;
        /* Keep CPU in reset after commit so the host can start capture from $8000. */
        hw.reset_assert();
        reset_asserted = true;
        upload_active = false;

        char rv[8];
        snprintf(rv, sizeof(rv), "%02X%02X",
                 hw.rom_image[0x7FFD], hw.rom_image[0x7FFC]);

        cJSON *resp = cJSON_CreateObject();
        cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
        json_attach_id(resp, req_id);
        cJSON_AddBoolToObject(resp, "ok", true);
        cJSON_AddStringToObject(resp, "cmd", "upload_rom");
        cJSON_AddStringToObject(resp, "action", "commit");
        cJSON_AddNumberToObject(resp, "bytes", (double)hw.rom_size);
        cJSON_AddStringToObject(resp, "reset_vector", rv);
        cJSON_AddBoolToObject(resp, "reset_asserted", true);
        json_send_object(resp);
        cJSON_Delete(resp);
        return;
    }

    json_send_error(req_id, "bad_action", "unknown upload_rom action");
}

static void queue_cycle_sample(uint16_t addr, uint8_t data, uint8_t rw_report) {
    if (!read_active) {
        return;
    }

    read_cycle_count++;
    (void)capture_queue_push(addr, data, rw_report, read_cycle_count);

    bool stp_fetch = (rw_report == 0u && data == STP_OPCODE);
    bool limit = (read_cycle_count >= read_max_cycles);
    if (stp_fetch || limit) {
        read_active = false;
        pending_done_ok = true;
        pending_done_reason = stp_fetch ? "stp" : "max_cycles";
        pending_done_addr = addr;
        pending_done = true;
    }
}

static void cmd_read(cJSON *root, const char *req_id) {
    const char *until = json_get_string(root, "until");
    if (!until || strcmp(until, "stp") != 0) {
        json_send_error(req_id, "unsupported_until", "only until=stp supported");
        return;
    }

    cJSON *phi2_item = cJSON_GetObjectItemCaseSensitive(root, "phi2_hz");
    if (cJSON_IsNumber(phi2_item)) {
        float hz = (float)phi2_item->valuedouble;
        if (hz >= 0.1f && hz <= 1000.0f) {
            phi2_set_hz(hz);
        }
    }

    read_max_cycles = json_get_uint(root, "max_cycles", 10000);
    if (read_max_cycles == 0) {
        read_max_cycles = 10000;
    }
    read_cycle_count = 0;
    capture_queue_clear();
    read_active = true;
    monitor_enabled = false;

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
    json_attach_id(resp, req_id);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "cmd", "read");
    cJSON_AddStringToObject(resp, "until", "stp");
    cJSON_AddNumberToObject(resp, "max_cycles", (double)read_max_cycles);
    json_send_object(resp);
    cJSON_Delete(resp);
}

/*
 * Host-polled capture event. Uses the normal request/response framed path
 * (same as request_addr) instead of unsolicited Pico→host frames.
 */
static void cmd_read_event(const char *req_id) {
    capture_sample_t sample;
    if (capture_queue_pop(&sample)) {
        char addr_s[8];
        char data_s[4];
        snprintf(addr_s, sizeof(addr_s), "%04X", sample.addr);
        snprintf(data_s, sizeof(data_s), "%02X", sample.data);

        cJSON *resp = cJSON_CreateObject();
        cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
        json_attach_id(resp, req_id);
        cJSON_AddBoolToObject(resp, "ok", true);
        cJSON_AddStringToObject(resp, "type", "event");
        cJSON_AddStringToObject(resp, "event", "cycle");
        cJSON_AddNumberToObject(resp, "seq", (double)sample.seq);
        cJSON_AddStringToObject(resp, "addr", addr_s);
        cJSON_AddStringToObject(resp, "data", data_s);
        cJSON_AddNumberToObject(resp, "rw", (double)sample.rw);
        json_send_object(resp);
        cJSON_Delete(resp);
        return;
    }

    if (pending_done) {
        char addr_s[8];
        snprintf(addr_s, sizeof(addr_s), "%04X", pending_done_addr);
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
        json_attach_id(resp, req_id);
        cJSON_AddBoolToObject(resp, "ok", pending_done_ok);
        cJSON_AddStringToObject(resp, "type", "event");
        cJSON_AddStringToObject(resp, "event", "done");
        cJSON_AddStringToObject(resp, "reason", pending_done_reason);
        cJSON_AddNumberToObject(resp, "cycles", (double)read_cycle_count);
        cJSON_AddStringToObject(resp, "addr", addr_s);
        json_send_object(resp);
        cJSON_Delete(resp);
        pending_done = false;
        return;
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
    json_attach_id(resp, req_id);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "event", "none");
    cJSON_AddBoolToObject(resp, "read_active", read_active);
    json_send_object(resp);
    cJSON_Delete(resp);
}

static void cmd_request_addr(const char *req_id) {
    char addr[8];
    snprintf(addr, sizeof(addr), "%04X", last_addr);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
    json_attach_id(resp, req_id);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "cmd", "request_addr");
    cJSON_AddStringToObject(resp, "addr", addr);
    cJSON_AddNumberToObject(resp, "phi2_hz", (double)*hw.current_hz);
    json_send_object(resp);
    cJSON_Delete(resp);
}

static void cmd_monitor(cJSON *root, const char *req_id) {
    monitor_enabled = json_get_bool(root, "enable", true);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
    json_attach_id(resp, req_id);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "cmd", "monitor");
    cJSON_AddBoolToObject(resp, "enable", monitor_enabled);
    json_send_object(resp);
    cJSON_Delete(resp);
}

static void cmd_status(const char *req_id) {
    char addr[8];
    snprintf(addr, sizeof(addr), "%04X", last_addr);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
    json_attach_id(resp, req_id);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "cmd", "status");
    cJSON_AddNumberToObject(resp, "phi2_hz", (double)*hw.current_hz);
    cJSON_AddBoolToObject(resp, "rom_active", *hw.rom_active);
    cJSON_AddBoolToObject(resp, "reset_asserted", reset_asserted);
    cJSON_AddStringToObject(resp, "last_addr", addr);
    cJSON_AddBoolToObject(resp, "read_active", read_active);
    cJSON_AddBoolToObject(resp, "monitor_enabled", monitor_enabled);
    cJSON_AddBoolToObject(resp, "upload_active", upload_active);
    json_send_object(resp);
    cJSON_Delete(resp);
}

/*
 * Live peek: patch LDA abs / STP at $8000, run until STP, return data seen
 * on the bus cycle whose address matches the request (RAM or ROM).
 */
static void cmd_peek(cJSON *root, const char *req_id) {
    if (read_active) {
        json_send_error(req_id, "busy", "capture armed; finish or abort read first");
        return;
    }
    if (upload_active) {
        json_send_error(req_id, "busy", "upload in progress");
        return;
    }
    if (!hw.rom_image || hw.rom_size < 4 || !hw.bus_poll) {
        json_send_error(req_id, "unavailable", "peek requires ROM image and bus_poll");
        return;
    }

    const char *addr_s = json_get_string(root, "addr");
    if (!addr_s || addr_s[0] == '\0') {
        json_send_error(req_id, "bad_request", "missing addr");
        return;
    }
    char *end = NULL;
    unsigned long parsed = strtoul(addr_s, &end, 16);
    if (end == addr_s || parsed > 0xFFFFul) {
        json_send_error(req_id, "bad_request", "addr must be 0000-FFFF hex");
        return;
    }
    uint16_t target = (uint16_t)parsed;

    hw.reset_assert();
    reset_asserted = true;

    uint8_t saved[4];
    memcpy(saved, hw.rom_image, 4);
    hw.rom_image[0] = 0xAD; /* LDA abs */
    hw.rom_image[1] = (uint8_t)(target & 0xFFu);
    hw.rom_image[2] = (uint8_t)((target >> 8) & 0xFFu);
    hw.rom_image[3] = STP_OPCODE;
    *hw.rom_active = true;

    peek_target = target;
    peek_seen_data = false;
    peek_data = 0;
    peek_done_stp = false;
    peek_active = true;

    sleep_ms(2);
    hw.reset_release();
    reset_asserted = false;

    /* ~64 bus cycles at 1 kHz is ~64 ms; allow headroom for USB/idle. */
    absolute_time_t deadline = make_timeout_time_ms(500);
    while (!peek_done_stp && !time_reached(deadline)) {
        hw.bus_poll();
        tight_loop_contents();
    }

    hw.reset_assert();
    reset_asserted = true;
    peek_active = false;
    memcpy(hw.rom_image, saved, 4);

    if (!peek_done_stp) {
        json_send_error(req_id, "timeout", "peek did not reach STP");
        return;
    }
    if (!peek_seen_data) {
        json_send_error(req_id, "no_cycle", "no bus cycle matched addr");
        return;
    }

    char out_addr[8];
    char out_data[4];
    snprintf(out_addr, sizeof(out_addr), "%04X", target);
    snprintf(out_data, sizeof(out_data), "%02X", peek_data);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
    json_attach_id(resp, req_id);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "cmd", "peek");
    cJSON_AddStringToObject(resp, "addr", out_addr);
    cJSON_AddStringToObject(resp, "data", out_data);
    json_send_object(resp);
    cJSON_Delete(resp);
}

void hardware_api_init(const hw_context_t *ctx) {
    hw = *ctx;
    last_addr = 0;
    last_data = 0;
    last_rw_report = 0;
    read_active = false;
    monitor_enabled = false;
    reset_asserted = false;
    capture_queue_clear();
    upload_reset_state();
    peek_active = false;
    peek_seen_data = false;
    peek_done_stp = false;
}

void hardware_api_handle_enq(void) {
    /* Static: PROTO_JSON_MAX is large enough for a full-ROM base64 chunk. */
    static uint8_t buf[PROTO_JSON_MAX];
    size_t len = 0;

    if (!proto_read_frame_payload(buf, sizeof(buf) - 1, &len)) {
        proto_send_nack();
        return;
    }
    buf[len] = '\0';

    cJSON *root = NULL;
    if (!json_parse_frame((char *)buf, &root)) {
        json_send_error(NULL, "parse_error", "invalid JSON");
        return;
    }

    const char *req_id = json_id_from_root(root);

    if (!json_check_version(root, req_id)) {
        json_send_error(req_id, "unsupported_version", "v must be 1");
        cJSON_Delete(root);
        return;
    }

    const char *cmd = json_get_string(root, "cmd");
    if (!cmd) {
        json_send_error(req_id, "bad_request", "missing cmd");
        cJSON_Delete(root);
        return;
    }

    /* Keep an armed capture alive across read_event polls, reset, and
     * non-destructive queries (status / request_addr). */
    if (
        strcmp(cmd, "read") != 0
        && strcmp(cmd, "read_event") != 0
        && strcmp(cmd, "reset") != 0
        && strcmp(cmd, "status") != 0
        && strcmp(cmd, "request_addr") != 0
    ) {
        read_active = false;
        capture_queue_clear();
    }

    if (strcmp(cmd, "reset") == 0) {
        cmd_reset(root, req_id);
    } else if (strcmp(cmd, "upload_rom") == 0) {
        cmd_upload_rom(root, req_id);
    } else if (strcmp(cmd, "read") == 0) {
        cmd_read(root, req_id);
    } else if (strcmp(cmd, "read_event") == 0) {
        cmd_read_event(req_id);
    } else if (strcmp(cmd, "request_addr") == 0) {
        cmd_request_addr(req_id);
    } else if (strcmp(cmd, "monitor") == 0) {
        cmd_monitor(root, req_id);
    } else if (strcmp(cmd, "status") == 0) {
        cmd_status(req_id);
    } else if (strcmp(cmd, "peek") == 0) {
        cmd_peek(root, req_id);
    } else {
        json_send_error(req_id, "unknown_cmd", cmd);
    }

    cJSON_Delete(root);
}

void hardware_api_on_bus_cycle(uint16_t addr, uint8_t data, bool rwb_pin) {
    /* rwb_pin is read-high sense (not necessarily GPIO GP23). On this board
     * main.c passes A15: ROM reads (high) → rw=0; RAM writes (low) → rw=1. */
    uint8_t rw_report = rwb_pin ? 0u : 1u;

    last_addr = addr;
    last_data = data;
    last_rw_report = rw_report;

    if (peek_active) {
        if (addr == peek_target) {
            peek_data = data;
            peek_seen_data = true;
        }
        /* STP opcode fetch from ROM (A15=1). */
        if (rwb_pin && data == STP_OPCODE && (addr & 0x8000u)) {
            peek_done_stp = true;
        }
        return;
    }

    if (!read_active) {
        return;
    }
    queue_cycle_sample(addr, data, rw_report);
}

void hardware_api_poll(void) {
    /* Capture events are returned via host-polled `read_event` commands. */
}

uint16_t hardware_api_last_addr(void) {
    return last_addr;
}

bool hardware_api_is_reading(void) {
    return peek_active || read_active || capture_q_count > 0 || pending_done;
}

bool hardware_api_monitor_enabled(void) {
    return monitor_enabled;
}
