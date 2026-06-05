#ifndef U2F_H
#define U2F_H

#include <stdint.h>
#include <stdbool.h>

// APDU status words
#define SW_NO_ERROR                 0x9000
#define SW_CONDITIONS_NOT_SATISFIED 0x6985
#define SW_WRONG_DATA               0x6A80
#define SW_WRONG_LENGTH             0x6700
#define SW_CLA_NOT_SUPPORTED        0x6E00
#define SW_INS_NOT_SUPPORTED        0x6D00

void u2f_init(void);
uint16_t u2f_handle_msg(const uint8_t *req, uint16_t req_len, uint8_t *resp, uint16_t *resp_len);
void u2f_wink(void);
bool u2f_user_present(void);
void u2f_update_led(void);

#endif
