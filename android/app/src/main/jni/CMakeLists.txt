cmake_minimum_required(VERSION 3.22.1)
project(miniwave C)

# Common headers live at the repo root
set(COMMON_DIR "${CMAKE_SOURCE_DIR}/../../../../../common")

add_library(miniwave SHARED
    miniwave_android.c
    ${COMMON_DIR}/emu2413.c
)

target_include_directories(miniwave PRIVATE
    ${COMMON_DIR}
)

target_compile_options(miniwave PRIVATE
    -std=c11 -O2 -Wall -Wextra
    -Wno-unused-parameter -Wno-unused-function
    -DNO_JACK
)

target_link_libraries(miniwave
    aaudio
    log
    android
    m
)
