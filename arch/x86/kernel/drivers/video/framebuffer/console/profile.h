#ifndef X86_CONSOLE_PROFILE_H
#define X86_CONSOLE_PROFILE_H
#include <stdint.h>

// Flush profiling (P1-A): only in images built with -ProfileVgpuFlush. Phases are timed with
// the ACPI PM timer of the console session and accumulated per phase, reported once at exit.
#define X86_PROFILE_FLUSH   0U  // whole x86_console_flush
#define X86_PROFILE_RENDER  1U  // the measured render step (all groups of the flush)
#define X86_PROFILE_GROUP   2U  // one draw_group call
#define X86_PROFILE_VERIFY  3U  // paging inventory verification in a pixel session
#define X86_PROFILE_TARGET  4U  // redirected target validity checks
#define X86_PROFILE_PREPARE 5U  // live drawing accounting (framebuffer live/group.c)
#define X86_PROFILE_PRESENT 6U  // VirtIO present of the flush
#define X86_PROFILE_POLL    7U  // VT-d fault scan of the flush
#define X86_PROFILE_PHASES  8U

#ifdef KERNEL_PROFILE_VGPU_FLUSH
void x86_profile_start(uint32_t port, uint32_t mask);
uint32_t x86_profile_now(void);
void x86_profile_add(uint32_t phase, uint32_t start);
int x86_profile_report(void);
#define X86_PROFILE_BEGIN(name) uint32_t name = x86_profile_now()
#define X86_PROFILE_END(phase, name) x86_profile_add(phase, name)
#else
#define X86_PROFILE_BEGIN(name) do { } while (0)
#define X86_PROFILE_END(phase, name) do { } while (0)
#endif
#endif
