/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef SIGNIFICANT_MOVE_H
#define SIGNIFICANT_MOVE_H

#include <linux/ioctl.h>
#include <linux/init.h>

int __init significant_move_hub_init(void);
void __exit significant_move_hub_exit(void);

#endif
