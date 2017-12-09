#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <memory.h>
#include "infoCollector.h"


// marco for debug print
//#define DEBUG

#ifdef DEBUG
#define DEBUG_PRINT(fmt, args...)    fprintf(stderr, fmt, ## args)
#else
#define DEBUG_PRINT(fmt, args...)    /* Don't do anything in release builds */
#endif

struct option long_options[] =
        {
                /* These options set a flag. */
                {"verbose",   no_argument, 0, 'v'},
                {"cpu",       no_argument, 0, 'u'},
                {"disk",      no_argument, 0, 'd'},
                {"fan",       no_argument, 0, 'f'},
                {"memory",    no_argument, 0, 'm'},
                {"gpu",       no_argument, 0, 'g'},
                {"battery",   no_argument, 0, 'b'},
                {"gpu",       no_argument, 0, 'g'},
                {"frequency", required_argument, 0, 't'},
                {"help",      no_argument, 0, 'h'},

                {0, 0,                     0, 0}
        };

int main(int argc, char *argv[]) {
    int c, flag = 0, updateInterval = 1, option_index = 0;

    // I chose to use getopt_long instead of argparse as argparse doesn't exit on OSX by default
    while ((c = getopt_long(argc, argv, "ubdfmgvh?t:", long_options, &option_index)) != -1)
        switch (c) {
            case 'u':
                flag |= _CPU_TEMP;
                DEBUG_PRINT("cput temp\n");
                break;
            case 'd':
                flag |= _DISK_STATUS;
                DEBUG_PRINT("disk status\n");
                break;
            case 'f':
                flag |= _FAN_STATUS;
                DEBUG_PRINT("fan status\n");
                break;
            case 'm':
                flag |= _MEM_STATUS;
                DEBUG_PRINT("mem status\n");
                break;
            case 'g':
                flag |= _GPU_STATUS;
                DEBUG_PRINT("gpu status\n");
                break;
            case 'b':
                flag |= _BATTERY_STATUS;
                DEBUG_PRINT("battery status\n");
                break;
            case 't':
                DEBUG_PRINT("user-defined interval\n");
                updateInterval = (int) strtol(optarg, NULL, 10);
                if (updateInterval <= 0) {
                    perror("updateInterval should be a positive value");
                    exit(1);
                }
                DEBUG_PRINT("user input intvl is %d\n", updateInterval);
                break;
            case 'v':
                flag |= _VERBOSE;
                DEBUG_PRINT("verbose\n");
                break;
            case 'h':
                puts("u: CPU temp, g: GPU temp, d: Disk Status, f: Fan status, m: Memory status, b: Battery status, v: all, t: specify update frequency");
                return 0;
            default:
                exit(1);
        }
    if (flag == 0) {
        perror("u: CPU temp, g: GPU temp, d: Disk Status, f: Fan status, m: Memory status, Battery status, v: all, t: specify update frequency");
        exit(1);
    }

    show(flag, (unsigned int) updateInterval);
    return 0;
}