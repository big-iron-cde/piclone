/*
 * U2F HID transport layer (FIDO U2F HID Protocol v1.2)
 */

#include "u2f_hid.h"
#include "u2f.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Protocol constants                                                */
/* ------------------------------------------------------------------ */
#define CID_RESERVED        0x00000000

#define INIT_DATA_LEN       57
#define CONT_DATA_LEN       59

#define CAPABILITY_WINK     0x01

/* ------------------------------------------------------------------ */
/*  State                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t cid;
    uint8_t  cmd;
    uint16_t bcnt;
    uint16_t len;
    uint8_t  buf[U2FHID_MAX_PAYLOAD];
    uint8_t  seq;
    bool     busy;
} rx_state_t;

static rx_state_t rx;

typedef struct {
    uint32_t cid;
    uint8_t  cmd;
    uint16_t bcnt;
    uint16_t sent;
    uint8_t  buf[U2FHID_MAX_PAYLOAD];
    bool     active;
} tx_state_t;

static tx_state_t tx;

static uint32_t next_cid = 1;

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */
static uint32_t read_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static void write_u32_be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

static void send_error(uint32_t cid, uint8_t code) {
    uint8_t pkt[U2FHID_PACKET_SIZE];
    memset(pkt, 0, sizeof(pkt));
    write_u32_be(pkt, cid);
    pkt[4] = U2FHID_ERROR;
    pkt[5] = 0;
    pkt[6] = 1;
    pkt[7] = code;
    tud_hid_report(0, pkt, U2FHID_PACKET_SIZE);
}

/* Queue a full response for transmission */
static void start_response(uint32_t cid, uint8_t cmd, const uint8_t *data, uint16_t len) {
    tx.cid = cid;
    tx.cmd = cmd;
    tx.bcnt = len;
    tx.sent = 0;
    tx.active = true;
    if (len > U2FHID_MAX_PAYLOAD) len = U2FHID_MAX_PAYLOAD;
    memcpy(tx.buf, data, len);
}

/* Build and try to send the next packet. Returns true if sent. */
static bool send_next_packet(void) {
    if (!tx.active) return false;
    if (!tud_hid_ready()) return false;

    uint8_t pkt[U2FHID_PACKET_SIZE];
    memset(pkt, 0, sizeof(pkt));
    write_u32_be(pkt, tx.cid);

    if (tx.sent == 0) {
        // Initialization packet
        pkt[4] = tx.cmd;
        pkt[5] = (uint8_t)(tx.bcnt >> 8);
        pkt[6] = (uint8_t)(tx.bcnt);
        uint16_t chunk = tx.bcnt;
        if (chunk > INIT_DATA_LEN) chunk = INIT_DATA_LEN;
        memcpy(&pkt[7], &tx.buf[0], chunk);
        tx.sent = chunk;
        if (tx.sent >= tx.bcnt) tx.active = false;
    } else {
        // Continuation packet
        pkt[4] = (tx.sent - INIT_DATA_LEN) / CONT_DATA_LEN; // seq number
        uint16_t chunk = tx.bcnt - tx.sent;
        if (chunk > CONT_DATA_LEN) chunk = CONT_DATA_LEN;
        memcpy(&pkt[5], &tx.buf[tx.sent], chunk);
        tx.sent += chunk;
        if (tx.sent >= tx.bcnt) tx.active = false;
    }

    tud_hid_report(0, pkt, U2FHID_PACKET_SIZE);
    return true;
}

/* ------------------------------------------------------------------ */
/*  Receive handling                                                  */
/* ------------------------------------------------------------------ */
static void process_complete_message(void) {
    uint32_t cid = rx.cid;
    uint8_t  cmd = rx.cmd;

    if (cmd == U2FHID_PING) {
        start_response(cid, U2FHID_PING, rx.buf, rx.bcnt);
        return;
    }

    if (cmd == U2FHID_INIT) {
        if (rx.bcnt < 8) {
            send_error(cid, ERR_INVALID_LEN);
            return;
        }
        uint8_t nonce[8];
        memcpy(nonce, rx.buf, 8);

        uint32_t resp_cid = (cid == CID_BROADCAST) ? next_cid++ : cid;
        if (next_cid == CID_RESERVED) next_cid = 1;
        if (next_cid == CID_BROADCAST) next_cid = 1;

        uint8_t resp[17];
        memcpy(resp, nonce, 8);
        write_u32_be(&resp[8], resp_cid);
        resp[12] = 2; // U2FHID protocol version
        resp[13] = 1; // major
        resp[14] = 0; // minor
        resp[15] = 0; // build
        resp[16] = CAPABILITY_WINK;
        start_response(cid, U2FHID_INIT, resp, sizeof(resp));
        return;
    }

    if (cmd == U2FHID_WINK) {
        u2f_wink();
        start_response(cid, U2FHID_WINK, NULL, 0);
        return;
    }

    if (cmd == U2FHID_LOCK) {
        // Not implemented; just acknowledge
        start_response(cid, U2FHID_LOCK, NULL, 0);
        return;
    }

    if (cmd == U2FHID_MSG) {
        uint8_t  resp[U2FHID_MAX_PAYLOAD];
        uint16_t resp_len = 0;
        uint16_t sw = u2f_handle_msg(rx.buf, rx.bcnt, resp, &resp_len);
        if (resp_len + 2 > U2FHID_MAX_PAYLOAD) {
            send_error(cid, ERR_INVALID_LEN);
            return;
        }
        resp[resp_len++] = (uint8_t)(sw >> 8);
        resp[resp_len++] = (uint8_t)(sw);
        start_response(cid, U2FHID_MSG, resp, resp_len);
        return;
    }

    send_error(cid, ERR_INVALID_CMD);
}

void u2f_hid_receive(uint8_t const *pkt) {
    uint32_t cid = read_u32_be(pkt);
    uint8_t  typ = pkt[4];

    if (typ & 0x80) {
        // Initialization packet
        uint8_t  cmd  = typ;
        uint16_t bcnt = ((uint16_t)pkt[5] << 8) | pkt[6];

        // Abort any active transaction if this is an INIT for the same CID or broadcast
        if (cmd == U2FHID_INIT) {
            // INIT always starts a new transaction, abort any active TX
            tx.active = false;
            rx.cid   = cid;
            rx.cmd   = cmd;
            rx.bcnt  = bcnt;
            rx.len   = 0;
            rx.seq   = 0;
            rx.busy  = true;
            if (bcnt > U2FHID_MAX_PAYLOAD) bcnt = U2FHID_MAX_PAYLOAD;
            uint16_t chunk = (bcnt < INIT_DATA_LEN) ? bcnt : INIT_DATA_LEN;
            memcpy(rx.buf, &pkt[7], chunk);
            rx.len = chunk;
            if (rx.len >= rx.bcnt) {
                process_complete_message();
                rx.busy = false;
            }
            return;
        }

        // If busy with a different CID, send busy error
        if (rx.busy && rx.cid != cid) {
            send_error(cid, ERR_CHANNEL_BUSY);
            return;
        }

        // Start new transaction (abort any active TX)
        tx.active = false;
        rx.cid  = cid;
        rx.cmd  = cmd;
        rx.bcnt = bcnt;
        rx.len  = 0;
        rx.seq  = 0;
        rx.busy = true;
        if (bcnt > U2FHID_MAX_PAYLOAD) bcnt = U2FHID_MAX_PAYLOAD;
        uint16_t chunk = (bcnt < INIT_DATA_LEN) ? bcnt : INIT_DATA_LEN;
        memcpy(rx.buf, &pkt[7], chunk);
        rx.len = chunk;
        if (rx.len >= rx.bcnt) {
            process_complete_message();
            rx.busy = false;
        }
    } else {
        // Continuation packet
        if (!rx.busy || rx.cid != cid) {
            return; // stray continuation packet
        }
        uint8_t seq = typ;
        if (seq != rx.seq) {
            send_error(cid, ERR_INVALID_SEQ);
            rx.busy = false;
            return;
        }
        rx.seq++;
        uint16_t remain = rx.bcnt - rx.len;
        uint16_t chunk = (remain < CONT_DATA_LEN) ? remain : CONT_DATA_LEN;
        memcpy(&rx.buf[rx.len], &pkt[5], chunk);
        rx.len += chunk;
        if (rx.len >= rx.bcnt) {
            process_complete_message();
            rx.busy = false;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */
void u2f_hid_init(void) {
    memset(&rx, 0, sizeof(rx));
    memset(&tx, 0, sizeof(tx));
    next_cid = 1;
}

void u2f_hid_task(void) {
    if (tx.active) {
        send_next_packet();
    }
}

/* ------------------------------------------------------------------ */
/*  TinyUSB callbacks                                                 */
/* ------------------------------------------------------------------ */
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void)itf; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    (void)itf;
    (void)report_id;
    (void)report_type;
    if (bufsize == U2FHID_PACKET_SIZE) {
        u2f_hid_receive(buffer);
    }
}
