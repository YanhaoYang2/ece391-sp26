/*! @file test_*.c
    @brief main function of the kernel used for testing for this MP
    @copyright Copyright (c) 2024-2026 University of Illinois
    @license SPDX-License-identifier: NCSA
*/

#include "skyline.h"
#include <stddef.h>

// Defines

// Function declarations
extern void init();
extern void kprintf(const char * fmt, ...);
extern uint64_t rand();
extern long gpu_draw();
extern void gpu_attach_buffer(void * fbuf_ptr);

struct skyline_star * skyline_star_list;
struct skyline_window skyline_windows[SKYLINE_WIN_MAX];
uint16_t skyline_win_cnt;
struct skyline_beacon skyline_beacon;

// Vars
static uint32_t fbuf[SKYLINE_WIDTH * SKYLINE_HEIGHT];

void main(void) {
    init();
    gpu_attach_buffer((void*)fbuf);

    while(1) {

        uint64_t number = rand();
        kprintf("Number = %lu\n", number);

        for (int i = 0; i < 480 * 640; i++) {
            fbuf[i] = 0x0000FF;
        }
        gpu_draw();
        sleep_sec(2);
        
        number = rand();
        kprintf("Number = %lu\n", number);

        for (int i = 0; i < 480 * 640; i++) {
            fbuf[i] = 0x00FF00;
        }
        gpu_draw();
        sleep_sec(2);

        number = rand();
        kprintf("Number = %lu\n", number);

        for (int i = 0; i < 480 * 640; i++) {
            fbuf[i] = 0xFF0000;
        }
        gpu_draw();
        sleep_sec(2);
    }
}
