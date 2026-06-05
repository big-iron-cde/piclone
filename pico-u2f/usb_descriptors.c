/*
 * USB descriptors for a single-interface U2F HID device.
 */

#include "tusb.h"

#define USB_VID           0x1234
#define USB_PID           0x5678
#define USB_BCD           0x0200

#define STRING_MANUFACTURER 1
#define STRING_PRODUCT      2
#define STRING_SERIAL       3

/* ------------------------------------------------------------------ */
/*  HID Report Descriptor (U2F HID, 64-byte IN/OUT reports)           */
/* ------------------------------------------------------------------ */
static uint8_t const desc_hid_report[] = {
    0x06, 0xD0, 0xF1,   // Usage Page (FIDO Alliance)
    0x09, 0x01,         // Usage (U2F HID)
    0xA1, 0x01,         // Collection (Application)
    0x09, 0x20,         //   Usage (Input Report Data)
    0x15, 0x00,         //   Logical Minimum (0)
    0x26, 0xFF, 0x00,   //   Logical Maximum (255)
    0x75, 0x08,         //   Report Size (8)
    0x95, 0x40,         //   Report Count (64)
    0x81, 0x02,         //   Input (Data, Variable, Absolute)
    0x09, 0x21,         //   Usage (Output Report Data)
    0x15, 0x00,         //   Logical Minimum (0)
    0x26, 0xFF, 0x00,   //   Logical Maximum (255)
    0x75, 0x08,         //   Report Size (8)
    0x95, 0x40,         //   Report Count (64)
    0x91, 0x02,         //   Output (Data, Variable, Absolute)
    0xC0                // End Collection
};

/* ------------------------------------------------------------------ */
/*  Device descriptor                                                 */
/* ------------------------------------------------------------------ */
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = STRING_MANUFACTURER,
    .iProduct           = STRING_PRODUCT,
    .iSerialNumber      = STRING_SERIAL,
    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

/* ------------------------------------------------------------------ */
/*  Configuration descriptor                                          */
/* ------------------------------------------------------------------ */
enum {
    ITF_NUM_HID = 0,
    ITF_NUM_TOTAL
};

#define EPNUM_HID_IN    0x81
#define EPNUM_HID_OUT   0x01

#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

static uint8_t const desc_configuration[] = {
    // Config descriptor
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_SELF_POWERED, 100),
    // HID IN/OUT descriptor
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report), EPNUM_HID_OUT, EPNUM_HID_IN, 64, 5)
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

/* ------------------------------------------------------------------ */
/*  HID Report descriptor callback                                    */
/* ------------------------------------------------------------------ */
uint8_t const *tud_hid_descriptor_report_cb(uint8_t itf) {
    (void)itf;
    return desc_hid_report;
}

/* ------------------------------------------------------------------ */
/*  String descriptors                                                */
/* ------------------------------------------------------------------ */
static char const *string_desc_arr[] = {
    [STRING_MANUFACTURER] = "PicoU2F",
    [STRING_PRODUCT]      = "PicoU2F Security Key",
    [STRING_SERIAL]       = "000000000001"
};

static uint16_t _desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    if (index == 0) {
        _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 + 1 * 2);
        _desc_str[1] = 0x0409; // English (US)
        return _desc_str;
    }

    if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
        return NULL;
    }

    char const *str = string_desc_arr[index];
    if (!str) return NULL;

    uint8_t len = 0;
    while (str[len] && len < 31) len++;

    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 + len * 2);
    for (uint8_t i = 0; i < len; i++) {
        _desc_str[1 + i] = str[i];
    }
    return _desc_str;
}
