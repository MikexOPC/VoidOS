#include <stddef.h>
#include <string.h>
#pragma function(memset, memcpy, memcmp)

// Freestanding compiler ABI helpers. Callers own valid bounded buffers; memcpy
// requires non-overlap, memcmp may stop at the first differing byte. Volatile
// accesses prevent this implementation being optimized into calls to itself.
// No allocator, OS runtime, unwinding or user-copy/fault-recovery semantics.

// ====== memset ======

void *memset(void *destination, int value, size_t bytes)
{
    volatile unsigned char *output = destination;
    for (size_t index = 0; index < bytes; ++index) output[index] = (unsigned char)value;
    return destination;
}

// ====== memcpy ======

void *memcpy(void *destination, const void *source, size_t bytes)
{
    volatile unsigned char *output = destination;
    const volatile unsigned char *input = source;
    for (size_t index = 0; index < bytes; ++index) output[index] = input[index];
    return destination;
}

// ====== memcmp ======

int memcmp(const void *first, const void *second, size_t bytes)
{
    const volatile unsigned char *a = first, *b = second;
    for (size_t index = 0; index < bytes; ++index)
        if (a[index] != b[index]) return a[index] < b[index] ? -1 : 1;
    return 0;
}
