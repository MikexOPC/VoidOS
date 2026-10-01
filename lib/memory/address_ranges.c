#include "address_ranges.h"

// ====== sovrapposizione senza sommare indirizzi non validati ====== //

int os_address_ranges_overlap(uint64_t first, uint64_t first_bytes, uint64_t second, uint64_t second_bytes)
{
    if (!first_bytes || !second_bytes)
        return 0;
    return first <= second ? second - first < first_bytes : first - second < second_bytes;
}
