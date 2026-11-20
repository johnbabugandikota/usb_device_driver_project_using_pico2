
#include "pico/stdlib.h"
#include "tusb.h"

#define LED_PIN 25
#define VENDOR_INTERFACE 0

int main(void)
{
    /* Initialize Pico GPIO */
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    /* Initialize TinyUSB device stack */
    tusb_init();

    uint8_t rx_buffer[64];
    uint32_t led_counter = 0;

    while (true)
    {
        /* Process USB events */
        tud_task();

        /* Check for data received on Vendor interface */
        if (tud_vendor_n_available(VENDOR_INTERFACE))
        {
            uint32_t received = tud_vendor_n_read(
                VENDOR_INTERFACE,
                rx_buffer,
                sizeof(rx_buffer)
            );

            if (received > 0)
            {
                /* Send received data back to host */
                uint32_t sent = tud_vendor_n_write(
                    VENDOR_INTERFACE,
                    rx_buffer,
                    received
                );

                /* Flush queued data to USB endpoint */
                if (sent > 0)
                {
                    tud_vendor_n_write_flush(VENDOR_INTERFACE);
                }
            }
        }

        /* Blink onboard LED */
        sleep_ms(1);
        led_counter++;

        if (led_counter >= 500)
        {
            gpio_xor_mask(1u << LED_PIN);
            led_counter = 0;
        }
    }

    return 0;
}

