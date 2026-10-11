#ifndef X86_WATCHDOG_TEST_MOCK_H
#define X86_WATCHDOG_TEST_MOCK_H
#include "../../../../../../../arch/x86/kernel/timer/hpet/watchdog/watchdog.h"
extern X86_WATCHDOG_ENV test_env;
extern BOOT_U32 test_mode, test_borrows, test_in_irq, test_interrupts;
extern BOOT_U32 test_hpet[1024], test_io[256];
extern volatile BOOT_U32 test_source[2], test_stop;
void watchdog_test_reset(BOOT_U32 mode);
#endif
