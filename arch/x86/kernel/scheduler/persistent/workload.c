#include "persistent.h"
#include "../integration/control.h"

#define PERSISTENT_ROUNDS 32U
static uint32_t stage, produced, consumed, cleanup, stop_sent;
static uint64_t consumer_generation;

// ====== persistent_wait_stage_private ======

static void persistent_wait_stage_private(uint32_t expected)
{
    for (uint32_t retry = 0; stage != expected; ++retry) {
        if (retry >= 8 || stage > expected) kernel_scheduler_fail_stop();
        x86_scheduler_yield();
    }
}

// ====== persistent_consumer_private ======

static void persistent_consumer_private(void)
{
    KERNEL_EVENT_REPLY result;
    consumer_generation = x86_scheduler_generation();
    for (uint32_t round = 0; round < PERSISTENT_ROUNDS; ++round) {
        uint32_t id = 4 + round % 2;
        stage = 2 * round + 1;
        result = x86_scheduler_event_request(3,id,0);
        if (result.outcome || result.epoch != round / 2) kernel_scheduler_fail_stop();
        stage = 2 * round + 2;
        result = x86_scheduler_event_request(4,id,result.epoch);
        if (result.outcome != KERNEL_EVENT_WAITING || produced != round + 1 || consumed != round)
            kernel_scheduler_fail_stop();
        ++consumed;
    }
    stage = 65;
    result = x86_scheduler_event_request(3,6,0);
    if (result.outcome || result.epoch) kernel_scheduler_fail_stop();
    stage = 66;
    result = x86_scheduler_event_request(4,6,0);
    if (result.outcome != KERNEL_EVENT_WAITING || !stop_sent) kernel_scheduler_fail_stop();
    result = x86_scheduler_event_request(7,0,consumer_generation);
    if (result.outcome != 1 || result.woken) kernel_scheduler_fail_stop();
    // A stopped task cannot park itself again, even with an unchanged event.
    result = x86_scheduler_event_request(4,6,0);
    if (result.outcome != KERNEL_EVENT_STOP_REQUESTED) kernel_scheduler_fail_stop();
    cleanup = 1; stage = 67; // Trusted finite fixture owns no driver payload/resources.
}

// ====== persistent_producer_private ======

static void persistent_producer_private(void)
{
    KERNEL_EVENT_REPLY result;
    for (uint32_t round = 0; round < PERSISTENT_ROUNDS; ++round) {
        persistent_wait_stage_private(2 * round + 2);
        if (produced != round || consumed != round) kernel_scheduler_fail_stop();
        produced = round + 1; // Persistent condition precedes event notification.
        result = x86_scheduler_event_request(5,4 + round % 2,0);
        if (result.outcome != 3 || result.woken != 1 || result.epoch != round / 2 + 1)
            kernel_scheduler_fail_stop();
    }
    persistent_wait_stage_private(66);
    if (!consumer_generation || produced != PERSISTENT_ROUNDS || consumed != PERSISTENT_ROUNDS)
        kernel_scheduler_fail_stop();
    result = x86_scheduler_event_request(6,0,consumer_generation);
    if (result.outcome != 1 || result.woken != 1 || result.reply.state != KERNEL_TASK_READY)
        kernel_scheduler_fail_stop();
    stop_sent = 1;
}

// ====== x86_scheduler_persistent_task ======

void x86_scheduler_persistent_task(uint32_t index)
{
    if (!x86_scheduler_task_valid()) kernel_scheduler_fail_stop();
    if (index == 0) persistent_consumer_private();
    else if (index == 1) persistent_producer_private();
    else kernel_scheduler_fail_stop();
}

// ====== x86_scheduler_persistent_complete ======

int x86_scheduler_persistent_complete(void)
{
    return stage == 67 && produced == PERSISTENT_ROUNDS && consumed == PERSISTENT_ROUNDS &&
        cleanup == 1 && stop_sent == 1 && consumer_generation != 0;
}

// ====== x86_scheduler_persistent_cycles ======

uint32_t x86_scheduler_persistent_cycles(void) { return consumed; }
