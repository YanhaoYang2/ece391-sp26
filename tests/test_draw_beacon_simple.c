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
    #define X_POS ((int32_t)50)
    #define Y_POS ((int32_t)50)
    #define DIAMETER ((uint8_t)5)
    #define PERIOD ((uint16_t)10)
    #define ONTIME ((uint16_t)5)

    init();

    static const uint32_t img[25] = {
        0xFF0000, 0xFF0000, 0xFF0000, 0xFF0000, 0xFF0000,
        0xFF0000, 0xFF0000, 0xFF0000, 0xFF0000, 0xFF0000,
        0xFF0000, 0xFF0000, 0xFF0000, 0xFF0000, 0xFF0000,
        0xFF0000, 0xFF0000, 0xFF0000, 0xFF0000, 0xFF0000,
        0xFF0000, 0xFF0000, 0xFF0000, 0xFF0000, 0xFF0000,
    };

    struct skyline_beacon beacon_object = {img, X_POS, Y_POS, DIAMETER, PERIOD, ONTIME};

    draw_beacon(fbuf, 22, &beacon_object);

    for (int y_iter = 0; y_iter < DIAMETER; ++y_iter) {
        for (int x_iter = 0; x_iter < DIAMETER; ++x_iter) {
            if (fbuf[(Y_POS + y_iter) * SKYLINE_WIDTH + (X_POS + x_iter)] != img[y_iter * DIAMETER + x_iter]) {
                kprintf("Failed\n");
                return;
            }
        }
    }

    kprintf("Passed\n");

    #undef X_POS
    #undef Y_POS
    #undef DIAMETER
    #undef PERIOD
    #undef ONTIME
}
