#ifndef KERNEL_PS2_MOUSE_STREAM_H
#define KERNEL_PS2_MOUSE_STREAM_H
#include "../controller.h"

int kernel_ps2_pair_start(KERNEL_PS2_CONTROLLER *owner);
int kernel_ps2_pair_stop(KERNEL_PS2_CONTROLLER *owner);

#endif
