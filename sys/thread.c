// thread.c - Thread creation and synchronization
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef THREAD_TRACE
#define TRACE
#endif

#ifdef THREAD_DEBUG
#define DEBUG
#endif

#include "thread.h"

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#include "heap.h"
#include "string.h"
#include "riscv.h"
#include "intr.h"
#include "error.h"
#include "misc.h"
#include "timer.h"

#include "process.h"
#include "memory.h"

// COMPILE-TIME PARAMETERS
//

#ifndef NTHR // maximum number of threads
#define NTHR 32
#endif

#ifndef SCHED_SLICE_MS // scheduler time slice
#define SCHED_SLICE_MS 20
#endif

// EXPORTED GLOBAL VARIABLES
//

char thrmgr_initialized = 0;

// INTERNAL TYPE DEFINITIONS
//

enum thread_state {
    THREAD_UNDEFINED = 0,
    THREAD_WAITING,
    THREAD_RUNNING,
    THREAD_READY,
    THREAD_EXITED
};

struct thread_context {
    uintptr_t s[12];
    void * ra;
    void * sp;
};

struct thread_stack_anchor {
    struct thread * ktp;
    void * kgp;
};

// [MP3cp2] The /thread_stack_anchor/ structure is placed at the base of the
// stack to allow us to restore the thread pointer when entering the kernel from
// U mode.

struct thread {
    struct thread_context ctx;  // must be first (thrasm.s)
    int id; // index into thrtab[]
    enum thread_state state;
    const char * name;
    struct thread_stack_anchor * stack_anchor;
    void * stack_lowest;
    struct process * proc;
    struct thread * parent;
    struct thread * list_next;
    struct condition * wait_cond;
    struct condition child_exit;

    // (you may add additional structure members here)
};

// INTERNAL MACRO DEFINITIONS
// 

// Pointer to running thread, which is kept in the tp (x4) register.

#define TP ((struct thread*)get_thread_pointer())

// Macro for changing thread state. If compiled for debugging (DEBUG is
// defined), prints function that changed thread state.

#define set_thread_state(t,s) do { \
    debug("Thread <%s:%d> state changed from %s to %s by <%s:%d> in %s", \
        (t)->name, (t)->id, \
        thread_state_name((t)->state), \
        thread_state_name(s), \
        TP->name, TP->id, \
        __func__); \
    (t)->state = (s); \
} while (0)

// INTERNAL FUNCTION DECLARATIONS
//

static void init_main_thread(void);
static void init_idle_thread(void);

// Initializes the main and idle threads. called from threads_init().


static const char * thread_state_name(enum thread_state state)
    __attribute__ ((unused)); // may be unused

// Returns a string representing a thread state. Used by debug and trace
// statements, so marked unused to avoid compiler warnings.



static void tlclear(struct thread_list * list);
static int tlempty(const struct thread_list * list);
static struct thread * tlpeek(const struct thread_list * list);
static void tlinsert(struct thread_list * list, struct thread * thr);
static struct thread * tlremove(struct thread_list * list);
static void tlappend(struct thread_list * l0, struct thread_list * l1);
static void tlprepend(struct thread_list * l0, struct thread_list * l1);

static void idle_thread_func(void);

// IMPORTED QUASI-FUNCTION DECLARATIONS
// defined in thrasm.s
//

extern struct thread * switch_threads(struct thread * next_thread);
extern void start_thread(void);

// INTERNAL GLOBAL VARIABLES
//

#define MAIN_TID 0
#define IDLE_TID (NTHR-1)

static struct thread main_thread;
static struct thread idle_thread;

extern char _main_stack_lowest[]; // from start.s
extern char _main_stack_anchor[]; // from start.s


static struct thread main_thread = {
    .id = MAIN_TID,
    .name = "main",
    .state = THREAD_RUNNING,
    .stack_anchor = (void*)_main_stack_anchor,
    .stack_lowest = _main_stack_lowest,
    .child_exit = { .name = "main_thread.child_exit" }
};

extern char _idle_stack_lowest[]; // from thrasm.s
extern char _idle_stack_anchor[]; // from thrasm.s

static struct thread idle_thread = {
    .id = IDLE_TID,
    .name = "idle",
    .state = THREAD_READY,
    .parent = &main_thread,
    .stack_anchor = (void*)_idle_stack_anchor,
    .stack_lowest = _idle_stack_lowest,
    .ctx.sp = _idle_stack_anchor,
    .ctx.ra = &idle_thread_func
};

static struct thread * thrtab[NTHR] = {
    [MAIN_TID] = &main_thread,
    [IDLE_TID] = &idle_thread
};

static struct thread_list ready_list = {
    .head = &idle_thread,
    .tail = &idle_thread
};

// EXPORTED THREAD FUNCTION DEFINITIONS
//

int running_thread(void) {
    return TP->id;
}

void thrmgr_init(void) {
    trace("%s()", __func__);


    init_main_thread();
    init_idle_thread();
    set_thread_pointer(&main_thread);
    thrmgr_initialized = 1;
}

int spawn_thread (
    const char * name,
    void (*entry)(void),
    ...)
{
    // YOUR CODE HERE
    va_list variadic;                           // define the variadic argument list for reading optional arguments
    struct thread * thrp;                       // pointer to the new thread
    int tid;                                    // thread table slot
    void * stack;                               // declare a allocating stack

    for (tid = 0; tid < NTHR; tid++){
        if (thrtab[tid] == NULL){
            break;                              // test for the first NULL in thrtab
        }
    }
        

    if (tid >= NTHR) return -EMTHR;             // error: too many threads
        
    thrp = kcalloc(1, sizeof(*thrp));           // allocate the thread and its stack
    stack = alloc_phys_page();           

    thrp->stack_lowest = stack;
    thrp->stack_anchor = (void *)((char *)stack + PAGE_SIZE - sizeof(struct thread_stack_anchor));

    thrp->stack_anchor->ktp = thrp;             // initialize the stack anchor
    thrp->stack_anchor->kgp = 0;

    thrp->id = tid;                             // initilize the thread
    thrp->name = (name != NULL) ? name : "none";
    thrp->state = THREAD_READY;
    thrp->parent = TP;
    thrp->proc = TP->proc;
    condition_init(&thrp->child_exit, "child_exit");    // initialize the thread condition

    // extract: read the variadic arguments and then store to ctx.s
    va_start(variadic, entry);
    for (int i = 0; i < 8; i++)
        thrp->ctx.s[i] = (uintptr_t)va_arg(variadic, uintptr_t);
    va_end(variadic);

    thrp->ctx.s[8] = (uintptr_t)entry;
    thrp->ctx.ra = &start_thread;
    thrp->ctx.sp = thrp->stack_anchor;

    thrtab[tid] = thrp;                 // insert the new thread
    tlinsert(&ready_list, thrp);

    return tid;
}


const char * thread_name(int tid) {
    assert (0 <= tid && tid < NTHR);
    assert (thrtab[tid] != NULL);
    return thrtab[tid]->name;
}

const char * running_thread_name(void) {
    return TP->name;
}

void exit_running_thread(void) {

    if (TP == &main_thread)
        halt();
    
    set_thread_state(TP, THREAD_EXITED);

    // YOUR CODE HERE
    struct thread * parent = TP->parent;             //save parent pointer before modifying

    disable_interrupts();                           //prevent other interrupts while modifying the relations

    for (int i = 0; i < NTHR; i++) {                    //reparent all the children
        if (thrtab[i] == NULL || thrtab[i]->parent != TP){  //skip all the non children
            continue;
        }
        if (thrtab[i]->state == THREAD_EXITED){
            thrtab[i]->parent = NULL;
        }          
        else{
            thrtab[i]->parent = &main_thread;
        }
    }
//wake the parent and prevent infinite running
    if (parent != NULL){
        condition_broadcast(&parent->child_exit);
    }

    enable_interrupts();
    while (1){
        yield_running_thread();
    }    
}


void yield_running_thread(void) {
    extern void switch_running_thread(struct thread *); // thrasm.s

    trace("%s() in <%s:%d>", __func__, TP->name, TP->id);

    // The idle thread is always runnable, and the idle thread only calls
    // yield() if the ready_list is not empty.

    assert (!tlempty(&ready_list));

    // YOUR CODE HERE
    struct thread * nextthread;             //declare the thread we will switch

    disable_interrupts();                   //avoid race conditions

    if (TP->state == THREAD_RUNNING) {      //yeild behave on runnging thread
        if (tlempty(&ready_list)) {
            enable_interrupts();
            return;
        }
        set_thread_state(TP, THREAD_READY);
        tlinsert(&ready_list, TP);          //put the current thread to the ready list
    }

    if (tlempty(&ready_list)){              //select the next runnable thread
        nextthread = &idle_thread;
    }                    
    else nextthread = tlremove(&ready_list);

    set_thread_state(nextthread, THREAD_RUNNING);

    // switch to the next thread mspace before switching execution
    if (nextthread->proc != NULL) {
        switch_mspace(nextthread->proc->mtag);
    } else {
        switch_mspace(main_thread.proc->mtag);
    }

    enable_interrupts();
    switch_running_thread(nextthread);      //enable the interrupts again
}

// The finish thread function is only called from switch_running_thread() in
// thrasm.s. It is not declared in thread.h.

void finish_thread_switch(struct thread * susp_thread) {
    // YOUR CODE HERE
    if (susp_thread == NULL) return;                            //skip invalid
    if (susp_thread->state != THREAD_EXITED) return;
    if (susp_thread->parent != NULL) return;                                    //decide if the thread can be freed
    if (susp_thread == &main_thread || susp_thread == &idle_thread) return;

    int i = susp_thread->id;                                    //remove the thread from the table
    if (0 <= i && i < NTHR && thrtab[i] == susp_thread)
        thrtab[i] = NULL;

    if (susp_thread->stack_lowest != NULL) {
        free_phys_page(susp_thread->stack_lowest);
        susp_thread->stack_lowest = NULL;
    }

    kfree(susp_thread);                                     //free the thread object
}

int join_thread(int u_tid) {

    trace("%s(%d) in <%s:%d>", __func__, u_tid, TP->name, TP->id);

    // YOUR CODE HERE
    if (u_tid < 0 || u_tid >= NTHR)                         //validate the requested TID
        return -ECHILD;

    while (1) {                                             //try to find a exited child
        struct thread * child = NULL;

        disable_interrupts();

        if (u_tid != 0) {
            child = thrtab[u_tid];
            if (child == NULL || child->parent != TP) {     //restrict the TID to a child
                enable_interrupts();
                return -ECHILD;
            }
            if (child->state == THREAD_EXITED) {            //check if the child has already exited
                thrtab[u_tid] = NULL;
                if (child->stack_lowest != NULL) {
                    kfree(child->stack_lowest);
                    child->stack_lowest = NULL;
                }
                child->parent = NULL;               //set to null before freeing to avoid double freeing
                enable_interrupts();
                kfree(child);
                return u_tid;
            }
        } else {
            int have_child_flag = 0;

            for (int i = 0; i < NTHR; i++) {        //go rhough the table to find the child of the thread
                child = thrtab[i];
                if (child == NULL || child->parent != TP) //skip
                    continue;

                have_child_flag = 1;

                if (child->state == THREAD_EXITED) {        //get the first child found
                    thrtab[i] = NULL;
                    if (child->stack_lowest != NULL) {
                        kfree(child->stack_lowest);
                        child->stack_lowest = NULL;
                    }
                    child->parent = NULL;
                    enable_interrupts();
                    kfree(child);
                    return i;
                }
            }

            if (!have_child_flag) {                      //if the caller does not have child, error code
                enable_interrupts();
                return -ECHILD;
            }
        }

        condition_wait(&TP->child_exit);
        enable_interrupts();
    }
}

struct process * thread_process(int tid) {
    assert (0 <= tid && tid < NTHR);
    assert (thrtab[tid] != NULL);
    return thrtab[tid]->proc;
}

struct process * running_thread_process(void) {
    return TP->proc;
}

void thread_attach_process(int tid, struct process * proc) {
    assert (0 <= tid && tid < NTHR);
    assert (thrtab[tid] != NULL);
    // assert (thrtab[tid]->proc == NULL);
    // assert (proc != NULL);
    thrtab[tid]->proc = proc;
}

void * running_thread_stack_anchor(void) {
    return TP->stack_anchor;
}

// EXPORTED CONDITION VARIABLE FUNCTION DEFINITIONS
//

void condition_init(struct condition * cond, const char * name) {
    memset(cond, 0, sizeof(*cond));
    tlclear(&cond->wait_list);
    cond->name = (name != NULL) ? name : "anon";
}

const char * condition_name(const struct condition * cond) {
    return cond->name;
}

void condition_wait(struct condition * cond) {

    trace("%s(<%s>) in <%s:%d>", __func__, cond->name, TP->name, TP->id);

    assert(TP->state == THREAD_RUNNING);

    // YOUR CODE HERE
    disable_interrupts();

    set_thread_state(TP, THREAD_WAITING);   //mark that current thread is waiting
    TP->wait_cond = cond;
    tlinsert(&cond->wait_list, TP);         //put the thread onto the contition's waitlist

    enable_interrupts();
    yield_running_thread();
}

void condition_broadcast(struct condition * cond) {

    trace("%s(<%s>) in <%s:%d>", __func__, cond->name, TP->name, TP->id);

    // Fast path: if there are no threads waiting, return.

    if (tlempty(&cond->wait_list))
        return;

    // YOUR CODE HERE
    struct thread_list tmp;
    struct thread * iter;                                  //iterator

    disable_interrupts();

    tmp = cond->wait_list;
    tlclear(&cond->wait_list);                          //detach the waitlist

    while ((iter = tlremove(&tmp)) != NULL) {              //move each waiting thread back to ready
        iter->wait_cond = NULL;
        set_thread_state(iter, THREAD_READY);
        tlinsert(&ready_list, iter);
    }

    enable_interrupts();
}

// EXPORTED READERS-WRITER LOCK FUNCTION DEFINITIONS
//

// A readers-writer lock (rw-lock) is implemented using the rwlock structure:
//
// struct rwlock {
//     struct condition released;
//     struct thread * owner;
//     unsigned long cnt;
// };
//
// The three states are:
// - UNLOCKED when (owner == NULL && cnt == 0),
// - LOCKED-SHARED when (owner == NULL && cnt > 0), and
// - LOCKED-EXCLUISIVE when (owner != NULL && cnt > 0).
//
// The configuration (owner != NULL && cnt == 0) is not valid.
//

void rwlock_init(struct rwlock * rwlk, const char * name) {
    // YOUR CODE HERE
    condition_init(&rwlk->released, "rwlock.released");
    rwlk->owner = NULL;
    rwlk->cnt = 0;
    rwlk->name = (name != NULL) ? name : "anon";
}

const char * rwlock_name(const struct rwlock * rwlk) {
    return rwlk->name;
}

void rwlock_acquire(struct rwlock * rwlk, int exclusive) {
    trace("%s(<%s>,%d)", __func__, rwlk->name, exclusive);

    // YOUR CODE HERE
    disable_interrupts();

    if (exclusive) {    //handle exclusive acquisition
        while ((rwlk->owner != NULL && rwlk->owner != TP) || (rwlk->owner == NULL && rwlk->cnt != 0))
            condition_wait(&rwlk->released);

        rwlk->owner = TP;
        rwlk->cnt++;    //makes the current thread the exlusive owner
    } else {                                //allow multiple reader simutaneously
        if (rwlk->owner == TP) {
            rwlk->cnt++;
            enable_interrupts();
            return;
        }

        while (rwlk->owner != NULL)             
            condition_wait(&rwlk->released);    //block while owner holds the lock

        rwlk->cnt++;
    }

    enable_interrupts();
}

void rwlock_release(struct rwlock * rwlk) {
    trace("%s(<%s>)", __func__, rwlk->name);

    // YOUR CODE HERE
    disable_interrupts();

    if (rwlk->owner == TP) {                        //exclusive owner release
        rwlk->cnt--;
        if (rwlk->cnt == 0) {
            rwlk->owner = NULL;                     
            condition_broadcast(&rwlk->released);
        }
        enable_interrupts();
        return;
    }

//shared reader
    rwlk->cnt--;
    if (rwlk->cnt == 0)
        condition_broadcast(&rwlk->released);

    enable_interrupts();
}


// INTERNAL FUNCTION DEFINITIONS
//

void init_main_thread(void) {
    main_thread.stack_anchor->ktp = &main_thread;
}

void init_idle_thread(void) {
}

const char * thread_state_name(enum thread_state state) {
    static const char * const names[] = {
        [THREAD_UNDEFINED] = "UNDEFINED",
        [THREAD_WAITING] = "WAITING",
        [THREAD_RUNNING] = "RUNNING",
        [THREAD_READY] = "READY",
        [THREAD_EXITED] = "EXITED"
    };

    if (0 <= (int)state && (int)state < sizeof(names)/sizeof(names[0]))
        return names[state];
    else
        return names[THREAD_UNDEFINED];
};


void tlclear(struct thread_list * list) {
    list->head = NULL;
    list->tail = NULL;
}

int tlempty(const struct thread_list * list) {
    return (list->head == NULL);
}

struct thread * tlpeek(const struct thread_list * list) {
    return list->head;
}

void tlinsert(struct thread_list * list, struct thread * thr) {
    thr->list_next = NULL;

    if (thr == NULL)
        return;

    if (list->tail != NULL) {
        assert (list->head != NULL);
        list->tail->list_next = thr;
    } else {
        assert(list->head == NULL);
        list->head = thr;
    }

    list->tail = thr;
}

struct thread * tlremove(struct thread_list * list) {
    struct thread * thr;

    thr = list->head;
    
    if (thr == NULL)
        return NULL;

    list->head = thr->list_next;
    
    if (list->head != NULL)
        thr->list_next = NULL;
    else
        list->tail = NULL;

    thr->list_next = NULL;
    return thr;
}

void tlappend(struct thread_list * l0, struct thread_list * l1) {
    if (l0->head != NULL) {
        assert(l0->tail != NULL);
        
        if (l1->head != NULL) {
            assert(l1->tail != NULL);
            l0->tail->list_next = l1->head;
            l0->tail = l1->tail;
        }
    } else {
        assert(l0->tail == NULL);
        l0->head = l1->head;
        l0->tail = l1->tail;
    }

    l1->head = NULL;
    l1->tail = NULL;
}

void tlprepend(struct thread_list * l0, struct thread_list * l1) {
    if (l1->head == NULL) {
        assert (l1->tail == NULL);
        return;
    }

    assert(l1->tail != NULL);
        
    if (l0->head != NULL) {
        assert(l0->tail != NULL);
        l1->tail->list_next = l0->head;
    } else {
        assert(l0->tail == NULL);
        l0->tail = l1->tail;
    }

    l0->head = l1->head;
    l1->head = NULL;
    l1->tail = NULL;
}

void idle_thread_func(void) {
    // The idle thread sleeps using wfi if the ready list is empty. Note that we
    // need to disable interrupts before checking if the thread list is empty to
    // avoid a race condition where an ISR marks a thread ready to run between
    // the call to tlempty() and the wfi instruction.

    for (;;) {
        // If there are runnable threads, yield to them.

        while (!tlempty(&ready_list))
            yield_running_thread();
        
        // No runnable threads. Sleep using the wfi instruction. Note that we
        // need to disable interrupts and check the runnable thread list one
        // more time (make sure it is empty) to avoid a race condition where an
        // ISR marks a thread ready before we call the wfi instruction.

        disable_interrupts();
        if (tlempty(&ready_list))
            asm ("wfi");
        enable_interrupts();
    }
}