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


#define BAR_FILLING "||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||"
#define BAR_WIDTH 30

// print a percentage bar
void printPercent(double percentage) {
    int val = (int) (percentage * 100);
    int leftFilling = (int) (percentage * BAR_WIDTH);
    int rightFilling = BAR_WIDTH - leftFilling;
    printw("%3d%% [%.*s%*s]", val, leftFilling, BAR_FILLING, rightFilling, "");
}


#define _SEPERATION "-----------------------------------------------------------"

void print_block(char *blockName, char *unit, double numerator, double denominator, int *row) {
    move((*row)++, 0);
    printw(_SEPERATION);
    move((*row)++, 0);
    printw("%s", blockName);
    move((*row)++, 0);

    // select color according to percentage
    double percentage = numerator / denominator;
    int colorIdx = 1;
    if (percentage < 0.5) {
        init_pair(1, COLOR_GREEN, COLOR_BLACK);
        colorIdx = 1;
    }
    if (percentage >= 0.5 && percentage < 0.75) {
        init_pair(2, COLOR_YELLOW, COLOR_BLACK);
        colorIdx = 2;
    }
    if (percentage >= 0.75) {
        init_pair(3, COLOR_RED, COLOR_BLACK);
        colorIdx = 3;
    }
    attron(COLOR_PAIR(colorIdx));
    printPercent(percentage);
    printw("%.2f%s/%.2f%s", numerator, unit, denominator, unit);
    attroff(COLOR_PAIR(colorIdx));
    move((*row)++, 0);
}

void show(int flag, unsigned int updateInterval) {
    signal(SIGINT, intHandler);

    // set up window
    WINDOW *wnd;
    wnd = initscr();
    cbreak();
    noecho();
    clear();
    refresh();
    start_color();
    init_pair(5, COLOR_BLUE, COLOR_BLACK);
    wbkgd(wnd, COLOR_PAIR(5));
    // control which row to print onto
    int row = 0;

    SMC_open();

    // DISK
    if (flag & _DISK_STATUS) {
        double totalDiskSize = get_total_disk_size();
        double freeDiskSize = get_free_disk_size();
        print_block("Disk Space:", "GB", totalDiskSize - freeDiskSize, totalDiskSize, &row);
    }
    int refreshRowIdx = row;

    double maxFanSpeed = SMC_get_fan_speed(FAN_0_MAX_RPM);

    while (keepRunning) {
        row = refreshRowIdx;

        // CPU
        if (flag & _CPU_TEMP) {
            double cpuTemperautre = SMC_get_temperature(CPU_0_PROXIMITY);
            print_block("CPU Temperature", "°C", cpuTemperautre, 100, &row);
        }

        // FAN
        if (flag & _FAN_STATUS) {
            int fan_num = SMC_get_fan_num();
            double fanSpeeds[fan_num];
            SMC_get_fan_speeds(fan_num, fanSpeeds);
            char blockName[12] = "Fan   Speed";

            // print each fan
            for (int i = 0; i < fan_num; ++i) {
                blockName[4] = i + 48;
                print_block(blockName, "rpm", fanSpeeds[i], maxFanSpeed, &row);
            }
        }

        // Memory
        if (flag & _MEM_STATUS) {
            double memTemperature = SMC_get_temperature(MEMORY_SLOTS_PROXIMITY);
            print_block("Memory Slot Temperature", "°C", memTemperature, 100, &row);

            int total_mem = get_total_memory();
            double used_mem = get_mem_used();
            print_block("Memory Usage", "GB", used_mem, total_mem, &row);
        }

        // GPU
        if (flag & _GPU_STATUS) {
            double gpuTemperautre = SMC_get_temperature(GPU_0_PROXIMITY);
            print_block("GPU Temperature", "°C", gpuTemperautre, 100, &row);
        }
        // Battery charge
        if (flag & _BATTERY_STATUS) {
            int batteryPercentage = SMC_get_current_battery_percent();
            print_block("Battery Charge", "", batteryPercentage, 100, &row);
            if (SMC_is_battery_powered()) {
                printw("battery charged!");
                move(row++, 0);
            } else {
                int batteryTime = SMC_get_time_remaining();
                if (batteryTime == -1) {
                    // -1 indicates the system is calculating the time
                    printw("Time remaining: Calculating");
                    move(row++, 0);
                } else {
                    printw("Time remaining: %d minutes", batteryTime);
                    move(row++, 0);
                }
            }
//            const char * batteryHealth = SMC_get_battery_health();
//            printw("battery health %s", batteryHealth);
//            move(row++,0);
        }

        // Battery temp
        if (flag & _BATTERY_STATUS) {
            double cpuTemperautre = SMC_get_temperature(BATTERY_0_TEMP);
            print_block("Battery Temperature", "°C", cpuTemperautre, 100, &row);
        }

        refresh();
        sleep(updateInterval);
    }

    SMC_close();
    endwin();
}