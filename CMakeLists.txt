cmake_minimum_required(VERSION 3.8)
project(FinalProject)

set(CMAKE_C_STANDARD 99)

set(SOURCE_FILES main.c systemManagementController.c systemManagementController.h infoCollector.c infoCollector.h)

add_executable(FinalProject ${SOURCE_FILES})

target_link_libraries(FinalProject "-framework IOKit" "-lcurses" "-framework CoreFoundation")