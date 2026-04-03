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
struct ngfs_mount {
    struct filesystem base; // base filesystem object exposed to the fs manager
    struct io * bkgio; // backing storage device for this mounted instance
    struct cache * cache; // black cahce for the backing storage device
    unsigned long long byte_capacity; // total device capacity in bytes
    unsigned long total_blocks; // total number of NGFS blocks on the device
    unsigned long fat_blocks; // number of blocks used for the FAT

    struct rwlock file_locks[NGFS_BLKSZ / NGFS_DENSZ]; // dir lock, allow concurrent reads and exlusive writes of the same file
};

struct ngfs_fileio {
    struct io io;
    struct ngfs_mount * fs;
    struct ngfs_dir_entry dir;
    unsigned long dir_index;
    unsigned long long pos;
};

struct ngfs_listingio {
    struct io io;
    struct ngfs_mount * fs;
    unsigned long next_index;
};


// INTERNAL FUNCTION DECLARATIONS
//
// Add ngfs_mount parameter to all internal helper functions to keep track fo the mounted instance on which these functions are operating.
static uint32_t get_next_data_block(const struct ngfs_mount * fs, uint32_t cur_block);
static void set_next_data_block(const struct ngfs_mount * fs, uint32_t cur_block, uint32_t next_block);
static uint32_t get_free_data_block(const struct ngfs_mount * fs);

static unsigned long ngfs_data_block_count(const struct ngfs_mount * fs);
static unsigned long ngfs_phys_device_blockno(const struct ngfs_mount * fs, unsigned long data_blockno);
static int ngfs_read_root_dir(struct ngfs_mount * fs, unsigned long index, struct ngfs_dir_entry * dir);
static int ngfs_find_root_by_name(struct ngfs_mount * fs, const char * name, struct ngfs_dir_entry * dir, unsigned long * indexptr);

static int ngfs_open_fileio(struct ngfs_mount * fs, const char * name, struct io ** ioptr);
static int ngfs_open_lsio(struct ngfs_mount * fs, struct io ** ioptr);

static int ngfs_update_root_dir_entry(struct ngfs_mount * fs, unsigned long index, const struct ngfs_dir_entry * dir);
static int ngfs_extend_file(struct ngfs_fileio * fileio, unsigned long long new_end);
static int ngfs_truncate_file(struct ngfs_fileio * fileio, unsigned long long new_end);

// INTERNAL GLOBAL VARIABLES
//
static struct filesystem ngfs = {
    .implname = "ngfs",
    .openfile = &ngfs_open,
    .createfile = &ngfs_create,
    .deletefile = &ngfs_delete,
    .flush = &ngfs_flush
};

static struct iointf ngfs_file_intf = {
    .implname = "ngfs_file",
    .reclaim = &ngfs_reclaim,
    .fetch = &ngfs_fetch,
    .read = &ngfs_read,
    .store = &ngfs_store,
    .write = &ngfs_write,
    .ioctl = &ngfs_ioctl
};

static struct iointf ngfs_listing_intf = {
    .implname = "ngfs_listing",
    .read = &ngfs_listing_read,
    .reclaim = &ngfs_listing_reclaim
};

// EXPORTED FUNCTION DEFINITIONS
//
int mount_ngfs(const char * mpname, struct io * bkgio) {
    // YOUR CODE HERE
    struct ngfs_mount * fs;
    unsigned int blksz;
    int result;
    unsigned long i;

    trace("%s(%s,%p)", __func__, mpname, bkgio);
    
    // Allocate and init the ngfs structure for the mounted instance
    fs = kcalloc(1, sizeof(*fs));
    if (fs == NULL)
        return -ENOMEM;

    // Deivce block size capacity checks.
    blksz = ioblksz(bkgio);
    assert (blksz > 0);

    if (NGFS_BLKSZ % blksz != 0) {
        debug("Incompatible device block size: %d", blksz);
        kfree(fs);
        return -ENOTSUP;
    }

    result = ioctl(bkgio, IOC_GETEND, &fs->byte_capacity);

    if (result != 0) {
        debug("ioctl(IOC_GETEND) returned %d", result);
        kfree(fs);
        return result;
    }

    if (fs->byte_capacity % NGFS_BLKSZ != 0) {
        debug("Device capacity is not a multiple of NGFS block size");
        kfree(fs);
        return -EBADFMT;
    }

    fs->total_blocks = fs->byte_capacity / NGFS_BLKSZ;

    if (fs->total_blocks == 0) {
        debug("Device capacity is 0 for NGFS");
        kfree(fs);
        return -EBADFMT;
    }

    // FAT size must be large enough to hold one FAT entry for each data block.
    // FAT blocks = ceil[total blocks / (ENTRIES_PER_BLOCK + 1)].
    fs->fat_blocks = (fs->total_blocks + NGFS_FAT_ENTRIES_PER_BLOCK) / (NGFS_FAT_ENTRIES_PER_BLOCK + 1);

    if (fs->fat_blocks >= fs->total_blocks) {
        debug("Device capacity is too small for NGFS");
        kfree(fs);
        return -EBADFMT;
    }

    fs->cache = create_cache(bkgio, NGFS_BLKSZ);

    if (fs->cache == NULL) {
        debug("create_cache() failed");
        kfree(fs);
        return -ENOMEM;
    }

    fs->bkgio = ioaddref(bkgio);

    // Initialize locks for each root-dir entry
    for (i = 0; i < NGFS_BLKSZ / NGFS_DENSZ; i++) {
        rwlock_init(&fs->file_locks[i], "ngfs.file");
    }

    fs->base = ngfs;

    // Register the mounted filesystem instance
    result = mount_filesys(mpname, &fs->base);
    if (result < 0) {
        iodropref(fs->bkgio);
        kfree(fs);
        return result;
    }

    return 0;
}

int ngfs_open(struct filesystem * fs, const char * name, struct io ** ioptr) {
    // YOUR CODE HERE
    struct ngfs_mount * const ngfs = (void*)fs;
    assert (fs != NULL);
    assert (ioptr != NULL);

    // Non-NULL and non-empty name goes to file interface,
    // NULL or empty name goes to listing interface.
    if (name != NULL && name[0] != '\0')
        return ngfs_open_fileio(ngfs, name, ioptr);
    else
        return ngfs_open_lsio(ngfs, ioptr);
}

void ngfs_reclaim(struct io * io) {
    // YOUR CODE HERE
}

long ngfs_fetch(struct io * io, unsigned long long pos, void * buf, long bufsz) {
    // YOUR CODE HERE
    struct ngfs_fileio * const fileio = (struct ngfs_fileio *)io;
    struct ngfs_mount * const fs = fileio->fs;
    unsigned long file_block_index;
    unsigned long block_offset;
    unsigned long phys_block_no;
    unsigned long i;
    long retlen;
    uint32_t curr_block;
    size_t copy_len;
    char * bufp;
    void * blk;
    int result;

    if (io == NULL || (buf == NULL && bufsz != 0)) {
        return -EINVAL;
    }

    if (buf == NULL || bufsz == 0) {
        return 0;
    }

    rwlock_acquire(&fs->file_locks[fileio->dir_index], 0); // acquire shared access lock

    if (pos > fileio->dir.size) {
        // start position beyond EOF
        retlen = -EINVAL;
        goto out_unlock;
    }

    if (pos == fileio->dir.size) {
        // start position at EOF, return 0
        retlen = 0;
        goto out_unlock;
    }

    if (fileio->dir.size - pos < (unsigned long long)bufsz) {
        // dynamically adjust the bufsz to prevent writing past EOF
        bufsz = fileio->dir.size - pos;
    }

    file_block_index = pos / NGFS_BLKSZ;
    block_offset = pos % NGFS_BLKSZ;

    curr_block = fileio->dir.start_block;

    for (i = 0; i < file_block_index; i++) {
        curr_block = get_next_data_block(fs, curr_block);
        if (curr_block == NGFS_BLOCK_END) {
            // early end in FAT chain, indicating a on-disk metadata inconsistency
            retlen = -EIO;
            goto out_unlock;
        }
    }

    bufp = (char *)buf;
    while (bufsz > 0) {
        phys_block_no = ngfs_phys_device_blockno(fs, curr_block); // map NGFS block No. to physical device block No.
        result = cache_fetch(fs->cache, phys_block_no * NGFS_BLKSZ, 1, &blk);
        if (result < 0) {
            retlen = result;
            goto out_unlock;
        }

        if (NGFS_BLKSZ - block_offset < (unsigned long)bufsz) {
            copy_len = NGFS_BLKSZ - block_offset;
        } else {
            copy_len = bufsz;
        }

        memcpy(bufp, (char *)blk + block_offset, copy_len);
        cache_release(fs->cache, blk, 0); // release after a pure read access, without marking dirty

        bufsz -= copy_len;
        bufp += copy_len;
        block_offset = 0; // only first block can have a non-zero offset

        if (bufsz > 0) {
            curr_block = get_next_data_block(fs, curr_block);
            if (curr_block == NGFS_BLOCK_END) {
                // early end in FAT chain, indicating a on-disk metadata inconsistency
                retlen = -EIO;
                goto out_unlock;
            }
        }
    }

    retlen = bufp - (char *)buf; // total bytes read
    
out_unlock:
    rwlock_release(&fs->file_locks[fileio->dir_index]);
    return retlen;
}

long ngfs_read(struct io * io, void * buf, long bufsz) {
    // YOUR CODE HERE
    // TODO: if read() is called by multiple threads, may need to serialize the file pos var with locks as it might be updated by multiple threads.
    struct ngfs_fileio * const fileio = (struct ngfs_fileio *)io;
    long retlen;
    if (io == NULL || (buf == NULL && bufsz != 0)) {
        return -EINVAL;
    }
    retlen = ngfs_fetch(io, fileio->pos, buf, bufsz);
    if (retlen > 0) {
        fileio->pos += retlen; // update file position after a successful read
    }
    return retlen;
}

long ngfs_store(struct io * io, unsigned long long pos, const void * buf, long len) {
    // YOUR CODE HERE
    struct ngfs_fileio * const fileio=(struct ngfs_fileio *)io;
    struct ngfs_mount *const fs=fileio->fs;
    unsigned long file_block_index;
    unsigned long block_offset;
    unsigned long phys_block_no;
    unsigned long i;
    long retlen;
    uint32_t curr_block;
    size_t copy_len;
    const char* bufp;
    void* blk;
    if (io == NULL || (buf == NULL && len != 0)) {
        return -EINVAL;
    }

    if (buf == NULL || len == 0) {
        return 0;
    }

    rwlock_acquire(&fs->file_locks[fileio->dir_index], 1); // acquire exclusive access lock for store
    if (pos > fileio->dir.size) {
        // start position beyond EOF
        retlen = -EINVAL;
        goto out_unlock;
    }

    if (pos == fileio->dir.size) {
        // start position at EOF, return 0
        retlen = 0;
        goto out_unlock;
    }

    if (fileio->dir.size - pos < (unsigned long long)len) {
        // dynamically adjust the len to prevent reading past EOF
        len = fileio->dir.size - pos;
    }

    file_block_index = pos / NGFS_BLKSZ;
    block_offset = pos % NGFS_BLKSZ;

    curr_block = fileio->dir.start_block;

    for (i = 0; i < file_block_index; i++) {
        curr_block = get_next_data_block(fs, curr_block);
        if (curr_block == NGFS_BLOCK_END) {
            // early end in FAT chain, indicating a on-disk metadata inconsistency
            retlen = -EIO;
            goto out_unlock;
        }
    }

    bufp = (char *)buf;
    while (len > 0) {
        phys_block_no = ngfs_phys_device_blockno(fs, curr_block); // map NGFS block No. to physical device block No.
        if (cache_fetch(fs->cache, phys_block_no * NGFS_BLKSZ, 1, &blk) < 0) {
            retlen = -EIO;
            goto out_unlock;
        }

        if (NGFS_BLKSZ - block_offset < (unsigned long)len) {
            copy_len = NGFS_BLKSZ - block_offset;
        } else {
            copy_len = len;
        }

        memcpy((char *)blk + block_offset, bufp, copy_len);
        cache_release(fs->cache, blk, 1); // release after write access, mark dirty
        
        len -= copy_len;
        bufp += copy_len;
        block_offset = 0; // only first block can have a non-zero offset

        if (len > 0) {
            curr_block = get_next_data_block(fs, curr_block);
            if (curr_block == NGFS_BLOCK_END) {
                // early end in FAT chain, indicating an on-disk metadata inconsistency
                retlen = -EIO;
                goto out_unlock;
            }
        }
    }

    retlen = bufp - (const char *)buf; // total bytes written
out_unlock:
    rwlock_release(&fs->file_locks[fileio->dir_index]);
    return retlen;
}

long ngfs_write(struct io * io, const void * buf, long len) {
    // YOUR CODE HERE
    return 0;
}

int ngfs_ioctl(struct io * io, int op, void * arg) {
    // YOUR CODE HERE
    struct ngfs_fileio * const fileio = (struct ngfs_fileio *)io;
    unsigned long long * const ullarg = (unsigned long long *)arg;
    unsigned long long new_end;
    int result;

    if (io == NULL) {
        return -EINVAL;
     }

    switch (op) {
        case IOC_GETPOS:
            if (ullarg == NULL) {
                debug("Invalid argument(s) to ngfs_ioctl(IOC_GETPOS)");
                return -EINVAL;
            }
            *ullarg = fileio->pos;
            return 0;

        case IOC_SETPOS:
            if (ullarg == NULL) {
                debug("Invalid argument(s) to ngfs_ioctl(IOC_SETPOS)");
                return -EINVAL;
            }
            if (*ullarg % io->blksz != 0) {
                debug("Invalid argument(s) to ngfs_ioctl(IOC_SETPOS)");
                return -EINVAL;
            }
            fileio->pos = *ullarg;
            return 0;

        case IOC_GETEND:
            if (ullarg == NULL) {
                debug("Invalid argument(s) to ngfs_ioctl(IOC_GETEND)");
                return -EINVAL;
            }
            *ullarg = fileio->dir.size;
            return 0;
        
        case IOC_SETEND:
            if (ullarg == NULL) {
                debug("Invalid argument(s) to ngfs_ioctl(IOC_SETEND)");
                return -EINVAL;
            }
            new_end = *ullarg;

            rwlock_acquire(&fileio->fs->file_locks[fileio->dir_index], 1); // acquire exclusive access lock for truncate/extend operations

            if (new_end == fileio->dir.size) {
                // new end is the same as current size, skip
                result = 0;
            } else if (new_end > fileio->dir.size) {
                // new end is larger than current size, extend called
                result = ngfs_extend_file(fileio, new_end);
            } else {
                // new end is smaller than current size, truncate called
                result = ngfs_truncate_file(fileio, new_end);
            }

            rwlock_release(&fileio->fs->file_locks[fileio->dir_index]);
            return result;

        default:
            return -ENOTSUP;
    }
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
    struct ngfs_mount * const ngfs = (struct ngfs_mount *)fs;
    if (ngfs == NULL) {
        debug("Invalid argument(s) to ngfs_flush");
        return;
    }
    // Flush the cache to ensure all dirty blocks are written back to the device
    cache_flush(ngfs->cache);
}

long ngfs_listing_read(struct io * lsio, void * buf, long bufsz) {
    // YOUR CODE HERE
    struct ngfs_listingio * const listingio = (struct ngfs_listingio *)lsio;
    struct ngfs_dir_entry dir;
    unsigned long i;
    size_t name_len;
    size_t copy_len;
    int result;

    if (listingio ==NULL || (buf == NULL && bufsz != 0))
        return -EINVAL;

    if (buf == NULL || bufsz == 0)
        return 0;
    
    for (i = listingio->next_index; i < NGFS_BLKSZ / NGFS_DENSZ; i++) {
        result = ngfs_read_root_dir(listingio->fs, i, &dir);
        if (result < 0)
            return result;
        if (dir.name[0] == '\0')
            continue; // empty entry, skip
        name_len = strlen(dir.name);
        if (name_len < (size_t)bufsz) {
            copy_len = name_len;
        } else {
            copy_len = (size_t)bufsz;
        }

        memcpy(buf, dir.name, copy_len);
        listingio->next_index = i + 1; // update next index for next call
        return copy_len;
    }

    return 0;
}

void ngfs_listing_reclaim(struct io * lsio) {
    // YOUR CODE HERE
    struct ngfs_listingio * const listingio = (struct ngfs_listingio *)lsio;
    kfree(listingio);
}


// INTERNAL FUNCTION DEFINITIONS
//
// Returns the value at index cur_block in the FAT
static uint32_t get_next_data_block(const struct ngfs_mount * fs, uint32_t cur_block) {
    // YOUR CODE HERE
    void * blk;
    struct ngfs_fat * fat_blk;
    unsigned long fat_blk_index;
    unsigned long fat_entry_index;
    uint32_t next_block;
    int result;

    assert (fs != NULL);
    assert (cur_block < ngfs_data_block_count(fs));

    fat_blk_index = cur_block / NGFS_FAT_ENTRIES_PER_BLOCK;
    fat_entry_index = cur_block % NGFS_FAT_ENTRIES_PER_BLOCK;

    result = cache_fetch(fs->cache, fat_blk_index * NGFS_BLKSZ, 1, &blk);
    assert (result >= 0);

    fat_blk = (struct ngfs_fat *) blk;
    next_block = fat_blk->fat[fat_entry_index];

    cache_release(fs->cache, blk, 0);

    return next_block;
}

// Sets FAT[cur_block]= next_block
static void set_next_data_block(const struct ngfs_mount * fs, uint32_t cur_block, uint32_t next_block) {
    // YOUR CODE HERE
    void * blk;
    struct ngfs_fat * fat_blk;
    unsigned long fat_blk_index;
    unsigned long fat_entry_index;
    int result;

    assert (fs != NULL);
    assert (cur_block < ngfs_data_block_count(fs));
    assert (next_block < NGFS_BLOCK_END);

    fat_blk_index = cur_block / NGFS_FAT_ENTRIES_PER_BLOCK;
    fat_entry_index = cur_block % NGFS_FAT_ENTRIES_PER_BLOCK;

    result = cache_fetch(fs->cache, fat_blk_index * NGFS_BLKSZ, 1, &blk);
    assert (result >= 0);

    fat_blk = (struct ngfs_fat *) blk;
    fat_blk->fat[fat_entry_index] = next_block;

    cache_release(fs->cache, blk, 1); // mark dirty after modifying FAT entry
}

//Scans the FAT, returning the first entry where FAT[entry]= NGFS_BLOCK_FREE
static uint32_t get_free_data_block(const struct ngfs_mount * fs) {
    // YOUR CODE HERE
    uint32_t block_no;
    if (fs == NULL) {
        debug("Invalid argument(s) to get_free_data_block");
        return NGFS_BLOCK_END; // indicate failure with an invalid block number
    }
    for (block_no = NGFS_ROOT_DATA_BLOCK + 1; block_no < ngfs_data_block_count(fs); block_no++) {
        if (get_next_data_block(fs, block_no) == NGFS_BLOCK_FREE) {
            return block_no;
        }
    }
    return NGFS_BLOCK_END; // no free block found
}

static unsigned long ngfs_data_block_count(const struct ngfs_mount * fs) {
    // YOUR CODE HERE
    // Number of data blocks = total number of blocks - number of FAT blocks.
    return fs->total_blocks - fs->fat_blocks;
}

static unsigned long ngfs_phys_device_blockno(const struct ngfs_mount * fs, unsigned long data_blockno) {
    // YOUR CODE HERE
    // Map NGFS block number to physical device block number
    // Physical device block number = number of FAT blocks + data block number
    return fs->fat_blocks + data_blockno;
}

static int ngfs_read_root_dir(struct ngfs_mount * fs, unsigned long index, struct ngfs_dir_entry * dir) {
    // YOUR CODE HERE
    void * blk;
    struct ngfs_dir_entry * entry;
    unsigned long root_phys_device_blkno;
    int result;

    if (fs == NULL || dir == NULL) {
        debug("Invalid argument(s) to ngfs_read_root_dir");
        return -EINVAL;
    }

    if (index >= NGFS_BLKSZ / NGFS_DENSZ) {
        debug("Root directory index %lu out of bounds", index);
        return -EINVAL;
    }

    // Convert root directory block number to physical device block number,
    // then fectch root directory block from the cache. cache_fetch expects
    // byte offset, so multiply block number by block size to get byte offset.
    root_phys_device_blkno = ngfs_phys_device_blockno(fs, NGFS_ROOT_DATA_BLOCK);
    // TODO: confirm throug testing that the cache api contract uses physical device block number instead of data block number
    result = cache_fetch(fs->cache, root_phys_device_blkno * NGFS_BLKSZ, 1, &blk);
    if (result < 0) {
        debug("cache_fetch() failed in ngfs_read_root_dir");
        return result;
    }

    // Read indexed directory entry from the cached block.
    entry = (struct ngfs_dir_entry *)blk;
    *dir = entry[index];

    cache_release(fs->cache, blk, 0); // release without marking dirty after a pure read access

    return 0;
}

static int ngfs_find_root_by_name(struct ngfs_mount * fs, const char * name, struct ngfs_dir_entry * dir, unsigned long * indexptr) {
    // YOUR CODE HERE
    struct ngfs_dir_entry current_entry;
    unsigned long i;
    int result;

    if (fs == NULL || name == NULL) {
        debug("Invalid argument(s) to ngfs_find_root_by_name");
        return -EINVAL;
    }

    for (i = 0; i < NGFS_BLKSZ / NGFS_DENSZ; i++) {
        result = ngfs_read_root_dir(fs, i, &current_entry);
        
        if (result < 0) {
            debug("ngfs_read_root_dir() failed in ngfs_find_root_by_name");
            return result;
        }

        if (current_entry.name[0] == '\0') {
            continue; // empty entry
        }

        if (strcmp(current_entry.name, name) == 0) {
            if (dir != NULL) {
                *dir = current_entry;
            }
            if (indexptr != NULL) {
                *indexptr = i;
            }
            return 0; // found entry
        }
    }

    return -ENOENT; // entry not found
}

static int ngfs_open_fileio(struct ngfs_mount * fs, const char * name, struct io ** ioptr) {
    // YOUR CODE HERE
    struct ngfs_fileio * fileio;
    struct ngfs_dir_entry dir;
    unsigned long dir_index;
    int result;

    // Look up requested file in the root directory
    result = ngfs_find_root_by_name(fs, name, &dir, &dir_index);
    if (result < 0) {
        debug("ngfs_find_root_by_name() failed in ngfs_open_fileio");
        return result;
    }

    fileio = kcalloc(1, sizeof(*fileio)); // allocate new object space for each open call
    if (fileio == NULL) {
        debug("kcalloc() failed in ngfs_open_fileio");
        return -ENOMEM;
    }

    fileio->fs = fs;
    fileio->dir = dir;
    fileio->dir_index = dir_index;
    fileio->pos = 0; // new file after openning start position placed at 0

    *ioptr = ioinit(&fileio->io, &ngfs_file_intf, 1, 1);
    return 0;
}

static int ngfs_open_lsio(struct ngfs_mount * fs, struct io ** ioptr) {
    // YOUR CODE HERE
    struct ngfs_listingio * lsio;
    lsio = kcalloc(1, sizeof(*lsio));
    if (lsio == NULL) {
        debug("kcalloc() failed in ngfs_open_lsio");
        return -ENOMEM;
    }

    lsio->fs = fs;
    lsio->next_index = 0;

    *ioptr = ioinit(&lsio->io, &ngfs_listing_intf, 1, 1); // init embedded io object to user NGFS listing interface
    return 0;
}

static int ngfs_update_root_dir_entry(struct ngfs_mount * fs, unsigned long index, const struct ngfs_dir_entry * dir) {
    // YOUR CODE HERE
    void * blk;
    struct ngfs_dir_entry * entry;
    unsigned long root_phys_device_blkno;
    int result;

    if (fs == NULL || dir == NULL) {
        debug("Invalid argument(s) to ngfs_update_root_dir_entry");
        return -EINVAL;
    }

    if (index >= NGFS_BLKSZ / NGFS_DENSZ) {
        debug("Root directory index %lu out of bounds", index);
        return -EINVAL;
    }

    root_phys_device_blkno = ngfs_phys_device_blockno(fs, NGFS_ROOT_DATA_BLOCK);
    result = cache_fetch(fs->cache, root_phys_device_blkno * NGFS_BLKSZ, 1, &blk);
    if (result < 0) {
        debug("cache_fetch() failed in ngfs_update_root_dir_entry");
        return result;
    }

    entry = (struct ngfs_dir_entry *)blk;
    entry[index] = *dir; // update indexed directory entry with new content
    cache_release(fs->cache, blk, 1); // release and mark dirty after modifying the cached block

    return 0;
}

static int ngfs_extend_file(struct ngfs_fileio * fileio, unsigned long long new_end) {
    // YOUR CODE HERE
    struct ngfs_mount * fs;
    unsigned long long old_size;
    unsigned long old_block_count;
    unsigned long new_blocks_count;
    uint32_t prev_block;
    uint32_t curr_block;
    uint32_t new_block;
    unsigned long i;
    int result;
    char zero_buf[NGFS_BLKSZ];

    unsigned long long zero_pos;
    unsigned long long remaining_zero_bytes;
    long chunk_size;

    if (fileio == NULL) {
        debug("Invalid argument(s) to ngfs_extend_file");
        return -EINVAL;
    }

    fs = fileio->fs;
    old_size = fileio->dir.size;

    if (new_end <= old_size) {
        // new end is not beyond current file size, incorrect call to this helper
        debug("Invalid argument(s) to ngfs_extend_file");
        return -EINVAL;
    }

    memset(zero_buf, 0, sizeof(zero_buf));
    // count blocks
    if (old_size == 0) {
        old_block_count = 0;
    } else {
        old_block_count = (old_size - 1) / NGFS_BLKSZ + 1; // number of blocks currently occupied by the file
    }
    if (new_end == 0) {
        new_blocks_count = 0;
    } else {
        new_blocks_count = (new_end - 1) / NGFS_BLKSZ + 1; // number of blocks needed for the new file size
    }

    if (new_blocks_count > old_block_count) {
        if (old_block_count == 0) {
            new_block = get_free_data_block(fs);
            if (new_block == NGFS_BLOCK_END) {
                debug("No free data block available in ngfs_extend_file");
                return -ENOMEM;
            }
            set_next_data_block(fs, new_block, NGFS_BLOCK_END);
            fileio->dir.start_block = new_block;
            prev_block = new_block;
            for (i = 1; i < new_blocks_count; i++) {
                new_block = get_free_data_block(fs);
                if (new_block == NGFS_BLOCK_END) {
                    debug("No free data block available in ngfs_extend_file");
                    return -ENOMEM;
                }
                set_next_data_block(fs, new_block, NGFS_BLOCK_END);
                set_next_data_block(fs, prev_block, new_block);
                prev_block = new_block;
            }
        } else {
            // find the last block in FAt and append new blocks to it
            curr_block = fileio->dir.start_block;
            for (i = 1; i < old_block_count; i++) {
                curr_block = get_next_data_block(fs, curr_block);
            }
            prev_block = curr_block;
            for (i = old_block_count; i < new_blocks_count; i++) {
                new_block = get_free_data_block(fs);
                if (new_block == NGFS_BLOCK_END) {
                    debug("No free data block available in ngfs_extend_file");
                    return -ENOMEM;
                }
                set_next_data_block(fs, new_block, NGFS_BLOCK_END);
                set_next_data_block(fs, prev_block, new_block);
                prev_block = new_block;
            }
        }
    }

    // zero fill the newly allocated blocks
    fileio->dir.size = new_end;
    zero_pos = old_size;
    remaining_zero_bytes = new_end - old_size;
    chunk_size = 0;
    while (remaining_zero_bytes > 0) {
        if (remaining_zero_bytes > NGFS_BLKSZ) {
            chunk_size = NGFS_BLKSZ;
        } else {
            chunk_size = (long)remaining_zero_bytes;
        }
        result = ngfs_store(&fileio->io, zero_pos, zero_buf, chunk_size);
        if (result < 0) {
            debug("ngfs_store() failed in ngfs_extend_file while zero-filling new blocks");
            return result;
        }
        zero_pos += result;
        remaining_zero_bytes -= result;
    }

    result = ngfs_update_root_dir_entry(fs, fileio->dir_index, &fileio->dir);
    if (result < 0) {
        debug("ngfs_update_root_dir_entry() failed in ngfs_extend_file after updating file size in directory entry");
        return result;
    }

    return 0;
}

static int ngfs_truncate_file(struct ngfs_fileio * fileio, unsigned long long new_end) {
    // YOUR CODE HERE
    struct ngfs_mount * fs;
    unsigned long long old_size;
    unsigned long old_block_count;
    unsigned long new_block_count;
    uint32_t curr_block;
    uint32_t next_block;
    uint32_t free_block;
    unsigned long i;
    int result;

    if (fileio == NULL) {
        debug("Invalid argument(s) to ngfs_truncate_file");
        return -EINVAL;
    }

    fs = fileio->fs;
    old_size = fileio->dir.size;

    if (new_end >= old_size) {
        // new end is larger or equal to current file size, incorrect call to this helper
        debug("Invalid argument(s) to ngfs_truncate_file");
        return -EINVAL;
    }

    // count blocks
    if (old_size == 0) {
        old_block_count = 0;
    } else {
        old_block_count = (old_size - 1) / NGFS_BLKSZ + 1;
    }
    if (new_end == 0) {
        new_block_count = 0;
    } else {
        new_block_count = (new_end - 1) / NGFS_BLKSZ + 1;
    }

    if (new_block_count == old_block_count) {
        fileio->dir.size = new_end;
    } else {
        if (new_block_count == 0) {
            curr_block = fileio->dir.start_block;
            while (curr_block != NGFS_BLOCK_END) {
                next_block = get_next_data_block(fs, curr_block);
                set_next_data_block(fs, curr_block, NGFS_BLOCK_FREE);
                curr_block = next_block;
            }
            fileio->dir.start_block = NGFS_BLOCK_END;
            fileio->dir.size = 0;
        } else {
            curr_block = fileio->dir.start_block;
            for (i = 1; i < new_block_count; i++) {
                curr_block = get_next_data_block(fs, curr_block);
            }
            free_block = get_next_data_block(fs, curr_block);
            set_next_data_block(fs, curr_block, NGFS_BLOCK_END); // mark new end of FAT chain
            while (free_block != NGFS_BLOCK_END) {
                next_block = get_next_data_block(fs, free_block);
                set_next_data_block(fs, free_block, NGFS_BLOCK_FREE);
                free_block = next_block;
            }
            fileio->dir.size = new_end;
        }
    }

    if (fileio->pos > new_end) {
        fileio->pos = new_end; // move file position back to new end if it is currently beyond new end after truncation
    }

    result = ngfs_update_root_dir_entry(fs, fileio->dir_index, &fileio->dir);
    if (result < 0) {
        debug("ngfs_update_root_dir_entry() failed in ngfs_truncate_file after updating file size in directory entry");
        return result;
    }

    return 0;
}
