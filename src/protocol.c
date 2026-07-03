/*
 * ENQ → STX → ACK → payload → EOT → ACK/NACK
 */

#include "protocol.h"
#include "pico/stdlib.h"
#include <stdio.h>

bool proto_read_byte(uint8_t *out, uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (true) {
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            *out = (uint8_t)c;
            return true;
        }
        if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
            return false;
        }
        tight_loop_contents();
    }
}

bool proto_write_byte(uint8_t b) {
    putchar_raw((int)b);
    return true;
}

bool proto_send_ack(void) {
    return proto_write_byte(CTRL_ACK);
}

bool proto_send_nack(void) {
    return proto_write_byte(CTRL_NACK);
}

static bool wait_for_stx(uint32_t timeout_ms) {
    uint8_t b;
    if (!proto_read_byte(&b, timeout_ms) || b != CTRL_STX) {
        return false;
    }
    return true;
}

static bool read_payload_until_eot(uint8_t *buf, size_t buf_size, size_t *out_len,
                                   uint32_t timeout_ms) {
    size_t n = 0;
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);

    while (true) {
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) {
            if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
                return false;
            }
            continue;
        }
        deadline = make_timeout_time_ms(timeout_ms);

        if (c == CTRL_EOT) {
            *out_len = n;
            return true;
        }
        if (n >= buf_size) {
            return false;
        }
        buf[n++] = (uint8_t)c;
    }
}

static bool wait_host_ack(uint32_t timeout_ms) {
    uint8_t b;
    if (!proto_read_byte(&b, timeout_ms)) {
        return false;
    }
    return b == CTRL_ACK;
}

bool proto_read_frame_payload(uint8_t *buf, size_t buf_size, size_t *out_len) {
    if (!wait_for_stx(3000)) {
        return false;
    }
    if (!proto_send_ack()) {
        return false;
    }
    if (!read_payload_until_eot(buf, buf_size, out_len, 5000)) {
        return false;
    }
    return proto_send_ack();
}

bool proto_send_frame(const uint8_t *payload, size_t len) {
    if (!proto_write_byte(CTRL_ENQ)) {
        return false;
    }
    if (!proto_write_byte(CTRL_STX)) {
        return false;
    }
    if (!wait_host_ack(3000)) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (!proto_write_byte(payload[i])) {
            return false;
        }
    }
    if (!proto_write_byte(CTRL_EOT)) {
        return false;
    }
    stdio_flush();
    return wait_host_ack(3000);
}
