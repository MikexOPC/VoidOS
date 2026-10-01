#ifndef OS_X86_BOOT_PROFILE_H
#define OS_X86_BOOT_PROFILE_H

// ====== geometria del profilo di boot x86 a quattro livelli ====== //

#define X86_BOOT_PAGE_BYTES 4096ULL
#define X86_BOOT_LOW_ADDRESS_LIMIT (1ULL << 47)

// ====== indirizzi virtuali e budget del boot attuale ====== //

#define X86_BOOT_INFO_VIRTUAL 0x0000600000000000ULL
#define X86_BOOT_STACK_GUARD 0x0000700000000000ULL
#define X86_BOOT_STACK_VIRTUAL (X86_BOOT_STACK_GUARD + X86_BOOT_PAGE_BYTES)
#define X86_BOOT_STACK_PAGES 16ULL
#define X86_BOOT_STACK_BYTES (X86_BOOT_STACK_PAGES * X86_BOOT_PAGE_BYTES)
#define X86_BOOT_STACK_TOP (X86_BOOT_STACK_VIRTUAL + X86_BOOT_STACK_BYTES)
#define X86_BOOT_TABLE_LIMIT 128U

// ====== vincoli verificati anche dai consumatori C++ ====== //

#ifdef __cplusplus
#define X86_BOOT_ASSERT(condition, message) static_assert(condition, message)
#else
#define X86_BOOT_ASSERT(condition, message) _Static_assert(condition, message)
#endif

X86_BOOT_ASSERT(X86_BOOT_PAGE_BYTES == 4096, "Paging implementation requires 4 KiB pages");
X86_BOOT_ASSERT(X86_BOOT_LOW_ADDRESS_LIMIT == (1ULL << 47), "Four-level lower canonical range required");
X86_BOOT_ASSERT(X86_BOOT_STACK_PAGES > 0 && X86_BOOT_STACK_PAGES <= 16,
                "Initial stack budget exceeds 64 KiB");
X86_BOOT_ASSERT(X86_BOOT_TABLE_LIMIT >= 4 && X86_BOOT_TABLE_LIMIT <= 128,
                "Page table budget must fit the current ownership metadata");
X86_BOOT_ASSERT(X86_BOOT_INFO_VIRTUAL >= X86_BOOT_PAGE_BYTES &&
                X86_BOOT_INFO_VIRTUAL <= X86_BOOT_LOW_ADDRESS_LIMIT - X86_BOOT_PAGE_BYTES,
                "Boot contract must fit the lower canonical range");
X86_BOOT_ASSERT((X86_BOOT_INFO_VIRTUAL & (X86_BOOT_PAGE_BYTES - 1)) == 0,
                "Boot contract must be page aligned");
X86_BOOT_ASSERT(X86_BOOT_STACK_GUARD >= X86_BOOT_PAGE_BYTES &&
                X86_BOOT_STACK_GUARD <= X86_BOOT_LOW_ADDRESS_LIMIT -
                    X86_BOOT_PAGE_BYTES - X86_BOOT_STACK_BYTES,
                "Guard and stack must fit the lower canonical range");
X86_BOOT_ASSERT((X86_BOOT_STACK_GUARD & (X86_BOOT_PAGE_BYTES - 1)) == 0,
                "Stack guard must be page aligned");
X86_BOOT_ASSERT(X86_BOOT_INFO_VIRTUAL + X86_BOOT_PAGE_BYTES <= X86_BOOT_STACK_GUARD ||
                X86_BOOT_INFO_VIRTUAL >= X86_BOOT_STACK_TOP,
                "Boot contract must not overlap the guard or stack");

#undef X86_BOOT_ASSERT
#endif
