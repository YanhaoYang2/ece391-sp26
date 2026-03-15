// timer.c - Timer and Alarms
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef TIMER_TRACE
#define TRACE
#endif

#ifdef TIMER_DEBUG
#define DEBUG
#endif

#include "timer.h"
#include "thread.h"
#include "riscv.h"
#include "sbi.h" // for sbi_set_timer
#include "intr.h"
#include "misc.h"
#include "string.h"

#include <stddef.h>
#include <limits.h> // for ULLONG_MAX
#include "heap.h"
// COMPILE-TIME OPTIONS
//

#ifndef BOLT_FREQ
#define BOLT_FREQ 50 // Hz [MP3cp3]
#endif

// INTERNAL TYPE DEFINITIONS
//

struct timer_alarm {
    // YOUR CODE HERE
  unsigned long long twake;   // wake time in ticks
  struct condition    woken;  // Condition from CP3
  struct timer_alarm *next;   // next one to wake
};

// EXPORTED GLOBAL VARIABLES
//

char timer_initialized = 0;
unsigned int timer_frequency = 0;


// INTERNAL GLOBAL VARIABLES
//
//used for preemptive scheduling
int bold_period;
static struct timer_alarm * sleep_list; // list of pending alarms

// INTERNAL FUNCTION DECLARATIONS
//


static void enable_timer_interrupts(void); // sets sie.STIE=1
static void disable_timer_interrupts(void); // clears sie.STIE


// EXPORTED FUNCTION DEFINITIONS
//

void timer_init(unsigned int freq) {
    // YOUR CODE HERE
    timer_frequency=freq;
    //set automatic time interrupt period according to frequency 
    bold_period=freq/BOLT_FREQ;
    timer_initialized=1;
    sbi_set_timer(0); //trigger first interrupt
    enable_interrupts();
}

void sleep_until(unsigned long long twake) {
    // YOUR CODE HERE
    // ignore those who have passed the wake up time
    if(rdtime()>=twake) return;
    disable_timer_interrupts();
    struct timer_alarm * ta=kcalloc(1,sizeof(*ta));
    ta->twake=twake;
    condition_init(&ta->woken,"time_wake_condition");
    ta->next=NULL;
    if(!sleep_list){
        //if nothing in sleep list we need to set mtimecmp correctly so that time interrupt occurs
        sleep_list=ta;
    }else{
        // insert the current time_alarm into sleeplist's correct position so it is still in ascending order
        struct timer_alarm* cur=sleep_list;
        struct timer_alarm* before=NULL;
        while(cur && cur->twake<ta->twake){
            before=cur;
            cur=cur->next;
        }
        ta->next=cur;
        if(before){
            before->next=ta;
        }else{
            sleep_list=ta;
        }
    }
    int s=disable_interrupts();
    enable_timer_interrupts();
    // condition wait until its wake up time is up 
    while(rdtime()<twake){
    condition_wait(&ta->woken);}
    restore_interrupts(s);
    kfree(ta);
}

void sleep_sec(unsigned int sec) {
    sleep_until(rdtime() + 1ULL * sec * timer_frequency);
}

void sleep_ms(unsigned int ms) {
    sleep_until(rdtime() + 1ULL * ms * timer_frequency / 1000);
}

void sleep_us(unsigned int us) {
    sleep_until(rdtime() + 1ULL * us * timer_frequency / 1000 / 1000);
}

void handle_timer_interrupt(void) {

    unsigned long long tnow = rdtime();

    trace("[%lu] %s()", tnow, __func__);

    // YOUR CODE HERE
    struct timer_alarm* cur=sleep_list;
    struct timer_alarm* prev=NULL;
    // let all waiting threads whose time has passed wake up 
    while(cur && cur->twake<=tnow){
        condition_broadcast(&cur->woken);
        prev=cur;
        cur=cur->next;
        prev->next=NULL;
    }
    sleep_list=cur;
    // if there is still condition waiting in sleep list set timer to that time otherwise use bold period
    if(sleep_list) sbi_set_timer(sleep_list->twake);
    else{
        unsigned long long tbolt=tnow+bold_period;
        sbi_set_timer(tbolt);
    }


}

// INTERNAL FUNCTION DEFINITIONS
//


void enable_timer_interrupts(void) {
    csrs_sie(RISCV_SIE_STIE);
}

void disable_timer_interrupts(void) {
    csrc_sie(RISCV_SIE_STIE);
}