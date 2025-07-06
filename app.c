#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h> // For open()
#include <unistd.h> // For close(), read(), write()
#include <errno.h>  // For errno

#define DEVICE_FILE "/dev/auto_monitor"
#define SYSLOG_CMD "dmesg | tail -n 20" // Command to view kernel logs

void print_menu() {
    printf("\n--- Auto Monitor User App ---\n");
    printf("1. Read current status from /dev/%s\n", DEVICE_FILE);
    printf("2. Inject simulated workload (via /dev/%s write)\n", DEVICE_FILE);
    printf("3. Read current_workload from Sysfs\n");
    printf("4. Inject simulated workload (via Sysfs write)\n");
    printf("5. Read resource_factor from Sysfs\n");
    printf("6. Read critical_alerts from Sysfs\n");
    printf("7. View kernel logs (dmesg)\n");
    printf("0. Exit\n");
    printf("Enter choice: ");
}

int read_sysfs_attr(const char* attr_path, char* buffer, size_t buf_size) {
    int fd = open(attr_path, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open Sysfs attribute");
        return -1;
    }
    ssize_t bytes_read = read(fd, buffer, buf_size - 1);
    if (bytes_read < 0) {
        perror("Failed to read Sysfs attribute");
        close(fd);
        return -1;
    }
    buffer[bytes_read] = '\0'; // Null-terminate
    close(fd);
    return 0;
}

int write_sysfs_attr(const char* attr_path, const char* value) {
    int fd = open(attr_path, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open Sysfs attribute for writing");
        return -1;
    }
    ssize_t bytes_written = write(fd, value, strlen(value));
    if (bytes_written < 0) {
        perror("Failed to write to Sysfs attribute");
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

int main() {
    int choice;
    int fd;
    char buffer[512];
    char input_str[64];
    long workload_val;

    while (1) {
        print_menu();
        if (scanf("%d", &choice) != 1) {
            printf("Invalid input. Please enter a number.\n");
            while (getchar() != '\n'); // Clear input buffer
            continue;
        }
        while (getchar() != '\n'); // Clear input buffer

        switch (choice) {
            case 1: // Read from /dev/auto_monitor
                fd = open(DEVICE_FILE, O_RDONLY);
                if (fd < 0) {
                    perror("Failed to open device");
                    break;
                }
                ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    printf("\n--- Device Status ---\n%s", buffer);
                } else {
                    perror("Failed to read from device");
                }
                close(fd);
                break;

            case 2: // Write to /dev/auto_monitor
                printf("Enter simulated workload (0-100): ");
                if (fgets(input_str, sizeof(input_str), stdin) == NULL) {
                    printf("Error reading input.\n");
                    break;
                }
                input_str[strcspn(input_str, "\n")] = 0; // Remove newline

                workload_val = strtol(input_str, NULL, 10);
                if (workload_val < 0 || workload_val > 100) {
                    printf("Invalid workload. Must be 0-100.\n");
                    break;
                }

                fd = open(DEVICE_FILE, O_WRONLY);
                if (fd < 0) {
                    perror("Failed to open device for writing");
                    break;
                }
                if (write(fd, input_str, strlen(input_str)) < 0) {
                    perror("Failed to write to device");
                } else {
                    printf("Workload %ld injected via /dev/%s.\n", workload_val, DEVICE_FILE);
                }
                close(fd);
                break;

            case 3: // Read Sysfs workload
                if (read_sysfs_attr("/sys/kernel/auto_monitor/current_workload", buffer, sizeof(buffer)) == 0) {
                    printf("\nSysfs current_workload: %s", buffer);
                }
                break;

            case 4: // Write Sysfs workload
                printf("Enter simulated workload (0-100) for Sysfs: ");
                if (fgets(input_str, sizeof(input_str), stdin) == NULL) {
                    printf("Error reading input.\n");
                    break;
                }
                input_str[strcspn(input_str, "\n")] = 0; // Remove newline

                workload_val = strtol(input_str, NULL, 10);
                if (workload_val < 0 || workload_val > 100) {
                    printf("Invalid workload. Must be 0-100.\n");
                    break;
                }
                if (write_sysfs_attr("/sys/kernel/auto_monitor/current_workload", input_str) == 0) {
                    printf("Workload %ld injected via Sysfs.\n", workload_val);
                }
                break;

            case 5: // Read Sysfs resource_factor
                if (read_sysfs_attr("/sys/kernel/auto_monitor/resource_factor", buffer, sizeof(buffer)) == 0) {
                    printf("\nSysfs resource_factor: %s", buffer);
                }
                break;

            case 6: // Read Sysfs critical_alerts
                if (read_sysfs_attr("/sys/kernel/auto_monitor/critical_alerts", buffer, sizeof(buffer)) == 0) {
                    printf("\nSysfs critical_alerts: %s", buffer);
                }
                break;

            case 7: // View kernel logs
                printf("\n--- Kernel Logs (dmesg) ---\n");
                system(SYSLOG_CMD);
                break;

            case 0:
                printf("Exiting application.\n");
                return 0;

            default:
                printf("Invalid choice. Please try again.\n");
        }
    }

    return 0;
}