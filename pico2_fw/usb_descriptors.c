
#include "tusb.h"
#include <string.h>

/* USB interface numbers */
enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_VENDOR,
    ITF_NUM_TOTAL
};

/* Endpoint addresses */
#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82

#define EPNUM_VENDOR_OUT  0x03
#define EPNUM_VENDOR_IN   0x83

/* Total configuration descriptor length */
#define CONFIG_TOTAL_LEN \
    (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_VENDOR_DESC_LEN)

/* USB Device Descriptor */
static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = 0x02,
    .bDeviceProtocol    = 0x01,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    /* Retain current VID/PID for initial testing */
    .idVendor           = 0x2E8A,
    .idProduct          = 0x0009,

    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1
};

/* Configuration Descriptor */
static uint8_t const desc_configuration[] = {

    /* Configuration */
    TUD_CONFIG_DESCRIPTOR(
        1,
        ITF_NUM_TOTAL,
        0,
        CONFIG_TOTAL_LEN,
        0x00,
        100
    ),

    /* CDC: Interface 0 + Interface 1 */
    TUD_CDC_DESCRIPTOR(
        ITF_NUM_CDC,
        4,
        EPNUM_CDC_NOTIF,
        8,
        EPNUM_CDC_OUT,
        EPNUM_CDC_IN,
        64
    ),

    /* Vendor-specific: Interface 2 */
    TUD_VENDOR_DESCRIPTOR(
        ITF_NUM_VENDOR,
        5,
        EPNUM_VENDOR_OUT,
        EPNUM_VENDOR_IN,
        64
    )
};

/* String Descriptors */
static char const *string_desc_arr[] = {
    NULL,
    "Raspberry Pi",
    "Pico 2 USB Device",
    "PICO2-USB-001",
    "Pico CDC Interface",
    "Pico Vendor Interface"
};

static uint16_t desc_string[32];

/* Device descriptor callback */
uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

/* Configuration descriptor callback */
uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    if (index == 0) {
        return desc_configuration;
    }

    return NULL;
}

/* String descriptor callback */
uint16_t const *tud_descriptor_string_cb(
    uint8_t index,
    uint16_t langid)
{
    (void)langid;

    uint8_t count;

    if (index == 0) {
        desc_string[1] = 0x0409;
        count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) /
                     sizeof(string_desc_arr[0])) {
            return NULL;
        }

        char const *str = string_desc_arr[index];

        count = (uint8_t)strlen(str);

        if (count > 31) {
            count = 31;
        }

        for (uint8_t i = 0; i < count; i++) {
            desc_string[1 + i] = (uint8_t)str[i];
        }
    }

    desc_string[0] = (TUSB_DESC_STRING << 8) | (2 * count + 2);

    return desc_string;
}

