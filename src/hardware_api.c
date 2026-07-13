#include "hardware_api.h"
#include "protocol.h"
#include "json_util.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include <stdio.h>
#include <string.h>

#define STP_OPCODE 0xDB
#define DATA_MASK  (0xFFu << 15)

static hw_context_t hw;
static uint16_t last_addr;
static uint8_t last_data;
static uint8_t last_rw_report; /* 0=read, 1=write (protocol) */
static bool read_active;
static bool monitor_enabled;
static bool reset_asserted;
static uint32_t read_max_cycles;
static uint32_t read_cycle_count;

/* Deferred capture TX — filled on PHI2 edge, sent from hardware_api_poll(). */
static bool pending_cycle;
static bool pending_done;
static uint16_t pending_addr;
static uint8_t pending_data;
static uint8_t pending_rw; /* protocol: 0 = read, 1 = write */
static uint32_t pending_seq;
static bool pending_done_ok;
static const char *pending_done_reason;

/* upload_rom state */
static bool upload_active;
static uint32_t upload_expected;
static uint32_t upload_received;
static uint32_t upload_next_offset;

static void upload_reset_state(void) {
    upload_active = false;
    upload_expected = 0;
    upload_received = 0;
    upload_next_offset = 0;
}

static bool upload_is_complete(void) {
    return upload_received >= hw.rom_size;
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

        bool was_active = *hw.rom_active;
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
        (void)was_active;
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
        hw.reset_release();
        reset_asserted = false;
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
        json_send_object(resp);
        cJSON_Delete(resp);
        return;
    }

    json_send_error(req_id, "bad_action", "unknown upload_rom action");
}

static void queue_cycle_sample(uint16_t addr, uint8_t data, uint8_t rw_report) {
    if (pending_cycle || pending_done) {
        return;
    }

    read_cycle_count++;
    pending_addr = addr;
    pending_data = data;
    pending_rw = rw_report;
    pending_seq = read_cycle_count;
    pending_cycle = true;

    bool stp_fetch = (rw_report == 0u && data == STP_OPCODE);
    bool limit = (read_cycle_count >= read_max_cycles);
    if (stp_fetch || limit) {
        read_active = false;
        pending_done_ok = true;
        pending_done_reason = stp_fetch ? "stp" : "max_cycles";
        pending_done = true;
    }
}

static void cmd_read(cJSON *root, const char *req_id) {
    const char *until = json_get_string(root, "until");
    if (!until || strcmp(until, "stp") != 0) {
        json_send_error(req_id, "unsupported_until", "only until=stp supported");
        return;
    }

    read_max_cycles = json_get_uint(root, "max_cycles", 10000);
    if (read_max_cycles == 0) {
        read_max_cycles = 10000;
    }
    read_cycle_count = 0;
    pending_cycle = false;
    pending_done = false;
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

    /*
     * Queue a cycle from the last bus sample so the next hardware_api_poll()
     * (main loop, after this command returns) sends ENQ without waiting for
     * the next PHI2 edge (~5 s at 0.2 Hz).
     */
    queue_cycle_sample(last_addr, last_data, last_rw_report);
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

void hardware_api_init(const hw_context_t *ctx) {
    hw = *ctx;
    last_addr = 0;
    last_data = 0;
    last_rw_report = 0;
    read_active = false;
    monitor_enabled = false;
    reset_asserted = false;
    pending_cycle = false;
    pending_done = false;
    upload_reset_state();
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

    if (strcmp(cmd, "read") != 0) {
        read_active = false;
        pending_cycle = false;
        pending_done = false;
    }

    if (strcmp(cmd, "reset") == 0) {
        cmd_reset(root, req_id);
    } else if (strcmp(cmd, "upload_rom") == 0) {
        cmd_upload_rom(root, req_id);
    } else if (strcmp(cmd, "read") == 0) {
        cmd_read(root, req_id);
    } else if (strcmp(cmd, "request_addr") == 0) {
        cmd_request_addr(req_id);
    } else if (strcmp(cmd, "monitor") == 0) {
        cmd_monitor(root, req_id);
    } else if (strcmp(cmd, "status") == 0) {
        cmd_status(req_id);
    } else {
        json_send_error(req_id, "unknown_cmd", cmd);
    }

    cJSON_Delete(root);
}

static void send_read_event_done(bool ok, const char *reason, uint16_t addr) {
    char addr_s[8];
    snprintf(addr_s, sizeof(addr_s), "%04X", addr);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
    cJSON_AddStringToObject(resp, "type", "event");
    cJSON_AddStringToObject(resp, "event", "done");
    cJSON_AddBoolToObject(resp, "ok", ok);
    cJSON_AddStringToObject(resp, "reason", reason);
    cJSON_AddNumberToObject(resp, "cycles", (double)read_cycle_count);
    cJSON_AddStringToObject(resp, "addr", addr_s);
    json_send_object(resp);
    cJSON_Delete(resp);
}

static bool send_read_event_cycle(uint32_t seq, uint16_t addr, uint8_t data, uint8_t rw) {
    char addr_s[8];
    char data_s[4];
    snprintf(addr_s, sizeof(addr_s), "%04X", addr);
    snprintf(data_s, sizeof(data_s), "%02X", data);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
    cJSON_AddStringToObject(resp, "type", "event");
    cJSON_AddStringToObject(resp, "event", "cycle");
    cJSON_AddNumberToObject(resp, "seq", (double)seq);
    cJSON_AddStringToObject(resp, "addr", addr_s);
    cJSON_AddStringToObject(resp, "data", data_s);
    cJSON_AddNumberToObject(resp, "rw", (double)rw);

    bool ok = json_send_object(resp);
    cJSON_Delete(resp);
    return ok;
}

void hardware_api_on_bus_cycle(uint16_t addr, uint8_t data, bool rwb_pin) {
    /* Protocol / docs: RWB high → read → rw=0; RWB low → write → rw=1. */
    uint8_t rw_report = rwb_pin ? 0u : 1u;

    last_addr = addr;
    last_data = data;
    last_rw_report = rw_report;

    if (!read_active) {
        return;
    }
    queue_cycle_sample(addr, data, rw_report);
}

void hardware_api_poll(void) {
    if (pending_cycle) {
        if (!send_read_event_cycle(pending_seq, pending_addr, pending_data, pending_rw)) {
            pending_cycle = false;
            read_active = false;
            pending_done_ok = false;
            pending_done_reason = "host_nack";
            pending_done = true;
        } else {
            pending_cycle = false;
        }
    }

    if (pending_done) {
        send_read_event_done(pending_done_ok, pending_done_reason, pending_addr);
        pending_done = false;
    }
}

uint16_t hardware_api_last_addr(void) {
    return last_addr;
}

bool hardware_api_is_reading(void) {
    return read_active || pending_cycle || pending_done;
}

bool hardware_api_monitor_enabled(void) {
    return monitor_enabled;
}
