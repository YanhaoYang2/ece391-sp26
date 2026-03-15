// cache.c - Block cache for a storage device
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//


#ifdef CACHE_TRACE
#define TRACE
#endif

#ifdef CACHE_DEBUG
#define DEBUG
#endif

#include "cache.h"
#include "conf.h"
#include "device.h"
#include "memory.h"
#include "error.h"
#include "string.h"
#include "thread.h"
#include "heap.h"
#include "misc.h"
#include "io.h"
#include "console.h"

// INTERNAL TYPE DEFINITIONS
//


struct cache {
    // YOUR CODE HERE
};


// INTERNAL FUNCTION DECLARATIONS
//


// EXPORTED FUNCTION DEFINITIONS
//

struct cache * create_cache(struct io * bkgio, unsigned long cache_blksz) {
    // YOUR CODE HERE
    return NULL;
}

int cache_fetch(
    struct cache * cache, unsigned long long pos, int exclusive, void ** pptr)
{
    // YOUR CODE HERE
    return 0;
}

void cache_release(struct cache * cache, void * pblk, int dirty) {
    // YOUR CODE HERE
    return;
}

int cache_flush(struct cache * cache) {
    // YOUR CODE HERE
}

// INTERNAL FUNCTION DEFINITIONS
//

