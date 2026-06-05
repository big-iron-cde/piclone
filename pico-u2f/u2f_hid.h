#ifndef U2F_HID_H
#define U2F_HID_H

#include <stdint.h>
#include <stdbool.h>

#define U2FHID_PACKET_SIZE  64
#define U2FHID_MAX_PAYLOAD  1024

// U2FHID commands
#define U2FHID_PING         0x01
#define U2FHID_MSG          0x03
#define U2FHID_LOCK         0x04
#define U2FHID_INIT         0x06
#define U2FHID_WINK         0x08
#define U2FHID_ERROR        0x3f
#define U2FHID_VENDOR_FIRST 0x40
#define U2FHID_VENDOR_LAST  0x7f

// Error codes
#define ERR_INVALID_CMD     0x01
#define ERR_INVALID_PAR     0x02
#define ERR_INVALID_LEN     0x03
#define ERR_INVALID_SEQ     0x04
#define ERR_MSG_TIMEOUT     0x05
#define ERR_CHANNEL_BUSY    0x06

// Broadcast channel
#define CID_BROADCAST       0xffffffff

void u2f_hid_init(void);
void u2f_hid_receive(uint8_t const *packet);
void u2f_hid_task(void);

#endif
