// ngfs.c - A FAT-like file system
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef NGFS_TRACE
#define TRACE
#endif

#ifdef NGFS_DEBUG
#define DEBUG
#endif

#include "ngfs.h"

#include "../cache.h"
#include "../device.h"
#include "../error.h"
#include "../filesys.h"
#include "../fsimpl.h"
#include "../heap.h"
#include "../misc.h"
#include "../string.h"
#include "../ioimpl.h"
#include "../thread.h"

// INTERNAL TYPE DEFINITIONS
//


// INTERNAL FUNCTION DECLARATIONS
//


// INTERNAL GLOBAL VARIABLES
//


int mount_ngfs(const char * name, struct io * bkgio) {
    // YOUR CODE HERE
    return 0;
}

int ngfs_open(struct filesystem * fs, const char * name, struct io ** ioptr) {
    // YOUR CODE HERE
    return 0;
}

void ngfs_reclaim(struct io * io) {
    // YOUR CODE HERE
}

long ngfs_fetch(struct io * io, unsigned long long pos, void * buf, long bufsz) {
    // YOUR CODE HERE
    return 0;
}

long ngfs_read(struct io * io, void * buf, long bufsz) {
    // YOUR CODE HERE
    return 0;
}

long ngfs_store(struct io * io, unsigned long long pos, const void * buf, long len) {
    // YOUR CODE HERE
    return 0;
}

long ngfs_write(struct io * io, const void * buf, long len) {
    // YOUR CODE HERE
    return 0;
}

int ngfs_ioctl(struct io * io, int op, void * arg) {
    // YOUR CODE HERE
    return 0;
}

int ngfs_create(struct filesystem * fs, const char * name) {
    // YOUR CODE HERE
    return 0;
}

int ngfs_delete(struct filesystem * fs, const char * name) {
    // YOUR CODE HERE
    return 0;
}

void ngfs_flush(struct filesystem * fs) {
    // YOUR CODE HERE
}

long ngfs_listing_read(struct io * lsio, void * buf, long bufsz) {
    // YOUR CODE HERE
    return 0;
}

void ngfs_listing_reclaim(struct io * lsio) {
    // YOUR CODE HERE
}
