// syscall.c - System call handling
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#include <stdint.h>
#ifdef SYSCALL_TRACE
#define TRACE
#endif

#ifdef SYSCALL_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "console.h"
#include "device.h"
#include "error.h"
#include "filesys.h"
#include "heap.h"
#include "intr.h"
#include "memory.h"
#include "misc.h"
#include "process.h"
#include "scnum.h"
#include "string.h"
#include "thread.h"
#include "timer.h"
#include "io.h"

#ifndef PATH_MAX
// Maximum path name length. Must be less than HEAP_ALLOC_MAX.
#define PATH_MAX 255
#endif

// EXPORTED FUNCTION DECLARATIONS
//

extern void handle_syscall(struct trap_frame *tfr);  // called from excp.c


// INTERNAL FUNCTION DECLARATIONS
//

static long syscall(const struct trap_frame *tfr);

static int sysexit(void);
static int sysexec(int fd, int argc, char ** argv);
static int sysfork(const struct trap_frame * tfr);
static int syswait(int tid);
static int sysprint(const char * msg);
static int sysusleep(unsigned long us);

static int sysdelete(const char * path);
static int syscreate(const char * path);

static int sysopen(int fd, const char * path);
static int sysclose(int fd);
static long sysread(int fd, void * buf, size_t bufsz);
static long syswrite(int fd, const void * buf, size_t len);
static int sysioctl(int fd, int cmd, uintptr_t arg_uma);
static int syspipe(int * wfd, int * rfd);
static int sysiodup(int oldfd, int newfd);

static int allocfd(struct process * proc, int reqfd, int notfd);

// EXPORTED FUNCTION DEFINITIONS
//

void handle_syscall(struct trap_frame * tfr) {
    // YOUR CODE HERE
    return;
}

// INTERNAL FUNCTION DEFINITIONS
//

long syscall(const struct trap_frame * tfr) {
    // YOUR CODE HERE
    return 0;
}

int sysexit(void) {
    // YOUR CODE HERE
    return 0;
}

int sysexec(int fd, int argc, char ** argv) {
    // YOUR CODE HERE
    return 0;
}

int sysfork(const struct trap_frame * tfr) {
    // YOUR CODE HERE
    return 0;
}

int syswait(int tid) {
    // YOUR CODE HERE
    return 0;
}

int sysprint(const char *msg) {
    // YOUR CODE HERE
    return 0;
}

int sysusleep(unsigned long us) {
    // YOUR CODE HERE
    return 0;
}

int syscreate(const char *path) {
    // YOUR CODE HERE
    return 0;
}

int sysdelete(const char * path) {
    // YOUR CODE HERE
    return 0;
}

int sysopen(int fd, const char * path) {
    // YOUR CODE HERE
    return 0;
}

int sysclose(int fd) {
    // YOUR CODE HERE
    return 0;
}

long sysread(int fd, void * buf, size_t bufsz) {
    // YOUR CODE HERE
    return 0;
}

long syswrite(int fd, const void *buf, size_t len) {
    // YOUR CODE HERE
    return 0;
}

int sysioctl(int fd, int op, uintptr_t arg_uma) {
    // YOUR CODE HERE
    return 0;
}

int syspipe(int * wfdptr, int * rfdptr) {
    // YOUR CODE HERE
    return 0;
}

int sysiodup(int oldfd, int newfd) {
    // YOUR CODE HERE
    return 0;
}

