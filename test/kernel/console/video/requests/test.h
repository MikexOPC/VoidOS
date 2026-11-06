#ifndef TEST_CONSOLE_REQUESTS_H
#define TEST_CONSOLE_REQUESTS_H
#include "kernel/console/video/requests/queue.h"
#include "kernel/console/video/service/console.h"
#include "test/kernel/sync/test.h"

void requests_context_prepare(void);
void requests_context_disable(void);
void requests_threads(void);
#endif
