#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tharun Ganeshram");
MODULE_DESCRIPTION("Autonomous System Health Monitor & Dynamic Resource Adjuster");
MODULE_VERSION("1.0");


#define MAX_WORKLOAD_LEVEL 100
#define MAX_RESOURCE_FACTOR 10

// Global data structure for tracking system data
struct auto_monitor_data {
    ktime_t last_check_time;
    unsigned long current_sim_workload_level;   // 0-MAX_WORKLOAD_LEVEL (simulated %)
    unsigned long resource_allocation_factor;   // 1-MAX_RESOURCE_FACTOR (simulated resource units)
    atomic_t critical_alerts;                   // Atomic counter for critical events
    atomic_t timer_ticks;                       // To count timer firings
    unsigned long simulated_gpu_temp;           // Simulated temperature (degrees Celsius)
    unsigned long simulated_memory_pressure;    // 0-MAX_MEMORY_PRESSURE (simulated %)
};
static struct auto_monitor_data monitor_state;

// Synchronization
static DEFINE_SPINLOCK(monitor_data_spinlock); // Protects monitor_state fields from access by HRTimer callback (atomic context)
static struct mutex monitor_config_mutex;     // Protects monitor_state fields from access by workqueue and user-space (process context)

// HRTimer
static struct hrtimer monitor_hrtimer;
#define HRTIMER_INTERVAL_MS 100

// Workqueue
static struct workqueue_struct *monitor_wq;
static struct work_struct monitor_work;

// Character Device
static int major_number;
static struct class* auto_monitor_class = NULL;
static struct device* auto_monitor_device = NULL;
#define DEVICE_NAME "auto_monitor"
#define CLASS_NAME "auto_monitor_class"

// Sysfs Attributes
static ssize_t workload_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t workload_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count);
static ssize_t resource_factor_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t alerts_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);

static struct kobj_attribute workload_attribute = __ATTR(current_workload, 0664, workload_show, workload_store);    // Read/Write
static struct kobj_attribute resource_attribute = __ATTR(resource_factor, 0444, resource_factor_show, NULL);        // Read-only
static struct kobj_attribute alerts_attribute = __ATTR(critical_alerts, 0444, alerts_show, NULL);                   // Read-only

static struct attribute *auto_monitor_attrs[] = {
    &workload_attribute.attr,
    &resource_attribute.attr,
    &alerts_attribute.attr,
    NULL,
};

static struct attribute_group auto_monitor_attr_group = {
    .attrs = auto_monitor_attrs,
};

static struct kobject *auto_monitor_kobj;

// Function Prototypes
static int auto_monitor_open(struct inode *inode, struct file *file);
static int auto_monitor_release(struct inode *inode, struct file *file);
static ssize_t auto_monitor_read(struct file *file, char __user *buf, size_t len, loff_t *offset);
static ssize_t auto_monitor_write(struct file *file, const char __user *buf, size_t len, loff_t *offset);

// Map use-space file system calls to functions
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = auto_monitor_open,
    .release = auto_monitor_release,
    .read = auto_monitor_read,
    .write = auto_monitor_write,
};

// Workqueue Handler (process context)
static void monitor_work_handler(struct work_struct *work)
{
    unsigned long flags;
    unsigned long current_wl, current_rf;

    // Protect monitor_state with mutex (against processes that can sleep)
    mutex_lock(&monitor_config_mutex);

    // Use spin_lock to safely read workload (modified in HRTimer)
    spin_lock_irqsave(&monitor_data_spinlock, flags);
    current_wl = monitor_state.current_sim_workload_level;
    spin_unlock_irqrestore(&monitor_data_spinlock, flags);

    current_rf = monitor_state.resource_allocation_factor;

    // Dynamic Resource Adjustment
    // Increase resource factor if workload is high, decrease if low.
    if (current_wl > 80 && current_rf < MAX_RESOURCE_FACTOR) {
        monitor_state.resource_allocation_factor++;
        printk(KERN_INFO "%s: Workload High (%lu%%), Increasing Resource Factor to %lu\n",
               DEVICE_NAME, current_wl, monitor_state.resource_allocation_factor);
        if (monitor_state.resource_allocation_factor == MAX_RESOURCE_FACTOR) {
            atomic_inc(&monitor_state.critical_alerts);
            printk(KERN_WARNING "%s: Critical Alert: Max Resources Reached!\n", DEVICE_NAME);
        }
    } else if (current_wl < 20 && current_rf > 1) {
        monitor_state.resource_allocation_factor--;
        printk(KERN_INFO "%s: Workload Low (%lu%%), Decreasing Resource Factor to %lu\n",
               DEVICE_NAME, current_wl, monitor_state.resource_allocation_factor);
    } else {
        printk(KERN_INFO "%s: Workload Stable (%lu%%), Resource Factor %lu\n",
               DEVICE_NAME, current_wl, monitor_state.resource_allocation_factor);
    }

    mutex_unlock(&monitor_config_mutex);
}

// HRTimer Callback (atomic context)
static enum hrtimer_restart monitor_timer_callback(struct hrtimer *timer)
{
    ktime_t now = ktime_get();
    unsigned long flags;

    // try to aquire spin lock (atomic context cannot sleep)
    spin_lock_irqsave(&monitor_data_spinlock, flags);

    //update time measures
    monitor_state.last_check_time = now;
    atomic_inc(&monitor_state.timer_ticks);

    // Simulate workload fluctuation, temp, and memory pressure
    // Would read real metrics from system or sensors outside of simulation context
    // Update every second
    if (atomic_read(&monitor_state.timer_ticks) % 10 == 0) {
        // Simulate a fluctuating workload around 50%, with occasional spikes (arbitrary)
        monitor_state.current_sim_workload_level = (monitor_state.current_sim_workload_level + get_random_u32() % 20 - 10);
        // Keep in bounds [0, MAX_WORKLOAD_LEVEL]
        if (monitor_state.current_sim_workload_level > MAX_WORKLOAD_LEVEL) monitor_state.current_sim_workload_level = MAX_WORKLOAD_LEVEL;
        else if (monitor_state.current_sim_workload_level < 0) monitor_state.current_sim_workload_level = 0;
    }

    // Simulated temp and memory pressure increase with workload (arbitrary)
    monitor_state.simulated_gpu_temp = 50 + (monitor_state.current_sim_workload_level / 2);
    monitor_state.simulated_memory_pressure = (monitor_state.current_sim_workload_level * 2) / 3;

    spin_unlock_irqrestore(&monitor_data_spinlock, flags);

    // Schedule monitor_state processing work to the workqueue (non-atomics)
    schedule_work(&monitor_work);

    // Restart the timer for the next interval
    hrtimer_forward_now(timer, ms_to_ktime(HRTIMER_INTERVAL_MS));
    return HRTIMER_RESTART;
}

// Sysfs show/store implementations
static ssize_t workload_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    unsigned long flags;
    unsigned long workload;
    spin_lock_irqsave(&monitor_data_spinlock, flags);
    workload = monitor_state.current_sim_workload_level;
    spin_unlock_irqrestore(&monitor_data_spinlock, flags);
    return sprintf(buf, "%lu\n", workload);
}

static ssize_t workload_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    unsigned long new_workload;
    unsigned long flags;

    // Convert string to unsigned long
    if (kstrtoul(buf, 10, &new_workload) < 0) return -EINVAL;

    if (new_workload > MAX_WORKLOAD_LEVEL) new_workload = MAX_WORKLOAD_LEVEL;
    else if (new_workload < 0) new_workload = 0;

    // Safely set current_sim_workload_level and update simulated temp and memory pressure
    spin_lock_irqsave(&monitor_data_spinlock, flags);

    monitor_state.current_sim_workload_level = new_workload;
    // Simulated temp and memory pressure increase with workload (arbitrary)
    monitor_state.simulated_gpu_temp = 50 + (new_workload / 2);
    monitor_state.simulated_memory_pressure = (new_workload * 2) / 3;
    
    spin_unlock_irqrestore(&monitor_data_spinlock, flags);

    printk(KERN_INFO "%s: User injected workload: %lu%%\n", DEVICE_NAME, new_workload);

    // Schedule immediate monitor_state processing
    schedule_work(&monitor_work);
    return count;
}

static ssize_t resource_factor_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    unsigned long factor;
    mutex_lock(&monitor_config_mutex);
    factor = monitor_state.resource_allocation_factor;
    mutex_unlock(&monitor_config_mutex);
    return sprintf(buf, "%lu\n", factor);
}

static ssize_t alerts_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", atomic_read(&monitor_state.critical_alerts));
}

// Character Device File Operations
static int auto_monitor_open(struct inode *inode, struct file *file)
{
    try_module_get(THIS_MODULE);
    printk(KERN_INFO "%s: Device opened.\n", DEVICE_NAME);
    return 0;
}

static int auto_monitor_release(struct inode *inode, struct file *file)
{
    module_put(THIS_MODULE);
    printk(KERN_INFO "%s: Device closed.\n", DEVICE_NAME);
    return 0;
}

static ssize_t auto_monitor_read(struct file *file, char __user *buf, size_t len, loff_t *offset)
{
    char summary_buf[256];
    int len_summary;
    unsigned long flags;


    printk(KERN_INFO "%s: Read requested. Params: max_return_len=%zu, summary_offset=%lld\n", DEVICE_NAME, len, (long long)*offset);

    // Protect monitor_state from atomic and process context
    mutex_lock(&monitor_config_mutex);
    spin_lock_irqsave(&monitor_data_spinlock, flags);

    // Fill summary_buf with monitor_states values
    len_summary = snprintf(summary_buf, sizeof(summary_buf),
                   "Workload: %lu%%\nResource Factor: %lu\nCritical Alerts: %d\nSimulated GPU Temp: %luC\nSimulated Memory Pressure: %lu%%\nTimer Ticks: %d\n",
                   monitor_state.current_sim_workload_level,
                   monitor_state.resource_allocation_factor,
                   atomic_read(&monitor_state.critical_alerts),
                   monitor_state.simulated_gpu_temp,
                   monitor_state.simulated_memory_pressure,
                   atomic_read(&monitor_state.timer_ticks));

    spin_unlock_irqrestore(&monitor_data_spinlock, flags);
    mutex_unlock(&monitor_config_mutex);
    
    printk(KERN_INFO "%s: Read total summary length=%d\n", DEVICE_NAME, len_summary);

    // Account for EOF
    if (*offset >= len_summary)
        return 0;

    // Copy summary_buf to user buf accounting for offset and max len
    ssize_t bytes_to_copy = min((size_t)len_summary - *offset, len);

    if (copy_to_user(buf, summary_buf + *offset, bytes_to_copy)){
        printk(KERN_ERR "%s: Failed to copy data to user space.\n", DEVICE_NAME);
        return -EFAULT;
    }

    // Update offset
    *offset += bytes_to_copy;

    printk(KERN_INFO "%s: Read returning %zu bytes.\n", DEVICE_NAME, bytes_to_copy);
    return bytes_to_copy;
}

static ssize_t auto_monitor_write(struct file *file, const char __user *buf, size_t len, loff_t *offset)
{
    char kbuf[256];
    unsigned long value;
    unsigned long flags;

    if (len > sizeof(kbuf) - 1)
        return -EINVAL;

    if (copy_from_user(kbuf, buf, len)){
        printk(KERN_ERR "%s: Failed to copy data from user space.\n", DEVICE_NAME);
        return -EFAULT;
    }

    // Null terminate
    kbuf[len] = '\0';

    // Simple write mechanism to set simulated workload for now (can add more functionality)
    // Convert string to unsigned long
    if (kstrtoul(kbuf, 10, &value) < 0)
        return -EINVAL;

    if (value > MAX_WORKLOAD_LEVEL) value = MAX_WORKLOAD_LEVEL;
    else if (value < 0) value = 0;

    // Safely set current_sim_workload_level and update simulated temp and memory pressure
    spin_lock_irqsave(&monitor_data_spinlock, flags);

    monitor_state.current_sim_workload_level = value;
    // Simulated temp and memory pressure increase with workload (arbitrary)
    monitor_state.simulated_gpu_temp = 50 + (value / 2);
    monitor_state.simulated_memory_pressure = (value * 2) / 3;

    spin_unlock_irqrestore(&monitor_data_spinlock, flags);

    printk(KERN_INFO "%s: /dev/auto_monitor user wrote simulated workload: %lu%%\n", DEVICE_NAME, value);
    
    // Schedule immediate monitor_state processing
    schedule_work(&monitor_work);
    return len;
}


// Module init
static int __init auto_monitor_init(void)
{
    int ret;

    printk(KERN_INFO "%s: Initializing...\n", DEVICE_NAME);

    // Initialize global state
    memset(&monitor_state, 0, sizeof(monitor_state));
    monitor_state.resource_allocation_factor = 5;
    monitor_state.current_sim_workload_level = 0;
    monitor_state.simulated_gpu_temp = 50;
    monitor_state.simulated_memory_pressure = 0;
    atomic_set(&monitor_state.critical_alerts, 0);
    atomic_set(&monitor_state.timer_ticks, 0);
    mutex_init(&monitor_config_mutex);

    // Register Character Device
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ALERT "%s: Failed to register a major number\n", DEVICE_NAME);
        return major_number;
    }
    printk(KERN_INFO "%s: Registered Device with major number %d\n", DEVICE_NAME, major_number);

    // Create device class and device node
    auto_monitor_class = class_create(CLASS_NAME);
    if (IS_ERR(auto_monitor_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "%s: Failed to create device class\n", DEVICE_NAME);
        return PTR_ERR(auto_monitor_class);
    }
    printk(KERN_INFO "%s: Device class created\n", DEVICE_NAME);

    auto_monitor_device = device_create(auto_monitor_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(auto_monitor_device)) {
        class_destroy(auto_monitor_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "%s: Failed to create device\n", DEVICE_NAME);
        return PTR_ERR(auto_monitor_device);
    }
    printk(KERN_INFO "%s: Device node /dev/%s created\n", DEVICE_NAME, DEVICE_NAME);

    // Create Sysfs directory and attributes
    auto_monitor_kobj = kobject_create_and_add(DEVICE_NAME, kernel_kobj);
    if (!auto_monitor_kobj) {
        printk(KERN_ALERT "%s: Failed to create kobject\n", DEVICE_NAME);
        device_destroy(auto_monitor_class, MKDEV(major_number, 0));
        class_destroy(auto_monitor_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return -ENOMEM;
    }
    ret = sysfs_create_group(auto_monitor_kobj, &auto_monitor_attr_group);
    if (ret) {
        printk(KERN_ALERT "%s: Failed to create sysfs group\n", DEVICE_NAME);
        kobject_put(auto_monitor_kobj);
        device_destroy(auto_monitor_class, MKDEV(major_number, 0));
        class_destroy(auto_monitor_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return ret;
    }
    printk(KERN_INFO "%s: Sysfs attributes created under /sys/kernel/%s/\n", DEVICE_NAME, DEVICE_NAME);


    // Initialize and start Workqueue
    monitor_wq = create_singlethread_workqueue(DEVICE_NAME);
    if (!monitor_wq) {
        printk(KERN_ALERT "%s: Failed to create workqueue\n", DEVICE_NAME);
        sysfs_remove_group(auto_monitor_kobj, &auto_monitor_attr_group);
        kobject_put(auto_monitor_kobj);
        device_destroy(auto_monitor_class, MKDEV(major_number, 0));
        class_destroy(auto_monitor_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return -ENOMEM;
    }
    INIT_WORK(&monitor_work, monitor_work_handler);
    printk(KERN_INFO "%s: Workqueue created\n", DEVICE_NAME);

    // Initialize and start HRTimer
    hrtimer_init(&monitor_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    monitor_hrtimer.function = monitor_timer_callback;
    hrtimer_start(&monitor_hrtimer, ms_to_ktime(HRTIMER_INTERVAL_MS), HRTIMER_MODE_REL);
    printk(KERN_INFO "%s: HRTimer started with %dms interval\n", DEVICE_NAME, HRTIMER_INTERVAL_MS);

    printk(KERN_INFO "%s: Module loaded successfully.\n", DEVICE_NAME);
    return 0;
}

// Module exit
static void __exit auto_monitor_exit(void)
{
    printk(KERN_INFO "%s: Exiting...\n", DEVICE_NAME);

    // Stop HRTimer
    hrtimer_cancel(&monitor_hrtimer);
    printk(KERN_INFO "%s: HRTimer stopped.\n", DEVICE_NAME);

    // Destroy Workqueue
    if (monitor_wq) {
        destroy_workqueue(monitor_wq);
        printk(KERN_INFO "%s: Workqueue destroyed.\n", DEVICE_NAME);
    }

    // Remove Sysfs attributes and kobject
    sysfs_remove_group(auto_monitor_kobj, &auto_monitor_attr_group);
    kobject_put(auto_monitor_kobj);
    printk(KERN_INFO "%s: Sysfs attributes removed.\n", DEVICE_NAME);

    // Destroy device node and class
    device_destroy(auto_monitor_class, MKDEV(major_number, 0));
    printk(KERN_INFO "%s: Device node /dev/%s removed.\n", DEVICE_NAME, DEVICE_NAME);
    class_destroy(auto_monitor_class);
    printk(KERN_INFO "%s: Device class destroyed.\n", DEVICE_NAME);

    // Unregister Character Device
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "%s: Character device unregistered.\n", DEVICE_NAME);

    printk(KERN_INFO "%s: Module unloaded.\n", DEVICE_NAME);
}

module_init(auto_monitor_init);
module_exit(auto_monitor_exit);