#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libusb-1.0/libusb.h>

#define VID 0x2e8a
#define PID 0x0009

#define VENDOR_INTERFACE 2

#define EP_OUT 0x03
#define EP_IN  0x83

int main(void)
{
    libusb_context *ctx = NULL;
    libusb_device_handle *dev_handle = NULL;

    int ret;

    /* Initialize libusb */
    ret = libusb_init(&ctx);

    if (ret < 0)
    {
        printf("libusb initialization failed: %s\n",
               libusb_error_name(ret));
        return 1;
    }

    printf("libusb initialized successfully\n");

    /* Open our Pico using VID and PID */
    dev_handle = libusb_open_device_with_vid_pid(ctx, VID, PID);

    if (dev_handle == NULL)
    {
        printf("Pico USB device not found!\n");
        libusb_exit(ctx);
        return 1;
    }

    printf("Pico USB device opened successfully\n");
    /* Claim the vendor-specific interface */
    ret = libusb_claim_interface(dev_handle, VENDOR_INTERFACE);

    if (ret < 0)
    {
        printf("Failed to claim Interface %d: %s\n",
               VENDOR_INTERFACE, libusb_error_name(ret));

        libusb_close(dev_handle);
        libusb_exit(ctx);
        return 1;
    }

    printf("Interface %d claimed successfully\n", VENDOR_INTERFACE);
    /* Send data to Pico */
    char message[] = "Hello Pico";
    int transferred = 0;

    ret = libusb_bulk_transfer(
        dev_handle,
        EP_OUT,
        (unsigned char *)message,
        strlen(message),
        &transferred,
        1000
    );

    if (ret < 0)
    {
        printf("Bulk OUT transfer failed: %s\n",
               libusb_error_name(ret));
    }
    else
    {
        printf("Sent %d bytes: %s\n", transferred, message);
    }    
    
        /* Receive echoed data from Pico */
    char rx_buffer[64] = {0};
    transferred = 0;

    ret = libusb_bulk_transfer(
        dev_handle,
        EP_IN,
        (unsigned char *)rx_buffer,
        sizeof(rx_buffer),
        &transferred,
        1000
    );

    if (ret < 0)
    {
        printf("Bulk IN transfer failed: %s\n",
               libusb_error_name(ret));
    }
    else
    {
        printf("Received %d bytes: %s\n",
               transferred, rx_buffer);
    }    
       /* Release interface and close device */
    libusb_release_interface(dev_handle, VENDOR_INTERFACE);
    libusb_close(dev_handle);
    libusb_exit(ctx);


    
    return 0;
}
