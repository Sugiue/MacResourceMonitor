#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include "infoCollector.h"


// marco for debug print
//#define DEBUG

#ifdef DEBUG
#define DEBUG_PRINT(fmt, args...)    fprintf(stderr, fmt, ## args)
#else
#define DEBUG_PRINT(fmt, args...)    /* Don't do anything in release builds */
#endif


int main(int argc, char * argv[]) {
    int c, flag = 0;
    while ((c = getopt(argc, argv, "udfmgvh?")) != -1)
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
            case 'v':
                flag |= _VERBOSE;
                DEBUG_PRINT("verbose\n");
                break;
            case 'h':
                puts("u: CPU temp, d: Disk Status, f: Fan status, m: Memory status, v: all");
                return 0;
            case '?':
                puts("u: CPU temp, d: Disk Status, f: Fan status, m: Memory status, v: all");
                return 0;
            default:
                perror("Invalid flag! u: CPU temp, d: Disk Status, f: Fan status, m: Memory status, v: all");
                abort ();
        }

    show(flag);
    return 0;
}