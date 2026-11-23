
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/slab.h>

#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/kref.h>
#include <linux/poll.h>

#include <linux/list.h>

#include <linux/workqueue.h>


/* Example VID/PID - replace with our Pico firmware IDs */
#define USB_DEMO_VENDOR_ID   0x2E8A
#define USB_DEMO_PRODUCT_ID  0x0009
#define USB_DEMO_FIFO_SIZE 4096
#define USB_DEMO_WRITE_MAX 64




//device class
static struct class *usb_demo_class;


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


    /* Character device */
    dev_t dev_num;
    struct cdev cdev;
    struct device *device;

    /* Userspace receive FIFo*/
    char fifo[USB_DEMO_FIFO_SIZE];
    unsigned int fifo_head;
    unsigned int fifo_tail;
    unsigned int fifo_count;


    /* Synchronization */
    struct mutex buffer_lock;
    struct mutex io_mutex;
    wait_queue_head_t read_wait;
    spinlock_t write_lock;
    struct list_head write_list; // list to track pending write transactions

    struct workqueue_struct *rx_wq;
	struct kref kref;
    bool disconnected;

};



struct usb_demo_write {
    struct usb_demo *dev;
    struct urb *urb;
    unsigned char *buffer;
    struct list_head list; //list to track pending write req's
};

//This allows each completed USB packet to be copied into a separate work item, so the USB URB buffer can safely be reused.//
struct usb_demo_rx_work {
    struct work_struct work;
    struct usb_demo *dev;
    unsigned int len;
    unsigned char data[];
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

//fops structure
static int usb_demo_open(struct inode *inode, struct file *file);

static ssize_t usb_demo_read(struct file *file,
char __user *user_buf,
size_t count,
loff_t *ppos);

static int usb_demo_release(struct inode *inode,
struct file *file);
static const struct file_operations usb_demo_fops;

static ssize_t usb_demo_write(struct file *file,
const char __user *user_buffer,
size_t count,
loff_t *offset);



static void usb_demo_cancel_pending_writes(struct usb_demo *dev)
{
    struct urb *urb;
    unsigned long flags;

    while (1) {
        spin_lock_irqsave(&dev->write_lock, flags);

        if (list_empty(&dev->write_list)) {
            spin_unlock_irqrestore(&dev->write_lock, flags);
            break;
        }

        struct usb_demo_write *write_data =
        list_first_entry(&dev->write_list,
        struct usb_demo_write,
        list);

        urb = write_data->urb;
        usb_get_urb(urb);

        spin_unlock_irqrestore(&dev->write_lock, flags);

        usb_kill_urb(urb);
        usb_put_urb(urb);
    }

    pr_info("usb_demo: all pending write URBs cancelled\n");
}



/* What this below callback does
When the USB host controller completes the transfer:
1. Checks whether the transfer succeeded.
2. Logs the transferred byte count.
3. Frees the transfer buffer.
4. Frees the URB.
5. Frees the transfer structure. */



static void usb_demo_write_complete(struct urb *urb)
{
    struct usb_demo_write *write_data = urb->context;
    struct usb_demo *dev = write_data->dev;
    unsigned long flags;

    /* Remove this completed URB from the tracking list */
    spin_lock_irqsave(&dev->write_lock, flags);
    list_del_init(&write_data->list);
    spin_unlock_irqrestore(&dev->write_lock, flags);

    if (urb->status) {
        pr_err("usb_demo: userspace Bulk OUT transfer failed: %d\n",
        urb->status);
    } else {
        pr_info("usb_demo: userspace Bulk OUT completed: %d bytes\n",
        urb->actual_length);
    }

    kfree(write_data->buffer);
    usb_free_urb(write_data->urb);
    kfree(write_data);
}



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

static void usb_demo_rx_work_handler(struct work_struct *work)
{
    struct usb_demo_rx_work *rx =
        container_of(work, struct usb_demo_rx_work, work);

    struct usb_demo *dev = rx->dev;

    mutex_lock(&dev->buffer_lock);

    for (unsigned int i = 0; i < rx->len; i++) {
        if (dev->fifo_count == USB_DEMO_FIFO_SIZE) {
            dev_err(&dev->interface->dev,
                    "FIFO full, dropping incoming data\n");
            break;
        }

        dev->fifo[dev->fifo_head] = rx->data[i];
        dev->fifo_head = (dev->fifo_head + 1) % USB_DEMO_FIFO_SIZE;
        dev->fifo_count++;
    }

    mutex_unlock(&dev->buffer_lock);

    wake_up_interruptible(&dev->read_wait);

    kfree(rx);
}

//bulk in call back func
static void usb_demo_bulk_in_complete(struct urb *urb)
{
    struct usb_demo *dev = urb->context;
    struct usb_demo_rx_work *rx;
    int ret;

    if (urb->status) {
        if (urb->status != -ENOENT &&
            urb->status != -ECONNRESET &&
            urb->status != -ESHUTDOWN) {
            dev_err(&dev->interface->dev,
                   "Bulk IN URB error: %d\n", urb->status);
        } else {
            return;
        }
    }

    if (urb->actual_length > 0) {
        rx = kmalloc(sizeof(*rx) + urb->actual_length, GFP_ATOMIC);

        if (!rx) {
            dev_err(&dev->interface->dev,
                   "Failed to allocate RX work item\n");
        } else {
            rx->dev = dev;
            rx->len = urb->actual_length;

            INIT_WORK(&rx->work, usb_demo_rx_work_handler);

            memcpy(rx->data, urb->transfer_buffer,
                   urb->actual_length);

            if (!queue_work(dev->rx_wq, &rx->work))
                kfree(rx);
        }
    }

    if (!READ_ONCE(dev->disconnected)) {
        ret = usb_submit_urb(urb, GFP_ATOMIC);

        if (ret) {
            dev_err(&dev->interface->dev,
                   "Failed to resubmit IN URB: %d\n", ret);
        }
    }
}

//This allows userspace to wait for data availability using poll
static __poll_t usb_demo_poll(struct file *file, poll_table *wait)
{
    struct usb_demo *dev = file->private_data;
    __poll_t mask = 0;

    /* Register caller with the read wait queue */
    poll_wait(file, &dev->read_wait, wait);

    mutex_lock(&dev->buffer_lock);

    /* Data available */
    if (dev->fifo_count > 0)
    mask |= POLLIN | POLLRDNORM;

    /* Device disconnected */
    if (dev->disconnected)
    mask |= POLLHUP;

    mutex_unlock(&dev->buffer_lock);

    return mask;
}

//write func


/*write()
│
├── Validate length
├── Allocate transfer structure
├── Allocate kernel buffer
├── copy_from_user()
├── Allocate URB
├── Configure Bulk OUT URB
├── usb_submit_urb()
│
└── Return to userspace immediately
│
▼
USB transfer
│
▼
Completion callback
│
└── Free resources */



static ssize_t usb_demo_write(struct file *file,
const char __user *user_buffer,
size_t count,
loff_t *offset)
{
    struct usb_demo *dev = file->private_data;
    struct usb_demo_write *write_data;
    int ret;
    unsigned long flags;

    if (count == 0)
    return 0;

    if (count > USB_DEMO_WRITE_MAX)
    return -EINVAL;

    mutex_lock(&dev->io_mutex);

    if (dev->disconnected) {
        ret = -ENODEV;
        goto unlock;
    }

    write_data = kzalloc(sizeof(*write_data), GFP_KERNEL);
    if (!write_data) {
        ret = -ENOMEM;
        goto unlock;
    }

    write_data->buffer = kmalloc(count, GFP_KERNEL);
    if (!write_data->buffer) {
        ret = -ENOMEM;
        goto free_write_data;
    }

    if (copy_from_user(write_data->buffer, user_buffer, count)) {
        ret = -EFAULT;
        goto free_buffer;
    }

    write_data->urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!write_data->urb) {
        ret = -ENOMEM;
        goto free_buffer;
    }

    usb_fill_bulk_urb(
    write_data->urb,
    dev->udev,
    usb_sndbulkpipe(dev->udev, dev->bulk_out_endpoint),
    write_data->buffer,
    count,
    usb_demo_write_complete,
    write_data
    );

    write_data->dev = dev;
    INIT_LIST_HEAD(&write_data->list);

    spin_lock_irqsave(&dev->write_lock, flags);
    list_add(&write_data->list, &dev->write_list);
    spin_unlock_irqrestore(&dev->write_lock, flags);

    ret = usb_submit_urb(write_data->urb, GFP_KERNEL);

    if (ret) {
        dev_err(&dev->interface->dev,
        "Failed to submit userspace Bulk OUT URB: %d\n",
        ret);

        spin_lock_irqsave(&dev->write_lock, flags);
        list_del_init(&write_data->list);
        spin_unlock_irqrestore(&dev->write_lock, flags);

        usb_free_urb(write_data->urb);
        goto free_buffer;
    }

    dev_info(&dev->interface->dev,
    "Userspace Bulk OUT submitted: %zu bytes\n",
    count);

    mutex_unlock(&dev->io_mutex);

    return count;

    free_buffer:
    kfree(write_data->buffer);

    free_write_data:
    kfree(write_data);

    unlock:
    mutex_unlock(&dev->io_mutex);
    return ret;
}





static void usb_demo_delete(struct kref *kref)
{
    struct usb_demo *dev;

    dev = container_of(kref, struct usb_demo, kref);

    /* Free URBs */
    if (dev->bulk_in_urb)
    usb_free_urb(dev->bulk_in_urb);

    if (dev->bulk_out_urb)
    usb_free_urb(dev->bulk_out_urb);

    /* Free transfer buffers */
    kfree(dev->bulk_in_buffer);
    kfree(dev->bulk_out_buffer);

    /* Release USB device reference */
    if(dev->udev)
    usb_put_dev(dev->udev);

     if(dev->rx_wq)
       destroy_workqueue(dev->rx_wq);

    /* Free driver-private structure */
    kfree(dev);
}



//usb demo read func
static ssize_t usb_demo_read(struct file *file,
char __user *user_buf,
size_t count,
loff_t *ppos)
{
    struct usb_demo *dev = file->private_data;
    unsigned int bytes_to_read;
    unsigned int first_chunk;
    char *temp;
    ssize_t ret;

    if (count == 0)
    return 0;

retry: 
    /* Wait until data is available or device disconnects */
    ret = wait_event_interruptible(dev->read_wait,
    READ_ONCE(dev->fifo_count) > 0 ||
    READ_ONCE(dev->disconnected));

    if (ret)
    return ret;

    mutex_lock(&dev->buffer_lock);

    /* No data remaining and device disconnected */
    if (dev->fifo_count == 0 && dev->disconnected) {
        mutex_unlock(&dev->buffer_lock);
        return -ENODEV;
    }

 /* Another reader consumed the data */
    if (dev->fifo_count == 0) {
        mutex_unlock(&dev->buffer_lock);
        goto retry;
    }


    bytes_to_read = min_t(unsigned int, count, dev->fifo_count);

    temp = kmalloc(bytes_to_read, GFP_KERNEL);
    if (!temp) {
        mutex_unlock(&dev->buffer_lock);
        return -ENOMEM;
    }

    /* First contiguous portion */
    first_chunk = min(bytes_to_read,
    USB_DEMO_FIFO_SIZE - dev->fifo_tail);

    memcpy(temp, &dev->fifo[dev->fifo_tail], first_chunk);

    /* Remaining portion, if FIFO wraps around */
    if (bytes_to_read > first_chunk) {
        memcpy(temp + first_chunk,
        dev->fifo,
        bytes_to_read - first_chunk);
    }

    /* Copy data to userspace */
    if (copy_to_user(user_buf, temp, bytes_to_read)) {
        ret = -EFAULT;
    } else {
        /* Update FIFO only after successful userspace copy */
        dev->fifo_tail =
        (dev->fifo_tail + bytes_to_read) % USB_DEMO_FIFO_SIZE;

        dev->fifo_count -= bytes_to_read;

        ret = bytes_to_read;
    }

    kfree(temp);
    mutex_unlock(&dev->buffer_lock);

    return ret;
}


//open function

static int usb_demo_open(struct inode *inode, struct file *file)
{
    struct usb_demo *dev;

    dev = container_of(inode->i_cdev, struct usb_demo, cdev);

    mutex_lock(&dev->buffer_lock);

    if (dev->disconnected ||
    !kref_get_unless_zero(&dev->kref)) {
        mutex_unlock(&dev->buffer_lock);
        return -ENODEV;
    }

    file->private_data = dev;

    mutex_unlock(&dev->buffer_lock);

    return 0;
}


static int usb_demo_release(struct inode *inode, struct file *file)
{
    struct usb_demo *dev = file->private_data;

    kref_put(&dev->kref, usb_demo_delete);

    return 0;
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

    kref_init(&dev->kref);
    dev->udev = usb_get_dev(udev);
    dev->interface = interface;


    /* Initialize userspace receive buffer synchronization */
    mutex_init(&dev->buffer_lock);
    mutex_init(&dev->io_mutex);

    spin_lock_init(&dev->write_lock);
    INIT_LIST_HEAD(&dev->write_list);
    init_waitqueue_head(&dev->read_wait);


    /* Allocate character device number */
    ret = alloc_chrdev_region(&dev->dev_num, 0, 1, "usb_demo");

    if (ret) {
        dev_err(&interface->dev,
        "Failed to allocate character device number\n");
        goto error_cleanup;
    }

    /* Initialize character device */
    dev->cdev.owner = THIS_MODULE;


    

    /* Allocate Bulk OUT URB */
    dev->bulk_out_urb = usb_alloc_urb(0, GFP_KERNEL);

    if (!dev->bulk_out_urb) {
        dev_err(&interface->dev, "Failed to allocate Bulk OUT URB\n");
        ret = -ENOMEM;
        goto error_device;
    }

    /* Allocate Bulk OUT transfer buffer */
    dev->bulk_out_buffer = kmalloc(64, GFP_KERNEL);

    if (!dev->bulk_out_buffer) {

        dev_err(&interface->dev,
        "Failed to allocate Bulk OUT buffer\n");
        ret = -ENOMEM;
        goto error_device;

    }

    /* Allocate Bulk IN URB */
    dev->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);

    if (!dev->bulk_in_urb) {

        dev_err(&interface->dev,
        "Failed to allocate Bulk IN URB\n");
        ret = -ENOMEM;
        goto error_device;
    }

    /* Allocate Bulk IN transfer buffer */
    dev->bulk_in_buffer = kmalloc(64, GFP_KERNEL);

    if (!dev->bulk_in_buffer) {
        // dev_err(&interface->dev, "Failed to allocate Bulk IN buffer\n");
        dev_err(&interface->dev,
        "Failed to allocate Bulk IN buffer\n");
        ret = -ENOMEM;
        goto error_device;
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

        if (!dev->bulk_in_endpoint ||
    !dev->bulk_out_endpoint ||
    !dev->bulk_in_maxpacket ||
    !dev->bulk_out_maxpacket) {    	
		dev_err(&interface->dev,"Required bulk endpoints not found\n");

        ret = -ENODEV;
        goto error_device;
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

	dev->rx_wq = alloc_workqueue("usb_demo_rx", WQ_UNBOUND, 1);

	if (!dev->rx_wq) {
    		ret = -ENOMEM;
    		goto error_cleanup;
	}
    /* Submit Bulk IN URB */
    ret = usb_submit_urb(dev->bulk_in_urb, GFP_KERNEL);

    if (ret) {
        dev_err(&interface->dev,
        "Failed to submit Bulk IN URB: %d\n", ret);

        usb_set_intfdata(interface, NULL);
        goto error_device;
    }

    dev_info(&interface->dev, "Bulk IN URB submitted\n");
    /* Submit URB bulk out asynchronously */

    ret = usb_submit_urb(dev->bulk_out_urb, GFP_KERNEL);

    if (ret) {
        dev_err(&interface->dev,
        "Failed to submit Bulk OUT URB: %d\n", ret);

        usb_kill_urb(dev->bulk_in_urb);
        usb_set_intfdata(interface, NULL);

        goto error_device;
    }

    dev_info(&interface->dev, "Bulk OUT URB submitted\n");



/* Initialize character device */
cdev_init(&dev->cdev, &usb_demo_fops);
dev->cdev.owner = THIS_MODULE;

/* Add character device to kernel */
ret = cdev_add(&dev->cdev, dev->dev_num, 1);

if (ret) {
    dev_err(&interface->dev,
            "Failed to add character device\n");

    usb_kill_urb(dev->bulk_in_urb);
    usb_kill_urb(dev->bulk_out_urb);

    usb_set_intfdata(interface, NULL);

    goto error_unregister;
}

/* Create device node only after USB initialization succeeds */
dev->device = device_create(
    usb_demo_class,
    &interface->dev,
    dev->dev_num,
    dev,
    "usb_demo"
);

if (IS_ERR(dev->device)) {
    ret = PTR_ERR(dev->device);
    dev_err(&interface->dev,
           "Failed to create /dev/usb_demo\n");

    usb_kill_urb(dev->bulk_in_urb);
    usb_kill_urb(dev->bulk_out_urb);

    usb_set_intfdata(interface, NULL);
    goto error_cdev;
}

    dev_info(&interface->dev,
    "USB device connected: VID=%04X PID=%04X\n",
    le16_to_cpu(udev->descriptor.idVendor),
    le16_to_cpu(udev->descriptor.idProduct));

    return 0;


   error_device:
    if (!IS_ERR_OR_NULL(dev->device))
        device_destroy(usb_demo_class, dev->dev_num);
    goto error_unregister;

error_cdev:
    cdev_del(&dev->cdev);

error_unregister:
    unregister_chrdev_region(dev->dev_num, 1);

error_cleanup:
    kref_put(&dev->kref, usb_demo_delete);
    return ret;


}

/* Called when the USB interface is disconnected */

static void usb_demo_disconnect(struct usb_interface *interface)
{
    struct usb_demo *dev;

    dev = usb_get_intfdata(interface);
    usb_set_intfdata(interface, NULL);

    if (!dev)
    return;

    dev_info(&interface->dev, "USB device disconnected\n");

    /* Mark device as disconnected */
    /* Prevent new write URBs from being submitted */
    mutex_lock(&dev->io_mutex);

    mutex_lock(&dev->buffer_lock);
    dev->disconnected = true;
    mutex_unlock(&dev->buffer_lock);

    mutex_unlock(&dev->io_mutex);

    /* Cancel all outstanding userspace write URBs */
    usb_demo_cancel_pending_writes(dev);

    /* Stop active USB transfers */
    usb_kill_urb(dev->bulk_in_urb);

	flush_workqueue(dev->rx_wq);
    usb_kill_urb(dev->bulk_out_urb);

    /* Wake any userspace process blocked in read() */
    wake_up_interruptible(&dev->read_wait);

    /* Remove userspace device node */
    device_destroy(usb_demo_class, dev->dev_num);

    /* Prevent further opens */
    cdev_del(&dev->cdev);

    /* Release device number */
    unregister_chrdev_region(dev->dev_num, 1);

    /* Release driver's reference */
    kref_put(&dev->kref, usb_demo_delete);


}


/* USB driver definition */
static struct usb_driver usb_demo_driver = {
    .name       = "usb_demo",
    .id_table   = usb_demo_table,
    .probe      = usb_demo_probe,
    .disconnect = usb_demo_disconnect,
};


//fops struct

static const struct file_operations usb_demo_fops = {
    .owner = THIS_MODULE,
    .open    = usb_demo_open,
    .read    = usb_demo_read,
    .release = usb_demo_release,
    .poll = usb_demo_poll,
    .write= usb_demo_write,
};


/* Module initialization */

static int __init usb_demo_init(void)
{
    int ret;

    pr_info("USB demo driver loading\n");

    usb_demo_class = class_create("usb_demo");

    if (IS_ERR(usb_demo_class))
    return PTR_ERR(usb_demo_class);

    ret = usb_register(&usb_demo_driver);

    if (ret) {
        class_destroy(usb_demo_class);
        return ret;
    }

    return 0;
}



/* Module cleanup */

static void __exit usb_demo_exit(void)
{
    usb_deregister(&usb_demo_driver);

    class_destroy(usb_demo_class);

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
