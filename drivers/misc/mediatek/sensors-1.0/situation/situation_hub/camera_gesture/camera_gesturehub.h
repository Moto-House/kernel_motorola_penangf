/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef CAMERA_GESTUREHUB_H
#define CAMERA_GESTUREHUB_H

#include <linux/ioctl.h>
#include <linux/init.h>

int __init camera_gesture_hub_init(void);
void __exit camera_gesture_hub_exit(void);

#endif
