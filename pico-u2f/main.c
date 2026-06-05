/*
 * pico-u2f — U2F security key firmware for Raspberry Pi Pico (RP2040)
 */

#include "pico/stdlib.h"
#include "u2f.h"
#include "u2f_hid.h"
#include "tusb.h"

int main(void) {
    // Board and GPIO init
    stdio_init_all();
    u2f_init();
    u2f_hid_init();

    // Start TinyUSB device stack
    tusb_init();

    while (1) {
        tud_task();
        u2f_hid_task();
        u2f_update_led();
        tight_loop_contents();
    }
}
