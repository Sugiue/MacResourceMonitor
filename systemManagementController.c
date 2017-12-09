//
// Created by Zhang Yuanshan on 11/23/17.
//

#include "systemManagementController.h"
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>

// some parts of this c file come from Github repo /osx-cpu-temp and /libsmc and /iStats


/**
SMC data types - 4 byte multi-character constants
*/
#define DATA_TYPE_UINT8  "ui8 "
#define DATA_TYPE_UINT16 "ui16"
#define DATA_TYPE_UINT32 "ui32"
#define DATA_TYPE_FLAG   "flag"
#define DATA_TYPE_FPE2   "fpe2"
#define DATA_TYPE_SFDS   "{fds"
#define DATA_TYPE_SP78   "sp78"


typedef struct {
    char major;
    char minor;
    char build;
    char reserved[1];
    UInt16 release;
} SMCKeyData_vers_t;

typedef struct {
    UInt16 version;
    UInt16 length;
    UInt32 cpuPLimit;
    UInt32 gpuPLimit;
    UInt32 memPLimit;
} SMCKeyData_pLimitData_t;

typedef struct {
    UInt32 dataSize;
    UInt32 dataType;
    char dataAttributes;
} SMCKeyData_keyInfo_t;

typedef char SMCBytes_t[32];

typedef struct {
    UInt32 key;
    SMCKeyData_vers_t vers;
    SMCKeyData_pLimitData_t pLimitData;
    SMCKeyData_keyInfo_t keyInfo;
    char result;
    char status;
    char data8;
    UInt32 data32;
    SMCBytes_t bytes;
} SMCKeyData_t;

typedef char UInt32Char_t[5];

typedef struct {
    UInt32Char_t key;
    UInt32 dataSize;
    UInt32Char_t dataType;
    SMCBytes_t bytes;
} SMCVal_t;


typedef enum {
    kSMCUserClientOpen = 0,
    kSMCUserClientClose = 1,
    kSMCHandleYPCEvent = 2,
    kSMCReadKey = 5,
    kSMCWriteKey = 6,
    kSMCGetKeyCount = 7,
    kSMCGetKeyFromIndex = 8,
    kSMCGetKeyInfo = 9
} selector_t;


static io_connect_t conn;

UInt32 str_to_uint32(char *str, int size, int base) {
    UInt32 total = 0;
    int i;

    for (i = 0; i < size; i++) {
        if (base == 16)
            total += str[i] << (size - 1 - i) * 8;
        else
            total += (unsigned char) (str[i] << (size - 1 - i) * 8);
    }
    return total;
}

void uint32_to_str(char *str, UInt32 val) {
    str[0] = '\0';
    sprintf(str, "%c%c%c%c",
            (unsigned int) val >> 24,
            (unsigned int) val >> 16,
            (unsigned int) val >> 8,
            (unsigned int) val);
}

kern_return_t SMC_open(void) {
    kern_return_t result;
    io_iterator_t iterator;
    io_object_t device;

    CFMutableDictionaryRef matchingDictionary = IOServiceMatching("AppleSMC");
    result = IOServiceGetMatchingServices(kIOMasterPortDefault, matchingDictionary, &iterator);
    if (result != kIOReturnSuccess) {
        printf("Error: IOServiceGetMatchingServices() = %08x\n", result);
        return 1;
    }

    device = IOIteratorNext(iterator);
    IOObjectRelease(iterator);
    if (device == 0) {
        printf("Error: no SMC found\n");
        return 1;
    }

    result = IOServiceOpen(device, mach_task_self(), 0, &conn);
    IOObjectRelease(device);
    if (result != kIOReturnSuccess) {
        printf("Error: IOServiceOpen() = %08x\n", result);
        return 1;
    }

    return kIOReturnSuccess;
}

kern_return_t SMC_close() {
    return IOServiceClose(conn);
}

kern_return_t SMC_call(int index, SMCKeyData_t *inputStructure, SMCKeyData_t *outputStructure) {
    size_t structureInputSize;
    size_t structureOutputSize;

    structureInputSize = sizeof(SMCKeyData_t);
    structureOutputSize = sizeof(SMCKeyData_t);

    return IOConnectCallStructMethod(conn, index,
            // inputStructure
                                     inputStructure, structureInputSize,
            // ouputStructure
                                     outputStructure, &structureOutputSize);
}

kern_return_t SMC_read_key_val(UInt32Char_t key, SMCVal_t *val) {
    kern_return_t result;
    SMCKeyData_t inputStructure;
    SMCKeyData_t outputStructure;

    memset(&inputStructure, 0, sizeof(SMCKeyData_t));
    memset(&outputStructure, 0, sizeof(SMCKeyData_t));
    memset(val, 0, sizeof(SMCVal_t));

    inputStructure.key = str_to_uint32(key, 4, 16);
    inputStructure.data8 = kSMCGetKeyInfo;

    result = SMC_call(kSMCHandleYPCEvent, &inputStructure, &outputStructure);
    if (result != kIOReturnSuccess)
        return result;

    val->dataSize = outputStructure.keyInfo.dataSize;
    uint32_to_str(val->dataType, outputStructure.keyInfo.dataType);
    inputStructure.keyInfo.dataSize = val->dataSize;
    inputStructure.data8 = kSMCReadKey;

    result = SMC_call(kSMCHandleYPCEvent, &inputStructure, &outputStructure);
    if (result != kIOReturnSuccess)
        return result;

    memcpy(val->bytes, outputStructure.bytes, sizeof(outputStructure.bytes));

    return kIOReturnSuccess;
}

double SMC_get_temperature(char *key) {
    SMCVal_t val;
    kern_return_t result;

    result = SMC_read_key_val(key, &val);
    // check read and returned value
    if (result == kIOReturnSuccess && val.dataSize > 0 && strcmp(val.dataType, DATA_TYPE_SP78) == 0) {
        // convert sp78 value to temperature
        int intValue = val.bytes[0] * 256 + (unsigned char) val.bytes[1];
        return intValue / 256.0;
    }
    // read failed
    return 0.0;
}

double SMC_get_fan_speed(char *key) {
    SMCVal_t val;
    kern_return_t result;

    result = SMC_read_key_val(key, &val);
    if (result == kIOReturnSuccess && val.dataSize > 0 && strcmp(val.dataType, DATA_TYPE_FPE2) == 0) {
        // convert fpe2 value to rpm
        int intValue = (unsigned char) val.bytes[0] * 256 + (unsigned char) val.bytes[1];
        return intValue / 4.0;

    }
    // read failed
    return 0.0;
}

void SMC_get_fan_speeds(int fanNum, double *speeds) {
    // loop through all fans and get info
    for (int i = 0; i < fanNum; i++) {
        char key[5] = FAN_0;
        key[1] += i;
        double speed = SMC_get_fan_speed(key);
        speeds[i] = speed;
    }
}

int SMC_get_fan_num() {
    SMCVal_t val;
    kern_return_t result;

    result = SMC_read_key_val(NUM_FANS, &val);
    if (result == kIOReturnSuccess && val.dataSize > 0 && strcmp(val.dataType, DATA_TYPE_UINT8) == 0) {
        return val.bytes[0];
    }

    return -1;
}


CFDictionaryRef powerSourceInfo() {
    CFTypeRef powerInfo = IOPSCopyPowerSourcesInfo();

    if (!powerInfo) return NULL;

    CFArrayRef powerSourcesList = IOPSCopyPowerSourcesList(powerInfo);
    if (!powerSourcesList) {
        CFRelease(powerInfo);
        return NULL;
    }

    if (CFArrayGetCount(powerSourcesList)) {
        CFDictionaryRef powerSourceInformation = IOPSGetPowerSourceDescription(powerInfo,
                                                                               CFArrayGetValueAtIndex(powerSourcesList,
                                                                                                      0));

        //CFRelease(powerInfo);
        //CFRelease(powerSourcesList);
        return powerSourceInformation;
    }

    CFRelease(powerInfo);
    CFRelease(powerSourcesList);
    return NULL;
}

int SMC_get_current_battery_percent() {
    CFNumberRef currentCapacity;
    int currentPercentage;

    CFDictionaryRef powerSourceInformation;

    // the result is a dictionary containing power source information
    powerSourceInformation = powerSourceInfo();
    if (powerSourceInformation == NULL) {
        return 0;
    }

    // returned result is the left battery percentage
    currentCapacity = CFDictionaryGetValue(powerSourceInformation, CFSTR(kIOPSCurrentCapacityKey));
    CFNumberGetValue(currentCapacity, kCFNumberIntType, &currentPercentage);

    return currentPercentage;
}

int SMC_is_battery_powered() {
    CFDictionaryRef powerSourceInformation = powerSourceInfo();
    if (powerSourceInformation == NULL) {
        return 0;
    }

    CFBooleanRef isChargedBoolean = CFDictionaryGetValue(powerSourceInformation, CFSTR(kIOPSIsChargedKey));
    CFBooleanRef isChargingBoolean = CFDictionaryGetValue(powerSourceInformation, CFSTR(kIOPSIsChargingKey));

    // if it is null then it is not fully charged
    int charged = isChargedBoolean == NULL ? 0 : CFBooleanGetValue(isChargedBoolean);
    int charging = CFBooleanGetValue(isChargingBoolean);

    // when a battery is full charged or is charging then the computer is powered
    if (charged || charging) {
        return 1;
    }
    return 0;
}

// return the health status of battery
const char *SMC_get_battery_health() {
    CFDictionaryRef powerSourceInformation = powerSourceInfo();
    if (powerSourceInformation == NULL) {
        return "Unknown";
    }

    CFStringRef batteryHealthRef = (CFStringRef) CFDictionaryGetValue(powerSourceInformation, CFSTR("BatteryHealth"));
    const char *batteryHealth = CFStringGetCStringPtr(batteryHealthRef, // CFStringRef theString,
                                                      kCFStringEncodingMacRoman); //CFStringEncoding encoding);
    if (batteryHealth == NULL)
        return "unknown";

    return batteryHealth;
}

// return the left time of the battery
// -1 indicates the system is calculating the time
int SMC_get_time_remaining() {
    CFDictionaryRef powerSourceInformation = powerSourceInfo();
    if (powerSourceInformation == NULL)
        return 0;

    int remainingMinutes;
    CFNumberRef timeRemaining = CFDictionaryGetValue(powerSourceInformation, CFSTR(kIOPSTimeToEmptyKey));
    CFNumberGetValue(timeRemaining, kCFNumberIntType, &remainingMinutes);

    return remainingMinutes;
}

int hasBattery() {
    CFDictionaryRef powerSourceInformation = powerSourceInfo();
    return powerSourceInformation != NULL;
}

int systemSupported(){
    // at least MAC_OS_X_VERSION_10_5 required due to the function used in SMC_call
    #if MAC_OS_X_VERSION_10_5
    return 1;
    #else
    return 0;
    #endif
}