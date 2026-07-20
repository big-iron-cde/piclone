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
#define PIN_D_FIRST 15
#define CAPTURE_QUEUE_DEPTH 256
#define READ_EVENT_BATCH_MAX 64

/* Pins exposed in status and used by the drive diagnostic. */
#define STATUS_PIN_RWB  23
#define STATUS_PIN_A15  26
#define STATUS_PIN_RESB 27
#define STATUS_PIN_PHI2 28

static hw_context_t hw;
static uint16_t last_addr;
static uint8_t last_data;
static uint8_t last_rw_report; /* 0=read, 1=write (protocol) */
static bool read_active;
static bool monitor_enabled;
static bool reset_asserted;
/* Set while a framed command exchange is in progress (ENQ seen until the
 * response has been sent). Checked by main.c to keep JSON monitor output
 * from interleaving into response frames. */
static bool exchange_active;
static uint32_t read_max_cycles;
static uint32_t read_cycle_count;
static uint16_t read_batch_size;

/* Drive diagnostic state. */
static bool drive_enabled;
static uint8_t drive_value;

static void drive_release(void) {
    drive_enabled = false;
    drive_value = 0;
    gpio_set_dir_in_masked(DATA_MASK);
}

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
        drive_release();
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

        drive_release();
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

    read_batch_size = (uint16_t)json_get_uint(root, "batch_size", 1);
    if (read_batch_size < 1) {
        read_batch_size = 1;
    }
    if (read_batch_size > READ_EVENT_BATCH_MAX) {
        read_batch_size = READ_EVENT_BATCH_MAX;
    }

    /* Drive must not force the bus during a capture. */
    drive_release();

    bool release_reset = json_get_bool(root, "release_reset", true);

    if (release_reset) {
        hw.reset_assert();
        reset_asserted = true;
    }

    read_cycle_count = 0;
    capture_queue_clear();
    read_active = true;
    monitor_enabled = false;

    if (release_reset) {
        hw.reset_release();
        reset_asserted = false;
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
    json_attach_id(resp, req_id);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "cmd", "read");
    cJSON_AddStringToObject(resp, "until", "stp");
    cJSON_AddNumberToObject(resp, "max_cycles", (double)read_max_cycles);
    cJSON_AddNumberToObject(resp, "batch_size", (double)read_batch_size);
    cJSON_AddBoolToObject(resp, "release_reset", release_reset);
    json_send_object(resp);
    cJSON_Delete(resp);
}

/*
 * Host-polled capture event. Uses the normal request/response framed path
 * (same as request_addr) instead of unsolicited Pico→host frames.
 */
static void cmd_clock(cJSON *root, const char *req_id) {
    cJSON *hz_item = cJSON_GetObjectItemCaseSensitive(root, "hz");
    if (!cJSON_IsNumber(hz_item)) {
        json_send_error(req_id, "bad_request", "hz must be a number");
        return;
    }

    float hz = (float)hz_item->valuedouble;
    if (hz < 0.1f || hz > 1000.0f) {
        json_send_error(req_id, "bad_request", "hz must be between 0.1 and 1000.0");
        return;
    }

    phi2_set_hz(hz);
    float actual_hz = *hw.current_hz;

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
    json_attach_id(resp, req_id);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "cmd", "clock");
    cJSON_AddNumberToObject(resp, "hz", (double)actual_hz);
    json_send_object(resp);
    cJSON_Delete(resp);
}

/* Build and send a single legacy ``cycle`` event. */
static void send_cycle_event(const capture_sample_t *sample, const char *req_id) {
    char addr_s[8];
    char data_s[4];
    snprintf(addr_s, sizeof(addr_s), "%04X", sample->addr);
    snprintf(data_s, sizeof(data_s), "%02X", sample->data);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
    json_attach_id(resp, req_id);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "type", "event");
    cJSON_AddStringToObject(resp, "event", "cycle");
    cJSON_AddNumberToObject(resp, "seq", (double)sample->seq);
    cJSON_AddStringToObject(resp, "addr", addr_s);
    cJSON_AddStringToObject(resp, "data", data_s);
    cJSON_AddNumberToObject(resp, "rw", (double)sample->rw);
    json_send_object(resp);
    cJSON_Delete(resp);
}

/* Build and send a batched ``cycles`` event. */
static void send_cycles_event(const capture_sample_t *samples, uint16_t count, const char *req_id) {
    cJSON *resp = cJSON_CreateObject();
    cJSON *cycles = cJSON_CreateArray();

    for (uint16_t i = 0; i < count; i++) {
        char addr_s[8];
        char data_s[4];
        snprintf(addr_s, sizeof(addr_s), "%04X", samples[i].addr);
        snprintf(data_s, sizeof(data_s), "%02X", samples[i].data);

        cJSON *cycle = cJSON_CreateObject();
        cJSON_AddNumberToObject(cycle, "seq", (double)samples[i].seq);
        cJSON_AddStringToObject(cycle, "addr", addr_s);
        cJSON_AddStringToObject(cycle, "data", data_s);
        cJSON_AddNumberToObject(cycle, "rw", (double)samples[i].rw);
        cJSON_AddItemToArray(cycles, cycle);
    }

    cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
    json_attach_id(resp, req_id);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "type", "event");
    cJSON_AddStringToObject(resp, "event", "cycles");
    cJSON_AddItemToObject(resp, "cycles", cycles);
    json_send_object(resp);
    cJSON_Delete(resp);
}

static void cmd_read_event(cJSON *root, const char *req_id) {
    bool batch_requested = cJSON_HasObjectItem(root, "batch_size");
    uint16_t batch_size = (uint16_t)json_get_uint(root, "batch_size", read_batch_size);
    if (batch_size < 1) {
        batch_size = 1;
    }
    if (batch_size > READ_EVENT_BATCH_MAX) {
        batch_size = READ_EVENT_BATCH_MAX;
    }

    capture_sample_t sample;
    if (!batch_requested && capture_queue_pop(&sample)) {
        /* Legacy one-cycle-per-poll behavior for hosts that don't send batch_size. */
        send_cycle_event(&sample, req_id);
        return;
    }

    if (batch_requested) {
        capture_sample_t batch_samples[READ_EVENT_BATCH_MAX];
        uint16_t count = 0;
        while (count < batch_size && capture_queue_pop(&batch_samples[count])) {
            count++;
        }
        if (count > 0) {
            send_cycles_event(batch_samples, count, req_id);
            return;
        }
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
    cJSON_AddStringToObject(resp, "type", "event");
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

#define PEEK_MAX_COUNT 64
#define LIVE_PEEK_TIMEOUT_MS 1000
#define LIVE_PEEK_MAX_CYCLES 64
#define LIVE_PEEK_HZ 1000.0f

/* Parse a hex-string field (e.g. "800C" or "0x800C") into a uint16_t. */
static bool json_get_hex16(cJSON *root, const char *key, uint16_t *out) {
    const char *s = json_get_string(root, key);
    if (!s) {
        return false;
    }
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 16);
    if (end == s || *end != '\0' || v > 0xFFFFul) {
        return false;
    }
    *out = (uint16_t)v;
    return true;
}

/* Live peek: patch ``LDA $addr`` / ``STP`` at ``$8000`` (and point the reset
 * vector there), run the CPU, and sample the bus cycle that reads ``addr``.
 * ROM bytes, clock speed, and capture state are all restored afterwards;
 * the CPU is left held in reset. Returns true with the sampled byte. */
static bool live_peek_run(uint16_t addr, uint8_t *out_data) {
    uint8_t saved[6];
    memcpy(saved, hw.rom_image, 4);
    saved[4] = hw.rom_image[0x7FFC];
    saved[5] = hw.rom_image[0x7FFD];

    hw.rom_image[0] = 0xAD;                    /* LDA absolute */
    hw.rom_image[1] = (uint8_t)(addr & 0xFFu);
    hw.rom_image[2] = (uint8_t)(addr >> 8);
    hw.rom_image[3] = STP_OPCODE;
    hw.rom_image[0x7FFC] = 0x00;               /* force reset vector to $8000 */
    hw.rom_image[0x7FFD] = 0x80;

    drive_release();
    hw.reset_assert();
    reset_asserted = true;

    read_cycle_count = 0;
    read_max_cycles = LIVE_PEEK_MAX_CYCLES;
    capture_queue_clear();
    read_active = true;

    /* Run the stub fast regardless of the configured clock, then restore. */
    float saved_hz = *hw.current_hz;
    phi2_set_hz(LIVE_PEEK_HZ);
    hw.reset_release();
    reset_asserted = false;

    /* The stub terminates itself with STP (or the cycle cap backstops it).
     * Pump ROM emulation directly; the main loop is blocked on us. */
    absolute_time_t deadline = make_timeout_time_ms(LIVE_PEEK_TIMEOUT_MS);
    while (!pending_done && !time_reached(deadline)) {
        proto_idle_pump();
        tight_loop_contents();
    }

    hw.reset_assert();
    reset_asserted = true;
    read_active = false;
    phi2_set_hz(saved_hz);

    memcpy(hw.rom_image, saved, 4);
    hw.rom_image[0x7FFC] = saved[4];
    hw.rom_image[0x7FFD] = saved[5];

    bool found = false;
    capture_sample_t s;
    while (capture_queue_pop(&s)) {
        if (!found && s.addr == addr && s.rw == 0u) {
            *out_data = s.data;
            found = true;
        }
    }
    capture_queue_clear();
    return found;
}

static void cmd_live_peek(uint16_t addr, const char *req_id) {
    if (read_active || upload_active) {
        json_send_error(req_id, "busy", "capture or upload in progress");
        return;
    }

    uint8_t data = 0;
    if (!live_peek_run(addr, &data)) {
        json_send_error(req_id, "no_cycle", "no bus cycle matched addr");
        return;
    }

    char addr_s[8];
    char data_s[4];
    snprintf(addr_s, sizeof(addr_s), "%04X", addr);
    snprintf(data_s, sizeof(data_s), "%02X", data);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
    json_attach_id(resp, req_id);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "cmd", "peek");
    cJSON_AddStringToObject(resp, "addr", addr_s);
    cJSON_AddStringToObject(resp, "data", data_s);
    json_send_object(resp);
    cJSON_Delete(resp);
}

static void cmd_peek(cJSON *root, const char *req_id) {
    /* Mode is selected by which fields are present: "addr" (hex string)
     * selects a live bus/RAM peek; "offset" selects ROM-image readback. */
    bool has_addr = cJSON_HasObjectItem(root, "addr");
    bool has_offset = cJSON_HasObjectItem(root, "offset");

    if (has_addr && has_offset) {
        json_send_error(req_id, "bad_request", "peek takes addr or offset, not both");
        return;
    }
    if (has_addr) {
        uint16_t addr;
        if (!json_get_hex16(root, "addr", &addr)) {
            json_send_error(req_id, "bad_request", "addr must be a hex string 0000-FFFF");
            return;
        }
        cmd_live_peek(addr, req_id);
        return;
    }
    if (!has_offset) {
        json_send_error(req_id, "bad_request", "peek requires addr or offset");
        return;
    }

    uint32_t offset = json_get_uint(root, "offset", 0);
    uint32_t count = json_get_uint(root, "count", 1);

    if (offset >= hw.rom_size) {
        json_send_error(req_id, "bad_offset", "offset out of range");
        return;
    }
    if (count == 0) {
        count = 1;
    }
    if (count > PEEK_MAX_COUNT) {
        count = PEEK_MAX_COUNT;
    }
    if (offset + count > hw.rom_size) {
        count = (uint32_t)(hw.rom_size - offset);
    }

    char hex[(PEEK_MAX_COUNT * 2) + 1];
    for (uint32_t i = 0; i < count; i++) {
        snprintf(hex + (i * 2), 3, "%02X", hw.rom_image[offset + i]);
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
    json_attach_id(resp, req_id);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "cmd", "peek");
    cJSON_AddNumberToObject(resp, "offset", (double)offset);
    cJSON_AddNumberToObject(resp, "count", (double)count);
    cJSON_AddStringToObject(resp, "data", hex);
    json_send_object(resp);
    cJSON_Delete(resp);
}

static void cmd_drive(cJSON *root, const char *req_id) {
    const char *value_str = json_get_string(root, "value");

    if (value_str != NULL) {
        unsigned int val = 0;
        if (sscanf(value_str, "%2x", &val) != 1) {
            json_send_error(req_id, "bad_value", "value must be a 2-digit hex string");
            return;
        }
        drive_enabled = true;
        drive_value = (uint8_t)val;
        gpio_set_dir_out_masked(DATA_MASK);
        gpio_put_masked(DATA_MASK, (uint32_t)drive_value << PIN_D_FIRST);
    } else if (json_get_bool(root, "enable", true) == false) {
        drive_release();
    } else {
        json_send_error(req_id, "bad_request", "drive requires value or enable:false");
        return;
    }

    char value_hex[4];
    snprintf(value_hex, sizeof(value_hex), "%02X", drive_value);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
    json_attach_id(resp, req_id);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "cmd", "drive");
    cJSON_AddBoolToObject(resp, "enabled", drive_enabled);
    cJSON_AddStringToObject(resp, "value", drive_enabled ? value_hex : "00");
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
    char data[4];
    snprintf(addr, sizeof(addr), "%04X", last_addr);
    snprintf(data, sizeof(data), "%02X", last_data);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "v", HW_API_VERSION);
    json_attach_id(resp, req_id);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "cmd", "status");
    cJSON_AddNumberToObject(resp, "phi2_hz", (double)*hw.current_hz);
    cJSON_AddBoolToObject(resp, "rom_active", *hw.rom_active);
    cJSON_AddBoolToObject(resp, "reset_asserted", reset_asserted);
    cJSON_AddStringToObject(resp, "last_addr", addr);
    cJSON_AddStringToObject(resp, "last_data", data);
    cJSON_AddNumberToObject(resp, "last_rw", (double)last_rw_report);
    cJSON_AddBoolToObject(resp, "read_active", read_active);
    cJSON_AddBoolToObject(resp, "monitor_enabled", monitor_enabled);
    cJSON_AddBoolToObject(resp, "upload_active", upload_active);
    cJSON_AddBoolToObject(resp, "drive_enabled", drive_enabled);
    cJSON_AddNumberToObject(resp, "resb", gpio_get(STATUS_PIN_RESB) ? 1 : 0);
    cJSON_AddNumberToObject(resp, "rwb", gpio_get(STATUS_PIN_RWB) ? 1 : 0);
    cJSON_AddNumberToObject(resp, "a15", gpio_get(STATUS_PIN_A15) ? 1 : 0);
    cJSON_AddNumberToObject(resp, "phi2", gpio_get(STATUS_PIN_PHI2) ? 1 : 0);
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
    exchange_active = false;
    read_batch_size = 1;
    drive_enabled = false;
    drive_value = 0;
    capture_queue_clear();
    upload_reset_state();
}

static void handle_enq_inner(void) {
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
     * non-destructive queries (status / request_addr / peek). */
    if (
        strcmp(cmd, "read") != 0
        && strcmp(cmd, "read_event") != 0
        && strcmp(cmd, "reset") != 0
        && strcmp(cmd, "status") != 0
        && strcmp(cmd, "request_addr") != 0
        && strcmp(cmd, "peek") != 0
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
    } else if (strcmp(cmd, "clock") == 0) {
        cmd_clock(root, req_id);
    } else if (strcmp(cmd, "read_event") == 0) {
        cmd_read_event(root, req_id);
    } else if (strcmp(cmd, "drive") == 0) {
        cmd_drive(root, req_id);
    } else if (strcmp(cmd, "request_addr") == 0) {
        cmd_request_addr(req_id);
    } else if (strcmp(cmd, "peek") == 0) {
        cmd_peek(root, req_id);
    } else if (strcmp(cmd, "monitor") == 0) {
        cmd_monitor(root, req_id);
    } else if (strcmp(cmd, "status") == 0) {
        cmd_status(req_id);
    } else {
        json_send_error(req_id, "unknown_cmd", cmd);
    }

    cJSON_Delete(root);
}

void hardware_api_handle_enq(void) {
    exchange_active = true;
    handle_enq_inner();
    exchange_active = false;
}

bool hardware_api_exchange_active(void) {
    return exchange_active;
}

void hardware_api_on_bus_cycle(uint16_t addr, uint8_t data, bool rwb_pin) {
    /* rwb_pin is read-high sense (not necessarily GPIO GP23). On this board
     * main.c passes A15: ROM reads (high) → rw=0; RAM writes (low) → rw=1. */
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
    /* Capture events are returned via host-polled `read_event` commands. */
}

uint16_t hardware_api_last_addr(void) {
    return last_addr;
}

bool hardware_api_is_reading(void) {
    return read_active || capture_q_count > 0 || pending_done;
}

bool hardware_api_monitor_enabled(void) {
    return monitor_enabled;
}

bool hardware_api_drive_enabled(void) {
    return drive_enabled;
}

uint8_t hardware_api_drive_value(void) {
    return drive_value;
}
