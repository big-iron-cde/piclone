/*
 * Hardware API — JSON v1 commands over framed serial
 *
 * All payloads require {"v":1,...}. Commands:
 *   reset, upload_rom (begin/chunk/commit), read, request_addr, monitor, status
 */

#ifndef HARDWARE_API_H
#define HARDWARE_API_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t *rom_image;
    size_t   rom_size;
    bool    *rom_active;
    float   *current_hz;
    void   (*reset_assert)(void);
    void   (*reset_release)(void);
} hw_context_t;

void hardware_api_init(const hw_context_t *ctx);

/* Call when ENQ (0x05) is seen on serial input. */
void hardware_api_handle_enq(void);

/* Call from rom_task on each PHI2 rising-edge sample. */
void hardware_api_on_bus_cycle(uint16_t addr, uint8_t data, bool rw);

uint16_t hardware_api_last_addr(void);
bool hardware_api_is_reading(void);
bool hardware_api_monitor_enabled(void);

#endif
