# Autonomous System Monitor & Dynamic Resource Manager

This prototype Linux kernel module was designed to simulate an autonomous system monitor and dynamic resource manager (similar to a task manager). It periodically assesses a simulated system workload, dynamically adjusts a "resource allocation factor" based on this workload, tracks critical alerts, and exposes its state & allows workload injection via both a character device and the Sysfs interface.

## Key Features

* **High-Resolution Timer (HRTimer):** Periodically triggers monitoring activities. Would intake system data in a non-simulated implementation but instead simulates fluctuating workload data.

* **Workqueue:** Defers resource adjustment and processing logic from atomic context to process context.

* **Simulated Metrics:** Tracks simulated workload, temperature, and memory pressure.

* **Dynamic Resource Adjustment:** Adjusts a "resource allocation factor" based on simulated workload.

* **Critical Alerting:** Increments an atomic counter for critical events (ex: max resources reached).

* **Character Device (`/dev/auto_monitor`):** Provides a traditional file-like interface for reading the module's full state and injecting simulated workload.

* **Sysfs Interface (`/sys/kernel/auto_monitor/`):** Exposes individual module parameters (workload, resource factor, critical alerts) as easily accessible files.

* **Synchronization:** Employs spinlocks and mutexes to protect data across concurrent kernel contexts.

## Prerequisites

To build and run this kernel module, you need an Ubuntu system (physical machine or VM) with the following installed:

* **Linux Kernel Headers:** Must match your running kernel version.

    ```
    sudo apt update
    sudo apt install linux-headers-$(uname -r)
    ```

* **Build Essentials:** GCC, Make, etc.

    ```
    sudo apt install build-essential flex bison
    ```

## Build Instructions

1.  **Navigate to the project directory:**

    ```
    cd /path/to/your/project/System_Monitor_Dynamic_Resource_Manager/
    ```

2.  **Compile the kernel module:**

    ```
    make
    ```

## Installation (Loading the Module)

1.  **Load the module into the kernel:**

    ```
    sudo insmod auto_health_monitor.ko
    ```

2.  **Verify module load:**

    ```
    lsmod | grep auto_monitor
    ```

    You should see `auto_health_monitor` listed.

## Testing the Module

**Highly recommended to open a separate terminal and run `sudo dmesg -w` to monitor kernel logs while testing.**

### **Testing with app.c**

The easiest way to test the module is via the simple CLI app.

1.  **Recompile app.c:**

    ```
    gcc app.c -o user_app
    ```

2.  **Run the app:**

    ```
    sudo ./user_app
    ```

    Follow the CLI instructions to test the module character device and Sysfs interface.

Otherwise, you can interact with the module via its character device and Sysfs interface directly.

### **Monitoring Kernel Logs**

This command will show all `printk` messages from your module, good for understanding behavior and debugging.

```
sudo dmesg -w
```

### **Testing Character Device Interface (`/dev/auto_monitor`)**

1.  **Verify Device Node:**

    ```
    ls -l /dev/auto_monitor
    ```

    **Expected:** `crw-rw---- ... /dev/auto_monitor` (the `c` indicates a character device).

2.  **Read from the device (get full state):**

    ```
    sudo cat /dev/auto_monitor
    ```

    **Expected:** Output showing the current workload, resource factor, alerts, temperatures, and timer ticks.
    **Example Output:**

    ```
    Workload: 0%
    Resource Factor: 5
    Critical Alerts: 0
    Simulated GPU Temp: 50C
    Simulated Memory Pressure: 0%
    Timer Ticks: 123
    ```

    *(Values will change over time due to the HRTimer and workqueue.)*

3.  **Write to the device (inject simulated workload):**

    ```
    echo "85" | sudo tee /dev/auto_monitor
    ```

    **Purpose:** Sets the `current_sim_workload_level` to 85%.
    **Observe in `dmesg -w`:** You should see `printk` messages from your module confirming the workload injection and subsequent resource factor adjustments by the workqueue handler.

### **Testing Sysfs Interface (`/sys/kernel/auto_monitor/`)**

1.  **Verify Sysfs Directory and Files:**

    ```
    ls -l /sys/kernel/auto_monitor/
    ```

    **Expected:** A listing of `current_workload`, `resource_factor`, and `critical_alerts`.

2.  **Read `current_workload`:**

    ```
    cat /sys/kernel/auto_monitor/current_workload
    ```

    **Expected:** The current simulated workload level.

3.  **Read `resource_factor`:**

    ```
    cat /sys/kernel/auto_monitor/resource_factor
    ```

    **Expected:** The current resource allocation factor.

4.  **Read `critical_alerts`:**

    ```
    cat /sys/kernel/auto_monitor/critical_alerts
    ```

    **Expected:** The current count of critical alerts.

5.  **Write to `current_workload` (inject simulated workload via Sysfs):**

    ```
    echo "85" | sudo tee /sys/kernel/auto_monitor/current_workload
    ```

    **Purpose:** Same as writing to the character device, but specifically targets the Sysfs attribute.
    **Observe in `dmesg -w`:** Similar `printk` messages as before.


### **Observing Dynamic Behavior**

To see the resource adjustment logic in action, set a high workload and then continuously monitor the resource factor and alerts:

1.  **Set a high workload:**

    ```
    echo "95" | sudo tee /sys/kernel/auto_monitor/current_workload
    ```

2.  **Watch the values change:**

    ```
    watch -n 1 'cat /sys/kernel/auto_monitor/resource_factor; cat /sys/kernel/auto_monitor/critical_alerts'
    ```

    *(This command refreshes every 1 second. You should see the resource factor adjust and critical alerts increment if the workload remains high.)*

## Cleanup (Unloading the Module)

1.  **Unload the module:**

    ```
    sudo rmmod auto_health_monitor
    ```

    **Observe in `dmesg -w`:** You should see messages confirming the HRTimer stopped, workqueue destroyed, Sysfs attributes removed, device node removed, and module unloaded.
