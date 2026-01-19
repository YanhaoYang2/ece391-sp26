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
    #define X_POS ((int32_t)100)
    #define Y_POS ((int32_t)200)
    #define COLOR 0xFF0000

    init();

    skyline_star_list = NULL;
    add_star(X_POS, Y_POS, COLOR);

    if (skyline_star_list != NULL && 
        skyline_star_list->x == X_POS && 
        skyline_star_list->y == Y_POS && 
        skyline_star_list->color == COLOR) 
    {
        kprintf("Passed\n");
    } else {
        kprintf("Failed\n");
    }

    #undef X_POS
    #undef Y_POS
    #undef COLOR
}
