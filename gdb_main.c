/*! @file gdb_main.c
    @brief main function of the kernel, used for GDB'ing thru (called from start.s)
    @copyright Copyright (c) 2024-2026 University of Illinois
    @license SPDX-License-identifier: NCSA
*/

#include <stddef.h>

extern unsigned long long get_secret();
extern void console_init(void);
extern void kprintf(const char * fmt, ...);

// struct that has padding
struct my_struct {
    int a;
    char b[2];
};


void main(void) {
    unsigned long long secret; 
    struct my_struct* ptr = NULL; // prevent struct from being optimized out

    // initialize the terminal
    console_init();
    
    // obtain the secret code
    secret = get_secret();

    kprintf("set a breakpoint at this line\n");

    // stop unused variable warning
    secret++;
    ptr++;

    // and we're done
    kprintf("done with mp0!\n");
}
