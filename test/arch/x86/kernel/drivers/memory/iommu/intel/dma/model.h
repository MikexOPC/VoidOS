#ifndef TEST_VTD_DMA_MODEL_H
#define TEST_VTD_DMA_MODEL_H
#include "edu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Host model of the QEMU edu registers used by edu.c, with failure injection.
typedef struct { BOOT_U32 Offset, Width; BOOT_U64 Value; } MODEL_WRITE;
typedef struct {
    BOOT_U64  Identity, Source, Target, Count, Command, Ticks, Step;
    BOOT_U32  Liveness, BusyReads, FailRead, FailWrite, Reads, Writes, Stall, Invert;
    MODEL_WRITE  Log[16];
} MODEL;

extern MODEL model;
extern unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL DMA:%u\n", (unsigned)__LINE__); exit(1); } } while (0)

void model_reset(void);
EDU_BUS model_bus(void);
#endif
