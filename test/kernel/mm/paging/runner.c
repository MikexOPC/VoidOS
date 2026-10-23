#include <stdio.h>
int run_paging_inventory_tests(void);

// ====== main ======

int main(void)
{
    unsigned int iteration;
    for (iteration = 0; iteration < 100; ++iteration) {
        int result = run_paging_inventory_tests();
        if (result) {
            printf("FAIL: iteration %u, paging group/code %d\n", iteration, result);
            return 1;
        }
    }
    puts("PASS: 9 paging groups repeated 100 times; pinned lease cleanup checked");
    return 0;
}
