/*
 * Serial framing: ENQ → STX → ACK → payload → EOT → ACK/NACK
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define CTRL_ENQ  0x05
#define CTRL_ACK  0x06
#define CTRL_STX  0x02
#define CTRL_EOT  0x04
#define CTRL_NACK 0x15

#define PROTO_JSON_MAX   2048
#define PROTO_BINARY_MAX 0x8000

bool proto_read_byte(uint8_t *out, uint32_t timeout_ms);
bool proto_write_byte(uint8_t b);

bool proto_send_ack(void);
bool proto_send_nack(void);

/* Host → Pico: read one framed payload (caller already consumed ENQ). */
bool proto_read_frame_payload(uint8_t *buf, size_t buf_size, size_t *out_len);

/* Pico → Host: send one framed payload; wait for host ACK (NACK = fail). */
bool proto_send_frame(const uint8_t *payload, size_t len);

#endif
