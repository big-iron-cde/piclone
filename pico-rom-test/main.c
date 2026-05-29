/*
 * pico-rom-test — Pico-as-ROM firmware for the breadboard 6502 prototype
 *
 * Auto-run variant: no interactive command interface. On USB connect the
 * firmware immediately starts the 65C02 clock, enables ROM emulation,
 * releases RESET, and streams watch-port data over USB-CDC.
 *
 * Connect over USB serial at any baud and the data appears immediately.
 *
 * To upload a custom 32 KB ROM image, send the line "loadbin" followed
 * by 32768 raw bytes. The built-in demo program (stores $05 / $08 at
 * $4000) runs out of the box if no upload is performed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/clocks.h"

// ─── Pin map (matches pico-as-rom-wiring.md) ─────────────────────────────

#define PIN_A_FIRST   0    // GP0..GP14 = A0..A14 (15 pins)
#define PIN_A_LAST    14
#define PIN_D_FIRST   15   // GP15..GP22 = D0..D7 (8 pins)
#define PIN_D_LAST    22
#define PIN_A15       26   // GP26 = A15 (used as Pico's chip-enable)
#define PIN_RESET     27   // GP27 = RESET drive (open-drain emulated)
#define PIN_PHI2      28   // GP28 = PHI2 clock output

#define DATA_MASK     (0xFFu << PIN_D_FIRST)
#define ADDR_LOW_MASK (0x7FFFu << PIN_A_FIRST)

#define ROM_SIZE      0x8000   // 32 KB — fits CPU $8000-$FFFF

// ─── State ───────────────────────────────────────────────────────────────

static uint8_t rom_image[ROM_SIZE];

static bool monitor_addr = false;
static bool rom_active   = false;

static bool     watch_active = false;
static uint16_t watch_addr   = 0x4000;
static volatile uint8_t watch_pending_data = 0;
static volatile bool    watch_pending      = false;
static uint8_t  watch_last_printed = 0xFF;
static bool     watch_have_printed = false;

// ─── Pin setup ───────────────────────────────────────────────────────────

static void pins_init(void) {
    // Address bus + A15 + reset start as inputs (safe — won't fight bus)
    for (int p = PIN_A_FIRST; p <= PIN_A_LAST; p++) {
        gpio_init(p);
        gpio_set_dir(p, GPIO_IN);
    }
    for (int p = PIN_D_FIRST; p <= PIN_D_LAST; p++) {
        gpio_init(p);
        gpio_set_dir(p, GPIO_IN);
    }
    gpio_init(PIN_A15);
    gpio_set_dir(PIN_A15, GPIO_IN);

    // RESET starts asserted (output LOW = CPU held in reset)
    gpio_init(PIN_RESET);
    gpio_set_dir(PIN_RESET, GPIO_OUT);
    gpio_put(PIN_RESET, 0);

    // PHI2 starts as GPIO output, held low
    gpio_init(PIN_PHI2);
    gpio_set_dir(PIN_PHI2, GPIO_OUT);
    gpio_put(PIN_PHI2, 0);
}

// ─── PHI2 (clock) ────────────────────────────────────────────────────────
// Timer-alarm based square wave.  Works from 1 Hz up to ~100 kHz.

static alarm_id_t phi2_alarm_id = 0;
static uint32_t   phi2_half_us  = 0;

static int64_t phi2_alarm_callback(alarm_id_t id, void *user_data) {
    (void)id;
    (void)user_data;
    gpio_xor_mask(1u << PIN_PHI2);
    return (int64_t)phi2_half_us;   // reschedule in half_period_us
}

static void phi2_start(uint32_t target_hz) {
    // Ensure pin is under SIO control
    gpio_set_function(PIN_PHI2, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_PHI2, GPIO_OUT);
    gpio_put(PIN_PHI2, 0);

    if (target_hz == 0) target_hz = 1;
    phi2_half_us = 500000u / target_hz;
    if (phi2_half_us < 2) phi2_half_us = 2;   // ~250 kHz ceiling

    phi2_alarm_id = add_alarm_in_us(phi2_half_us, phi2_alarm_callback, NULL, true);

    printf("PHI2 on: target %lu Hz (half-period=%lu us)\n",
           (unsigned long)target_hz, (unsigned long)phi2_half_us);
}

static void phi2_stop(void) {
    if (phi2_alarm_id) {
        cancel_alarm(phi2_alarm_id);
        phi2_alarm_id = 0;
    }
    gpio_put(PIN_PHI2, 0);
    printf("PHI2 off (held low)\n");
}

// ─── Reset ───────────────────────────────────────────────────────────────

static void reset_assert(void) {
    gpio_set_dir(PIN_RESET, GPIO_OUT);
    gpio_put(PIN_RESET, 0);
    printf("RESET asserted (CPU held in reset)\n");
}

static void reset_release(void) {
    gpio_set_dir(PIN_RESET, GPIO_IN);
    printf("RESET released (CPU should run)\n");
}

// ─── Address bus sampling ───────────────────────────────────────────────

static uint16_t addr_read(void) {
    uint32_t pins = gpio_get_all();
    uint16_t addr = (pins >> PIN_A_FIRST) & 0x7FFFu;
    if (pins & (1u << PIN_A15)) addr |= 0x8000u;
    return addr;
}

static uint8_t data_read(void) {
    uint32_t pins = gpio_get_all();
    return (uint8_t)((pins >> PIN_D_FIRST) & 0xFFu);
}

// ─── ROM image ──────────────────────────────────────────────────────────

static void rom_image_init(void) {
    memset(rom_image, 0xEA, sizeof(rom_image));

    // ── Built-in demo program at CPU $8000 ─────────────────────────────
    // CLC
    // LDA #$05
    // STA $4000          ; Pico watch port
    // ADC #$03           ; A = 8
    // STA $4000
    // JMP $8000
    // --------------------------------------------------------------------
    rom_image[0x0000] = 0x18;       // CLC
    rom_image[0x0001] = 0xA9;       // LDA #$05
    rom_image[0x0002] = 0x05;
    rom_image[0x0003] = 0x8D;       // STA $4000
    rom_image[0x0004] = 0x00;
    rom_image[0x0005] = 0x40;
    rom_image[0x0006] = 0x69;       // ADC #$03
    rom_image[0x0007] = 0x03;
    rom_image[0x0008] = 0x8D;       // STA $4000
    rom_image[0x0009] = 0x00;
    rom_image[0x000A] = 0x40;
    rom_image[0x000B] = 0x4C;       // JMP $8000
    rom_image[0x000C] = 0x00;
    rom_image[0x000D] = 0x80;

    // Default reset vector: $FFFC/$FFFD → $8000
    rom_image[0xFFFC - 0x8000] = 0x00;
    rom_image[0xFFFD - 0x8000] = 0x80;
    // Default IRQ/BRK vector: $FFFE/$FFFF → $8000
    rom_image[0xFFFE - 0x8000] = 0x00;
    rom_image[0xFFFF - 0x8000] = 0x80;
}

// ─── ROM emulation (polling) ───────────────────────────────────────────

static void rom_task(void) {
    uint32_t pins = gpio_get_all();
    bool     a15  = (pins >> PIN_A15) & 1u;

    if (rom_active) {
        if (a15) {
            uint16_t addr = (pins >> PIN_A_FIRST) & 0x7FFFu;
            uint8_t  byte = rom_image[addr];
            gpio_set_dir_out_masked(DATA_MASK);
            gpio_put_masked(DATA_MASK, (uint32_t)byte << PIN_D_FIRST);
        } else {
            gpio_set_dir_in_masked(DATA_MASK);
        }
    }

    if (watch_active) {
        uint16_t addr = (pins >> PIN_A_FIRST) & 0x7FFFu;
        if (a15) addr |= 0x8000u;
        if (addr == watch_addr) {
            uint8_t data = (uint8_t)((pins >> PIN_D_FIRST) & 0xFFu);
            if (data == (uint8_t)(watch_addr >> 8)) {
                return;
            }
            watch_pending_data = data;
            watch_pending      = true;
        }
    }
}

// ─── ROM upload (raw binary over USB-CDC) ───────────────────────────────

static void cmd_loadbin(void) {
    printf("OK send %d bytes\n", ROM_SIZE);
    stdio_flush();

    bool was_active = rom_active;
    rom_active = false;
    gpio_set_dir_in_masked(DATA_MASK);
    reset_assert();

    int n = 0;
    absolute_time_t deadline = make_timeout_time_ms(2000);
    while (n < ROM_SIZE) {
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) {
            if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
                printf("\nupload timeout after %d bytes\n", n);
                rom_active = was_active;
                reset_release();
                return;
            }
            continue;
        }
        rom_image[n++] = (uint8_t)c;
        deadline = make_timeout_time_ms(2000);
    }

    rom_active = was_active;
    reset_release();
    printf("loaded %d bytes\n", n);
    printf("  reset vector -> $%02X%02X\n",
           rom_image[0xFFFD - 0x8000], rom_image[0xFFFC - 0x8000]);
}

// ─── Main loop ──────────────────────────────────────────────────────────

static void print_banner(void) {
    printf("\n=== pico-rom-test — auto-run firmware ===\n");
    printf("65C02 + Pico-as-ROM + HM62256LP on 3.3 V\n");
    printf("Clock: 1 Hz  ROM: ON  Watch: $%04X\n", watch_addr);
    printf("Send 'loadbin' + 32768 raw bytes to upload a custom ROM.\n");
    printf("Streaming data below...\n\n");
    stdio_flush();
}

int main(void) {
    stdio_init_all();
    pins_init();
    rom_image_init();

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    // Wait (with a heartbeat) until the host opens the USB-CDC port.
    absolute_time_t led_toggle = get_absolute_time();
    bool led_on = false;
    while (!stdio_usb_connected()) {
        if (absolute_time_diff_us(get_absolute_time(), led_toggle) <= 0) {
            led_on = !led_on;
            gpio_put(PICO_DEFAULT_LED_PIN, led_on);
            led_toggle = make_timeout_time_ms(250);
        }
        tight_loop_contents();
    }

    // Connected — auto-start everything.
    sleep_ms(200);
    phi2_start(1);          // 1 Hz — slow enough to watch by eye
    rom_active   = true;
    watch_active = true;
    watch_have_printed = false;
    watch_pending      = false;
    reset_release();
    print_banner();

    gpio_put(PICO_DEFAULT_LED_PIN, 1);  // solid on = connected & running

    char line[16];
    int  pos = 0;
    absolute_time_t next_blink = get_absolute_time();

    while (true) {
        // ROM emulation runs every iteration (~MHz polling rate)
        rom_task();

        // Slow heartbeat blink (1 Hz)
        if (absolute_time_diff_us(get_absolute_time(), next_blink) <= 0) {
            gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN);
            next_blink = make_timeout_time_ms(500);
        }

        // Watch: print only on change
        if (watch_pending) {
            uint8_t d = watch_pending_data;
            watch_pending = false;
            if (!watch_have_printed || d != watch_last_printed) {
                printf("[$%04X = $%02X]\n", watch_addr, d);
                watch_last_printed = d;
                watch_have_printed = true;
            }
        }

        // Minimal serial input — only "loadbin" is recognised
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) continue;

        if (c == '\r' || c == '\n') {
            line[pos] = 0;
            if (strcmp(line, "loadbin") == 0) {
                cmd_loadbin();
                printf("Streaming data below...\n\n");
            }
            pos = 0;
        } else if (pos < (int)sizeof(line) - 1) {
            line[pos++] = (char)c;
        } else {
            // shift out oldest char to make room
            memmove(line, line + 1, sizeof(line) - 2);
            line[sizeof(line) - 2] = (char)c;
            pos = sizeof(line) - 1;
        }
    }
}
