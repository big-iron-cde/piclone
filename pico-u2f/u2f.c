/*
 * U2F raw message processing and cryptography
 */

#include "u2f.h"
#include "sha256.h"
#include "pico/stdlib.h"
#include "pico/rand.h"
#include "hardware/gpio.h"
#include "uECC.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Pin config                                                        */
/* ------------------------------------------------------------------ */
#ifndef U2F_BUTTON_PIN
#define U2F_BUTTON_PIN  0
#endif

#define U2F_LED_PIN     PICO_DEFAULT_LED_PIN

/* ------------------------------------------------------------------ */
/*  Attestation material                                              */
/* ------------------------------------------------------------------ */
static const uint8_t attest_priv_key[32] = {
    0xcd, 0x8e, 0x71, 0xab, 0x7f, 0x08, 0x30, 0x2d,
    0x40, 0x09, 0x03, 0xa7, 0x3b, 0xd3, 0x12, 0x76,
    0x29, 0xe0, 0x2b, 0x73, 0x31, 0x9a, 0xc4, 0xe2,
    0x24, 0x06, 0x5d, 0x33, 0x52, 0x1b, 0x7d, 0x8c
};

static const uint8_t attest_cert[] = {
    0x30, 0x82, 0x01, 0x79, 0x30, 0x82, 0x01, 0x1f, 0xa0, 0x03, 0x02, 0x01,
    0x02, 0x02, 0x14, 0x27, 0x31, 0x9b, 0x0d, 0xab, 0xfd, 0x02, 0x5f, 0x7b,
    0x42, 0x24, 0xec, 0x7a, 0x1d, 0xf7, 0x7b, 0xb4, 0x27, 0x31, 0x51, 0x30,
    0x0a, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02, 0x30,
    0x12, 0x31, 0x10, 0x30, 0x0e, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x07,
    0x50, 0x69, 0x63, 0x6f, 0x55, 0x32, 0x46, 0x30, 0x1e, 0x17, 0x0d, 0x32,
    0x36, 0x30, 0x36, 0x30, 0x31, 0x31, 0x37, 0x30, 0x35, 0x31, 0x34, 0x5a,
    0x17, 0x0d, 0x33, 0x36, 0x30, 0x35, 0x32, 0x39, 0x31, 0x37, 0x30, 0x35,
    0x31, 0x34, 0x5a, 0x30, 0x12, 0x31, 0x10, 0x30, 0x0e, 0x06, 0x03, 0x55,
    0x04, 0x03, 0x0c, 0x07, 0x50, 0x69, 0x63, 0x6f, 0x55, 0x32, 0x46, 0x30,
    0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01,
    0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42,
    0x00, 0x04, 0xfa, 0x47, 0x83, 0xfb, 0x94, 0x09, 0xac, 0xab, 0x3e, 0x5f,
    0x70, 0xd6, 0x12, 0x7e, 0xb9, 0x19, 0x39, 0x81, 0x81, 0xcd, 0x90, 0x1a,
    0x23, 0x51, 0x11, 0xe2, 0xe4, 0x1d, 0x08, 0x25, 0xc4, 0xb7, 0x47, 0x9a,
    0x19, 0xf8, 0xbf, 0xa9, 0xd1, 0xe5, 0x47, 0xd0, 0xcf, 0x41, 0x1b, 0x19,
    0x55, 0x7b, 0xc8, 0x47, 0x91, 0x48, 0xb8, 0xda, 0x30, 0x43, 0x06, 0x4e,
    0x63, 0xca, 0x2d, 0x1e, 0xae, 0xd7, 0xa3, 0x53, 0x30, 0x51, 0x30, 0x1d,
    0x06, 0x03, 0x55, 0x1d, 0x0e, 0x04, 0x16, 0x04, 0x14, 0xde, 0x1c, 0x7d,
    0x3c, 0x19, 0x58, 0x8e, 0xef, 0x83, 0xd3, 0x88, 0xf3, 0x94, 0x2e, 0xa5,
    0x50, 0xc6, 0x2f, 0x2c, 0xc7, 0x30, 0x1f, 0x06, 0x03, 0x55, 0x1d, 0x23,
    0x04, 0x18, 0x30, 0x16, 0x80, 0x14, 0xde, 0x1c, 0x7d, 0x3c, 0x19, 0x58,
    0x8e, 0xef, 0x83, 0xd3, 0x88, 0xf3, 0x94, 0x2e, 0xa5, 0x50, 0xc6, 0x2f,
    0x2c, 0xc7, 0x30, 0x0f, 0x06, 0x03, 0x55, 0x1d, 0x13, 0x01, 0x01, 0xff,
    0x04, 0x05, 0x30, 0x03, 0x01, 0x01, 0xff, 0x30, 0x0a, 0x06, 0x08, 0x2a,
    0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02, 0x03, 0x48, 0x00, 0x30, 0x45,
    0x02, 0x20, 0x2b, 0x17, 0x09, 0x6d, 0x89, 0xd2, 0xbd, 0x19, 0x1b, 0x1b,
    0x82, 0xe8, 0xb1, 0x24, 0x17, 0x8b, 0x09, 0x9d, 0xbd, 0x09, 0x66, 0x43,
    0xa0, 0x32, 0xe8, 0x2e, 0xe0, 0x5a, 0x7d, 0x22, 0x8a, 0xbb, 0x02, 0x21,
    0x00, 0xdb, 0xef, 0x15, 0x14, 0x6e, 0x67, 0x77, 0xf4, 0xf7, 0x18, 0x05,
    0x78, 0xf3, 0x3b, 0x0f, 0x1c, 0x4a, 0x1c, 0x8f, 0xc0, 0x39, 0x4a, 0x32,
    0x11, 0x31, 0x6a, 0x9b, 0x71, 0xae, 0xec, 0x8e, 0x25
};

#define ATTEST_CERT_LEN  (sizeof(attest_cert))

/* ------------------------------------------------------------------ */
/*  Key handle format                                                 */
/* ------------------------------------------------------------------ */
#define KEY_MAGIC       0xA5
#define KEY_APP_LEN     32
#define KEY_PRIV_LEN    32
#define KEY_HANDLE_LEN  (1 + KEY_APP_LEN + KEY_PRIV_LEN)

/* ------------------------------------------------------------------ */
/*  Counter                                                           */
/* ------------------------------------------------------------------ */
static uint32_t auth_counter = 1;

/* ------------------------------------------------------------------ */
/*  RNG wrapper for micro-ecc                                         */
/* ------------------------------------------------------------------ */
static int u2f_rng(uint8_t *dest, unsigned size) {
    for (unsigned i = 0; i < size; i++) {
        dest[i] = (uint8_t)(get_rand_32() & 0xFF);
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/*  DER signature encoding                                            */
/* ------------------------------------------------------------------ */
static uint16_t der_encode_sig(const uint8_t raw[64], uint8_t *der) {
    uint8_t r_len = 32;
    uint8_t s_len = 32;
    uint8_t r_off = 0;
    uint8_t s_off = 0;

    while (r_len > 0 && raw[r_off] == 0) { r_off++; r_len--; }
    if (r_len > 0 && (raw[r_off] & 0x80)) r_len++;

    while (s_len > 0 && raw[s_off + 32] == 0) { s_off++; s_len--; }
    if (s_len > 0 && (raw[s_off + 32] & 0x80)) s_len++;

    uint8_t seq_len = 2 + r_len + 2 + s_len;
    uint16_t pos = 0;
    der[pos++] = 0x30;
    der[pos++] = seq_len;
    der[pos++] = 0x02;
    der[pos++] = r_len;
    if (r_len > 0 && (raw[r_off] & 0x80)) {
        der[pos++] = 0x00;
        r_len--;
    }
    memcpy(&der[pos], &raw[r_off], r_len);
    pos += r_len;
    der[pos++] = 0x02;
    der[pos++] = s_len;
    if (s_len > 0 && (raw[s_off + 32] & 0x80)) {
        der[pos++] = 0x00;
        s_len--;
    }
    memcpy(&der[pos], &raw[s_off + 32], s_len);
    pos += s_len;
    return pos;
}

/* ------------------------------------------------------------------ */
/*  User presence / LED                                               */
/* ------------------------------------------------------------------ */
static volatile bool wink_active = false;
static absolute_time_t wink_until;

void u2f_wink(void) {
    wink_active = true;
    wink_until = make_timeout_time_ms(500);
    gpio_put(U2F_LED_PIN, 1);
}

bool u2f_user_present(void) {
    // Active LOW button
    return !gpio_get(U2F_BUTTON_PIN);
}

void u2f_update_led(void) {
    if (wink_active && absolute_time_diff_us(get_absolute_time(), wink_until) <= 0) {
        wink_active = false;
        gpio_put(U2F_LED_PIN, 0);
    }
}

/* ------------------------------------------------------------------ */
/*  APDU helpers                                                      */
/* ------------------------------------------------------------------ */
static uint16_t parse_apdu(const uint8_t *req, uint16_t req_len,
                           uint8_t *out_ins, uint8_t *out_p1,
                           const uint8_t **out_data, uint16_t *out_data_len) {
    if (req_len < 7) return SW_WRONG_LENGTH;
    if (req[0] != 0x00) return SW_CLA_NOT_SUPPORTED;

    *out_ins = req[1];
    *out_p1  = req[2];

    // Extended length
    if (req[4] == 0x00 && req[5] == 0x00 && req[6] == 0x00) {
        // No data (VERSION style) — accept 7 or 8 bytes for compatibility
        *out_data = NULL;
        *out_data_len = 0;
        if (req_len != 7 && req_len != 8) return SW_WRONG_LENGTH;
        return SW_NO_ERROR;
    }

    uint16_t lc = ((uint16_t)req[5] << 8) | req[6];
    if (req_len != 7 + lc + 2) return SW_WRONG_LENGTH;
    *out_data = &req[7];
    *out_data_len = lc;
    return SW_NO_ERROR;
}

/* ------------------------------------------------------------------ */
/*  U2F command handlers                                              */
/* ------------------------------------------------------------------ */
static uint16_t handle_version(uint8_t *resp, uint16_t *resp_len) {
    memcpy(resp, "U2F_V2", 6);
    *resp_len = 6;
    return SW_NO_ERROR;
}

static uint16_t handle_register(const uint8_t *data, uint16_t data_len,
                                uint8_t *resp, uint16_t *resp_len) {
    if (data_len != 64) return SW_WRONG_LENGTH;
    const uint8_t *challenge = &data[0];
    const uint8_t *app_param = &data[32];

    // Wait for user presence (max 10 s)
    bool pressed = false;
    absolute_time_t deadline = make_timeout_time_ms(10000);
    while (absolute_time_diff_us(get_absolute_time(), deadline) > 0) {
        if (u2f_user_present()) {
            pressed = true;
            break;
        }
        sleep_ms(10);
    }
    if (!pressed) return SW_CONDITIONS_NOT_SATISFIED;

    // Generate key pair
    uint8_t priv_key[32];
    uint8_t pub_key[64]; // uncompressed x||y, will prepend 0x04
    for (int i = 0; i < 32; i++) priv_key[i] = (uint8_t)(get_rand_32() & 0xFF);
    if (!uECC_compute_public_key(priv_key, pub_key, uECC_secp256r1())) {
        return SW_WRONG_DATA;
    }

    // Build key handle
    uint8_t key_handle[KEY_HANDLE_LEN];
    key_handle[0] = KEY_MAGIC;
    memcpy(&key_handle[1], app_param, KEY_APP_LEN);
    memcpy(&key_handle[1 + KEY_APP_LEN], priv_key, KEY_PRIV_LEN);

    // Signature base
    uint8_t base[1 + 32 + 32 + KEY_HANDLE_LEN + 65];
    uint16_t base_len = 0;
    base[base_len++] = 0x00;
    memcpy(&base[base_len], app_param, 32); base_len += 32;
    memcpy(&base[base_len], challenge, 32); base_len += 32;
    memcpy(&base[base_len], key_handle, KEY_HANDLE_LEN); base_len += KEY_HANDLE_LEN;
    base[base_len++] = 0x04;
    memcpy(&base[base_len], pub_key, 64); base_len += 64;

    uint8_t hash[32];
    sha256(base, base_len, hash);

    uint8_t raw_sig[64];
    if (!uECC_sign(attest_priv_key, hash, 32, raw_sig, uECC_secp256r1())) {
        return SW_WRONG_DATA;
    }

    uint8_t der_sig[72];
    uint16_t der_len = der_encode_sig(raw_sig, der_sig);

    // Build response
    uint16_t pos = 0;
    resp[pos++] = 0x05; // reserved byte
    resp[pos++] = 0x04; // uncompressed point
    memcpy(&resp[pos], pub_key, 64); pos += 64;
    resp[pos++] = KEY_HANDLE_LEN;
    memcpy(&resp[pos], key_handle, KEY_HANDLE_LEN); pos += KEY_HANDLE_LEN;
    memcpy(&resp[pos], attest_cert, ATTEST_CERT_LEN); pos += ATTEST_CERT_LEN;
    memcpy(&resp[pos], der_sig, der_len); pos += der_len;

    *resp_len = pos;
    return SW_NO_ERROR;
}

static uint16_t handle_authenticate(uint8_t p1, const uint8_t *data, uint16_t data_len,
                                    uint8_t *resp, uint16_t *resp_len) {
    if (data_len < 65 + 1) return SW_WRONG_LENGTH;
    const uint8_t *challenge   = &data[0];
    const uint8_t *app_param     = &data[32];
    uint8_t        kh_len        = data[64];
    const uint8_t *key_handle    = &data[65];

    if (data_len != 65 + 1 + kh_len) return SW_WRONG_LENGTH;
    if (kh_len != KEY_HANDLE_LEN) return SW_WRONG_DATA;
    if (key_handle[0] != KEY_MAGIC) return SW_WRONG_DATA;
    if (memcmp(&key_handle[1], app_param, KEY_APP_LEN) != 0) return SW_WRONG_DATA;

    if (p1 == 0x07) {
        // check-only
        return SW_CONDITIONS_NOT_SATISFIED;
    }

    bool enforce_up = (p1 == 0x03);
    if (enforce_up) {
        bool pressed = false;
        absolute_time_t deadline = make_timeout_time_ms(10000);
        while (absolute_time_diff_us(get_absolute_time(), deadline) > 0) {
            if (u2f_user_present()) {
                pressed = true;
                break;
            }
            sleep_ms(10);
        }
        if (!pressed) return SW_CONDITIONS_NOT_SATISFIED;
    }

    uint8_t user_presence = 0x01; // user present
    auth_counter++;
    uint8_t counter_be[4] = {
        (uint8_t)(auth_counter >> 24),
        (uint8_t)(auth_counter >> 16),
        (uint8_t)(auth_counter >> 8),
        (uint8_t)(auth_counter)
    };

    uint8_t base[32 + 1 + 4 + 32];
    memcpy(&base[0],  app_param, 32);
    base[32] = user_presence;
    memcpy(&base[33], counter_be, 4);
    memcpy(&base[37], challenge, 32);

    uint8_t hash[32];
    sha256(base, sizeof(base), hash);

    const uint8_t *priv_key = &key_handle[1 + KEY_APP_LEN];
    uint8_t raw_sig[64];
    if (!uECC_sign(priv_key, hash, 32, raw_sig, uECC_secp256r1())) {
        return SW_WRONG_DATA;
    }

    uint8_t der_sig[72];
    uint16_t der_len = der_encode_sig(raw_sig, der_sig);

    uint16_t pos = 0;
    resp[pos++] = user_presence;
    memcpy(&resp[pos], counter_be, 4); pos += 4;
    memcpy(&resp[pos], der_sig, der_len); pos += der_len;

    *resp_len = pos;
    return SW_NO_ERROR;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */
void u2f_init(void) {
    uECC_set_rng(&u2f_rng);

    gpio_init(U2F_BUTTON_PIN);
    gpio_set_dir(U2F_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(U2F_BUTTON_PIN);

    gpio_init(U2F_LED_PIN);
    gpio_set_dir(U2F_LED_PIN, GPIO_OUT);
    gpio_put(U2F_LED_PIN, 0);
}

uint16_t u2f_handle_msg(const uint8_t *req, uint16_t req_len,
                        uint8_t *resp, uint16_t *resp_len) {
    uint8_t ins, p1;
    const uint8_t *data;
    uint16_t data_len;

    uint16_t sw = parse_apdu(req, req_len, &ins, &p1, &data, &data_len);
    if (sw != SW_NO_ERROR) return sw;

    switch (ins) {
        case 0x01: // U2F_REGISTER
            return handle_register(data, data_len, resp, resp_len);
        case 0x02: // U2F_AUTHENTICATE
            return handle_authenticate(p1, data, data_len, resp, resp_len);
        case 0x03: // U2F_VERSION
            return handle_version(resp, resp_len);
        default:
            return SW_INS_NOT_SUPPORTED;
    }
}
