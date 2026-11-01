#ifndef OS_TEST_KERNEL_SYNC_H
#define OS_TEST_KERNEL_SYNC_H
#include "kernel/sync/internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// ====== synchronization_test_contract ======

void test_set_if(uint64_t value);
void test_set_pause(void (*hook)(void));
void test_watch_fatal(KERNEL_LOCK *lock);
void test_cases(void);
void test_provider(void);
void test_threads(uint32_t count);
void test_fatal(const char *name);
int test_clock(void *context, uint32_t *value);
#endif
