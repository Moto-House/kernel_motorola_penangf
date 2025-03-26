/* Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/kobject.h>
#include <linux/of.h>

#define CDBG(fmt, args...) pr_err(fmt, ##args)
static struct kobject *hqdbg_cam_kobj;

typedef unsigned char kal_uint8;
kal_uint8 otp_status_main = 0xFF;
kal_uint8 otp_status_front = 0xFF;
kal_uint8 otp_status_sub = 0xFF;
kal_uint8 otp_status_macro = 0xFF;

#define MODULE_INFO_SIZE  32
char module_info_main[MODULE_INFO_SIZE];
char module_info_front[MODULE_INFO_SIZE];
char module_info_wide[MODULE_INFO_SIZE];

//sys/camera_dbg/otp_status
static ssize_t otp_status_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	int cont = 0;
	cont += snprintf(buf, PAGE_SIZE, "MainCamera:%d,FrontCamera:%d,SubCamera:%d,MacroCamera:%d\n",
			otp_status_main, otp_status_front, otp_status_sub, otp_status_macro);

	return cont;
}
static ssize_t otp_status_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *cmd, size_t count)
{
	return count;
}
static struct kobj_attribute attr_otp_status = {
	.attr = {
		.name = "otp_status",
		.mode = 0644,
	},
	.show = otp_status_show,
	.store = otp_status_store,
};


//sys/camera_dbg/module_info
static ssize_t module_info_main_show(struct kobject *kobj,
						struct kobj_attribute *attr, char *buf){

	int cont = 0;
	cont += snprintf(buf, PAGE_SIZE, "%s", module_info_main);
	return cont;
}
static ssize_t module_info_main_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *cmd, size_t count)
{
	return count;
}
static struct kobj_attribute attr_module_info_main = {
	.attr = {
		.name = "module_info_main",
		.mode = 0644,
	},
	.show = module_info_main_show,
	.store = module_info_main_store,
};


static ssize_t module_info_front_show(struct kobject *kobj,
						struct kobj_attribute *attr, char *buf){

	int cont = 0;
	cont += snprintf(buf, PAGE_SIZE, "%s", module_info_front);
	return cont;
}
static ssize_t module_info_front_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *cmd, size_t count)
{
	return count;
}
static struct kobj_attribute attr_module_info_front = {
	.attr = {
		.name = "module_info_front",
		.mode = 0644,
	},
	.show = module_info_front_show,
	.store = module_info_front_store,
};


static ssize_t module_info_wide_show(struct kobject *kobj,
						struct kobj_attribute *attr, char *buf){

	int cont = 0;
	cont += snprintf(buf, PAGE_SIZE, "%s", module_info_wide);
	return cont;
}
static ssize_t module_info_wide_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *cmd, size_t count)
{
	return count;
}
static struct kobj_attribute attr_module_info_wide = {
	.attr = {
		.name = "module_info_wide",
		.mode = 0644,
	},
	.show = module_info_wide_show,
	.store = module_info_wide_store,
};


int mtk_cam_otp_init_module(void)
{
	//sys/camera_dbg
	int ret = 0;
	hqdbg_cam_kobj = kobject_create_and_add("camera_dbg", NULL);
	pr_err("init module::enter",__func__);
	if (IS_ERR(hqdbg_cam_kobj)){
		pr_err("%s: create sysfs cam fail\n",__func__);
		return -ENOMEM;
	}
	ret = sysfs_create_file(hqdbg_cam_kobj, &attr_otp_status.attr);
	if (ret < 0){
		pr_err("create camera_dbg attribute file fail\n");
	}
	ret = sysfs_create_file(hqdbg_cam_kobj, &attr_module_info_main.attr);
	if (ret < 0){
		pr_err("create attr_module_info_main attribute file fail\n");
	}
	ret = sysfs_create_file(hqdbg_cam_kobj, &attr_module_info_front.attr);
	if (ret < 0){
		pr_err("create attr_module_info_front attribute file fail\n");
	}
	ret = sysfs_create_file(hqdbg_cam_kobj, &attr_module_info_wide.attr);
	if (ret < 0){
		pr_err("create attr_module_info_wide attribute file fail\n");
	}

	return ret;
}

void mtk_cam_otp_exit_module(void)
{
	if (hqdbg_cam_kobj) {
		sysfs_remove_file(hqdbg_cam_kobj, &attr_otp_status.attr);
		kobject_put(hqdbg_cam_kobj);
	}
}

