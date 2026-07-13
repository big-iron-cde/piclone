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

/*
 * JSON frame buffer must fit one full ROM upload chunk as base64:
 *   32768 raw → 43692 b64 chars + JSON envelope (~100–200 bytes).
 * Sized at 48 KiB so a single-chunk upload (and optional "id") fits.
 * The receive buffer in hardware_api_handle_enq is static — do not put
 * a buffer this large on the stack.
 */
#define PROTO_JSON_MAX   (48 * 1024)
#define PROTO_BINARY_MAX 0x8000

/* Optional hook called while waiting on USB so ROM/PHI2 sampling can continue. */
typedef void (*proto_idle_fn)(void);
void proto_set_idle_hook(proto_idle_fn fn);

bool proto_read_byte(uint8_t *out, uint32_t timeout_ms);
bool proto_write_byte(uint8_t b);

bool proto_send_ack(void);
bool proto_send_nack(void);

/* Host → Pico: read one framed payload (caller already consumed ENQ). */
bool proto_read_frame_payload(uint8_t *buf, size_t buf_size, size_t *out_len);

/* Pico → Host: send one framed payload; wait for host ACK (NACK = fail). */
bool proto_send_frame(const uint8_t *payload, size_t len);

#endif
