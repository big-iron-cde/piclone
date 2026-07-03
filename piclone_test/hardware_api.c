#include "hardware_api.h"
#include "protocol.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define STP_OPCODE 0xDB

static hw_context_t hw;
static uint16_t last_addr;
static bool read_active;
static bool monitor_enabled;
static uint32_t read_max_cycles;
static uint32_t read_cycle_count;

static bool json_has_cmd(const char *json, const char *cmd) {
    char needle[48];
    snprintf(needle, sizeof(needle), "\"cmd\":\"%s\"", cmd);
    return strstr(json, needle) != NULL;
}

static bool json_bool_field(const char *json, const char *key, bool default_val) {
    char needle[32];
    snprintf(needle, sizeof(needle), "\"%s\":true", key);
    if (strstr(json, needle)) {
        return true;
    }
    snprintf(needle, sizeof(needle), "\"%s\":false", key);
    if (strstr(json, needle)) {
        return false;
    }
    return default_val;
}

static uint32_t json_uint_field(const char *json, const char *key, uint32_t default_val) {
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p) {
        return default_val;
    }
    p += strlen(pattern);
    while (*p == ' ') {
        p++;
    }
    return (uint32_t)strtoul(p, NULL, 10);
}

static bool send_json_response(const char *json) {
    return proto_send_frame((const uint8_t *)json, strlen(json));
}

static void cmd_reset(const char *json) {
    bool assert_reset = json_bool_field(json, "assert", true);
    if (assert_reset) {
        hw.reset_assert();
    } else {
        hw.reset_release();
    }
    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"cmd\":\"reset\",\"asserted\":%s}",
             assert_reset ? "true" : "false");
    send_json_response(resp);
}

static void cmd_upload_rom(const char *json) {
    uint32_t size = json_uint_field(json, "size", (uint32_t)hw.rom_size);
    if (size != hw.rom_size) {
        send_json_response("{\"ok\":false,\"error\":\"bad_size\"}");
        return;
    }

    send_json_response("{\"ok\":true,\"cmd\":\"upload_rom\",\"awaiting\":32768}");

    bool was_active = *hw.rom_active;
    *hw.rom_active = false;
    gpio_set_dir_in_masked(0xFFu << 15);
    hw.reset_assert();

    if (!proto_read_binary_frame(hw.rom_image, hw.rom_size)) {
        *hw.rom_active = was_active;
        hw.reset_release();
        send_json_response("{\"ok\":false,\"error\":\"binary_frame\"}");
        return;
    }

    *hw.rom_active = was_active;
    hw.reset_release();

    char resp[160];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"cmd\":\"upload_rom\",\"bytes\":%u,"
             "\"reset_vector\":\"%02X%02X\"}",
             (unsigned)hw.rom_size,
             hw.rom_image[0x7FFD], hw.rom_image[0x7FFC]);
    send_json_response(resp);
}

static void cmd_read(const char *json) {
    if (!strstr(json, "\"until\":\"stp\"") && !strstr(json, "\"until\": \"stp\"")) {
        send_json_response("{\"ok\":false,\"error\":\"unsupported_until\"}");
        return;
    }

    read_max_cycles = json_uint_field(json, "max_cycles", 10000);
    if (read_max_cycles == 0) {
        read_max_cycles = 10000;
    }
    read_cycle_count = 0;
    read_active = true;
    monitor_enabled = false;  /* ASCII table corrupts framed cycle stream */

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"cmd\":\"read\",\"until\":\"stp\",\"max_cycles\":%lu}",
             (unsigned long)read_max_cycles);
    send_json_response(resp);
}

static void cmd_request_addr(void) {
    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"cmd\":\"request_addr\",\"addr\":\"%04X\",\"phi2_hz\":%.1f}",
             last_addr, *hw.current_hz);
    send_json_response(resp);
}

static void cmd_monitor(const char *json) {
    monitor_enabled = json_bool_field(json, "enable", true);
    char resp[96];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"cmd\":\"monitor\",\"enable\":%s}",
             monitor_enabled ? "true" : "false");
    send_json_response(resp);
}

void hardware_api_init(const hw_context_t *ctx) {
    hw = *ctx;
    last_addr = 0;
    read_active = false;
    monitor_enabled = false;
}

void hardware_api_handle_enq(void) {
    uint8_t buf[PROTO_JSON_MAX];
    size_t len = 0;

    if (!proto_read_frame_payload(buf, sizeof(buf) - 1, &len)) {
        proto_send_nack();
        return;
    }
    buf[len] = '\0';

    /* Abort a stale read capture when any other command arrives. */
    if (!json_has_cmd((char *)buf, "read")) {
        read_active = false;
    }

    if (json_has_cmd((char *)buf, "reset")) {
        cmd_reset((char *)buf);
    } else if (json_has_cmd((char *)buf, "upload_rom")) {
        cmd_upload_rom((char *)buf);
    } else if (json_has_cmd((char *)buf, "read")) {
        cmd_read((char *)buf);
    } else if (json_has_cmd((char *)buf, "request_addr")) {
        cmd_request_addr();
    } else if (json_has_cmd((char *)buf, "monitor")) {
        cmd_monitor((char *)buf);
    } else {
        send_json_response("{\"ok\":false,\"error\":\"unknown_cmd\"}");
    }
}

void hardware_api_on_bus_cycle(uint16_t addr, uint8_t data, bool rw) {
    last_addr = addr;
    if (!read_active) {
        return;
    }

    read_cycle_count++;
    char resp[160];
    snprintf(resp, sizeof(resp),
             "{\"type\":\"cycle\",\"seq\":%lu,\"addr\":\"%04X\",\"data\":\"%02X\",\"rw\":%d}",
             (unsigned long)read_cycle_count, addr, data, rw ? 1 : 0);
    if (!proto_send_frame((const uint8_t *)resp, strlen(resp))) {
        read_active = false;
        send_json_response("{\"type\":\"done\",\"ok\":false,\"reason\":\"host_nack\"}");
        return;
    }

    bool stp_fetch = (!rw && data == STP_OPCODE);
    bool limit = (read_cycle_count >= read_max_cycles);

    if (stp_fetch || limit) {
        read_active = false;
        char done[160];
        snprintf(done, sizeof(done),
                 "{\"type\":\"done\",\"ok\":true,\"reason\":\"%s\",\"cycles\":%lu,\"addr\":\"%04X\"}",
                 stp_fetch ? "stp" : "max_cycles",
                 (unsigned long)read_cycle_count, addr);
        send_json_response(done);
    }
}

uint16_t hardware_api_last_addr(void) {
    return last_addr;
}

bool hardware_api_is_reading(void) {
    return read_active;
}

bool hardware_api_monitor_enabled(void) {
    return monitor_enabled;
}
