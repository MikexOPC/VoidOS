#ifndef KERNEL_PS2_MOUSE_CONTROLLER_H
#define KERNEL_PS2_MOUSE_CONTROLLER_H

#include "kernel/drivers/input/ps2/controller/controller.h"

int kernel_ps2_mouse_prepare(KERNEL_PS2_CONTROLLER *owner);
int kernel_ps2_mouse_command(KERNEL_PS2_CONTROLLER *owner, uint8_t command);

#endif
