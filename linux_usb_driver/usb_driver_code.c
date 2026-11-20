
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
    struct urb *bulk_out_urb;
    unsigned char *bulk_out_buffer;
    struct urb *bulk_in_urb;
    unsigned char *bulk_in_buffer;
};

/* Supported USB devices */
static const struct usb_device_id usb_demo_table[] = {
    {
        USB_DEVICE_AND_INTERFACE_INFO(
            USB_DEMO_VENDOR_ID,
            USB_DEMO_PRODUCT_ID,
            0xFF,    /* Vendor-specific interface class */
            0x00,    /* Subclass */
            0x00     /* Protocol */
        ),
    },
    { }
};

MODULE_DEVICE_TABLE(usb, usb_demo_table);

static void usb_demo_bulk_out_complete(struct urb *urb)
{
    struct usb_demo *dev = urb->context;

    if (urb->status) {
        dev_err(&dev->interface->dev,
                "Bulk OUT transfer failed: %d\n",
                urb->status);
        return;
    }

    dev_info(&dev->interface->dev,
             "Bulk OUT transfer completed: %d bytes\n",
             urb->actual_length);
}

static void usb_demo_bulk_in_complete(struct urb *urb)
{
    struct usb_demo *dev = urb->context;

    if (urb->status) {
        dev_err(&dev->interface->dev,
                "Bulk IN transfer failed: %d\n",
                urb->status);
        return;
    }

    dev_info(&dev->interface->dev,
             "Bulk IN transfer completed: %d bytes\n",
             urb->actual_length);

    if (urb->actual_length > 0) {
        dev_info(&dev->interface->dev,
                 "Received data: %.*s\n",
                 urb->actual_length,
                 dev->bulk_in_buffer);
    }
}


/* Called when a matching USB interface is connected */
static int usb_demo_probe(struct usb_interface *interface,
                          const struct usb_device_id *id)
{
    struct usb_demo *dev;
    struct usb_device *udev;
    struct usb_host_interface *iface_desc;
    struct usb_endpoint_descriptor *ep;
    int i,ret;

    udev = interface_to_usbdev(interface);

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->udev = usb_get_dev(udev);
    dev->interface = interface;
/* Allocate Bulk OUT URB */
dev->bulk_out_urb = usb_alloc_urb(0, GFP_KERNEL);

if (!dev->bulk_out_urb) {
    dev_err(&interface->dev, "Failed to allocate Bulk OUT URB\n");
    usb_put_dev(dev->udev);
    kfree(dev);
    return -ENOMEM;
}

/* Allocate Bulk OUT transfer buffer */
dev->bulk_out_buffer = kmalloc(64, GFP_KERNEL);

if (!dev->bulk_out_buffer) {
    dev_err(&interface->dev, "Failed to allocate Bulk OUT buffer\n");
    usb_free_urb(dev->bulk_out_urb);
    usb_put_dev(dev->udev);
    kfree(dev);
    return -ENOMEM;
} 

/* Allocate Bulk IN URB */
dev->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);

if (!dev->bulk_in_urb) {
    dev_err(&interface->dev, "Failed to allocate Bulk IN URB\n");

    usb_free_urb(dev->bulk_out_urb);
    kfree(dev->bulk_out_buffer);
    usb_put_dev(dev->udev);
    kfree(dev);

    return -ENOMEM;
}

/* Allocate Bulk IN transfer buffer */
dev->bulk_in_buffer = kmalloc(64, GFP_KERNEL);

if (!dev->bulk_in_buffer) {
    dev_err(&interface->dev, "Failed to allocate Bulk IN buffer\n");

    usb_free_urb(dev->bulk_in_urb);
    usb_free_urb(dev->bulk_out_urb);
    kfree(dev->bulk_out_buffer);
    usb_put_dev(dev->udev);
    kfree(dev);

    return -ENOMEM;
}
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

	usb_free_urb(dev->bulk_in_urb);	
	usb_free_urb(dev->bulk_out_urb);
	kfree(dev->bulk_in_buffer);	
	kfree(dev->bulk_out_buffer);
    	usb_put_dev(dev->udev);
    	kfree(dev);

    	return -ENODEV;
      }   

/*set interface data*/

usb_set_intfdata(interface, dev);


/* Prepare test data */
memcpy(dev->bulk_out_buffer, "Hello Pico", 10);



/* Configure Bulk OUT URB */
usb_fill_bulk_urb(
    dev->bulk_out_urb,
    dev->udev,
    usb_sndbulkpipe(dev->udev, dev->bulk_out_endpoint),
    dev->bulk_out_buffer,
    10,
    usb_demo_bulk_out_complete,
    dev
);


/* Prepare Bulk IN URB */
usb_fill_bulk_urb(
    dev->bulk_in_urb,
    dev->udev,
    usb_rcvbulkpipe(dev->udev, dev->bulk_in_endpoint),
    dev->bulk_in_buffer,
    64,
    usb_demo_bulk_in_complete,
    dev
);

/* Submit Bulk IN URB */
ret = usb_submit_urb(dev->bulk_in_urb, GFP_KERNEL);

if (ret) {
    dev_err(&interface->dev,
            "Failed to submit Bulk IN URB: %d\n", ret);
    usb_set_intfdata(interface, NULL);\
    usb_free_urb(dev->bulk_in_urb);
    usb_free_urb(dev->bulk_out_urb);
    kfree(dev->bulk_in_buffer);
    kfree(dev->bulk_out_buffer);
    usb_put_dev(dev->udev);
    kfree(dev);

    return ret;
}



dev_info(&interface->dev, "Bulk IN URB submitted\n");
/* Submit URB bulk out asynchronously */

ret = usb_submit_urb(dev->bulk_out_urb, GFP_KERNEL);

if (ret) {
    dev_err(&interface->dev,
            "Failed to submit Bulk OUT URB: %d\n", ret);

    /* Stop the already-submitted IN URB */
    usb_kill_urb(dev->bulk_in_urb);

    usb_set_intfdata(interface, NULL);

    usb_free_urb(dev->bulk_in_urb);
    usb_free_urb(dev->bulk_out_urb);

    kfree(dev->bulk_in_buffer);
    kfree(dev->bulk_out_buffer);

    usb_put_dev(dev->udev);
    kfree(dev);

    return ret;
}

dev_info(&interface->dev, "Bulk OUT URB submitted\n");
   

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

	usb_kill_urb(dev->bulk_in_urb);
    /* Stop any active USB transfer */
    usb_kill_urb(dev->bulk_out_urb);

	/* Free both URBs */
    usb_free_urb(dev->bulk_in_urb);
    /* Free URB and transfer buffer */
    usb_free_urb(dev->bulk_out_urb);
	kfree(dev->bulk_in_buffer);    
	kfree(dev->bulk_out_buffer);

    /* Release device reference and private data */
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



/*Allocate dev
    ↓
Allocate OUT URB + buffer
    ↓
Allocate IN URB + buffer
    ↓
Discover endpoints
    ↓
Validate endpoints
    ↓
usb_set_intfdata(interface, dev)
    ↓
Prepare OUT data
    ↓
Configure IN URB
    ↓
Submit IN URB
    ↓
Configure OUT URB
    ↓
Submit OUT URB
    ↓
probe() returns */
