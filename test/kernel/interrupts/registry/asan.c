#include <stdio.h>

int run_irq_registry_tests(void);

// ====== main ======

int main(void)
{
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        int result = run_irq_registry_tests();
        if (result) {
            printf("FAIL: IRQ registry ASan group %d, cycle %u\n",result,cycle);
            return 1;
        }
    }
    puts("PASS: IRQ registry ASan, 100 repetitions of eight lifecycle/error groups");
    return 0;
}
