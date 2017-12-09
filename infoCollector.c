//
// Created by Zhang Yuanshan on 11/24/17.
//

#include <stdio.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <mach/vm_statistics.h>
#include <mach/mach_types.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <curses.h>

#include "infoCollector.h"
#include "systemManagementController.h"

#define Byte_TO_GB (1024.0 * 1024 * 1024)
#define WARNING_WHEN_HIGH 0
#define WARNING_WHEN_LOW 1

// marco for debug print
#define DEBUG

#ifdef DEBUG
#define DEBUG_PRINT(fmt, args...)    fprintf(stderr, fmt, ## args)
#else
#define DEBUG_PRINT(fmt, args...)    /* Don't do anything in release builds */
#endif

static volatile int keepRunning = 1;

void intHandler(int nothing) {
    keepRunning = 0;
}

double get_total_disk_size() {
    struct statvfs buf;

    // get current file system's stats
    if (statvfs("/", &buf) != 0) {
        perror("statvfs failed");
    }

    // size = block size * block numbers
    double totalSize = buf.f_frsize * buf.f_blocks / Byte_TO_GB;
    return totalSize;
}

double get_free_disk_size() {
    struct statvfs buf;

    if (statvfs("/", &buf) != 0) {
        perror("statvfs failed");
    }
    double freeSize = buf.f_frsize * buf.f_bfree / Byte_TO_GB;
    return freeSize;
}

double get_mem_used() {
    vm_size_t pageSize;
    mach_port_t machPort;
    mach_msg_type_number_t count;
    vm_statistics64_data_t vmStats;

    machPort = mach_host_self();
    count = sizeof(vmStats) / sizeof(natural_t);
    if (KERN_SUCCESS == host_page_size(machPort, &pageSize) &&
        KERN_SUCCESS == host_statistics64(machPort, HOST_VM_INFO, (host_info64_t) &vmStats, &count)) {
        // used memory = page size * (active + inactive + wired)
        // note that on Mac inactive mem is still used
        long long usedMemory = ((int64_t) vmStats.active_count + (int64_t) vmStats.inactive_count +
                                (int64_t) vmStats.wire_count) * (int64_t) pageSize;
        return (usedMemory / Byte_TO_GB);
    }

    return 0;
}

// use sysctl to get total memory
// this is platform based, other ways to get it include /proc/stat, sysconfig
int get_total_memory() {
    int mib[2];
    int64_t memSize;
    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    size_t length = sizeof(int64_t);
    sysctl(mib, 2, &memSize, &length, NULL, 0);

    return (int) (memSize / Byte_TO_GB);
}

#define GREEN_BLACK 1
#define YELLOW_BLACK 2
#define RED_BLACK 3
#define CYAN_BLACK 4
#define BLUE_BLACK 5

void init_color_pair() {
    init_pair(GREEN_BLACK, COLOR_GREEN, COLOR_BLACK);
    init_pair(YELLOW_BLACK, COLOR_YELLOW, COLOR_BLACK);
    init_pair(RED_BLACK, COLOR_RED, COLOR_BLACK);
    init_pair(CYAN_BLACK, COLOR_CYAN, COLOR_BLACK);
    init_pair(BLUE_BLACK, COLOR_BLUE, COLOR_BLACK);
}

#define BAR_FILLING "||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||"
#define BAR_WIDTH 10

// print a percentage bar
void printPercent(double percentage) {
    int leftFilling = (int) (percentage * BAR_WIDTH);
    int rightFilling = BAR_WIDTH - leftFilling;
    printw(" [%.*s%*s]", leftFilling, BAR_FILLING, rightFilling, "");
}


void print_seperation(int *row, char *title) {
    move((*row)++, 0);
    attron(COLOR_PAIR(CYAN_BLACK));
    printw("--- %s ---", title);
    attroff(COLOR_PAIR(CYAN_BLACK));
    move((*row)++, 0);
}


void print_temperature(char *title, int *row, double temperature) {
    int colorIdx;

    if (temperature < 40) {
        colorIdx = GREEN_BLACK;
    } else if (temperature >= 40 && temperature < 60) {
        colorIdx = YELLOW_BLACK;
    } else {
        colorIdx = RED_BLACK;
    }

    attron(COLOR_PAIR(colorIdx));
    printw("%s: ", title);
    printw("%.2f °C", temperature);
    attroff(COLOR_PAIR(colorIdx));
    move((*row)++, 0);
}


void print_usage(char *title, char *unit, double numerator, double denominator, int *row, int warningType) {
    // choose different warning type, if WARNING_WHEN_HIGH then use red color for usage above 75%
    // select color according to percentage
    int colorIdx = 1;
    double percentage = numerator / denominator;
    if (percentage < 0.5) {
        if (warningType == WARNING_WHEN_HIGH) {
            colorIdx = GREEN_BLACK;
        } else {
            colorIdx = RED_BLACK;
        }
    }
    if (percentage >= 0.5 && percentage < 0.75) {
        colorIdx = YELLOW_BLACK;
    }
    if (percentage >= 0.75) {
        if (warningType == WARNING_WHEN_HIGH) {
            colorIdx = RED_BLACK;
        } else {
            colorIdx = GREEN_BLACK;
        }
    }

        attron(COLOR_PAIR(colorIdx));
        printw("%s: %.2f %s", title, numerator, unit);
        printPercent(percentage);
        attroff(COLOR_PAIR(colorIdx));

    move((*row)++, 0);


}

void show_disk_status(int *row) {
    print_seperation(row, "Disk Status");
    double totalDiskSize = get_total_disk_size();
    double freeDiskSize = get_free_disk_size();
    printw("Total Disk Size: %.2f GB", totalDiskSize);
    move((*row)++, 0);
    print_usage("Used Disk Space", "GB", totalDiskSize - freeDiskSize, totalDiskSize, row, WARNING_WHEN_HIGH);
}

int sparkleController = 1;
void show_CPU_status(int *row) {
    print_seperation(row, "CPU Status");
    double cpuTemperautre = SMC_get_temperature(CPU_0_PROXIMITY);
    if (sparkleController) {
        print_temperature("CPU temp", row, cpuTemperautre);
    } else {
        move((*row)++, 0);
    }

    // make cpu temp sparkle if it is above 70 degree
    if (cpuTemperautre >= 70) {
        sparkleController = !sparkleController;
    } else {
        sparkleController = 1;
    }
}

void show_fan_status(int *row) {
    int fan_num = SMC_get_fan_num();
    // for machines such as Macbook which doesn't have any built-in fan
    if (fan_num == 0) {
        return;
    }

    print_seperation(row, "Fan Status");

    double maxFanSpeed = SMC_get_fan_speed(FAN_0_MAX_RPM);
    printw("Max Fan Speed: %.0f rpm", maxFanSpeed);
    move((*row)++, 0);

    printw("Installed Fans: %d", fan_num);
    move((*row)++, 0);

    double fanSpeeds[fan_num];
    SMC_get_fan_speeds(fan_num, fanSpeeds);
    char blockName[12] = "Fan   Speed";

    // print each fan
    for (int i = 0; i < fan_num; ++i) {
        blockName[4] = i + 48;
        print_usage(blockName, "rpm", fanSpeeds[i], maxFanSpeed, row, WARNING_WHEN_HIGH);
    }
}

void show_mem_status(int *row) {
    print_seperation(row, "Memory Status");
    int total_mem = get_total_memory();
    printw("Installed Mem: %d GB", total_mem);
    move((*row)++, 0);

    double memTemperature = SMC_get_temperature(MEMORY_SLOTS_PROXIMITY);
    print_temperature("Mem Temp: ", row, memTemperature);

    double used_mem = get_mem_used();
    print_usage("Memory Usage", "GB", used_mem, total_mem, row, WARNING_WHEN_HIGH);
}

void show_GPU_status(int *row) {
    print_seperation(row, "GPU Status");

    double gpuTemperautre = SMC_get_temperature(GPU_0_PROXIMITY);
    print_temperature("GPU Temp: ", row, gpuTemperautre);
}

void show_battery_status(int *row) {
    // for machines such as iMac which does not have a built-in battery
    if (!hasBattery()) {
        return;
    }

    print_seperation(row, "Battery Status");

    int batteryPercentage = SMC_get_current_battery_percent();
    if (SMC_is_battery_powered()) {
        printw("Battery Charged!");
        move(*row++, 0);
    } else {
        int batteryTime = SMC_get_time_remaining();
        if (batteryTime == -1) {
            // -1 indicates the system is calculating the time
            printw("Time remaining: Calculating");
            move(*row++, 0);
        } else {
            int hours = batteryTime / 60;
            int mintues = batteryTime - hours * 60;
            printw("Time remaining: %02d:%02d", hours, mintues);
            move(*row++, 0);
        }
    }
    print_usage("Battery Charge", "%", batteryPercentage, 100, row, WARNING_WHEN_LOW);
    printw("\n");
    double batteryTemperautre = SMC_get_temperature(BATTERY_0_TEMP);
    print_temperature("Battery Temp", row, batteryTemperautre);
}


void show(int flag, unsigned int updateInterval) {
    if (!systemSupported()) {
        perror("System not supported!");
        return;
    }

    signal(SIGINT, intHandler);

    // set up window
    WINDOW *wnd;
    wnd = initscr();
    cbreak();
    noecho();
    clear();
    refresh();
    start_color();
    init_color_pair();
    wbkgd(wnd, COLOR_PAIR(BLUE_BLACK));
    SMC_open();

    // control which row to print onto
    int row = 0;
    while (keepRunning) {
        clear();

        row = 0;
        // DISK
        if (flag & _DISK_STATUS) {
            show_disk_status(&row);
        }
        // CPU
        if (flag & _CPU_TEMP) {
            show_CPU_status(&row);
        }
        // FAN
        if (flag & _FAN_STATUS) {
            show_fan_status(&row);
        }
        // Memory
        if (flag & _MEM_STATUS) {
            show_mem_status(&row);
        }
        // GPU
        if (flag & _GPU_STATUS) {
            show_GPU_status(&row);
        }
        // Battery charge
        if (flag & _BATTERY_STATUS) {
            show_battery_status(&row);
        }

        refresh();
        sleep(updateInterval);
    }

    SMC_close();
    endwin();
}