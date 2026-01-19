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

void main(void) {
    #define X_POS 50
    #define Y_POS 100
    #define WIDTH 5
    #define HEIGHT 10
    #define COLOR 0xFF0000

    init();

    skyline_win_cnt = 0;
    add_window(X_POS, Y_POS, WIDTH, HEIGHT, COLOR);

    if (
        skyline_win_cnt == 1 && 
        skyline_windows[0].x == 50 && 
        skyline_windows[0].y == 100 && 
        skyline_windows[0].w == 5 && 
        skyline_windows[0].h == 10 && 
        skyline_windows[0].color == 0xFF0000) 
    {
        kprintf("Passed\n");
    } else {
        kprintf("Failed\n");
    }

    #undef X_POS
    #undef Y_POS
    #undef WIDTH
    #undef HEIGHT
    #undef COLOR
}
