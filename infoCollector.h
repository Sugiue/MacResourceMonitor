//
// Created by Zhang Yuanshan on 11/24/17.
//

#ifndef FINALPROJECT_INFOCOLLECTOR_H
#define FINALPROJECT_INFOCOLLECTOR_H

// marco for flag passed in
#define _CPU_TEMP (0b1)
#define _DISK_STATUS (0b10)
#define _FAN_STATUS (0b100)
#define _MEM_STATUS (0b1000)
#define _GPU_STATUS (0b10000)
#define _VERBOSE (0B11111)

void show(int flag);

#endif //FINALPROJECT_INFOCOLLECTOR_H
