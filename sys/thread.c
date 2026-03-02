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
    //check for available positions in thrab 0 is main thread, 31 is idle thread
    int tid=-1;
    for(int i=1;i<NTHR-1;i++){
        // find an empty space in thrtab
        if(thrtab[i]==NULL){
            tid=i;
            break;
        }
    }
    if(tid==-1) return -EMTHR; // no empty space in thrtab, return too many threads error code
    // allocating space for thread and its stack, if space not enough, return out of memory error code
    struct thread* new_thread= kmalloc(sizeof(struct thread));
    if(!new_thread)return -ENOMEM;
    void* stack=kmalloc(HEAP_ALLOC_MAX);
    if(!stack) {
        // prevent memory leak;
        kfree(new_thread); 
        return -ENOMEM;}
    //starts initializing 

    memset(new_thread, 0, sizeof(*new_thread));
    new_thread->id=tid;
    if(name) new_thread->name=name;
    else new_thread->name="member_thread";
    va_list ap;
    va_start(ap,entry);
    //mapping entry funct argument into context s[]
    new_thread->ctx.s[0]=va_arg(ap,uintptr_t);
    new_thread->ctx.s[1]=va_arg(ap,uintptr_t);
    new_thread->ctx.s[2]=va_arg(ap,uintptr_t);
    new_thread->ctx.s[3]=va_arg(ap,uintptr_t);
    new_thread->ctx.s[4]=va_arg(ap,uintptr_t);
    new_thread->ctx.s[5]=va_arg(ap,uintptr_t);
    new_thread->ctx.s[6]=va_arg(ap,uintptr_t);
    new_thread->ctx.s[7]=va_arg(ap,uintptr_t);
    va_end(ap);
    new_thread->ctx.s[8]=(uintptr_t)entry;
    set_thread_state(new_thread,THREAD_READY);
    new_thread->parent=TP;
    new_thread->stack_anchor=stack+HEAP_ALLOC_MAX-sizeof(struct thread_stack_anchor); //start of sp
    new_thread->stack_lowest=stack;
    new_thread->ctx.sp=stack+HEAP_ALLOC_MAX-sizeof(struct thread_stack_anchor);
    // a trampoline before entering entry func
    new_thread->ctx.ra=&start_thread;
    condition_init(&new_thread->child_exit, "child_exit");
    new_thread->list_next=NULL;
    int s=disable_interrupts();
    tlinsert(&ready_list,new_thread);
    thrtab[tid]=new_thread;
    restore_interrupts(s);
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
    // notify joiners
    if(TP->parent){
        condition_broadcast(&TP->parent->child_exit);
    }
    // process child thread
    for(int i=0;i<NTHR; i++){
        if(thrtab[i] && thrtab[i]->parent==TP){
            //reclaim exited
            if(thrtab[i]->state==THREAD_EXITED){
                kfree(thrtab[i]);
                thrtab[i]=NULL;
            }else{
                // orphan the rest 
                thrtab[i]->parent=NULL;
            }
        }
    }
    yield_running_thread();
    halt();
}


void yield_running_thread(void) {
    extern void switch_running_thread(struct thread *); // thrasm.s

    trace("%s() in <%s:%d>", __func__, TP->name, TP->id);

    // The idle thread is always runnable, and the idle thread only calls
    // yield() if the ready_list is not empty.

    assert (!tlempty(&ready_list));

    // YOUR CODE HERE
    int s=disable_interrupts();
    if(TP->state==THREAD_RUNNING){
        set_thread_state(TP,THREAD_READY);
        tlinsert(&ready_list,TP);
    }
    struct thread * next_thread= tlremove(&ready_list);
    next_thread->state=THREAD_RUNNING;
    enable_interrupts();
    switch_running_thread(next_thread);
    restore_interrupts(s);
    
}

// The finish thread function is only called from switch_running_thread() in
// thrasm.s. It is not declared in thread.h.

void finish_thread_switch(struct thread * susp_thread) {
    // YOUR CODE HERE
    if(susp_thread->state!=THREAD_EXITED){
        return;
    }
    // skips for static stack
    if(susp_thread->id==MAIN_TID || susp_thread->id==IDLE_TID){
        return;
    }
    kfree(susp_thread->stack_lowest);
    int parent_id=-1;
    for(int i=0;i<NTHR;i++){
        if(susp_thread->parent==thrtab[i]){
            parent_id=i ;
        }
    }
     // if it is an orphan, we also need to reclaim its thread struct and thrtab[]
    if(parent_id==-1){
        int id=susp_thread->id;
        kfree(susp_thread);
        thrtab[id]=NULL;
    }
}

int join_thread(int u_tid) {

    trace("%s(%d) in <%s:%d>", __func__, u_tid, TP->name, TP->id);

    // YOUR CODE HERE
    if(u_tid!=0 && thrtab[u_tid]&&thrtab[u_tid]->parent !=TP){
        //specified child is not your child
        return -ECHILD;
    }
    if(u_tid==0){
        int error_id=-1;
         for(int i=0;i<NTHR; i++){
            if(thrtab[i]&&thrtab[i]->parent==TP){
            //reclaim exited
            error_id=i;
        }
    }
    //no child
    if(error_id==-1){
        return -ECHILD;
    }
    }
    // fast path exited child thread already exists
    if(u_tid!=0&& thrtab[u_tid]&& thrtab[u_tid]->state==THREAD_EXITED){
        kfree(thrtab[u_tid]);
        thrtab[u_tid]=NULL;
        return u_tid;
    }
    if(u_tid==0){
            for(int i=0;i<NTHR; i++){
            if(thrtab[i]&&thrtab[i]->parent==TP){
                if(thrtab[i]->state==THREAD_EXITED){
                    kfree(thrtab[i]);
                    thrtab[i]=NULL;
                    return i;
                }
        }
    }
}

    //wait on child exit condition 
    condition_wait(&TP->child_exit);
    if(u_tid!=0){
        // must signal a child exit to wake up condition wait
        assert(thrtab[u_tid]->state==THREAD_EXITED);
        kfree(thrtab[u_tid]);
        thrtab[u_tid]=NULL;
        return u_tid;
    }
    // tid==0, return any child in exit state
    int exit_child=-1;
    for(int i=0;i<NTHR; i++){
            if(thrtab[i]&&thrtab[i]->parent==TP){
                if(thrtab[i]->state==THREAD_EXITED){
                    kfree(thrtab[i]);
                    thrtab[i]=NULL;
                    exit_child=i;
                    break;// one thread at a time shall be freed and its id shall be returned
                }
            }
        }
    assert(exit_child!=-1); // there must be one child in exit state
    return exit_child;
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
    //prevent isr screw with waitlist
    int s=disable_interrupts();
    set_thread_state(TP,THREAD_WAITING);
    tlinsert(&cond->wait_list,TP);
    yield_running_thread();
    restore_interrupts(s);
}

void condition_broadcast(struct condition * cond) {

    trace("%s(<%s>) in <%s:%d>", __func__, cond->name, TP->name, TP->id);

    // Fast path: if there are no threads waiting, return.

    if (tlempty(&cond->wait_list))
        return;

    // YOUR CODE HERE
    //remove waked up from wait list and insert into ready list
     //prevent isr screw with waitlist
    int s=disable_interrupts();
    while(!tlempty(&cond->wait_list)){
        struct thread * waked_UP=tlremove(&cond->wait_list);
        waked_UP->state=THREAD_READY;
        tlinsert(&ready_list,waked_UP);
    }
    restore_interrupts(s);
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
    memset(rwlk, 0, sizeof(*rwlk));
    rwlk->owner=NULL;
    rwlk->cnt=0;
    rwlk->name = (name != NULL) ? name : "anon";
}

const char * rwlock_name(const struct rwlock * rwlk) {
    return rwlk->name;
}

void rwlock_acquire(struct rwlock * rwlk, int exclusive) {
    trace("%s(<%s>,%d)", __func__, rwlk->name, exclusive);

    // YOUR CODE HERE
    //want to own the lock exclusively
    if(exclusive!=0){
         if(rwlk->owner==TP){
            //if  recursively called, add multiplicity
                int s=disable_interrupts();
                rwlk->cnt+=1;
                restore_interrupts(s);
                return;
            }
        else if(rwlk->owner==NULL){
            int s=disable_interrupts();
            //only quit if count is zero and owner is null fully unlocked
            while(rwlk->cnt!=0 || rwlk->owner!=NULL){
                condition_wait(&rwlk->released);
            }
                rwlk->owner=TP;
                rwlk->cnt+=1;
                restore_interrupts(s);
            }
        
    }else {
        // want to own the lock shared
            int s=disable_interrupts();
            //wait until rwlk released from exclusive
            while(rwlk->owner!=NULL){
                condition_wait(&rwlk->released);}
            rwlk->owner=NULL;
            rwlk->cnt+=1;
            restore_interrupts(s);
        }
        
}


void rwlock_release(struct rwlock * rwlk) {
    trace("%s(<%s>)", __func__, rwlk->name);

    // YOUR CODE HERE
    int s =disable_interrupts();
    rwlk->cnt-=1;
    // if zero, let waiting list know it is unlocked
    if(rwlk->cnt==0){
        condition_broadcast(&rwlk->released);
        rwlk->owner=NULL;
    }
    restore_interrupts(s);
}


// INTERNAL FUNCTION DEFINITIONS
//

void init_main_thread(void) {
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