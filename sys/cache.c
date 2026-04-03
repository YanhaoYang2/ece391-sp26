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
#define num_blocks 64
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
static uint64_t counter=0;
struct blk {
     uint8_t* data ; 
    int dirty;
    int occupied;
    // this condition is to condition broad cast all other threads waiting on this block after it is freed
    struct condition release;
    // this rwlock if block level rwlock to reduce global competition for cache lock 
    struct rwlock blk_lock;
    int last_accessed;
    unsigned long long position;
};
struct cache {
    // YOUR CODE HERE
    struct rwlock  lock;
     struct blk ** bks; // array of individual pointers to each block cache
     unsigned long blksz;
     struct io*   cache_io;

};


// INTERNAL FUNCTION DECLARATIONS
//


// EXPORTED FUNCTION DEFINITIONS
//

struct cache * create_cache(struct io * bkgio, unsigned long cache_blksz) {
    // YOUR CODE HERE

    struct cache* ca=kmalloc(sizeof(*ca));
    if(ca==NULL) return -ENOMEM; 
      memset(ca,0, sizeof(*ca));
    ca->bks=kmalloc(sizeof(struct blk*)*num_blocks);
    if(ca->bks==NULL) {
        kfree(ca);
        return -ENOMEM;
    }
  

    for(int i=0;i<num_blocks;i++){
        ca->bks[i]=kmalloc(sizeof(struct blk));
        if(!ca->bks[i]){
            for(int j=0;j<i;j++){
                kfree(ca->bks[j]);
            }
            kfree(ca->bks);
            kfree(ca);
            return -ENOMEM;
        }
        memset(ca->bks[i], 0, sizeof(struct blk));
        
        // Allocate data buffer for this block
        ca->bks[i]->data=kmalloc(cache_blksz);
        if(!ca->bks[i]->data){
            for(int j=0;j<=i;j++){
                kfree(ca->bks[j]->data);
                kfree(ca->bks[j]);
            }
            kfree(ca->bks);
            kfree(ca);
            return -ENOMEM;
        }
    }
    rwlock_init(&ca->lock,"cache_rwlock");
  
    ca->blksz=cache_blksz;
    ca->cache_io=bkgio;
    //initialized blocks
    for(int i=0;i<num_blocks;i++){
    ca->bks[i]->occupied=0;
    rwlock_init(&ca->bks[i]->blk_lock,"block_lock");
    condition_init(&ca->bks[i]->release,"wait_for_release");}
    return ca;
}

int cache_fetch(
    struct cache * cache, unsigned long long pos, int exclusive, void ** pptr)
{
    // YOUR CODE HERE
    //check for cache hit
    rwlock_acquire(&cache->lock,1);
    counter++;
    // if hit
    for(int i=0;i<num_blocks;i++){
        if(cache->bks[i]->occupied==1&& cache->bks[i]->position==pos){
            rwlock_acquire(&cache->bks[i]->blk_lock,1);
            // release the cache lock as soon as we get block lock so other threads could access the cache
            rwlock_release(&cache->lock);
            *pptr=cache->bks[i]->data;
            cache->bks[i]->last_accessed=counter;
            rwlock_release(&cache->bks[i]->blk_lock);
            return 0;
        }
    }
    // cache miss, we need to find a cache block and read from device
    int oldest_unused=INT32_MAX;
    int oldest_index=-1;
    for(int i=0;i<num_blocks;i++){
        // we find an unoccupied spot!
        if(cache->bks[i]->occupied==0){
             rwlock_acquire(&cache->bks[i]->blk_lock,1);
            rwlock_release(&cache->lock);
            if(cache->bks[i]->dirty==1){
                iostore(cache->cache_io,cache->bks[i]->position,cache->bks[i]->data,cache->blksz);
            }

            cache->bks[i]->dirty=0;
            cache->bks[i]->occupied=1;
            cache->bks[i]->position=pos;
            cache->bks[i]->last_accessed= counter;
            iofetch(cache->cache_io,pos,cache->bks[i]->data,cache->blksz);
            
            // rwlock_release(&cache->bks[i].blk_lock);
            return 0;
                }
        if(cache->bks[i]->last_accessed<oldest_unused){
            oldest_unused=cache->bks[i]->last_accessed;
            oldest_index=i;
        }

    }
        

    while(cache->bks[oldest_index]->occupied==1){
     condition_wait(&cache->bks[oldest_index]->release);
    }
    // only acquire the block lock after it is not occupied by another thread
        rwlock_acquire(&cache->bks[oldest_index]->blk_lock,1);
        rwlock_release(&cache->lock);
     if(cache->bks[oldest_index]->dirty==1){
        iostore(cache->cache_io,cache->bks[oldest_index]->position,cache->bks[oldest_index]->data,cache->blksz);
     }
      cache->bks[oldest_index]->dirty=0;
      cache->bks[oldest_index]->occupied=1;
      cache->bks[oldest_index]->position=pos;
      cache->bks[oldest_index]->last_accessed= counter;
      iofetch(cache->cache_io,pos,cache->bks[oldest_index]->data,cache->blksz);
      *pptr=cache->bks[oldest_index]->data;
      rwlock_release(&cache->bks[oldest_index]->blk_lock);
    return 0;
}

void cache_release(struct cache * cache, void * pblk, int dirty) {
    // YOUR CODE HERE
    
    for (int i=0;i<num_blocks;i++){
       if(cache->bks[i]->data==pblk){
        rwlock_acquire(&cache->bks[i]->blk_lock,1);
        cache->bks[i]->occupied=0;
        cache->bks[i]->dirty=dirty;
        condition_broadcast(&cache->bks[i]->release);
         rwlock_release(&cache->bks[i]->blk_lock);
       }
    }
   
    return;
}

int cache_flush(struct cache * cache) {
    // YOUR CODE HERE
    for(int i=0;i<num_blocks;i++){
        // prevent dirty changed by other threads between dirty check and memio store
        rwlock_acquire(&cache->bks[i]->blk_lock,1);
        if(cache->bks[i]->dirty==1&& cache->bks[i]->occupied==0){
             iostore(cache->cache_io,cache->bks[i]->position,cache->bks[i]->data,cache->blksz);
             cache->bks[i]->dirty=0;
        }
        rwlock_release(&cache->bks[i]->blk_lock);
    }
    return 0;
}

 unsigned int cache_blksz(const struct cache * ca){
    return ca->blksz;
 }
// INTERNAL FUNCTION DEFINITIONS
//

