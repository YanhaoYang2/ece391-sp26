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

struct ngfs_flock {
    char name[NGFS_MAX_FILENAME_LEN + 1];
    struct rwlock lock;              // per-file read/write lock
    unsigned int open_refcnt;        // number of opened fileio objects
    int deleted;                     // delayed reclaim after delete
    struct ngfs_flock * next;
};

struct ngfs_mount {
    struct filesystem base; // base filesystem object exposed to the fs manager
    struct io * bkgio; // backing storage device for this mounted instance
    struct cache * cache; // black cahce for the backing storage device
    unsigned long long byte_capacity; // total device capacity in bytes
    unsigned long total_blocks; // total number of NGFS blocks on the device
    unsigned long fat_blocks; // number of blocks used for the FAT

    struct rwlock dir_lock;   // root dir metadata + flock registry
    struct rwlock fat_lock;   // FAT allocation/free/update
    struct ngfs_flock * flock_head;
};

struct ngfs_fileio {
    struct io io;
    struct ngfs_mount * fs;
    struct ngfs_dir_entry dir;
    unsigned long dir_index;
    unsigned long long pos;
    struct ngfs_flock * flock;       // lock object for this file
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

static int ngfs_get_root_dentry(struct ngfs_mount * fs, struct ngfs_dir_entry * dir);
static unsigned long ngfs_root_entry_count(struct ngfs_mount * fs);
static unsigned long ngfs_root_capacity_entries(struct ngfs_mount * fs);
static int ngfs_root_block_for_index(struct ngfs_mount * fs, unsigned long index, uint32_t * blkno_out);
static int ngfs_ensure_root_capacity(struct ngfs_mount * fs, unsigned long needed_entries);
static int ngfs_set_root_entry_count(struct ngfs_mount * fs, unsigned long nentries);

static struct ngfs_flock * ngfs_find_flock_locked(struct ngfs_mount * fs, const char * name);
static int ngfs_create_flock_locked(struct ngfs_mount * fs, const char * name, struct ngfs_flock ** out);
static void ngfs_put_flock(struct ngfs_mount * fs, struct ngfs_flock * flock);
static int ngfs_build_flock_registry(struct ngfs_mount * fs);

static int ngfs_sync_file_dentry_locked(struct ngfs_fileio * fileio);

static long ngfs_fetch_nolock(struct ngfs_fileio * fileio, unsigned long long pos, void * buf, long bufsz);
static long ngfs_store_nolock(struct ngfs_fileio * fileio, unsigned long long pos, const void * buf, long len);

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

    // Initialize locks
    rwlock_init(&fs->dir_lock, "ngfs.dir");
    rwlock_init(&fs->fat_lock, "ngfs.fat");
    fs->flock_head = NULL;

    fs->base = ngfs;

    result = ngfs_build_flock_registry(fs);
    if (result < 0) {
        iodropref(fs->bkgio);
        kfree(fs);
        return result;
    }

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
    struct ngfs_fileio * const fileio = (struct ngfs_fileio *)io;
    if (fileio == NULL) {
        debug("Invalid argument(s) to ngfs_reclaim");
        return;
    }

    if (fileio->flock != NULL) {
        ngfs_put_flock(fileio->fs, fileio->flock);
    }

    kfree(fileio);
}

long ngfs_fetch(struct io * io, unsigned long long pos, void * buf, long bufsz) {
    // YOUR CODE HERE
    struct ngfs_fileio * const fileio = (struct ngfs_fileio *)io;
    long retlen;

    if (io == NULL) {
        return -EINVAL;
    }

    rwlock_acquire(&fileio->flock->lock, 0);
    retlen = ngfs_fetch_nolock(fileio, pos, buf, bufsz); // logic moved to helper
    rwlock_release(&fileio->flock->lock);

    return retlen;
}

long ngfs_read(struct io * io, void * buf, long bufsz) {
    // YOUR CODE HERE
    if (io == NULL || (buf == NULL && bufsz != 0)) {
        return -EINVAL;
    }

    struct ngfs_fileio * const fileio = (struct ngfs_fileio *)io;
    long retlen;
    retlen = ngfs_fetch(io, fileio->pos, buf, bufsz);
    if (retlen > 0) {
        fileio->pos += retlen; // update file position after a successful read
    }
    return retlen;
}

long ngfs_store(struct io * io, unsigned long long pos, const void * buf, long len) {
    // YOUR CODE HERE
    struct ngfs_fileio * const fileio=(struct ngfs_fileio *)io;
    long retlen;

    if (io == NULL) {
        return -EINVAL;
    }

    rwlock_acquire(&fileio->flock->lock, 1);
    retlen = ngfs_store_nolock(fileio, pos, buf, len); // logics moved to helper
    rwlock_release(&fileio->flock->lock);

    return retlen;
}

long ngfs_write(struct io * io, const void * buf, long len) {
    // YOUR CODE HERE
    struct ngfs_fileio * const fileio = (struct ngfs_fileio *)io;
    unsigned long long end_pos;
    long retlen;
    int result;

    if (io == NULL || (buf == NULL && len != 0)) {
        return -EINVAL;
    }

    if (buf == NULL || len == 0) {
        return 0;
    }

    rwlock_acquire(&fileio->fs->dir_lock, 0);
    rwlock_acquire(&fileio->flock->lock, 1);

    end_pos = fileio->pos + (unsigned long long)len;
    if (end_pos > fileio->dir.size) {
        result = ngfs_extend_file(fileio, end_pos);
        if (result < 0) {
            rwlock_release(&fileio->flock->lock);
            rwlock_release(&fileio->fs->dir_lock);
            return result;
        }
    }

    retlen = ngfs_store_nolock(fileio, fileio->pos, buf, len);
    if (retlen > 0) {
        fileio->pos += retlen;
    }

    rwlock_release(&fileio->flock->lock);
    rwlock_release(&fileio->fs->dir_lock);
    return retlen;
}

int ngfs_ioctl(struct io * io, int op, void * arg) {
    // YOUR CODE HERE
    if (io == NULL) {
        return -EINVAL;
    }
    
    struct ngfs_fileio * const fileio = (struct ngfs_fileio *)io;
    unsigned long long * const ullarg = (unsigned long long *)arg;
    unsigned long long new_end;
    int result;

    switch (op) {
        case IOC_GETPOS:
            if (ullarg == NULL) {
                debug("Invalid argument to ngfs_ioctl(IOC_GETPOS)");
                return -EINVAL;
            }
            *ullarg = fileio->pos;
            return 0;

        case IOC_SETPOS:
            if (ullarg == NULL) {
                debug("Invalid argument to ngfs_ioctl(IOC_SETPOS)");
                return -EINVAL;
            }
            fileio->pos = *ullarg;
            return 0;

        case IOC_GETEND:
            if (ullarg == NULL) {
                debug("Invalid argument to ngfs_ioctl(IOC_GETEND)");
                return -EINVAL;
            }
            *ullarg = fileio->dir.size;
            return 0;
        
        case IOC_SETEND:
            if (ullarg == NULL) {
                debug("Invalid argument to ngfs_ioctl(IOC_SETEND)");
                return -EINVAL;
            }
            new_end = *ullarg;

            rwlock_acquire(&fileio->fs->dir_lock, 0);
            rwlock_acquire(&fileio->flock->lock, 1);

            if (new_end == fileio->dir.size) {
                result = 0;
            } else if (new_end > fileio->dir.size) {
                result = ngfs_extend_file(fileio, new_end);
            } else {
                result = ngfs_truncate_file(fileio, new_end);
            }

            rwlock_release(&fileio->flock->lock);
            rwlock_release(&fileio->fs->dir_lock);
            return result;

        default:
            return -ENOTSUP;
    }
}

int ngfs_create(struct filesystem * fs, const char * name) {
    // YOUR CODE HERE
    struct ngfs_mount * const ngfs = (struct ngfs_mount *)fs;
    struct ngfs_dir_entry dir;
    unsigned long nentries;
    int result;

    if (fs == NULL || name == NULL) {
        debug("Invalid argument(s) to ngfs_create");
        return -EINVAL;
    }

    if (name[0] == '\0') {
        debug("Invalid argument(s) to ngfs_create: empty file name");
        return -EINVAL;
    }

    if (strcmp(name, ".") == 0) {
        return -EINVAL; // reinforce the . dir
    }

    if (strlen(name) > NGFS_MAX_FILENAME_LEN) {
        debug("Invalid argument(s) to ngfs_create: file name too long");
        return -EINVAL;
    }

    rwlock_acquire(&ngfs->dir_lock, 1);

    result = ngfs_find_root_by_name(ngfs, name, NULL, NULL);
    if (result == 0) {
        rwlock_release(&ngfs->dir_lock);
        debug("File already exists in ngfs_create");
        return -EEXIST;
    } else if (result != -ENOENT) {
        rwlock_release(&ngfs->dir_lock);
        debug("Error searching for file in ngfs_create: %d", result);
        return result;
    }

    nentries = ngfs_root_entry_count(ngfs);

    result = ngfs_ensure_root_capacity(ngfs, nentries + 1);
    if (result < 0) {
        rwlock_release(&ngfs->dir_lock);
        return result;
    }

    memset(&dir, 0, sizeof(dir));
    strncpy(dir.name, name, sizeof(dir.name));
    dir.name[sizeof(dir.name) - 1] = '\0';
    dir.size = 0;
    dir.start_block = NGFS_BLOCK_END;

    result = ngfs_update_root_dir_entry(ngfs, nentries, &dir);
    if (result < 0) {
        rwlock_release(&ngfs->dir_lock);
        return result;
    }

    result = ngfs_set_root_entry_count(ngfs, nentries + 1);
    if (result < 0) {
        rwlock_release(&ngfs->dir_lock);
        return result;
    }

    result = ngfs_create_flock_locked(ngfs, name, NULL);
    if (result < 0) {
        rwlock_release(&ngfs->dir_lock);
        return result;
    }

    rwlock_release(&ngfs->dir_lock);
    return 0;
}

int ngfs_delete(struct filesystem * fs, const char * name) {
    // YOUR CODE HERE
    struct ngfs_mount * const ngfs = (struct ngfs_mount *)fs;
    struct ngfs_dir_entry dir;
    struct ngfs_dir_entry next_dir;
    struct ngfs_dir_entry empty_dir;
    struct ngfs_flock * flock;
    struct ngfs_flock ** link;
    unsigned long dir_index;
    unsigned long i;
    unsigned long nentries;
    uint32_t curr_block;
    uint32_t next_block;
    int result;
    int flock_freed = 0;

    if (fs == NULL || name == NULL) {
        debug("Invalid argument(s) to ngfs_delete");
        return -EINVAL;
    }

    if (name[0] == '\0') {
        debug("Invalid argument(s) to ngfs_delete: empty file name");
        return -EINVAL;
    }

    if (strcmp(name, ".") == 0) {
        return -EINVAL;
    }

    rwlock_acquire(&ngfs->dir_lock, 1);

    result = ngfs_find_root_by_name(ngfs, name, &dir, &dir_index);
    if (result < 0) {
        rwlock_release(&ngfs->dir_lock);
        return result;
    }

    flock = ngfs_find_flock_locked(ngfs, name);
    if (flock == NULL) {
        rwlock_release(&ngfs->dir_lock);
        return -EIO;
    }

    rwlock_acquire(&flock->lock, 1);

    nentries = ngfs_root_entry_count(ngfs);

    if (dir.size > 0 && dir.start_block != NGFS_BLOCK_END) {
        // need to free the data blocks occupied by the file
        rwlock_acquire(&ngfs->fat_lock, 1);
        curr_block = dir.start_block;
        while (curr_block != NGFS_BLOCK_END) {
            next_block = get_next_data_block(ngfs, curr_block);
            set_next_data_block(ngfs, curr_block, NGFS_BLOCK_FREE); // mark block as free in FAT
            curr_block = next_block;
        }
        rwlock_release(&ngfs->fat_lock);
    }

    // Shift up later directory entries to fill the gap
    for (i = dir_index; i + 1 < nentries; i++) {
        result = ngfs_read_root_dir(ngfs, i + 1, &next_dir);
        if (result < 0) {
            rwlock_release(&flock->lock);
            rwlock_release(&ngfs->dir_lock);
            debug("Error reading root dir in ngfs_delete: %d", result);
            return result;
        }

        result = ngfs_update_root_dir_entry(ngfs, i, &next_dir);
        if (result < 0) {
            rwlock_release(&flock->lock);
            rwlock_release(&ngfs->dir_lock);
            return result;
        }
    }

    memset(&empty_dir, 0, sizeof(empty_dir));
    empty_dir.start_block = NGFS_BLOCK_END;

    result = ngfs_update_root_dir_entry(ngfs, nentries - 1, &empty_dir);
    if (result < 0) {
        rwlock_release(&flock->lock);
        rwlock_release(&ngfs->dir_lock);
        return result;
    }

    result = ngfs_set_root_entry_count(ngfs, nentries - 1);
    if (result < 0) {
        rwlock_release(&flock->lock);
        rwlock_release(&ngfs->dir_lock);
        return result;
    }

    flock->deleted = 1;

    if (flock->open_refcnt == 0) {
        for (link = &ngfs->flock_head; *link != NULL; link = &(*link)->next) {
            if (*link == flock) {
                *link = flock->next;
                flock_freed = 1;
                break;
            }
        }
    }

    rwlock_release(&flock->lock);

    if (flock_freed) {
        kfree(flock);
    }

    rwlock_release(&ngfs->dir_lock);
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
    unsigned long nentries;
    size_t name_len, copy_len;
    int result;

    if (listingio == NULL || (buf == NULL && bufsz != 0)) {
        return -EINVAL;
    }

    if (buf == NULL || bufsz == 0) {
        return 0;
    }

    rwlock_acquire(&listingio->fs->dir_lock, 0);

    nentries = ngfs_root_entry_count(listingio->fs);

    while (listingio->next_index < nentries) {
        result = ngfs_read_root_dir(listingio->fs, listingio->next_index, &dir);
        if (result < 0) {
            rwlock_release(&listingio->fs->dir_lock);
            return result;
        }

        listingio->next_index++;

        name_len = strlen(dir.name);
        copy_len = (name_len < (size_t)bufsz) ? name_len : (size_t)bufsz;
        memcpy(buf, dir.name, copy_len);

        rwlock_release(&listingio->fs->dir_lock);
        return copy_len;
    }

    rwlock_release(&listingio->fs->dir_lock);
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
    assert(
        next_block == NGFS_BLOCK_FREE ||
        next_block == NGFS_BLOCK_END ||
        next_block < ngfs_data_block_count(fs)
    );

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
        debug("Invalid argument to get_free_data_block");
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
    unsigned long phys_blkno;
    unsigned long offset;
    uint32_t root_blkno;
    int result;

    if (fs == NULL || dir == NULL) {
        return -EINVAL;
    }

    if (index >= ngfs_root_entry_count(fs)) {
        return -ENOENT;
    }

    result = ngfs_root_block_for_index(fs, index, &root_blkno);
    if (result < 0) {
        return result;
    }

    phys_blkno = ngfs_phys_device_blockno(fs, root_blkno);
    result = cache_fetch(fs->cache, phys_blkno * NGFS_BLKSZ, 1, &blk);
    if (result < 0) {
        return result;
    }

    offset = index % (NGFS_BLKSZ / NGFS_DENSZ);
    entry = (struct ngfs_dir_entry *)blk;
    *dir = entry[offset];

    cache_release(fs->cache, blk, 0);
    return 0;
}

static int ngfs_find_root_by_name(struct ngfs_mount * fs, const char * name, struct ngfs_dir_entry * dir, unsigned long * indexptr) {
    // YOUR CODE HERE
    struct ngfs_dir_entry current_entry;
    unsigned long i, nentries;
    int result;

    if (fs == NULL || name == NULL) {
        return -EINVAL;
    }

    nentries = ngfs_root_entry_count(fs);

    for (i = 0; i < nentries; i++) {
        result = ngfs_read_root_dir(fs, i, &current_entry);
        if (result < 0) {
            return result;
        }

        if (strcmp(current_entry.name, name) == 0) {
            if (dir != NULL) {
                *dir = current_entry;
            }
            if (indexptr != NULL) {
                *indexptr = i;
            }
            return 0;
        }
    }

    return -ENOENT; // entry not found
}

static int ngfs_open_fileio(struct ngfs_mount * fs, const char * name, struct io ** ioptr) {
    // YOUR CODE HERE
    struct ngfs_fileio * fileio;
    struct ngfs_dir_entry dir;
    struct ngfs_flock * flock;
    unsigned long dir_index;
    int result;

    rwlock_acquire(&fs->dir_lock, 1);

    result = ngfs_find_root_by_name(fs, name, &dir, &dir_index);
    if (result < 0) {
        rwlock_release(&fs->dir_lock);
        debug("ngfs_find_root_by_name() failed in ngfs_open_fileio");
        return result;
    }

    flock = ngfs_find_flock_locked(fs, name);
    if (flock == NULL) {
        rwlock_release(&fs->dir_lock);
        return -EIO;
    }

    flock->open_refcnt++;

    rwlock_release(&fs->dir_lock);

    fileio = kcalloc(1, sizeof(*fileio)); // allocate new object space for each open call
    if (fileio == NULL) {
        ngfs_put_flock(fs, flock);
        debug("kcalloc() failed in ngfs_open_fileio");
        return -ENOMEM;
    }

    fileio->fs = fs;
    fileio->dir = dir;
    fileio->dir_index = dir_index;
    fileio->pos = 0; // new file after openning start position placed at 0
    fileio->flock = flock;

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
    unsigned long phys_blkno;
    unsigned long offset;
    uint32_t root_blkno;
    int result;

    if (fs == NULL || dir == NULL) {
        return -EINVAL;
    }

    result = ngfs_root_block_for_index(fs, index, &root_blkno);
    if (result < 0) {
        return result;
    }

    phys_blkno = ngfs_phys_device_blockno(fs, root_blkno);
    result = cache_fetch(fs->cache, phys_blkno * NGFS_BLKSZ, 1, &blk);
    if (result < 0) {
        return result;
    }

    offset = index % (NGFS_BLKSZ / NGFS_DENSZ);
    entry = (struct ngfs_dir_entry *)blk;
    entry[offset] = *dir;

    cache_release(fs->cache, blk, 1);
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
        debug("Invalid argument to ngfs_extend_file");
        return -EINVAL;
    }

    fs = fileio->fs;
    old_size = fileio->dir.size;

    if (new_end <= old_size) {
        // new end is not beyond current file size, incorrect call to this helper
        debug("Invalid argument to ngfs_extend_file");
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
        rwlock_acquire(&fs->fat_lock, 1);
        if (old_block_count == 0) {
            new_block = get_free_data_block(fs);
            if (new_block == NGFS_BLOCK_END) {
                rwlock_release(&fs->fat_lock);
                return -ENOMEM;
            }
            set_next_data_block(fs, new_block, NGFS_BLOCK_END);
            fileio->dir.start_block = new_block;
            prev_block = new_block;
            for (i = 1; i < new_blocks_count; i++) {
                new_block = get_free_data_block(fs);
                if (new_block == NGFS_BLOCK_END) {
                    rwlock_release(&fs->fat_lock);
                    return -ENOMEM;
                }
                set_next_data_block(fs, new_block, NGFS_BLOCK_END);
                set_next_data_block(fs, prev_block, new_block);
                prev_block = new_block;
            }
        } else {
            curr_block = fileio->dir.start_block;
            for (i = 1; i < old_block_count; i++) {
                curr_block = get_next_data_block(fs, curr_block);
            }
            prev_block = curr_block;
            for (i = old_block_count; i < new_blocks_count; i++) {
                new_block = get_free_data_block(fs);
                if (new_block == NGFS_BLOCK_END) {
                    rwlock_release(&fs->fat_lock);
                    return -ENOMEM;
                }
                set_next_data_block(fs, new_block, NGFS_BLOCK_END);
                set_next_data_block(fs, prev_block, new_block);
                prev_block = new_block;
            }
        }
        rwlock_release(&fs->fat_lock);
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
        result = ngfs_store_nolock(fileio, zero_pos, zero_buf, chunk_size);
        if (result <= 0) {
            debug("ngfs_store() failed in ngfs_extend_file while zero-filling new blocks");
            return result < 0 ? result : -EIO;
        }
        zero_pos += result;
        remaining_zero_bytes -= result;
    }

    result = ngfs_sync_file_dentry_locked(fileio);
    if (result < 0) {
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
        debug("Invalid argument to ngfs_truncate_file");
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
        rwlock_acquire(&fs->fat_lock, 1);
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
                if (curr_block == NGFS_BLOCK_END) {
                    rwlock_release(&fs->fat_lock);
                    return -EIO;
                }
            }
            free_block = get_next_data_block(fs, curr_block);
            set_next_data_block(fs, curr_block, NGFS_BLOCK_END);
            while (free_block != NGFS_BLOCK_END) {
                next_block = get_next_data_block(fs, free_block);
                set_next_data_block(fs, free_block, NGFS_BLOCK_FREE);
                free_block = next_block;
            }
            fileio->dir.size = new_end;
        }
        rwlock_release(&fs->fat_lock);
    }

    if (fileio->pos > new_end) {
        fileio->pos = new_end; // move file position back to new end if it is currently beyond new end after truncation
    }

    result = ngfs_sync_file_dentry_locked(fileio);
    if (result < 0) {
        return result;
    }

    return 0;
}

static struct ngfs_flock * ngfs_find_flock_locked(struct ngfs_mount * fs, const char * name) {
    struct ngfs_flock * cur;

    if (fs == NULL || name == NULL) {
        return NULL;
    }

    for (cur = fs->flock_head; cur != NULL; cur = cur->next) {
        if (strcmp(cur->name, name) == 0) {
            return cur;
        }
    }

    return NULL;
}

static int ngfs_create_flock_locked(struct ngfs_mount * fs, const char * name, struct ngfs_flock ** out) {
    struct ngfs_flock * flock;

    if (fs == NULL || name == NULL) {
        return -EINVAL;
    }

    flock = kcalloc(1, sizeof(*flock));
    if (flock == NULL) {
        return -ENOMEM;
    }

    strncpy(flock->name, name, sizeof(flock->name));
    flock->name[sizeof(flock->name) - 1] = '\0';
    rwlock_init(&flock->lock, "ngfs.file");
    flock->open_refcnt = 0;
    flock->deleted = 0;
    flock->next = fs->flock_head;
    fs->flock_head = flock;

    if (out != NULL) {
        *out = flock;
    }

    return 0;
}

static void ngfs_put_flock(struct ngfs_mount * fs, struct ngfs_flock * flock) {
    struct ngfs_flock ** link;

    if (fs == NULL || flock == NULL) {
        return;
    }

    rwlock_acquire(&fs->dir_lock, 1);

    if (flock->open_refcnt > 0) {
        flock->open_refcnt--;
    }

    if (flock->deleted && flock->open_refcnt == 0) {
        for (link = &fs->flock_head; *link != NULL; link = &(*link)->next) {
            if (*link == flock) {
                *link = flock->next;
                kfree(flock);
                break;
            }
        }
    }

    rwlock_release(&fs->dir_lock);
}

static int ngfs_build_flock_registry(struct ngfs_mount * fs) {
    struct ngfs_dir_entry dir;
    unsigned long i, nentries;
    int result;

    if (fs == NULL) {
        return -EINVAL;
    }

    rwlock_acquire(&fs->dir_lock, 1);

    nentries = ngfs_root_entry_count(fs);
    for (i = 1; i < nentries; i++) {
        result = ngfs_read_root_dir(fs, i, &dir);
        if (result < 0) {
            rwlock_release(&fs->dir_lock);
            return result;
        }

        if (dir.name[0] == '\0') {
            continue;
        }

        if (ngfs_find_flock_locked(fs, dir.name) == NULL) {
            result = ngfs_create_flock_locked(fs, dir.name, NULL);
            if (result < 0) {
                rwlock_release(&fs->dir_lock);
                return result;
            }
        }
    }

    rwlock_release(&fs->dir_lock);
    return 0;
}

static int ngfs_get_root_dentry(struct ngfs_mount * fs, struct ngfs_dir_entry * dir) {
    void * blk;
    struct ngfs_dir_entry * entry;
    unsigned long root_phys_device_blkno;
    int result;

    if (fs == NULL || dir == NULL) {
        return -EINVAL;
    }

    root_phys_device_blkno = ngfs_phys_device_blockno(fs, NGFS_ROOT_DATA_BLOCK);
    result = cache_fetch(fs->cache, root_phys_device_blkno * NGFS_BLKSZ, 1, &blk);
    if (result < 0) {
        return result;
    }

    entry = (struct ngfs_dir_entry *)blk;
    *dir = entry[0];

    cache_release(fs->cache, blk, 0);
    return 0;
}

static unsigned long ngfs_root_entry_count(struct ngfs_mount * fs) {
    struct ngfs_dir_entry root;
    int result;

    result = ngfs_get_root_dentry(fs, &root);
    if (result < 0) {
        return 0;
    }

    return root.size / NGFS_DENSZ;
}

static unsigned long ngfs_root_capacity_entries(struct ngfs_mount * fs) {
    unsigned long blocks = 1;
    uint32_t blk = NGFS_ROOT_DATA_BLOCK;

    while (1) {
        uint32_t next = get_next_data_block(fs, blk);
        if (next == NGFS_BLOCK_END) {
            break;
        }
        blk = next;
        blocks++;
    }

    return blocks * (NGFS_BLKSZ / NGFS_DENSZ);
}

static int ngfs_root_block_for_index(struct ngfs_mount * fs, unsigned long index, uint32_t * blkno_out) {
    unsigned long entries_per_block;
    unsigned long block_hops;
    uint32_t blk;

    if (fs == NULL || blkno_out == NULL) {
        return -EINVAL;
    }

    entries_per_block = NGFS_BLKSZ / NGFS_DENSZ;
    block_hops = index / entries_per_block;
    blk = NGFS_ROOT_DATA_BLOCK;

    while (block_hops > 0) {
        blk = get_next_data_block(fs, blk);
        if (blk == NGFS_BLOCK_END) {
            return -EIO;
        }
        block_hops--;
    }

    *blkno_out = blk;
    return 0;
}

static int ngfs_set_root_entry_count(struct ngfs_mount * fs, unsigned long nentries) {
    void * blk;
    struct ngfs_dir_entry * entry;
    unsigned long root_phys_device_blkno;
    int result;

    if (fs == NULL || nentries == 0) {
        return -EINVAL;
    }

    root_phys_device_blkno = ngfs_phys_device_blockno(fs, NGFS_ROOT_DATA_BLOCK);
    result = cache_fetch(fs->cache, root_phys_device_blkno * NGFS_BLKSZ, 1, &blk);
    if (result < 0) {
        return result;
    }

    entry = (struct ngfs_dir_entry *)blk;
    entry[0].size = nentries * NGFS_DENSZ;
    entry[0].start_block = NGFS_ROOT_DATA_BLOCK;
    strncpy(entry[0].name, ".", sizeof(entry[0].name));
    entry[0].name[sizeof(entry[0].name) - 1] = '\0';

    cache_release(fs->cache, blk, 1);
    return 0;
}

static int ngfs_ensure_root_capacity(struct ngfs_mount * fs, unsigned long needed_entries) {
    unsigned long capacity;
    uint32_t blk;
    uint32_t newblk;

    if (fs == NULL) {
        return -EINVAL;
    }

    capacity = ngfs_root_capacity_entries(fs);
    if (capacity >= needed_entries) {
        return 0;
    }

    rwlock_acquire(&fs->fat_lock, 1);

    blk = NGFS_ROOT_DATA_BLOCK;
    while (get_next_data_block(fs, blk) != NGFS_BLOCK_END) {
        blk = get_next_data_block(fs, blk);
    }

    while (capacity < needed_entries) {
        newblk = get_free_data_block(fs);
        if (newblk == NGFS_BLOCK_END) {
            rwlock_release(&fs->fat_lock);
            return -ENOMEM;
        }

        set_next_data_block(fs, newblk, NGFS_BLOCK_END);
        set_next_data_block(fs, blk, newblk);

        {
            void * newblk_ptr;
            unsigned long phys = ngfs_phys_device_blockno(fs, newblk);
            int result = cache_fetch(fs->cache, phys * NGFS_BLKSZ, 1, &newblk_ptr);
            if (result < 0) {
                rwlock_release(&fs->fat_lock);
                return result;
            }

            memset(newblk_ptr, 0, NGFS_BLKSZ);
            cache_release(fs->cache, newblk_ptr, 1);
        }

        blk = newblk;
        capacity += NGFS_BLKSZ / NGFS_DENSZ;
    }

    rwlock_release(&fs->fat_lock);
    return 0;
}

static int ngfs_sync_file_dentry_locked(struct ngfs_fileio * fileio) {
    unsigned long cur_index;
    int result;

    if (fileio == NULL || fileio->flock == NULL) {
        return -EINVAL;
    }

    result = ngfs_find_root_by_name(fileio->fs, fileio->flock->name, NULL, &cur_index);
    if (result < 0) {
        return result;
    }

    fileio->dir_index = cur_index;
    return ngfs_update_root_dir_entry(fileio->fs, cur_index, &fileio->dir);
}

static long ngfs_fetch_nolock(struct ngfs_fileio * fileio, unsigned long long pos, void * buf, long bufsz) {
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

    if (fileio == NULL || (buf == NULL && bufsz != 0)) {
        return -EINVAL;
    }

    if (buf == NULL || bufsz == 0) {
        return 0;
    }

    if (pos > fileio->dir.size) {
        return -EINVAL;
    }

    if (pos == fileio->dir.size) {
        return 0;
    }

    if (fileio->dir.size - pos < (unsigned long long)bufsz) {
        bufsz = fileio->dir.size - pos;
    }

    file_block_index = pos / NGFS_BLKSZ;
    block_offset = pos % NGFS_BLKSZ;
    curr_block = fileio->dir.start_block;

    for (i = 0; i < file_block_index; i++) {
        curr_block = get_next_data_block(fs, curr_block);
        if (curr_block == NGFS_BLOCK_END) {
            return -EIO;
        }
    }

    bufp = (char *)buf;
    while (bufsz > 0) {
        phys_block_no = ngfs_phys_device_blockno(fs, curr_block);
        result = cache_fetch(fs->cache, phys_block_no * NGFS_BLKSZ, 1, &blk);
        if (result < 0) {
            return result;
        }

        copy_len = (NGFS_BLKSZ - block_offset < (unsigned long)bufsz)
            ? (NGFS_BLKSZ - block_offset)
            : (size_t)bufsz;

        memcpy(bufp, (char *)blk + block_offset, copy_len);
        cache_release(fs->cache, blk, 0);

        bufsz -= copy_len;
        bufp += copy_len;
        block_offset = 0;

        if (bufsz > 0) {
            curr_block = get_next_data_block(fs, curr_block);
            if (curr_block == NGFS_BLOCK_END) {
                return -EIO;
            }
        }
    }

    retlen = bufp - (char *)buf;
    return retlen;
}

static long ngfs_store_nolock(struct ngfs_fileio * fileio, unsigned long long pos, const void * buf, long len) {
    struct ngfs_mount * const fs = fileio->fs;
    unsigned long file_block_index;
    unsigned long block_offset;
    unsigned long phys_block_no;
    unsigned long i;
    long retlen;
    uint32_t curr_block;
    size_t copy_len;
    const char * bufp;
    void * blk;
    int result;

    if (fileio == NULL || (buf == NULL && len != 0)) {
        return -EINVAL;
    }

    if (buf == NULL || len == 0) {
        return 0;
    }

    if (pos > fileio->dir.size) {
        return -EINVAL;
    }

    if (pos == fileio->dir.size) {
        return 0;
    }

    if (fileio->dir.size - pos < (unsigned long long)len) {
        len = fileio->dir.size - pos;
    }

    file_block_index = pos / NGFS_BLKSZ;
    block_offset = pos % NGFS_BLKSZ;
    curr_block = fileio->dir.start_block;

    for (i = 0; i < file_block_index; i++) {
        curr_block = get_next_data_block(fs, curr_block);
        if (curr_block == NGFS_BLOCK_END) {
            return -EIO;
        }
    }

    bufp = (const char *)buf;
    while (len > 0) {
        phys_block_no = ngfs_phys_device_blockno(fs, curr_block);
        result = cache_fetch(fs->cache, phys_block_no * NGFS_BLKSZ, 1, &blk);
        if (result < 0) {
            return result;
        }

        copy_len = (NGFS_BLKSZ - block_offset < (unsigned long)len)
            ? (NGFS_BLKSZ - block_offset)
            : (size_t)len;

        memcpy((char *)blk + block_offset, bufp, copy_len);
        cache_release(fs->cache, blk, 1);

        len -= copy_len;
        bufp += copy_len;
        block_offset = 0;

        if (len > 0) {
            curr_block = get_next_data_block(fs, curr_block);
            if (curr_block == NGFS_BLOCK_END) {
                return -EIO;
            }
        }
    }

    retlen = bufp - (const char *)buf;
    return retlen;
}
