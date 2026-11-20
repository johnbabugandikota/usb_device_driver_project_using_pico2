
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUSB_RHPORT1_MODE 0

// Enable USB device functionality
#define CFG_TUD_ENABLED 1

// USB endpoint 0 control transfer size
#define CFG_TUD_ENDPOINT0_SIZE 64

// Enable one CDC interface
#define CFG_TUD_CDC 1

// Enable one Vendor-specific interface
#define CFG_TUD_VENDOR 1

// CDC transfer buffers
#define CFG_TUD_CDC_RX_BUFSIZE 256
#define CFG_TUD_CDC_TX_BUFSIZE 256
#define CFG_TUD_CDC_EP_BUFSIZE 64

// Vendor-specific transfer buffers
#define CFG_TUD_VENDOR_RX_BUFSIZE 256
#define CFG_TUD_VENDOR_TX_BUFSIZE 256
#define CFG_TUD_VENDOR_EPSIZE 64

#endif

