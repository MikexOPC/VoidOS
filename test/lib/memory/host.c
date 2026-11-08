#include <stdio.h>

int run_address_ranges_tests(void);

// ====== main ======

int main(void)
{
    int result = run_address_ranges_tests();
    if (result != 0) {
        fprintf(stderr, "FAIL: address_ranges case %d\n", result);
        return 1;
    }
    puts("PASS: address_ranges boundaries, symmetry and exhaustive small intervals");
    return 0;
}
