/*
 * pico-rom-test — Pico-as-ROM firmware for the breadboard 6502 prototype
 *
 * Hardware API over USB-CDC (framed serial):
 *   ENQ → STX → ACK → JSON payload → EOT → ACK/NACK
 *
 * Commands: reset, upload_rom, read (until STP), request_addr, monitor
 *
 * On USB connect the firmware auto-starts clock, ROM emulation, and RESET.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/clocks.h"

#include "protocol.h"
#include "hardware_api.h"

// ─── Pin map (matches README §2) ────────────────────────────────────────

#define PIN_A_FIRST   0
#define PIN_A_LAST    14
#define PIN_D_FIRST   15
#define PIN_D_LAST    22
#define PIN_A15       26
#define PIN_RESET     27
#define PIN_PHI2      28
#define PIN_RWB       23

#define DATA_MASK     (0xFFu << PIN_D_FIRST)

#define ROM_SIZE      0x8000

// ─── State ───────────────────────────────────────────────────────────────

static uint8_t rom_image[ROM_SIZE];
static bool rom_active = false;

static bool     phi2_last_state  = false;
static bool     reset_last_state = true;
static float    current_hz       = 0.2f;
static uint16_t seq_counter      = 1;

// ─── Pin setup ───────────────────────────────────────────────────────────

static void pins_init(void) {
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

    gpio_init(PIN_RESET);
    gpio_set_dir(PIN_RESET, GPIO_OUT);
    gpio_put(PIN_RESET, 0);

    gpio_init(PIN_PHI2);
    gpio_set_dir(PIN_PHI2, GPIO_OUT);
    gpio_put(PIN_PHI2, 0);

    gpio_init(PIN_RWB);
    gpio_set_dir(PIN_RWB, GPIO_IN);
}

// ─── PHI2 (clock) ────────────────────────────────────────────────────────

static alarm_id_t phi2_alarm_id = 0;
static uint32_t   phi2_half_us  = 0;

static int64_t phi2_alarm_callback(alarm_id_t id, void *user_data) {
    (void)id;
    (void)user_data;
    gpio_xor_mask(1u << PIN_PHI2);
    return (int64_t)phi2_half_us;
}

static void phi2_start_us(uint32_t half_period_us) {
    gpio_set_function(PIN_PHI2, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_PHI2, GPIO_OUT);
    gpio_put(PIN_PHI2, 0);

    phi2_half_us = half_period_us;
    if (phi2_half_us < 2) phi2_half_us = 2;

    phi2_alarm_id = add_alarm_in_us(phi2_half_us, phi2_alarm_callback, NULL, true);

    float hz = 1000000.0f / (2.0f * (float)phi2_half_us);
    printf("PHI2 on: target %.1f Hz (half-period=%lu us)\n",
           hz, (unsigned long)phi2_half_us);
}

// ─── Reset ───────────────────────────────────────────────────────────────

static void reset_assert(void) {
    gpio_set_dir(PIN_RESET, GPIO_OUT);
    gpio_put(PIN_RESET, 0);
}

static void reset_release(void) {
    gpio_set_dir(PIN_RESET, GPIO_IN);
}

// ─── ROM image ──────────────────────────────────────────────────────────

static void rom_image_init(void) {
    memset(rom_image, 0xEA, sizeof(rom_image));

    rom_image[0x0000] = 0x18;       // CLC
    rom_image[0x0001] = 0xA9;       // LDA #$05
    rom_image[0x0002] = 0x05;
    rom_image[0x0003] = 0x8D;       // STA $4000
    rom_image[0x0004] = 0x00;
    rom_image[0x0005] = 0x40;
    rom_image[0x0006] = 0x69;       // ADC #$0F
    rom_image[0x0007] = 0x0F;
    rom_image[0x0008] = 0x8D;       // STA $4000
    rom_image[0x0009] = 0x00;
    rom_image[0x000A] = 0x40;
    rom_image[0x000B] = 0x4C;       // JMP $8000
    rom_image[0x000C] = 0x00;
    rom_image[0x000D] = 0x80;

    rom_image[0x7FFC] = 0x00;
    rom_image[0x7FFD] = 0x80;
    rom_image[0x7FFE] = 0x00;
    rom_image[0x7FFF] = 0x80;
}

// ─── ROM emulation (polling) ───────────────────────────────────────────

static void rom_task(void) {
    uint32_t pins = gpio_get_all();
    bool     phi2 = (pins >> PIN_PHI2) & 1u;
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

    if (phi2 && !phi2_last_state) {
        bool reset_state = gpio_get(PIN_RESET);
        if (reset_state && !reset_last_state) {
            seq_counter = 1;
            if (hardware_api_monitor_enabled()) {
                printf("\n+----+------+---------+----+-------+\n");
                printf("| NO | DATA | ADDRESS | RW | CLOCK |\n");
                printf("+----+------+---------+----+-------+\n");
            }
        }
        reset_last_state = reset_state;

        uint16_t addr = (pins >> PIN_A_FIRST) & 0x7FFFu;
        if (a15) addr |= 0x8000u;
        uint8_t data = (uint8_t)((pins >> PIN_D_FIRST) & 0xFFu);
        bool rwb = (pins >> PIN_RWB) & 1u;

        hardware_api_on_bus_cycle(addr, data, rwb);

        if (hardware_api_monitor_enabled() && !hardware_api_is_reading()) {
            printf("| %02d |  %02X  |  %04X   |  %d | %5.1f |\n",
                   seq_counter, data, addr, rwb ? 1 : 0, current_hz);
            seq_counter++;
            if (seq_counter > 99) seq_counter = 1;
        }
    }
    phi2_last_state = phi2;
}

// ─── Main loop ──────────────────────────────────────────────────────────

static void print_banner(void) {
    printf("\n=== pico-rom-test — Hardware API ===\n");
    printf("Protocol: ENQ STX ACK JSON EOT ACK\n");
    printf("Commands: reset, upload_rom, read, request_addr, monitor\n");
    printf("65C02 clock: %.1f Hz  ROM: ON\n", current_hz);
    stdio_flush();
}

int main(void) {
    stdio_init_all();
    pins_init();
    rom_image_init();

    hw_context_t ctx = {
        .rom_image     = rom_image,
        .rom_size      = ROM_SIZE,
        .rom_active    = &rom_active,
        .current_hz    = &current_hz,
        .reset_assert  = reset_assert,
        .reset_release = reset_release,
    };
    hardware_api_init(&ctx);

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

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

    sleep_ms(200);
    phi2_start_us(2500000);
    current_hz = 0.2f;
    rom_active = true;
    reset_release();
    print_banner();

    gpio_put(PICO_DEFAULT_LED_PIN, 1);

    absolute_time_t next_blink = get_absolute_time();

    while (true) {
        rom_task();

        if (absolute_time_diff_us(get_absolute_time(), next_blink) <= 0) {
            gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN);
            next_blink = make_timeout_time_ms(500);
        }

        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) continue;
        if (c == CTRL_ENQ) {
            hardware_api_handle_enq();
        }
    }
}
