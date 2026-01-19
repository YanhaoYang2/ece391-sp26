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
    #define X_POS 50
    #define Y_POS 50
    #define WIDTH 10
    #define HEIGHT 10
    #define COLOR 0xFFFFFF
    
    init();

    struct skyline_window window_object = {X_POS, Y_POS, WIDTH, HEIGHT, COLOR};
    
    draw_window(fbuf, &window_object);

    for (int y_iter = Y_POS; y_iter < Y_POS + HEIGHT; ++y_iter) {
        for (int x_iter = X_POS; x_iter < X_POS + WIDTH; ++x_iter) {
            if (fbuf[y_iter * SKYLINE_WIDTH + x_iter] != COLOR) {
                kprintf("Failed\n");
                return;
            }
        }
    }

    kprintf("Passed\n");

    #undef X_POS
    #undef Y_POS
    #undef WIDTH
    #undef HEIGHT
    #undef COLOR
}
