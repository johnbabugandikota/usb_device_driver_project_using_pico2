
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/slab.h>

/* Example VID/PID - replace with our Pico firmware IDs */
#define USB_DEMO_VENDOR_ID   0x2E8A
#define USB_DEMO_PRODUCT_ID  0x0009

/* Driver-private data */
struct usb_demo {
    struct usb_device *udev;
    struct usb_interface *interface;

    unsigned char bulk_in_endpoint;
    unsigned char bulk_out_endpoint;

    size_t bulk_in_maxpacket;
    size_t bulk_out_maxpacket;

};

/* Supported USB devices */
static const struct usb_device_id usb_demo_table[] = {
    { USB_DEVICE(USB_DEMO_VENDOR_ID, USB_DEMO_PRODUCT_ID) },
    { }
};

MODULE_DEVICE_TABLE(usb, usb_demo_table);

/* Called when a matching USB interface is connected */
static int usb_demo_probe(struct usb_interface *interface,
                          const struct usb_device_id *id)
{
    struct usb_demo *dev;
    struct usb_device *udev;
    struct usb_host_interface *iface_desc;
    struct usb_endpoint_descriptor *ep;
    int i;

    udev = interface_to_usbdev(interface);

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->udev = usb_get_dev(udev);
    dev->interface = interface;
    iface_desc=interface->cur_altsetting;
    for(i=0;i<iface_desc->desc.bNumEndpoints;i++){
	ep=&iface_desc->endpoint[i].desc;
    if (usb_endpoint_is_bulk_in(ep)) {

            dev->bulk_in_endpoint = ep->bEndpointAddress;

            dev->bulk_in_maxpacket = usb_endpoint_maxp(ep);

            dev_info(&interface->dev,
                     "Bulk IN endpoint: 0x%02X, MPS: %zu\n",
                     dev->bulk_in_endpoint,
                     dev->bulk_in_maxpacket);
        }

        if (usb_endpoint_is_bulk_out(ep)) {

            dev->bulk_out_endpoint = ep->bEndpointAddress;

            dev->bulk_out_maxpacket =usb_endpoint_maxp(ep);
	    dev_info(&interface->dev,
                     "Bulk OUT endpoint: 0x%02X, MPS: %zu\n",
                     dev->bulk_out_endpoint,
                     dev->bulk_out_maxpacket);    	
	}
    }
    if (!dev->bulk_in_endpoint || !dev->bulk_out_endpoint) {
    	dev_err(&interface->dev,
            "Required bulk endpoints not found\n");

    	usb_put_dev(dev->udev);
    	kfree(dev);

    	return -ENODEV;
      }    
    usb_set_intfdata(interface, dev);

    dev_info(&interface->dev,
             "USB device connected: VID=%04X PID=%04X\n",
             le16_to_cpu(udev->descriptor.idVendor),
             le16_to_cpu(udev->descriptor.idProduct));

    return 0;
}

/* Called when the USB interface is disconnected */
static void usb_demo_disconnect(struct usb_interface *interface)
{
    struct usb_demo *dev;

    dev = usb_get_intfdata(interface);

    usb_set_intfdata(interface, NULL);

    if (dev) {
        dev_info(&interface->dev, "USB device disconnected\n");

        usb_put_dev(dev->udev);
        kfree(dev);
    }
}

/* USB driver definition */
static struct usb_driver usb_demo_driver = {
    .name       = "usb_demo",
    .id_table   = usb_demo_table,
    .probe      = usb_demo_probe,
    .disconnect = usb_demo_disconnect,
};

/* Module initialization */
static int __init usb_demo_init(void)
{
    pr_info("USB demo driver loading\n");

    return usb_register(&usb_demo_driver);
}

/* Module cleanup */
static void __exit usb_demo_exit(void)
{
    usb_deregister(&usb_demo_driver);

    pr_info("USB demo driver unloaded\n");
}

module_init(usb_demo_init);
module_exit(usb_demo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("USB Driver Learning Project");
MODULE_DESCRIPTION("Basic Linux USB device driver");

