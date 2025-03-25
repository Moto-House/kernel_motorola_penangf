// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 MediaTek Inc.
 *
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>

#include <vibrator_notify.h>
#include <cam_vcm_notify.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/proc_fs.h>

#define VIB_DEVICE "regulator_vibrator"

#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME " %s(%d) :" fmt, __func__, __LINE__

#define DEFAULT_MIN_LIMIT 15

struct reg_vibr_config {
	unsigned int min_volt;
	unsigned int max_volt;
	struct regulator *reg;
};

struct reg_vibr {
	atomic_t reg_status;
	atomic_t vibr_state;
	atomic_t vibr_shutdown;
	struct workqueue_struct *vibr_queue;
	struct work_struct vibr_work;
	struct led_classdev vibr_cdev;
	struct reg_vibr_config vibr_conf;
	struct notifier_block oc_handle;
};
/*Penang code for EKPENAN4GU-2875 by zouxiaomin at 20240326 start*/

static unsigned long vibrator_duration;
//#define VIB_PATH "sys/class/leds/vibrator/duration"

static bool camera_detect_flag = 0;
#define VIBR_LONG_DUR	180
//#define VIBR_SHORT_DUR	70
#include <linux/notifier.h>
#include <linux/export.h>

#include <linux/blkdev.h>
#include <linux/unistd.h>
//#include <stdio.h>
#include <linux/string.h>

#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/leds.h>

static struct kobject *vib_dur_kobj;

static BLOCKING_NOTIFIER_HEAD(vibrator_notifier_list);

/**
 *	vibrator_register_client - register a client notifier
 *	@nb: notifier block to callback on events
 */
int vibrator_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&vibrator_notifier_list, nb);
}
EXPORT_SYMBOL(vibrator_register_client);

/**
 *	vibrator_unregister_client - unregister a client notifier
 *	@nb: notifier block to callback on events
 */
int vibrator_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&vibrator_notifier_list, nb);
}
EXPORT_SYMBOL(vibrator_unregister_client);

/**
 * vibrator_notifier_call_chain - notify clients of vibrator_events
 *
 */

int vibrator_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&vibrator_notifier_list, val, v);
}
EXPORT_SYMBOL_GPL(vibrator_notifier_call_chain);
/*Penang code for EKPENAN4GU-2875 by zouxiaomin at 20240326 end*/

static int mt_vibra_init_config(struct device *dev,
		struct reg_vibr_config *vibr_conf)
{
	int ret;

	vibr_conf->reg = devm_regulator_get(dev, "vib");
	if (IS_ERR(vibr_conf->reg)) {
		ret = PTR_ERR(vibr_conf->reg);
		pr_notice("Error load dts: get regulator return %d\n", ret);
		vibr_conf->reg = NULL;
		return ret;
	}

	ret = of_property_read_u32(dev->of_node, "max-volt",
		&vibr_conf->max_volt);
	if (ret) {
		pr_notice("Error load dts: get max-volt failed!\n");
		ret = -EINVAL;
		return ret;
	}

	ret = of_property_read_u32(dev->of_node, "min-volt",
		&vibr_conf->min_volt);
	if (ret) {
		pr_notice("Error load dts: get min-volt failed!\n");
		ret = -EINVAL;
		return ret;
	}

	if (vibr_conf->min_volt > vibr_conf->max_volt) {
		pr_notice("Error load dts: get error voltage(min > max)!\n");
		ret = -EINVAL;
		return ret;
	}

	pr_info("vibr_conf %u-%u\n",
		vibr_conf->min_volt, vibr_conf->max_volt);

	return ret;
}

static int vibr_power_set(struct reg_vibr *vibr)
{
	int ret;

	pr_info("set voltage = %u-%u\n",
		vibr->vibr_conf.min_volt, vibr->vibr_conf.max_volt);
	ret = regulator_set_voltage(vibr->vibr_conf.reg,
		vibr->vibr_conf.min_volt, vibr->vibr_conf.max_volt);
	if (ret < 0)
		pr_notice("set voltage fail, ret = %d\n", ret);

	return ret;
}

static void vibr_enable(struct reg_vibr *vibr)
{
	pr_info("vibr enable : dur : %lu, camera: %d, reg_stat:%d\n", vibrator_duration, camera_detect_flag, atomic_read(&vibr->reg_status));

/*Penang code for EKPENAN4GU-2875 by zouxiaomin at 20240326 start*/
	if (!atomic_read(&vibr->reg_status)) {
		if ((vibrator_duration > VIBR_LONG_DUR) && (camera_detect_flag == CAM_EVENT_ON)) {
			return;
		}
	}
/*Penang code for EKPENAN4GU-2875 by zouxiaomin at 20240326 end*/
	if (!atomic_read(&vibr->reg_status)) {
		if (regulator_enable(vibr->vibr_conf.reg))
			pr_notice("set vibr_reg enable failed!\n");
		else
			atomic_set(&vibr->reg_status, 1);
	} else {
		pr_notice("vibr_reg already enabled.\n");
	}
}

static void vibr_disable(struct reg_vibr *vibr)
{
	pr_info("vibr disable : reg_stat:%d\n", atomic_read(&vibr->reg_status));
	if (atomic_read(&vibr->reg_status)) {
		if (regulator_disable(vibr->vibr_conf.reg))
			pr_notice("set vibr_reg disable failed!\n");
		else
			atomic_set(&vibr->reg_status, 0);
	} else {
		pr_notice("vibr_reg already disabled.\n");
	}
}

static void update_vibrator(struct work_struct *work)
{
	struct reg_vibr *vibr = container_of(work, struct reg_vibr, vibr_work);

	if (!atomic_read(&vibr->vibr_state))
		vibr_disable(vibr);
	else
		vibr_enable(vibr);
}

static int regulator_vibrator_set(struct led_classdev *led_cdev, enum led_brightness value)
{
	struct reg_vibr *vibr = container_of(led_cdev, struct reg_vibr, vibr_cdev);

	if (atomic_read(&vibr->vibr_shutdown) || value == LED_OFF)
		atomic_set(&vibr->vibr_state, 0);
	else
		atomic_set(&vibr->vibr_state, 1);

	queue_work(vibr->vibr_queue, &vibr->vibr_work);
	return 0;
}

static const struct of_device_id vibr_of_ids[] = {
	{ .compatible = "regulator-vibrator", },
	{}
};

static int regulator_oc_event(struct notifier_block *nb,
	unsigned long event, void *data)
{
	struct reg_vibr *vibr = container_of(nb, struct reg_vibr, oc_handle);

	switch (event) {
	case REGULATOR_EVENT_OVER_CURRENT:
	case REGULATOR_EVENT_FAIL:
		pr_info("get regulator oc event: %lu", event);
		atomic_set(&vibr->vibr_state, 0);
		queue_work(vibr->vibr_queue, &vibr->vibr_work);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}
/*Penang code for EKPENAN4GU-2875 by zouxiaomin at 20240326 start*/
struct notifier_block vibrator_notif;

static int camera_notifier_callback(struct notifier_block *self,unsigned long event, void *data)
{
	if (CAM_EVENT_ON == event) {
		camera_detect_flag = CAM_EVENT_ON;
		printk("camera_notifier_callback,camera opened\n");
	} else if (CAM_EVENT_OFF == event) {
		camera_detect_flag = CAM_EVENT_OFF;
		printk("camera_notifier_callback,camera closed\n");
	}
	return 0;
}
  static ssize_t vib_dur_show(struct kobject *kobj,
  						struct kobj_attribute *attr, char *buf){
  	return sprintf(buf, "%lu\n", vibrator_duration);
  }
  static ssize_t vib_dur_store(struct kobject *kobj, struct kobj_attribute *attr,
  			  const char *cmd, size_t count)
  {
  	unsigned long state;
	ssize_t ret;

	ret = kstrtoul(cmd, 10, &state);
	if (ret)
		return ret;

	vibrator_duration = state;
	return count;
  }
  static struct kobj_attribute attr_vib_dur =
  	__ATTR(vib_dur, 0644, vib_dur_show, vib_dur_store);
/*Penang code for EKPENAN4GU-2875 by zouxiaomin at 20240326 end*/
static int vib_probe(struct platform_device *pdev)
{
	int ret;
	struct reg_vibr *m_vibr;
	int rets =0;

	pr_info("probe start +++");
	/*Penang code for EKPENAN4GU-2875 by zouxiaomin at 20240326 start*/
	vibrator_notif.notifier_call = camera_notifier_callback;
	rets = vibrator_register_client(&vibrator_notif);
	if (rets) {
		printk("Unable to register vibrator_notifier");
	}
	/*Penang code for EKPENAN4GU-2875 by zouxiaomin at 20240326 end*/
	m_vibr = devm_kzalloc(&pdev->dev, sizeof(struct reg_vibr), GFP_KERNEL);
	if (!m_vibr) {
		ret = -ENOMEM;
		goto err;
	}
	m_vibr->vibr_queue = create_singlethread_workqueue(VIB_DEVICE);
	if (!m_vibr->vibr_queue) {
		ret = -ENOMEM;
		pr_notice("unable to create workqueue!\n");
		goto err;
	}

	ret = mt_vibra_init_config(&pdev->dev, &m_vibr->vibr_conf);
	if (ret) {
		pr_notice("failed to parse devicetree(%d)!\n", ret);
		goto err;
	}

	INIT_WORK(&m_vibr->vibr_work, update_vibrator);
	atomic_set(&m_vibr->vibr_shutdown, 0);

	if (regulator_is_enabled(m_vibr->vibr_conf.reg))
		atomic_set(&m_vibr->reg_status, 1);
	else
		atomic_set(&m_vibr->reg_status, 0);

	ret = of_property_read_string(pdev->dev.of_node, "label",
		&(m_vibr->vibr_cdev.name));
	if (ret) {
		pr_notice("Error load dts: get regulator label return %d\n", ret);
		goto err;
	}

	m_vibr->vibr_cdev.brightness_set_blocking = regulator_vibrator_set;

	ret = devm_led_classdev_register(&pdev->dev, &m_vibr->vibr_cdev);
	if (ret < 0) {
		pr_info("led class register fail\n");
		goto err;
	}

	/* register oc notification for this regulator */
	m_vibr->oc_handle.notifier_call = regulator_oc_event;
	ret = devm_regulator_register_notifier(m_vibr->vibr_conf.reg,
		&m_vibr->oc_handle);
	if (ret)
		pr_info("regulator notifier request failed\n");

	platform_set_drvdata(pdev, m_vibr);
	ret = vibr_power_set(m_vibr);
	if (ret < 0) {
		pr_info("set voltage for regulator fail\n");
		goto err;
	}

/*Penang code for EKPENAN4GU-2875 by zouxiaomin at 20240326 start*/
	vib_dur_kobj = kobject_create_and_add("vib_duration", NULL);
	ret = sysfs_create_file(vib_dur_kobj, &attr_vib_dur.attr);
	if (ret < 0){
  		pr_err("create vib_duration attribute file fail\n");
  	}
/*Penang code for EKPENAN4GU-2875 by zouxiaomin at 20240326 end*/

	pr_info("probe success, end ---");
	return 0;

err:
	pr_notice("probe failed(%d), end ---!\n", ret);
	return ret;
}

static int __maybe_unused vib_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct reg_vibr *vibr = platform_get_drvdata(pdev);

	if (atomic_read(&vibr->vibr_state)) {
		atomic_set(&vibr->vibr_state, 0);
		vibr_disable(vibr);
		pr_notice("vibr disabled, enter suspend.");
	}

	return 0;
}

static int vib_remove(struct platform_device *pdev)
{
	struct reg_vibr *vibr = platform_get_drvdata(pdev);

	cancel_work_sync(&vibr->vibr_work);
	devm_led_classdev_unregister(&pdev->dev, &vibr->vibr_cdev);

	return 0;
}

static void vib_shutdown(struct platform_device *pdev)
{
	struct reg_vibr *vibr = platform_get_drvdata(pdev);

	pr_info("shutdown: enter!\n");

	atomic_set(&vibr->vibr_shutdown, 1);
	if (atomic_read(&vibr->vibr_state)) {
		atomic_set(&vibr->vibr_state, 0);
		pr_info("vibrator will disable!\n");
		vibr_disable(vibr);
	}
}

static SIMPLE_DEV_PM_OPS(vib_pm_ops, vib_suspend, NULL);
#define VIB_PM_OPS	(&vib_pm_ops)

static struct platform_driver vibrator_driver = {
	.probe = vib_probe,
	.remove = vib_remove,
	.shutdown = vib_shutdown,
	.driver = {
			.name = VIB_DEVICE,
			.pm = VIB_PM_OPS,
			.of_match_table = vibr_of_ids,
		   },
};

module_platform_driver(vibrator_driver);
MODULE_AUTHOR("Mediatek Corporation");
MODULE_DESCRIPTION("Regulator Vibrator Driver (VIB)");
MODULE_LICENSE("GPL");

