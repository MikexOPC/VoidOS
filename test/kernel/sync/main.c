#include "test.h"
#include <stdlib.h>

// ====== main ======

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "fatal")) test_fatal(argv[2]);
    else if (argc == 3 && !strcmp(argv[1], "threads")) test_threads((uint32_t)strtoul(argv[2], NULL, 10));
    else { assert(argc == 1); test_cases(); test_provider(); test_progress_wait(); }
    return 0;
}
