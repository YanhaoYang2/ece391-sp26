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
    #define COLOR ((uint32_t)0xFFFFFF)

    init();

    struct skyline_star star_object = {NULL, X_POS, Y_POS, COLOR};
    
    draw_star(fbuf, &star_object);

    if (fbuf[Y_POS * SKYLINE_WIDTH + X_POS] == COLOR) {
        kprintf("Passed\n");
    } else {
        kprintf("Failed\n");
    }

    #undef X_POS
    #undef Y_POS
    #undef COLOR
}
