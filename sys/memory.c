// memory.c - Physical and virtual memory manager
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef MEMORY_TRACE
#define TRACE
#endif

#ifdef MEMORY_DEBUG
#define DEBUG
#endif

#include "memory.h"
#include "conf.h"
#include "board-conf.h"
#include "console.h"
#include "error.h"
#include "heap.h"
#include "misc.h"
#include "process.h"
#include "riscv.h"
#include "string.h"
#include "thread.h"

// COMPILE-TIME CONFIGURATION
//

// Minimum amount of memory in the initial heap block.

#ifndef HEAP_INIT_MIN
#define HEAP_INIT_MIN 256
#endif

// INTERNAL CONSTANT DEFINITIONS
//

#define PTE_ORDER 3
#define PTE_CNT (1U << (PAGE_ORDER - PTE_ORDER))

#ifndef PAGING_MODE
#define PAGING_MODE RISCV_SATP_MODE_Sv39
#endif

#ifndef ROOT_LEVEL
#define ROOT_LEVEL 2
#endif

// IMPORTED GLOBAL SYMBOLS
//

// linker-provided (kernel.ld)
extern char _kimg_start[];
extern char _kimg_text_start[];
extern char _kimg_text_end[];
extern char _kimg_rodata_start[];
extern char _kimg_rodata_end[];
extern char _kimg_data_start[];
extern char _kimg_data_end[];
extern char _kimg_end[];

// EXPORTED GLOBAL VARIABLES
//

char memory_initialized = 0;

// INTERNAL TYPE DEFINITIONS
//

// We keep free physical pages in a linked list of _chunks_, where each chunk
// consists of several consecutive pages of memory. Initially, all free pages
// are in a single large chunk. To allocate a block of pages, we break up the
// smallest chunk on the list.

struct page_chunk {
    struct page_chunk * next;   // next page chunk in list
    unsigned long pagecnt;      // number of pages in chunk
};

struct pte {
    uint64_t flags : 8;
    uint64_t rsw : 2;
    uint64_t ppn : 44;
    uint64_t reserved : 7;
    uint64_t pbmt : 2;
    uint64_t n : 1;
};

// INTERNAL MACRO DEFINITIONS
//

#define VPN(vma) ((vma) / PAGE_SIZE)
#define VPN2(vma) ((VPN(vma) >> (2 * 9)) % PTE_CNT)
#define VPN1(vma) ((VPN(vma) >> (1 * 9)) % PTE_CNT)
#define VPN0(vma) ((VPN(vma) >> (0 * 9)) % PTE_CNT)

// The following macros test is a PTE is valid, global, or a leaf. The argument
// is a struct pte (*not* a pointer to a struct pte).

#define PTE_VALID(pte) (((pte).flags & PTE_V) != 0)
#define PTE_GLOBAL(pte) (((pte).flags & PTE_G) != 0)
#define PTE_LEAF(pte) (((pte).flags & (PTE_R | PTE_W | PTE_X)) != 0)

#define PT_INDEX(lvl, vpn) \
    (((vpn) & (0x1FF << (lvl * (PAGE_ORDER - PTE_ORDER)))) >> (lvl * (PAGE_ORDER - PTE_ORDER)))

// CONSTANTS
//
#define CAT_SIZE 15
const char * art = 
" _\0"
" \\`*-.\0"
"  )  _`-.\0"
" .  : `. .\0"
" : _   '  \\ \0"
" ; *` _.   `*-._\0"
" `-.-'          `-.\0"
"   ;       `       `.\0"
"   :.       .        \\ \0"
"   . \\  .   :   .-'   .\0"
"   '  `+.;  ;  '      :\0"
"   :  '  |    ;       ;-.\0"
"   ; '   : :`-:     _.`* ;\0"
".*' /  .*' ; .*`- +'  `*'\0"
"`*-*   `*-*  `*-*'\0";


// INTERNAL FUNCTION DECLARATIONS
//


static inline mtag_t active_space_mtag(void);
static inline mtag_t ptab_to_mtag(struct pte * root, unsigned int asid);
static inline struct pte * mtag_to_ptab(mtag_t mtag);
static inline struct pte * active_space_ptab(void);

static inline void * pageptr(uintptr_t n);
static inline uintptr_t pagenum(const void * p);
static inline int wellformed(uintptr_t vma);

static inline struct pte leaf_pte(const void * pp, uint_fast8_t rwxug_flags);
static inline struct pte ptab_pte(const struct pte * pt, uint_fast8_t g_flag);
static inline struct pte null_pte(void);


static int print_memory_info(struct mregion * mmio_imm, unsigned long mmiocnt,
                             struct mregion * ram_imm, unsigned long ramcnt,
                             struct mregion * resv_imm, unsigned long resvcnt);


// INTERNAL GLOBAL VARIABLES
//

static mtag_t main_mtag;

static struct pte main_pt2[PTE_CNT] __attribute__((section(".bss.pagetable"), aligned(4096)));

static struct pte main_pt1_0x80000[PTE_CNT]
__attribute__((section(".bss.pagetable"), aligned(4096)));

static struct pte main_pt0_0x80000[PTE_CNT]
__attribute__((section(".bss.pagetable"), aligned(4096)));

static struct pte main_pt0_0x80001[PTE_CNT]
__attribute__((section(".bss.pagetable"), aligned(4096)));

static struct page_chunk * free_chunk_list;

// EXPORTED FUNCTION DECLARATIONS
//

void memory_init (
    const struct mregion * ram_og, unsigned long ramcnt,
    const struct mregion * mmio_og, unsigned long mmiocnt,
    const struct mregion * resv_og, unsigned long resvcnt)
{

    const void * const text_start = _kimg_text_start;
    const void * const text_end = _kimg_text_end;
    const void * const rodata_start = _kimg_rodata_start;
    const void * const rodata_end = _kimg_rodata_end;
    const void * const data_start = _kimg_data_start;

    // All parameters are used for running on real hardware and printing memory info; 
    // you can ignore them in this simplified memory_init() implementation


    struct mregion mmio[mmiocnt];
    struct mregion ram[ramcnt];
    struct mregion resv[resvcnt+1];

    memcpy(mmio, mmio_og, mmiocnt * sizeof(struct mregion));
    memcpy(ram, ram_og, ramcnt * sizeof(struct mregion));
    memcpy(resv, resv_og, resvcnt * sizeof(struct mregion));

    //The memory print function expects the kernel to be mapped as a reserved
    //region and the array to be sorted. For the qvirt target, the second 
    //reserved region is in high memory, so we move it right one and insert our
    //kernel as the second to last entry.
    resvcnt++;
    resv[resvcnt-1].pma = resv[resvcnt-2].pma;
    resv[resvcnt-1].size = resv[resvcnt-2].size;
    resv[resvcnt-2].pma = 
        ROUND_DOWN((uintptr_t)(void *)_kimg_start, PAGE_SIZE);
    resv[resvcnt-2].size = 
        ROUND_UP((uintptr_t)(void *)_kimg_end, PAGE_SIZE) - 
        ROUND_DOWN((uintptr_t)(void *)_kimg_start, PAGE_SIZE);

    kprintfluffy(40, &art, "");   
    kprintfluffy(40, &art, "System Memory Map:");
    kprintfluffy(40, &art, "");

    int fluffy = print_memory_info(mmio, mmiocnt, ram, ramcnt, resv, resvcnt);

    for(int i = 0; i < CAT_SIZE - 5 - fluffy; i++){ 
        kprintfluffy(40, &art, "");
    }

    void * heap_start;
    void * heap_end;

    uintptr_t pma;
    const void * pp;


    // Kernel must fit inside 2MB megapage (one level 1 PTE)

    if (MEGA_SIZE < _kimg_end - _kimg_start) panic(NULL);

    // Initialize main page table with the following direct mapping:
    //
    //         0 to RAM_START:           RW gigapages (MMIO region)
    // _kimg_start to _kimg_end:         RX/R/RW pages based on kernel image
    // _kimg_end to RAM_START+MEGA_SIZE: RW pages (heap and free page pool)
    // RAM_START+MEGA_SIZE to RAM_END:   RW megapages (free page pool)
    //
    // RAM_START = 0x80000000
    // MEGA_SIZE = 2 MB
    // GIGA_SIZE = 1 GB

    // Identity mapping of MMIO region as two gigapage mappings
    for (pma = 0; pma < RAM_START_PMA; pma += GIGA_SIZE)
        main_pt2[VPN2(pma)] = leaf_pte((void * )pma, PTE_R | PTE_W | PTE_G);

    // Third gigarange has a second-level subtable
    main_pt2[VPN2(RAM_START_PMA)] = ptab_pte(main_pt1_0x80000, PTE_G);

    // First two physical megaranges of RAM are mapped as individual pages with
    // permissions based on kernel image region.
    //
    // This also means that the kernel must be smaller than 4 MB

    main_pt1_0x80000[VPN1(RAM_START_PMA)] = ptab_pte(main_pt0_0x80000, PTE_G);
    main_pt1_0x80000[VPN1(RAM_START_PMA + MEGA_SIZE)] = ptab_pte(main_pt0_0x80001, PTE_G);

    //Map the reserved region before the kernel
    for (pp = RAM_START; pp < text_start; pp+=PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] = leaf_pte(pp, PTE_R);
    }

    for (pp = text_start; pp < text_end; pp += PAGE_SIZE) {
        assert(PTE_VALID(main_pt1_0x80000[VPN1((uintptr_t)pp)])); //Kernel too big.
        struct pte * pt1 = pageptr(main_pt1_0x80000[VPN1((uintptr_t)pp)].ppn);
        pt1[VPN0((uintptr_t)pp)] = leaf_pte(pp, PTE_R | PTE_X | PTE_G);
    }

    for (pp = rodata_start; pp < rodata_end; pp += PAGE_SIZE) {
        assert(PTE_VALID(main_pt1_0x80000[VPN1((uintptr_t)pp)]));
        struct pte * pt1 = pageptr(main_pt1_0x80000[VPN1((uintptr_t)pp)].ppn);
        pt1[VPN0((uintptr_t)pp)] = leaf_pte(pp, PTE_R | PTE_G);
    }

    for (pp = data_start; pp < RAM_START + MEGA_SIZE; pp += PAGE_SIZE) {
        assert(PTE_VALID(main_pt1_0x80000[VPN1((uintptr_t)pp)]));
        struct pte * pt1 = pageptr(main_pt1_0x80000[VPN1((uintptr_t)pp)].ppn);
        pt1[VPN0((uintptr_t)pp)] = leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Remaining RAM mapped in 2MB megapages - dtb area for QEMU

    for (pp = RAM_START + MEGA_SIZE; pp < RAM_END - MEGA_SIZE; pp += MEGA_SIZE) {
        main_pt1_0x80000[VPN1((uintptr_t)pp)] = leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    //Map DTB as read only
    main_pt1_0x80000[VPN1((uintptr_t)pp)] = leaf_pte(pp, PTE_R | PTE_G);

    // Enable paging; this part always makes me nervous.

    main_mtag = ptab_to_mtag(main_pt2, 0);
    csrw_satp(main_mtag);
    kprintf("Enabled Address Translation.\n");
    sfence_vma();

    // Give the memory between the end of the kernel image and the next page
    // boundary to the heap allocator, but make sure it is at least
    // HEAP_INIT_MIN bytes.

    heap_start = _kimg_end;
    heap_end = (void * )ROUND_UP((uintptr_t)heap_start, PAGE_SIZE);

    if (heap_end - heap_start < HEAP_INIT_MIN) {
        heap_end += ROUND_UP(HEAP_INIT_MIN - (heap_end - heap_start), PAGE_SIZE);
    }

    if (RAM_END < heap_end)
        panic("out of memory");

    // Initialize heap memory manager

    heap_init(heap_start, heap_end - heap_start);

    debug("Heap allocator: [%p,%p): %zu KB free", heap_start, heap_end,
            (heap_end - heap_start) / 1024);

    debug("Heap allocator: [%p,%p): %zu KB free",
            heap_start, heap_end, (heap_end - heap_start) / 1024);

    // YOUR CODE HERE
    // Initialize free chunk list
    struct mregion excd[resvcnt + mmiocnt + 3];//the excluded physical regions
    //counter for the entries in excd
    unsigned long excdc = 0;
    //boundaries for the kernal and page table
    uintptr_t kernel_start;
    uintptr_t kernel_end;
    uintptr_t ptab_start;
    uintptr_t ptab_end;

    free_chunk_list = NULL;

//copy the original reserved regions and add the mmio regions
    for (unsigned long i = 0; i + 1 < resvcnt; i++) excd[excdc++] = resv_og[i];
    for (unsigned long i = 0; i < mmiocnt; i++) excd[excdc++] = mmio_og[i];

//add kernal image to excd
    kernel_start = ROUND_DOWN((uintptr_t)_kimg_start, PAGE_SIZE);
    kernel_end = ROUND_UP((uintptr_t)_kimg_end, PAGE_SIZE);
    excd[excdc++] = (struct mregion) {
        .pma = kernel_start,
        .size = kernel_end - kernel_start
    };
//compute the conservative excd
    ptab_start = ROUND_DOWN((uintptr_t)main_pt2, PAGE_SIZE);
    if ((uintptr_t)main_pt1_0x80000 < ptab_start) ptab_start = ROUND_DOWN((uintptr_t)main_pt1_0x80000, PAGE_SIZE);
    if ((uintptr_t)main_pt0_0x80000 < ptab_start) ptab_start = ROUND_DOWN((uintptr_t)main_pt0_0x80000, PAGE_SIZE);
    if ((uintptr_t)main_pt0_0x80001 < ptab_start) ptab_start = ROUND_DOWN((uintptr_t)main_pt0_0x80001, PAGE_SIZE);

    ptab_end = ROUND_UP((uintptr_t)main_pt2 + sizeof(main_pt2), PAGE_SIZE);
    if ((uintptr_t)main_pt1_0x80000 + sizeof(main_pt1_0x80000) > ptab_end) ptab_end = ROUND_UP((uintptr_t)main_pt1_0x80000 + sizeof(main_pt1_0x80000), PAGE_SIZE);
    if ((uintptr_t)main_pt0_0x80000 + sizeof(main_pt0_0x80000) > ptab_end) ptab_end = ROUND_UP((uintptr_t)main_pt0_0x80000 + sizeof(main_pt0_0x80000), PAGE_SIZE);
    if ((uintptr_t)main_pt0_0x80001 + sizeof(main_pt0_0x80001) > ptab_end) ptab_end = ROUND_UP((uintptr_t)main_pt0_0x80001 + sizeof(main_pt0_0x80001), PAGE_SIZE);

    excd[excdc].pma = ptab_start;
    excd[excdc].size = ptab_end - ptab_start;
    excdc++;

//exclude the inital heap bootstrap range
    excd[excdc].pma = (uintptr_t)heap_start;
    excd[excdc].size = (uintptr_t)heap_end - (uintptr_t)heap_start;
    excdc++;
//iterate through each descriptor and carve out gaps
    for (unsigned long i = 0; i < ramcnt; i++) {
        uintptr_t scanc = ram_og[i].pma;//RAM's scan cursor
        uintptr_t const ram_end = ram_og[i].pma + ram_og[i].size;//end of RAM
        while (scanc < ram_end) {//find the next boundary and clear free gaps
            uintptr_t nearest = ram_end;
            int consumed = 0;
//find the overlap relationship
            for (unsigned long j = 0; j < excdc; j++) {
                //start and end address of excluded interval
                uintptr_t const e_start = excd[j].pma;
                uintptr_t const e_end = excd[j].pma + excd[j].size;

                if (e_end <= scanc || ram_end <= e_start) continue;//continue if not overlap
                if (e_start <= scanc) {
                    scanc = e_end;
                    consumed = 1;
                    break;
                }
                if (e_start < nearest) nearest = e_start;
            }
            if (consumed) continue;

            if (scanc < nearest) {
                uintptr_t const chunk_start = ROUND_UP(scanc, PAGE_SIZE);
                uintptr_t const chunk_end = ROUND_DOWN(nearest, PAGE_SIZE);

                if (chunk_start < chunk_end) {//the existed span to a free node
                    struct page_chunk * const chunk = (struct page_chunk *)chunk_start;
                    chunk->next = free_chunk_list;
                    chunk->pagecnt = (chunk_end - chunk_start) / PAGE_SIZE;
                    free_chunk_list = chunk;
                }
            }

            scanc = nearest;
        }
    }
    
    kprintfluffy(40, &art, "Free Chunk List Initialized!");
    kprintfluffy(40, &art, "%d Free Pages.", free_phys_page_count());
    kprintfluffy(40, &art, "");

    debug("Page allocator: [%p,%p): %u pages free",
            heap_end, RAM_END, free_chunk_list->pagecnt);

    // Allow supervisor to access user memory. We could be more precise by only
    // enabling supervisor access to user memory when we are explicitly trying
    // to access user memory, and disable it at other times. This would catch
    // bugs that cause inadvertent access to user memory (due to bugs).

    csrs_sstatus(RISCV_SSTATUS_SUM);

    memory_initialized = 1;
}

mtag_t active_mspace(void) {
    // YOUR CODE HERE
    return active_space_mtag();
}

mtag_t switch_mspace(mtag_t mtag) {
    // YOUR CODE HERE
    mtag_t const previous_val = active_space_mtag();
    csrw_satp(mtag);
    sfence_vma();
    return previous_val;
}

mtag_t clone_active_mspace(void) {
    // YOUR CODE HERE
    //root page table of source space and for clone.
    struct pte * const root_src = active_space_ptab();
    struct pte * const root_cl = alloc_phys_page();
    uintptr_t ptr_src;//source physical pointer

    memset(root_cl, 0, PAGE_SIZE);

    for (unsigned int i = 0; i < PTE_CNT; i++) {//interate every level2 PTE
        //continue if invalid or shallow copy
        if (!PTE_VALID(root_src[i])) {
            root_cl[i] = null_pte();
            continue;
        }
        if (PTE_GLOBAL(root_src[i])) {
            root_cl[i] = root_src[i];
            continue;
        }
        if (PTE_LEAF(root_src[i]))
            panic("non-global gigapage clone unsupported");
//the corresponding table for source and destinated ptr on level1
        struct pte * const root_src1 = pageptr(root_src[i].ppn);
        struct pte * const root_cl1 = alloc_phys_page();
        memset(root_cl1, 0, PAGE_SIZE);
        root_cl[i] = ptab_pte(root_cl1, 0);

        for (unsigned int j = 0; j < PTE_CNT; j++) {//interate the level 1 PTE
            if (!PTE_VALID(root_src1[j])) {
                root_cl1[j] = null_pte();
                continue;
            }
            if (PTE_GLOBAL(root_src1[j])) {
                root_cl1[j] = root_src1[j];
                continue;
            }
            if (PTE_LEAF(root_src1[j]))
                panic("non-global megapage clone unsupported");

            struct pte * const root_src0 = pageptr(root_src1[j].ppn);
            struct pte * const root_cl0 = alloc_phys_page();
            memset(root_cl0, 0, PAGE_SIZE);
            root_cl1[j] = ptab_pte(root_cl0, 0);
//same process for level 0
            for (unsigned int k = 0; k < PTE_CNT; k++) {
                if (!PTE_VALID(root_src0[k])) {
                    root_cl0[k] = null_pte();
                    continue;
                }
                if (PTE_GLOBAL(root_src0[k])) {
                    root_cl0[k] = root_src0[k];
                    continue;
                }
                if (!PTE_LEAF(root_src0[k]))
                    panic("invalid level-0 non-leaf PTE in clone");
//allocate a new data page abd deep cpy
                ptr_src = (uintptr_t)pageptr(root_src0[k].ppn);
                void * const dst_pp = alloc_phys_page();
                memcpy(dst_pp, (void *)ptr_src, PAGE_SIZE);
                struct pte tmp;//recreation lead with same bits
                tmp.flags = (root_src0[k].flags & (PTE_R | PTE_W | PTE_X | PTE_U | PTE_G))| PTE_A | PTE_D | PTE_V;
                tmp.ppn = pagenum(dst_pp);
                root_cl0[k] = tmp;
            }
        }
    }

    return ptab_to_mtag(root_cl, 0);
}

void reset_active_mspace(void) {
    // YOUR CODE HERE
    struct pte * const root_pt2 = active_space_ptab();//current(2) root page table

    for (unsigned int i = 0; i < PTE_CNT; i++) {//iterate and lear all non global mappings of L2
        if (!PTE_VALID(root_pt2[i]) || PTE_GLOBAL(root_pt2[i]))
            continue;
        if (PTE_LEAF(root_pt2[i]))
            panic("non-global gigapage reset unsupported");

        struct pte * const root_pt1 = pageptr(root_pt2[i].ppn);//L1 table
        int root_pt1_empty = 1;

        for (unsigned int j = 0; j < PTE_CNT; j++) {//L1 traversal clearing
            if (!PTE_VALID(root_pt1[j]))
                continue;
            if (PTE_GLOBAL(root_pt1[j])) {
                root_pt1_empty = 0;
                continue;
            }
            if (PTE_LEAF(root_pt1[j])) {
                free_phys_pages(pageptr(root_pt1[j].ppn), MEGA_SIZE / PAGE_SIZE);
                root_pt1[j] = null_pte();
                continue;
            }

            struct pte * const pt0 = pageptr(root_pt1[j].ppn);//L0 table
            int pt0_empty = 1;

            for (unsigned int k = 0; k < PTE_CNT; k++) {//L0 traversal clearing
                if (!PTE_VALID(pt0[k]))
                    continue;
                if (PTE_GLOBAL(pt0[k])) {
                    pt0_empty = 0;
                    continue;
                }
                if (!PTE_LEAF(pt0[k]))
                    panic("invalid level-0 non-leaf PTE in reset");
                free_phys_page(pageptr(pt0[k].ppn));
                pt0[k] = null_pte();
            }

            if (pt0_empty) {
                free_phys_page(pt0);
                root_pt1[j] = null_pte();
            } else {
                root_pt1_empty = 0;
            }
        }
//if all is empty, free them
        if (root_pt1_empty) {
            free_phys_page(root_pt1);
            root_pt2[i] = null_pte();
        }
    }

    sfence_vma();
    return;
}

mtag_t discard_active_mspace(void) {
    // YOUR CODE HERE
    mtag_t const dsc_mtag = active_space_mtag();//mtag for discarded memory
    if (dsc_mtag == main_mtag)
        return main_mtag;
    switch_mspace(main_mtag);//switch first

    struct pte * const root_pt2 = mtag_to_ptab(dsc_mtag);//old mtage root page table

    for (unsigned int i = 0; i < PTE_CNT; i++) {//free all non global mappings
        if (!PTE_VALID(root_pt2[i]) || PTE_GLOBAL(root_pt2[i]))
            continue;
        struct pte * const root_pt1 = pageptr(root_pt2[i].ppn);

        for (unsigned int j = 0; j < PTE_CNT; j++) {
            if (!PTE_VALID(root_pt1[j]) || PTE_GLOBAL(root_pt1[j]))
                continue;

            if (PTE_LEAF(root_pt1[j])) {//non global megapage leaves
                free_phys_pages(pageptr(root_pt1[j].ppn), MEGA_SIZE / PAGE_SIZE);//freeing covered page run
                root_pt1[j] = null_pte();
                continue;
            }

            struct pte * const pt0 = pageptr(root_pt1[j].ppn);

            for (unsigned int k = 0; k < PTE_CNT; k++) {//same free process
                if (!PTE_VALID(pt0[k]) || PTE_GLOBAL(pt0[k]))
                    continue;

                if (!PTE_LEAF(pt0[k]))
                    panic("invalid level-0 non-leaf PTE in discard");

                free_phys_page(pageptr(pt0[k].ppn));
                pt0[k] = null_pte();
            }

            free_phys_page(pt0);//free L0
            root_pt1[j] = null_pte();
        }

        free_phys_page(root_pt1);//free L1
        root_pt2[i] = null_pte();
    }

    free_phys_page(root_pt2);
    return main_mtag;
}

// The map_page() function maps a single page into the active address space at
// the specified address. The map_range() function maps a range of contiguous
// pages into the active address space. Note that map_page() is a special case
// of map_range(), so it can be implemented by calling map_range(). Or
// map_range() can be implemented by calling map_page() for each page in the
// range. The current implementation does the latter.

// We currently map 4K pages only. At some point it may be desirable to support
// mapping megapages and gigapages.

void * map_page(uintptr_t vma, void * pp, int rwxug_flags) {
    // YOUR CODE HERE
    struct pte * ptr_pt2;//pointers to the three levels page tables
    struct pte * ptr_pt1;
    struct pte * ptr_pt0;
    unsigned int i2;//indices of each table from vma
    unsigned int i1;
    unsigned int i0;
    uint_fast8_t g_flag;//global bit
    //mark global for inter table entries
    g_flag = (rwxug_flags & PTE_G) ? PTE_G : 0;
    ptr_pt2 = active_space_ptab();
    i2 = VPN2(vma);

    if (!PTE_VALID(ptr_pt2[i2])) {//allocate missing L1 table
        ptr_pt1 = alloc_phys_page();
        memset(ptr_pt1, 0, PAGE_SIZE);
        ptr_pt2[i2] = ptab_pte(ptr_pt1, g_flag);
    } 
    ptr_pt1 = pageptr(ptr_pt2[i2].ppn);
    i1 = VPN1(vma);

    if (!PTE_VALID(ptr_pt1[i1])) {//allocate missing L0 table
        ptr_pt0 = alloc_phys_page();
        memset(ptr_pt0, 0, PAGE_SIZE);
        ptr_pt1[i1] = ptab_pte(ptr_pt0, g_flag);
    } 

    ptr_pt0 = pageptr(ptr_pt1[i1].ppn);//overwrite L0 leaf mapping
    i0 = VPN0(vma);
    ptr_pt0[i0] = leaf_pte(pp, rwxug_flags);
    sfence_vma();
    return (void *)vma;
}

void * map_range(uintptr_t vma, size_t size, void * pp, int rwxug_flags) {
    // YOUR CODE HERE
    uintptr_t start;//virtual page start and end to map
    uintptr_t end;
    uintptr_t pstart;//physical page to start
    uintptr_t virt_page;//virtual page cursor

    if (size == 0) return (void *)vma;
    if (vma + size < vma) return NULL;

    start = ROUND_DOWN(vma, PAGE_SIZE);//align virtual bounds and match physical start
    end = ROUND_UP(vma + size, PAGE_SIZE);
    pstart = ROUND_DOWN((uintptr_t)pp - (vma - start), PAGE_SIZE);

    for (virt_page = start; virt_page < end; virt_page += PAGE_SIZE) {//map with interating physical pages
        if (map_page(virt_page, (void *)(pstart + (virt_page - start)), rwxug_flags) == NULL)
            return NULL;
    }

    return (void *)vma;
}

void * alloc_and_map_range(uintptr_t vma, size_t size, int rwxug_flags) {
    // YOUR CODE HERE
    uintptr_t start;//same declaration as above
    uintptr_t end;
    uintptr_t virt_page;
    void * phys_virt;//physical page for each virtual page

    if (size == 0)
        return (void *)vma;

    if (vma + size < vma)
        panic("alloc_and_map_range wraparound");

    start = ROUND_DOWN(vma, PAGE_SIZE);//convert byte to page range
    end = ROUND_UP(vma + size, PAGE_SIZE);
//allocate physical page to virtual page and then map
    for (virt_page = start; virt_page < end; virt_page += PAGE_SIZE) {
        phys_virt = alloc_phys_page();
        if (map_page(virt_page, phys_virt, rwxug_flags) == NULL)
            panic("alloc_and_map_range map failed");
    }

    return (void *)vma;
}

void set_range_flags(const void * vp, size_t size, int rwxug_flags) {
    // YOUR CODE HERE
    uintptr_t start;//aligned virtual range
    uintptr_t end;
    uintptr_t vma;
    struct pte * ptr_pt2;//pointers to the tables at each level
    struct pte * ptr_pt1;
    struct pte * ptr_pt0;
    struct pte pte2;//PTE values for type check
    struct pte pte1;
    struct pte pte0;

    if (size == 0) return;
    start = ROUND_DOWN((uintptr_t)vp, PAGE_SIZE);
    end = ROUND_UP((uintptr_t)vp + size, PAGE_SIZE);

    for (vma = start; vma < end; vma += PAGE_SIZE) {//go through each page and write the leaf bits
        ptr_pt2 = active_space_ptab();
        pte2 = ptr_pt2[VPN2(vma)];
        if (!PTE_VALID(pte2) || PTE_LEAF(pte2))
            panic("set_range_flags on unmapped range");
        ptr_pt1 = pageptr(pte2.ppn);

        pte1 = ptr_pt1[VPN1(vma)];
        if (!PTE_VALID(pte1) || PTE_LEAF(pte1))
            panic("set_range_flags on unmapped range");
        ptr_pt0 = pageptr(pte1.ppn);

        pte0 = ptr_pt0[VPN0(vma)];
        if (!PTE_VALID(pte0) || !PTE_LEAF(pte0))
            panic("set_range_flags on unmapped range");

//preserve mapped physical page
        ptr_pt0[VPN0(vma)].flags = rwxug_flags | PTE_A | PTE_D | PTE_V;
    }

    sfence_vma();
    return;
}

void unmap_and_free_range(void * vp, size_t size) {
    // YOUR CODE HERE
    uintptr_t start;
    uintptr_t end;
    uintptr_t vma;
    struct pte * ptr_pt2;
    struct pte * ptr_pt1;
    struct pte * ptr_pt0;
    struct pte pte2;
    struct pte pte1;
    struct pte pte0;

    if (size == 0) return;
    start = ROUND_DOWN((uintptr_t)vp, PAGE_SIZE);
    end = ROUND_UP((uintptr_t)vp + size, PAGE_SIZE);
//iterate the page in range and free back physical page
    for (vma = start; vma < end; vma += PAGE_SIZE) {
        ptr_pt2 = active_space_ptab();
        pte2 = ptr_pt2[VPN2(vma)];
        if (!PTE_VALID(pte2) || PTE_LEAF(pte2))
            continue;

        ptr_pt1 = pageptr(pte2.ppn);
        pte1 = ptr_pt1[VPN1(vma)];
        if (!PTE_VALID(pte1) || PTE_LEAF(pte1))
            continue;

        ptr_pt0 = pageptr(pte1.ppn);
        pte0 = ptr_pt0[VPN0(vma)];
        if (!PTE_VALID(pte0) || !PTE_LEAF(pte0))
            continue;

        free_phys_page(pageptr(pte0.ppn));
        ptr_pt0[VPN0(vma)] = null_pte();
    }

    sfence_vma();
    return;
}

int enforce_vptr(const void * vp, size_t size, int rwxug_flags) {
    // YOUR CODE HERE
    //same variable declaration as above
    uintptr_t start;
    uintptr_t end;
    uintptr_t vma;
    struct pte * ptr_pt2;
    struct pte * ptr_pt1;
    struct pte * ptr_pt0;
    struct pte pte2;
    struct pte pte1;
    struct pte pte0;
    void * new_page;//newly allocated page(lazy mapping)

    if (vp == NULL) return -EFAULT;//invalid ptr
    start = (uintptr_t)vp;
    if (!wellformed(start)) return -EFAULT;//validate address format
    if (size == 0) return 0;

    start = ROUND_DOWN(start, PAGE_SIZE);
    end = ROUND_UP((uintptr_t)vp + size, PAGE_SIZE);

    for (vma = start; vma < end; vma += PAGE_SIZE) {//lazy allocation when allowed
        int need_map = 0;

        ptr_pt2 = active_space_ptab();
        pte2 = ptr_pt2[VPN2(vma)];
        if (!PTE_VALID(pte2) || PTE_LEAF(pte2)) {
            need_map = 1;
        } else {
            ptr_pt1 = pageptr(pte2.ppn);
            pte1 = ptr_pt1[VPN1(vma)];
            if (!PTE_VALID(pte1) || PTE_LEAF(pte1)) {
                need_map = 1;
            } else {
                ptr_pt0 = pageptr(pte1.ppn);
                pte0 = ptr_pt0[VPN0(vma)];
                if (!PTE_VALID(pte0) || !PTE_LEAF(pte0)) need_map = 1; 
                //must include all required permission bits
                if ((pte0.flags & rwxug_flags) != (uint_fast8_t)rwxug_flags) return -EACCESS;
            }
        }

        if (!need_map) continue;
        if (rwxug_flags & PTE_X) return -EFAULT;
        new_page = alloc_phys_page();//lazy allocate and install missing pages
        if (map_page(vma, new_page, rwxug_flags) == NULL) return -EFAULT;
}

    return 0;
}

int validate_vstr(const char * vs, int rwxug_flags) {
    // YOUR CODE HERE
    uintptr_t curr_pos;//current scan position
    uintptr_t page_end;//end bound of the current page segment
    struct pte * ptr_pt2;
    struct pte * ptr_pt1;
    struct pte * ptr_pt0;
    struct pte pte2;
    struct pte pte1;
    struct pte pte0;
    const char * scan;//current page's byte scanner

    curr_pos = (uintptr_t)vs;

    while (curr_pos < UMEM_END_VMA) {// iterate pages to scan until found null or fail
        uintptr_t const vma = ROUND_DOWN(curr_pos, PAGE_SIZE);//vma for table lookup
        ptr_pt2 = active_space_ptab();
        pte2 = ptr_pt2[VPN2(vma)];
        if (!PTE_VALID(pte2) || PTE_LEAF(pte2))
            return -EFAULT;

        ptr_pt1 = pageptr(pte2.ppn);
        pte1 = ptr_pt1[VPN1(vma)];
        if (!PTE_VALID(pte1) || PTE_LEAF(pte1))
            return -EFAULT;

        ptr_pt0 = pageptr(pte1.ppn);
        pte0 = ptr_pt0[VPN0(vma)];
        
        if (!PTE_VALID(pte0) || !PTE_LEAF(pte0))
            return -EFAULT;
//currrent page must have good permission bits
        if ((pte0.flags & rwxug_flags) != (uint_fast8_t)rwxug_flags)
            return -EACCESS;

        page_end = vma + PAGE_SIZE;
        if (UMEM_END_VMA < page_end) page_end = UMEM_END_VMA;
        for (scan = (const char *)curr_pos; (uintptr_t)scan < page_end; scan++) {
            if (*scan == '\0') return 0;
        }//byte scan wthin the correct page window

        curr_pos = page_end;
    }

    return -EFAULT;
}

void * alloc_phys_page(void) {
    // YOUR CODE HERE
    return alloc_phys_pages(1);
}

void free_phys_page(void * pp) {
    // YOUR CODE HERE
    free_phys_pages(pp, 1);
    return;
}

void * alloc_phys_pages(unsigned int cnt) {
    // YOUR CODE HERE
    //best fir chunk found and its predecessor
    struct page_chunk * best = NULL;
    struct page_chunk * best_prev = NULL;
    //traversal pointer of predesessor and traversal cursor of the list
    struct page_chunk * prev = NULL;
    struct page_chunk * curr_pos = free_chunk_list;

    while (curr_pos != NULL) {//iterate to fun the best free chunk
        if (curr_pos->pagecnt >= cnt) {
            if (best == NULL || curr_pos->pagecnt < best->pagecnt) {
                best = curr_pos;
                best_prev = prev;
                if (curr_pos->pagecnt == cnt) break;
            }
        }
        prev = curr_pos;
        curr_pos = curr_pos->next;
    }

    if (best->pagecnt == cnt) {//find the best fit and return the base
        if (best_prev == NULL)
            free_chunk_list = best->next;
        else
            best_prev->next = best->next;
        return best;
    }

    best->pagecnt -= cnt;
    return (void *)((uintptr_t)best + best->pagecnt * PAGE_SIZE);
}

void free_phys_pages(void * pp, unsigned int cnt) {
    // YOUR CODE HERE
    struct page_chunk * chunk;

    if (pp == NULL || cnt == 0) return;
//push freed run to the front
    chunk = pp;
    chunk->pagecnt = cnt;
    chunk->next = free_chunk_list;
    free_chunk_list = chunk;
    return;
}

unsigned long free_phys_page_count(void) {
    // YOUR CODE HERE
    unsigned long pagecnt = 0;
    struct page_chunk * chunk = free_chunk_list;

    while (chunk != NULL) {//get the sum of page counts
        pagecnt += chunk->pagecnt;
        chunk = chunk->next;
    }

    return pagecnt;
}

int handle_umode_page_fault(struct trap_frame * tfr, uintptr_t vma) {
    // YOUR CODE HERE
    uintptr_t const virt_page = ROUND_DOWN(vma, PAGE_SIZE);
    (void)tfr;

    if (!wellformed(vma)) return 0;
    if (vma < UMEM_START_VMA || UMEM_END_VMA <= vma) return 0;
    if (alloc_and_map_range(virt_page, PAGE_SIZE, PTE_R | PTE_W | PTE_U) == NULL) return 0;
    
    return 1;
}

// INTERNAL FUNCTION DEFINITIONS
//


mtag_t active_space_mtag(void) {
    return csrr_satp();
}

static inline mtag_t ptab_to_mtag(struct pte * ptab, unsigned int asid) {
    return (
            ((unsigned long)PAGING_MODE << RISCV_SATP_MODE_shift) |
            ((unsigned long)asid << RISCV_SATP_ASID_shift) |
            pagenum(ptab) << RISCV_SATP_PPN_shift);
}

static inline struct pte * mtag_to_ptab(mtag_t mtag) {
    return (struct pte * )((mtag << 20) >> 8);
}

static inline struct pte * active_space_ptab(void) {
    return mtag_to_ptab(active_space_mtag());
}

static inline void * pageptr(uintptr_t n) {
    return (void *)(n << PAGE_ORDER);
}

static inline unsigned long pagenum(const void * p) {
    return (unsigned long)p >> PAGE_ORDER;
}

static inline int wellformed(uintptr_t vma) {
    // Address bits 63:38 must be all 0 or all 1
    uintptr_t const bits = (intptr_t)vma >> 38;
    return (!bits || !(bits+1));
}

static inline struct pte leaf_pte(const void * pp, uint_fast8_t rwxug_flags) {
    return (struct pte) {
        .flags = rwxug_flags | PTE_A | PTE_D | PTE_V,
            .ppn = pagenum(pp)
    };
}

static inline struct pte ptab_pte(const struct pte * pt, uint_fast8_t g_flag) {
    return (struct pte) {
        .flags = g_flag | PTE_V,
            .ppn = pagenum(pt)
    };
}

static inline struct pte null_pte(void) {
    return (struct pte) { };
}

//This function assumes:
//- resv strictly overlaps with RAM
//- MMIO and RAM do not overlap
//- Arrays are sorted
//- 0 size entries are invalid
//- That I'm very sorry to whoever has to read this.
//
// * THIS FUNCTION IS DESTRUCTIVE TO MREGION ARRAYS.
//
//...returns how many times kprintfluffy was called :innocent:
static int print_memory_info(struct mregion * mmio_imm, unsigned long mmiocnt,
                             struct mregion * ram_imm, unsigned long ramcnt,
                             struct mregion * resv_imm, unsigned long resvcnt) {
    unsigned long mmio_idx = 0;
    unsigned long ram_idx = 0;
    unsigned long resv_idx = 0;

    int fluffy = 0;

    //The first thing we do is copy out the arguments to prevent destruction of
    //the arguments.

    struct mregion mmio[mmiocnt];
    struct mregion ram[ramcnt];
    struct mregion resv[resvcnt];

    memcpy(mmio, mmio_imm, mmiocnt * sizeof(struct mregion));
    memcpy(ram, ram_imm, ramcnt * sizeof(struct mregion));
    memcpy(resv, resv_imm, resvcnt * sizeof(struct mregion));

    while (mmio_idx < mmiocnt || ram_idx < ramcnt) {
        uintptr_t mmio_loc = mmio[mmio_idx].pma;
        uintptr_t ram_loc = ram[ram_idx].pma;
        uintptr_t resv_loc = resv[resv_idx].pma;
        
        if (mmio_loc < ram_loc && mmio_idx < mmiocnt) {
            kprintfluffy(40, &art, "%08p - %08p - MMIO", 
                         mmio_loc, 
                         mmio_loc + mmio[mmio_idx].size - 1);
            fluffy++;
            mmio_idx++;
        } else {

            if (ram_loc <= resv_loc && 
               ram_loc + ram[ram_idx].size > resv_loc && resv_idx < resvcnt) {

                if (ram_loc < resv_loc) {
                    kprintfluffy(40, 
                                 &art, 
                                 "%p - %p - Free RAM", 
                                 ram_loc, 
                                 resv_loc - 1);
                    ram[ram_idx].size -= resv_loc - ram_loc;
                    ram[ram_idx].pma = resv_loc;
                    fluffy++;
                } else {

                    if (resv_loc <= (uintptr_t)_kimg_start && 
                        resv_loc + resv[resv_idx].size > (uintptr_t)_kimg_start) {

                        if (resv_loc < (uintptr_t)_kimg_start) {
                            kprintfluffy(40, 
                                         &art, 
                                         "%p - %p - SBI Reserved", 
                                         resv_loc, 
                                         (uintptr_t)_kimg_start - 1);

                            resv[resv_idx].size -= 
                                (uintptr_t)_kimg_start - resv_loc;
                            resv[resv_idx].pma = (uintptr_t)_kimg_start;
                            ram[ram_idx].pma = (uintptr_t)_kimg_start;
                            ram[ram_idx].size -= 
                                (uintptr_t)_kimg_start - resv_loc;
                            fluffy++;
                        } else {
                            kprintfluffy(40, 
                                         &art, 
                                         "%p - %p - Kernel TEXT", 
                                         _kimg_text_start, 
                                         ROUND_UP((uintptr_t)_kimg_text_end, 
                                                  PAGE_SIZE) - 1);

                            kprintfluffy(40, 
                                         &art, 
                                         "%p - %p - Kernel RODATA", 
                                         _kimg_rodata_start, 
                                         ROUND_UP((uintptr_t)_kimg_rodata_end, 
                                                  PAGE_SIZE) - 1);

                            kprintfluffy(40, 
                                         &art, 
                                         "%p - %p - Kernel DATA", 
                                         _kimg_data_start, 
                                         ROUND_UP((uintptr_t)_kimg_data_end, 
                                                  PAGE_SIZE) - 1);

                            resv[resv_idx].pma = 
                                ROUND_UP((uintptr_t)_kimg_end, PAGE_SIZE);
                            resv[resv_idx].size -= 
                                ROUND_UP((uintptr_t)_kimg_end, PAGE_SIZE) - resv_loc;
                            ram[ram_idx].pma = 
                                ROUND_UP((uintptr_t)_kimg_end, PAGE_SIZE);
                            ram[ram_idx].size -= 
                                ROUND_UP((uintptr_t)_kimg_end, PAGE_SIZE) - resv_loc;
                            fluffy+=3;
                        }
                    } else {
                        kprintfluffy(40, 
                                     &art, 
                                     "%p - %p - SBI Reserved", 
                                     resv_loc, 
                                     resv_loc + resv[resv_idx].size - 1);
                        ram[ram_idx].pma = resv_loc + resv[resv_idx].size;
                        ram[ram_idx].size -= resv[resv_idx].size;
                        resv_idx++;
                        fluffy++;
                    }
                }
            } else {
                kprintfluffy(40, 
                             &art, 
                             "%p - %p - Free RAM", 
                             ram_loc, 
                             ram_loc + ram[ram_idx].size - 1);
                fluffy++;
                ram_idx++;
            }
        }

        //Ignore NULL entries (don't need to do this first because first entries 
        //are guaranteed to be populated)
        while(mmio[mmio_idx].size == 0 && mmio_idx < mmiocnt)
                mmio_idx++;
        while(ram[ram_idx].size == 0 && ram_idx < ramcnt)
                ram_idx++;
        while(resv[resv_idx].size == 0 && resv_idx < resvcnt)
                resv_idx++;

    }
    return fluffy;
}
