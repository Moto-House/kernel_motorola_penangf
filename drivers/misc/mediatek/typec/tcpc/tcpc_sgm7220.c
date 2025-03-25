/*
 * SG Micro sgm7220 Type-C Port Control Driver
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/semaphore.h>
#include <linux/pm_runtime.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/cpu.h>
#include <linux/version.h>
#include <linux/pm_wakeup.h>
#include <linux/sched/clock.h>
#include <uapi/linux/sched/types.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
//#include <linux/wakelock.h>
#include <linux/pm_wakeup.h>
#include "inc/pd_dbg_info.h"
#include "inc/tcpci.h"
#include "inc/tcpci_timer.h"
#include "inc/tcpci_typec.h"
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/hrtimer.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
#include <linux/sched/rt.h>
#endif

#define SGM7220_DRV_VERSION "20240223"
#define I2C_RETRIES 2
#define I2C_RETRY_DELAY 5 /* ms */
#define SGM7220_IRQ_WAKE_TIME 500 /* ms */
/* SGM7220 Register Map */
#define SGM7220_DEVICE_ID0_ADDR 0x00
#define SGM7220_DEVICE_ID1_ADDR 0x01
#define SGM7220_DEVICE_ID2_ADDR 0x02
#define SGM7220_DEVICE_ID3_ADDR 0x03
#define SGM7220_DEVICE_ID4_ADDR 0x04
#define SGM7220_DEVICE_ID5_ADDR 0x05
#define SGM7220_DEVICE_ID6_ADDR 0x06
#define SGM7220_DEVICE_ID7_ADDR 0x07
#define SGM7220_Reg08_ADDR 0x08
#define SGM7220_Reg09_ADDR 0x09
#define SGM7220_Reg0A_ADDR 0x0A
#define SGM7220_Reg45_ADDR 0x45

/* for Register08 */
#define ACTIVE_CABLE_SHIFT 0
#define ACTIVE_CABLE_MASK 0x01
#define ACCESSORY_CONNECTED_SHIFT 1
#define ACCESSORY_CONNECTED_MASK 0x07
#define CURRENT_MODE_DETECT_SHIFT 4
#define CURRENT_MODE_DETECT_MASK 0x03
#define CURRENT_MODE_ADVERTISE_SHIFT 6
#define CURRENT_MODE_ADVERTISE_MASK 0x03
/* for Register09 */
#define DISABLE_UFP_ACCESSORY 0x01
#define DRP_DUTY_CYCLE_SHIFT 1
#define DRP_DUTY_CYCLE_MASK 0x03
#define INTERRUPT_STATUS_SHIFT 4
#define INTERRUPT_STATUS_MASK 0x01
#define CABLE_DIR_SHIFT 5
#define CABLE_DIR_MASK 0x01
#define ATTACHED_STATE_SHIFT 6
#define ATTACHED_STATE_MASK 0x03
/* for Register0A */
#define DISABLE_TERM_SHIFT 0
#define DISABLE_TERM_MASK 0x01
#define SOURCE_PREF_SHIFT 1
#define SOURCE_PREF_MASK 0x03
#define I2C_SOFT_RESET_SHIFT 3
#define I2C_SOFT_RESET_MASK 0x01
#define MODE_SELECT_SHIFT 4
#define MODE_SELECT_MASK 0x03
#define DEBOUNCE_SHIFT 6
#define DEBOUNCE_MASK 0x03
/* for Register45 */
#define DISABLE_RD_RP_SHIFT 2
#define DISABLE_RD_RP_MASK 0x01

/* constants */
enum current_advertise_type {
	ADV_CUR_DEFAULT = 0, // default current: 500mA or 900mA
	ADV_CUR_1P5, // 1.5A
	ADV_CUR_3A, // 3A
};

enum current_detect_type {
	DET_CUR_DEFAULT = 0, // default
	DET_CUR_1P5, // Medium
	DET_CUR_ACCESSORY, // Charge through accessory - 500mA
	DET_CUR_3A, // High
};

enum accessory_connected_type {
	NO_ACCESSORY_ATTACHED = 0,
	AUDIO_ACCESSOYR = 4,
	AUDIO_CHARGED_THRU_ACCESSORY = 5,
	DEBUG_ACCESSORY = 6,
};

enum active_cable_attach_type { CABLE_NOT_ATTACHED = 0, CABLE_ATTACHED };

enum attached_state_type {
	NO_ATTACHED = 0,
	ATTACHED_DFP,
	ATTACHED_UFP,
	ATTACHED_TO_ACCESSORY
};

enum cable_dir_type { ORIENT_CC1 = 0, ORIENT_CC2 };

enum drp_duty_cycle_type {
	CYCLE_30 = 0,
	CYCLE_40,
	CYCLE_50,
	CYCLE_60,
};

enum mode_select_type {
	ACCORDING_TO_PORT = 0,
	MODE_UFP,
	MODE_DFP,
	MODE_DRP,
};

enum sourcec_pref_type {
	STANDARD_DRP = 0,
	DRP_TRY_SINK,
};

/* Type-C Attrs */
struct type_c_parameters {
	enum current_advertise_type current_advertise;
	enum current_detect_type current_detect;
	enum accessory_connected_type accessory_connected;
	enum active_cable_attach_type active_cable_attach;
	enum attached_state_type attached_state;
	enum cable_dir_type cable_dir;
	enum drp_duty_cycle_type drp_duty_cycle;
	enum mode_select_type mode_select;
	enum sourcec_pref_type sourcec_pref;
};

struct state_disorder_monitor {
	int count;
	int err_detected;
	enum attached_state_type former_state;
	unsigned long time_before;
};

typedef union { /* 0x08:  */
	uint8_t byte;
	struct {
		uint8_t Active_Cable_Detection : 1;
		uint8_t Accessory_Connect : 3;
		uint8_t Current_Mode_Detect : 2;
		uint8_t Current_Mode_Advertise : 2;
	} Bits_Reg08;
} Reg08_t;

typedef union { /* 0x09 */
	uint8_t byte;
	struct {
		uint8_t Disable_UFP_Accessory : 1;
		uint8_t DRP_Duty_Cycle : 2;
		uint8_t NU : 1;
		uint8_t Interrupt_Status : 1;
		uint8_t Cable_Dir : 1;
		uint8_t Attached_State : 2;
	} Bits_Reg09;
} Reg09_t;

typedef union { /* 0x0A */
	uint8_t byte;
	struct {
		uint8_t Disable_Termination : 1;
		uint8_t Source_Perform : 2;
		uint8_t IIC_Soft_Reset : 1;
		uint8_t Mode_Select : 2;
		uint8_t CC_Debounce : 2;
	} Bits_Reg0A;
} Reg0A_t;

typedef union { /* 0x45 */
	uint8_t byte;
	struct {
		uint8_t Reserved : 2;
		uint8_t Disable_RD_RP : 1;
		uint8_t NU : 5;
	} Bits_Reg45;
} Reg45_t;

typedef struct {
	Reg08_t Reg08;
	Reg09_t Reg09;
	Reg0A_t Reg0A;
	Reg45_t Reg45;
} Reg_Value_t;

enum BOARDID_CONFIG {
	PN_BOARDID_CONFIG_VA = 0,
	PN_BOARDID_CONFIG_VB = 1,
	PN_BOARDID_CONFIG_VC = 2,
	PN_BOARDID_CONFIG_VD = 3,
	PN_BOARDID_CONFIG_VE = 4,
	PN_BOARDID_CONFIG_VF = 5,
	PN_BOARDID_CONFIG_VG = 6,
	PN_BOARDID_CONFIG_VH = 7,
	PN_BOARDID_CONFIG_VI = 8,
	PN_BOARDID_CONFIG_VJ = 9,
	PN_BOARDID_CONFIG_VK = 10,
	PN_BOARDID_CONFIG_VL = 11,
	PN_BOARDID_CONFIG_VM = 12,
	PN_BOARDID_CONFIG_VN = 13,
	PN_BOARDID_CONFIG_VO = 14,
	PN_BOARDID_CONFIG_VP = 15,
	PN_BOARDID_CONFIG_VQ = 16,
	PN_BOARDID_CONFIG_VR = 17,
	PN_BOARDID_CONFIG_VS = 18,
};

struct sgm7220_chip {
	struct i2c_client *client;
	struct device *dev;
	struct tcpc_desc *tcpc_desc;
	struct tcpc_device *tcpc;
	struct kthread_worker irq_worker;
	struct kthread_work irq_work;
	struct task_struct *irq_worker_task;
	struct wakeup_source *irq_wake_lock;
	struct wakeup_source *i2c_wake_lock;
	struct semaphore suspend_lock;
	int irq_gpio;
	int irqnum;
	Reg_Value_t Registers;
	struct type_c_parameters type_c_param;
	struct state_disorder_monitor monitor;
	struct class *device_class;
};

static int sgm7220_read_reg(struct i2c_client *client, uint8_t reg,
				  uint8_t *readbuf)
{
	struct sgm7220_chip *chip = i2c_get_clientdata(client);
	int ret = 0;

#if 1
	__pm_stay_awake(chip->i2c_wake_lock);
	down(&chip->suspend_lock);
	ret = i2c_smbus_read_byte_data(client, reg);
	up(&chip->suspend_lock);
	__pm_relax(chip->i2c_wake_lock);

	if (ret < 0) {
		pr_err("%s: (0x%02x) error, (ret = %d)\n", __func__, reg, ret);
		return ret;
	}
	ret &= 0xff;
	*readbuf = ret;
#else
	sgm7220_read_regs(client, reg, readbuf, 1);
#endif

	return 0;
}

static int sgm7220_write_reg(struct i2c_client *client, uint8_t reg,
				   uint8_t value)
{
	struct sgm7220_chip *chip = i2c_get_clientdata(client);
	int ret = 0;

#if 1
	__pm_stay_awake(chip->i2c_wake_lock);
	down(&chip->suspend_lock);
	ret = i2c_smbus_write_byte_data(client, reg, value);
	up(&chip->suspend_lock);
	__pm_relax(chip->i2c_wake_lock);
#else
	uint8_t buf = 0;
	buf = value;
	ret = sgm7220_write_regs(client, reg, &buf, 1);
#endif

	if (ret < 0)
		pr_err("%s: (0x%02x) error, (ret = %d)\n", __func__, reg, ret);

	return ret;
}

static int sgm7220_read_config(struct i2c_client *client, uint8_t reg,
			       uint8_t mask, uint8_t shift, uint8_t *bitval)
{
	//	struct sgm7220_chip *chip = i2c_get_clientdata(client);
	int ret = 0;
	uint8_t regval;

	ret = sgm7220_read_reg(client, reg, &regval);
	if (ret < 0)
		pr_err("%s: (0x%02x) error, (ret = %d)\n", __func__, reg, ret);

	regval &= (mask << shift);
	*bitval = (regval >> shift);

	return ret;
}

static int sgm7220_update_config(struct i2c_client *client, uint8_t reg,
				 uint8_t mask, uint8_t shift, uint8_t bitval)
{
	//	struct sgm7220_chip *chip = i2c_get_clientdata(client);
	int ret = 0;
	uint8_t regval;

	sgm7220_read_reg(client, reg, &regval);

	regval &= ~(mask << shift);
	regval |= (bitval << shift);

	ret = sgm7220_write_reg(client, reg, regval);
	if (ret < 0)
		pr_err("%s: (0x%02x) error, (ret = %d)\n", __func__, reg, ret);

	return ret;
}

/* Detect non-DFP -> DFP changes that happen more than 3 times within 10s */
static void sgm7220_state_disorder_detect(struct sgm7220_chip *chip)
{
	unsigned long timeout;

	/* count the (non-DFP -> DFP) changes */
	if ((chip->monitor.former_state != chip->type_c_param.attached_state) &&
	    (chip->type_c_param.attached_state == ATTACHED_DFP)) {
		if (!chip->monitor.count) {
			chip->monitor.time_before = jiffies;
		}
		chip->monitor.count++;
	}

	/* store the state */
	chip->monitor.former_state = chip->type_c_param.attached_state;

	if (chip->monitor.count > 3) {
		timeout = msecs_to_jiffies(10 * 1000); /* 10 seconds */
		if (time_before(jiffies, chip->monitor.time_before + timeout)) {
			chip->monitor.err_detected = 1;
			/* disable id irq before qpnp react to cc chip's id output */
			// interfere_id_irq_from_usb(0);
		}
		chip->monitor.count = 0;
	}

	if ((chip->type_c_param.attached_state == NO_ATTACHED) &&
	    chip->monitor.err_detected) {
		/* enable id irq */
		// interfere_id_irq_from_usb(1);
		chip->monitor.err_detected = 0;
	}
}

static int sgm7220_process_interrupt_register(struct sgm7220_chip *chip)
{
	struct tcpc_device *tcpc = chip->tcpc;
	uint8_t bitval = 0;
	int ret = 0;

	/* check attach state */
	ret = sgm7220_read_config(chip->client, SGM7220_Reg09_ADDR,
				  ATTACHED_STATE_MASK, ATTACHED_STATE_SHIFT,
				  &bitval);
	if (ret < 0) {
		pr_err("%s: read attach_state fail!\n", __func__);
		return ret;
	}
	chip->type_c_param.attached_state = bitval;

	/* when as DFP, check whether cable is active */
	ret = sgm7220_read_config(chip->client, SGM7220_Reg08_ADDR,
				  ACTIVE_CABLE_MASK, ACTIVE_CABLE_SHIFT,
				  &bitval);
	if (ret < 0) {
		pr_err("%s: read active_cable fail!\n", __func__);
		return ret;
	}
	chip->type_c_param.active_cable_attach = bitval;

	/* when as UFP, check the current detection */
	ret = sgm7220_read_config(chip->client, SGM7220_Reg08_ADDR,
				  CURRENT_MODE_DETECT_MASK,
				  CURRENT_MODE_DETECT_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read current_detection fail!\n", __func__);
		return ret;
	}
	chip->type_c_param.current_detect = bitval;

	/* when connect accessory, check the type of accessory */
	ret = sgm7220_read_config(chip->client, SGM7220_Reg08_ADDR,
				  ACCESSORY_CONNECTED_MASK,
				  ACCESSORY_CONNECTED_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read accessory_connected fail!\n", __func__);
		return ret;
	}
	chip->type_c_param.accessory_connected = bitval;

	/* in case configured as DRP may detect some non-standard SDP */
	/* chargers as UFP, which may lead to a cyclic switching of DFP */
	/* and UFP on state detection result. */
	sgm7220_state_disorder_detect(chip);

	/* check cable dir */
	ret = sgm7220_read_config(chip->client, SGM7220_Reg09_ADDR,
				  CABLE_DIR_MASK, CABLE_DIR_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}
	chip->type_c_param.cable_dir = bitval;
	tcpc->typec_polarity = chip->type_c_param.cable_dir;
	pr_info("%s: type=%d, cable_dir(%d)\n", __func__,
		chip->type_c_param.attached_state, tcpc->typec_polarity);
	//pr_info("%s: attach_new=%d, attach_old=%d\n", __func__, tcpc->typec_attach_new, tcpc->typec_attach_old);

	switch (chip->type_c_param.attached_state) {
	case NO_ATTACHED:
		tcpc->typec_attach_new = TYPEC_UNATTACHED;
		if (tcpc->typec_attach_old == TYPEC_ATTACHED_SRC) {
			tcpci_source_vbus(tcpc, TCP_VBUS_CTRL_TYPEC,
					  TCPC_VBUS_SOURCE_0V, 0);
		}
		pr_info("%s: NO_ATTACHED attach_new=%d, attach_old=%d\n", __func__,
			tcpc->typec_attach_new, tcpc->typec_attach_old);
		tcpci_notify_typec_state(tcpc);
		tcpc->typec_attach_old = TYPEC_UNATTACHED;
		tcpc->typec_attach_new = TYPEC_UNATTACHED;

		ret = sgm7220_update_config(chip->client, SGM7220_Reg0A_ADDR,
					    MODE_SELECT_MASK, MODE_SELECT_SHIFT,
					    MODE_DRP);
		if (ret < 0) {
			pr_err("%s: force DRP mode fail!\n", __func__);
		}
		break;

	case ATTACHED_DFP:
		if (tcpc->typec_attach_new != TYPEC_ATTACHED_SRC) {
			tcpc->typec_attach_new = TYPEC_ATTACHED_SRC;
			if (chip->type_c_param.active_cable_attach ==
			    CABLE_ATTACHED) {
				chip->tcpc_desc->rp_lvl = TYPEC_CC_RP_3_0;
				tcpci_source_vbus(tcpc, TCP_VBUS_CTRL_TYPEC,
						  TCPC_VBUS_SOURCE_5V, -1);
			} else {
				chip->tcpc_desc->rp_lvl = TYPEC_CC_RP_DFT;
				tcpci_source_vbus(tcpc, TCP_VBUS_CTRL_TYPEC,
						  TCPC_VBUS_SOURCE_5V, -1);
			}
			pr_info("%s: ATTACHED_SRC attach_new=%d, attach_old=%d\n", __func__,
				tcpc->typec_attach_new, tcpc->typec_attach_old);
			tcpci_notify_typec_state(tcpc);
			tcpc->typec_attach_old = TYPEC_ATTACHED_SRC;
			tcpc->typec_attach_new = TYPEC_UNATTACHED;
		}
		break;

	case ATTACHED_UFP:
		if (tcpc->typec_attach_new != TYPEC_ATTACHED_SNK) {
			tcpc->typec_attach_new = TYPEC_ATTACHED_SNK;
			if (chip->type_c_param.current_detect ==
			    DET_CUR_DEFAULT)
				tcpc->typec_remote_rp_level =
					TYPEC_CC_VOLT_SNK_DFT;
			else if (chip->type_c_param.current_detect ==
				 DET_CUR_1P5)
				tcpc->typec_remote_rp_level =
					TYPEC_CC_VOLT_SNK_1_5;
			else if (chip->type_c_param.current_detect ==
				 DET_CUR_3A)
				tcpc->typec_remote_rp_level =
					TYPEC_CC_VOLT_SNK_3_0;
			else
				tcpc->typec_remote_rp_level =
					TYPEC_CC_VOLT_SNK_DFT;
			// tcpci_sink_vbus(tcpc, TCP_VBUS_CTRL_TYPEC, TCPC_VBUS_SINK_5V, -1);
			pr_info("%s: ATTACHED_SNK attach_new=%d, attach_old=%d\n", __func__,
				tcpc->typec_attach_new, tcpc->typec_attach_old);
			tcpci_notify_typec_state(tcpc);
			tcpc->typec_attach_old = TYPEC_ATTACHED_SNK;
			tcpc->typec_attach_new = TYPEC_UNATTACHED;
		}
		break;

	case ATTACHED_TO_ACCESSORY:
		if (tcpc->typec_attach_new != TYPEC_ATTACHED_AUDIO &&
		    (chip->type_c_param.accessory_connected ==
			     AUDIO_CHARGED_THRU_ACCESSORY ||
		     chip->type_c_param.accessory_connected ==
			     AUDIO_ACCESSOYR)) {
			if (chip->type_c_param.accessory_connected ==
				    AUDIO_CHARGED_THRU_ACCESSORY &&
			    chip->type_c_param.current_detect ==
				    DET_CUR_ACCESSORY) {
				/* this moment, sink device could pull a maximum current of 500mA  */
				// tcpci_sink_vbus(tcpc, TCP_VBUS_CTRL_TYPEC, TCPC_VBUS_SINK_5V, 500);
			}
			tcpc->typec_attach_new = TYPEC_ATTACHED_AUDIO;
			pr_info("%s: attach_new=%d, attach_old=%d\n", __func__,
				tcpc->typec_attach_new, tcpc->typec_attach_old);
			tcpci_notify_typec_state(tcpc);
			tcpc->typec_attach_old = TYPEC_ATTACHED_AUDIO;
			tcpc->typec_attach_new = TYPEC_UNATTACHED;
		} else if (tcpc->typec_attach_new != TYPEC_ATTACHED_DEBUG &&
			   chip->type_c_param.accessory_connected ==
				   DEBUG_ACCESSORY) {
			tcpc->typec_attach_new = TYPEC_ATTACHED_DEBUG;
			pr_info("%s: attach_new=%d, attach_old=%d\n", __func__,
				tcpc->typec_attach_new, tcpc->typec_attach_old);
			tcpci_notify_typec_state(tcpc);
			tcpc->typec_attach_old = TYPEC_ATTACHED_DEBUG;
			tcpc->typec_attach_new = TYPEC_UNATTACHED;
		}
		break;

	default:
		pr_err("%s: Unknown type[0x%02x]\n", __func__,
		       chip->type_c_param.attached_state);
		break;
	}
	return 0;
}

static void sgm7220_irq_work_handler(struct kthread_work *work)
{
	struct sgm7220_chip *chip =
		container_of(work, struct sgm7220_chip, irq_work);
	uint8_t bitval = 0;
	int ret = 0;

	tcpci_lock_typec(chip->tcpc);


	/* Reset INT port */
	ret = sgm7220_read_config(chip->client, SGM7220_Reg09_ADDR,
				  INTERRUPT_STATUS_MASK, INTERRUPT_STATUS_SHIFT,
				  &bitval);
	if (ret < 0)
		pr_err("%s read INT bit fail!\n", __func__);

	sgm7220_process_interrupt_register(chip);

	ret = sgm7220_update_config(chip->client, SGM7220_Reg09_ADDR,
				    INTERRUPT_STATUS_MASK,
				    INTERRUPT_STATUS_SHIFT, 0x01);
	if (ret < 0)
		pr_err("%s: Reset INT bit fail!\n", __func__);

	tcpci_unlock_typec(chip->tcpc);
}

static irqreturn_t sgm7220_irq_handler(int irq, void *handle)
{
	struct sgm7220_chip *chip = (struct sgm7220_chip *)handle;

	__pm_wakeup_event(chip->irq_wake_lock, SGM7220_IRQ_WAKE_TIME);

	kthread_queue_work(&chip->irq_worker, &chip->irq_work);
	return IRQ_HANDLED;
}

static int sgm7220_init_alert(struct tcpc_device *tcpc)
{
	struct sgm7220_chip *chip = tcpc_get_dev_data(tcpc);
	struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };
	int ret = 0;
	char *name;
	int len;

	len = strlen(chip->tcpc_desc->name);
	name = devm_kzalloc(chip->dev, len + 5, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	snprintf(name, PAGE_SIZE, "%s-IRQ", chip->tcpc_desc->name);
	if (ret < 0 || ret >= PAGE_SIZE)
		pr_info("%s-%d, snprintf fail\n", __func__, __LINE__);

	pr_info("%s name = %s, gpio = %d\n", __func__, chip->tcpc_desc->name,
		chip->irq_gpio);

	if (!gpio_is_valid(chip->irq_gpio)) {
		pr_err("%s: irq gpio is invalid\n", __func__);
		return ret;
	}

	ret = devm_gpio_request(chip->dev, chip->irq_gpio, name);
	if (ret < 0) {
		pr_err("%s: irq gpio%d request failed (ret = %d)\n", __func__,
		       chip->irq_gpio, ret);
		return ret;
	}

	ret = gpio_direction_input(chip->irq_gpio);
	if (ret < 0) {
		pr_err("%s: failes to set GPIO%d as input pin (ret = %d\n)",
		       __func__, chip->irq_gpio, ret);
		goto err_irq_gpio_dir;
	}

	chip->irqnum = gpio_to_irq(chip->irq_gpio);
	if (chip->irqnum <= 0) {
		pr_err("%s gpio to irq fail, chip->irqnum = %d\n", __func__,
		       chip->irqnum);
	}

	pr_info("%s : IRQ number = %d\n", __func__, chip->irqnum);

	kthread_init_worker(&chip->irq_worker);
	chip->irq_worker_task =
		kthread_run(kthread_worker_fn, &chip->irq_worker, "%s",
			    chip->tcpc_desc->name);
	if (IS_ERR(chip->irq_worker_task))
		pr_err("%s: Could not create tcpc task\n", __func__);

	sched_setscheduler(chip->irq_worker_task, SCHED_FIFO, &param);
	kthread_init_work(&chip->irq_work, sgm7220_irq_work_handler);
	ret = request_irq(chip->irqnum, sgm7220_irq_handler,
			  IRQF_TRIGGER_FALLING | IRQF_NO_THREAD |
				  IRQF_NO_SUSPEND,
			  name, chip);
	if (ret < 0)
		pr_err("%s: error failed to request IRQ (ret = %d)\n", __func__,
		       chip->irqnum);

	enable_irq_wake(chip->irqnum);
	return 0;

err_irq_gpio_dir:
	if (gpio_is_valid(chip->irq_gpio))
		gpio_free(chip->irq_gpio);
	return ret;
}

static int sgm7220_config_initialization(struct sgm7220_chip *chip)
{
	int ret = 0;
	/* do initialization here, before enable irq,
	 * clear irq,
	 * config DRP/UFP/DFP mode,
	 * and etc..
	 */
	pr_info("%s enter \n", __func__);

	ret = sgm7220_update_config(chip->client, SGM7220_Reg0A_ADDR,
				    MODE_SELECT_MASK, MODE_SELECT_SHIFT,
				    chip->type_c_param.mode_select);
	if (ret < 0) {
		pr_err("%s: init mode_select fail!\n", __func__);
		return ret;
	}

	ret = sgm7220_update_config(chip->client, SGM7220_Reg0A_ADDR,
				    SOURCE_PREF_MASK, SOURCE_PREF_SHIFT,
				    chip->type_c_param.sourcec_pref);
	if (ret < 0) {
		pr_err("%s: init sourcec_pref fail!\n", __func__);
		return ret;
	}

	ret = sgm7220_update_config(chip->client, SGM7220_Reg08_ADDR,
				    CURRENT_MODE_ADVERTISE_MASK,
				    CURRENT_MODE_ADVERTISE_SHIFT,
				    chip->type_c_param.current_advertise);
	if (ret < 0) {
		pr_err("%s: init current_advertise fail!\n", __func__);
		return ret;
	}

	/* if sgm7220 is connected to certain device before powerup, then after powering up, the INT PIN would be set low,
	   This results that when unplug the device, no interruption event occur.
	   At this moment, Updating the bit as '0x01' would set INT PIN high.  
	*/
	ret = sgm7220_update_config(chip->client, SGM7220_Reg09_ADDR,
				    INTERRUPT_STATUS_MASK,
				    INTERRUPT_STATUS_SHIFT, 0x01);
	if (ret < 0) {
		pr_err("%s: Reset INT bit fail!\n", __func__);
		return ret;
	}

	sgm7220_read_reg(chip->client, SGM7220_Reg08_ADDR,
			       &chip->Registers.Reg08.byte);
	sgm7220_read_reg(chip->client, SGM7220_Reg09_ADDR,
			       &chip->Registers.Reg09.byte);
	sgm7220_read_reg(chip->client, SGM7220_Reg0A_ADDR,
			       &chip->Registers.Reg0A.byte);
	sgm7220_read_reg(chip->client, SGM7220_Reg45_ADDR,
			       &chip->Registers.Reg45.byte);
	pr_info("%s: sgm7220 config initatial finished! reg08:0x%02x, reg09:0x%02x, reg0A:0x%02x, reg45:0x%02x\n",
		__func__, chip->Registers.Reg08.byte,
		chip->Registers.Reg09.byte, chip->Registers.Reg0A.byte,
		chip->Registers.Reg45.byte);

	return 0;
}

static int sgm7220_tcpc_init(struct tcpc_device *tcpc, bool sw_reset)
{
	struct sgm7220_chip *chip = tcpc_get_dev_data(tcpc);
	int ret = 0;

	pr_info("%s enter \n", __func__);

	if (sw_reset) {
		ret = sgm7220_update_config(chip->client, SGM7220_Reg0A_ADDR,
					    I2C_SOFT_RESET_MASK,
					    I2C_SOFT_RESET_SHIFT, 1);
		if (ret < 0)
			pr_err("%s: I2C_SOFT_RESET fail! (ret = %d)\n",
			       __func__, ret);
	}

	switch (chip->tcpc_desc->role_def) {
	case TYPEC_ROLE_SNK:
		chip->type_c_param.mode_select = MODE_UFP;
		break;
	case TYPEC_ROLE_SRC:
		chip->type_c_param.mode_select = MODE_DFP;
		break;
	case TYPEC_ROLE_DRP:
		chip->type_c_param.mode_select = MODE_DRP;
		chip->type_c_param.sourcec_pref = STANDARD_DRP;
		break;
	case TYPEC_ROLE_TRY_SNK:
		chip->type_c_param.mode_select = MODE_DRP;
		chip->type_c_param.sourcec_pref = DRP_TRY_SINK;
		break;
	default:
		break;
	}

	switch (chip->tcpc_desc->rp_lvl) {
	case TYPEC_CC_RP_DFT:
		chip->type_c_param.current_advertise = ADV_CUR_DEFAULT;
		break;
	case TYPEC_CC_RP_1_5:
		chip->type_c_param.current_advertise = ADV_CUR_1P5;
		break;
	case TYPEC_CC_RP_3_0:
		chip->type_c_param.current_advertise = ADV_CUR_3A;
		break;
	default:
		break;
	}

	ret = sgm7220_config_initialization(chip);
	if (ret < 0) {
		pr_err("%s: fails to do initialization (ret = %d)\n", __func__,
		       ret);
	}

	/* reset the INT port */
	// tcpci_alert_status_clear(tcpc, 0xffffffff);  /* the second param could be filled in with any value */

	return 0;
}

static int sgm7220_alert_status_clear(struct tcpc_device *tcpc, uint32_t mask)
{
	// struct sgm7220_chip *chip = tcpc_get_dev_data(tcpc);
	// int ret = 0;

	pr_info("%s enter \n", __func__);
	// sgm7220_process_interrupt_register(chip);

	// ret = sgm7220_update_config(chip->client, SGM7220_Reg09_ADDR, INTERRUPT_STATUS_MASK,
	// 															  INTERRUPT_STATUS_SHIFT, 0x01);
	// if (ret < 0)
	// 	pr_err("%s: update reg fail!\n", __func__);

	return 0;
}

static int sgm7220_fault_status_clear(struct tcpc_device *tcpc, uint8_t status)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static int sgm7220_get_alert_mask(struct tcpc_device *tcpc, uint32_t *mask)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static int sgam7220_get_alert_status(struct tcpc_device *tcpc, uint32_t *alert)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static int sgm7220_get_power_status(struct tcpc_device *tcpc,
				    uint16_t *pwr_status)
{
	pr_info("%s enter \n", __func__);
	*pwr_status = 0;
	return 0;
}

static int sgm7220_get_fault_status(struct tcpc_device *tcpc, uint8_t *status)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static int sgm7220_get_cc(struct tcpc_device *tcpc, int *cc1, int *cc2)
{

	struct sgm7220_chip *chip = tcpc_get_dev_data(tcpc);
	int *active_cc, *secondary_cc;

	*cc1 = TYPEC_CC_VOLT_OPEN;
	*cc2 = TYPEC_CC_VOLT_OPEN;

	if (chip->type_c_param.attached_state == NO_ATTACHED) {
		pr_debug("cc not attached\n");
		return 0;
	}

	pr_info("%s enter \n", __func__);
	active_cc = (chip->type_c_param.cable_dir)? cc2 : cc1;
	secondary_cc = (chip->type_c_param.cable_dir)? cc1 : cc2;

	if (chip->type_c_param.attached_state == ATTACHED_DFP) {
		switch (chip->type_c_param.current_detect) {
		case 0: /* RP Default */
			*active_cc |= TYPEC_CC_VOLT_SNK_DFT;
			break;
		case 1: /* RP 1.5V */
			*active_cc |= TYPEC_CC_VOLT_SNK_1_5;
			break;
		case 3: /* RP 3.0V */
			*active_cc |= TYPEC_CC_VOLT_SNK_3_0;
			break;
		default:
			*active_cc |= TYPEC_CC_VOLT_SNK_DFT;
			break;
		}
	} else if (chip->type_c_param.attached_state == ATTACHED_TO_ACCESSORY) {
		switch (chip->type_c_param.accessory_connected) {
		case 4: /* audio accessory */
			*active_cc |= TYPEC_CC_VOLT_RA;
			*secondary_cc |= TYPEC_CC_VOLT_RA;
			break;
		case 5: /* charged thru audio accessory */
			*active_cc |= TYPEC_CC_VOLT_RA;
			*secondary_cc |= TYPEC_CC_VOLT_RA;
			break;
		case 6: /* DFP, debug accessory */
			*active_cc |= TYPEC_CC_VOLT_RD;
			*secondary_cc |= TYPEC_CC_VOLT_RD;
			break;
		default:
			break;
		}
	} else if (chip->type_c_param.attached_state == ATTACHED_UFP) {
		if (chip->type_c_param.active_cable_attach) {
			*active_cc |= TYPEC_CC_VOLT_RA;
			*secondary_cc |= TYPEC_CC_VOLT_RD;
		} else {
			*active_cc |= TYPEC_CC_VOLT_RD;
		}
	}
	return 0;
}

static int sgm7220_set_cc(struct tcpc_device *tcpc, int pull)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static int sgm7220_set_polarity(struct tcpc_device *tcpc, int polarity)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static int sgm7220_set_low_rp_duty(struct tcpc_device *tcpc, bool low_rp)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static int sgm7220_set_vconn(struct tcpc_device *tcpc, int enable)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static int sgm7220_tcpc_deinit(struct tcpc_device *tcpc)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static int sgm7220_set_watchdog(struct tcpc_device *tcpc, bool en)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static struct tcpc_ops sgm7220_tcpc_ops = {
	.init = sgm7220_tcpc_init,
	//int (*init_alert_mask)(struct tcpc_device *tcpc),
	.alert_status_clear = sgm7220_alert_status_clear,
	.fault_status_clear = sgm7220_fault_status_clear,
	//int (*set_alert_mask)(struct tcpc_device *tcpc, uint32_t mask),
	.get_alert_mask = sgm7220_get_alert_mask,
	.get_alert_status = sgam7220_get_alert_status,
	.get_power_status = sgm7220_get_power_status,
	.get_fault_status = sgm7220_get_fault_status,
	.get_cc = sgm7220_get_cc,
	.set_cc = sgm7220_set_cc,
	.set_polarity = sgm7220_set_polarity,
	.set_low_rp_duty = sgm7220_set_low_rp_duty,
	.set_vconn = sgm7220_set_vconn,
	.deinit = sgm7220_tcpc_deinit,
	//int (*alert_vendor_defined_handler)(struct tcpc_device *tcpc),
	.set_watchdog = sgm7220_set_watchdog,
};

static int sgm7220_parse_dt(struct sgm7220_chip *chip, struct device *dev)
{
	struct device_node *np = dev->of_node;
	int ret = 0;

	pr_info("%s\n", __func__);

	if (!np)
		return -EINVAL;

#if (!defined(CONFIG_MTK_GPIO) || defined(CONFIG_MTK_GPIOLIB_STAND))
	ret = of_get_named_gpio(np, "sgm7220,irq-gpio", 0);
	if (ret < 0) {
		pr_err("%s: error invalid irq gpio err (ret = %d)\n", __func__,
		       ret);
		return ret;
	}
	chip->irq_gpio = ret;
#else
	ret = of_property_read_u32(np, "sgm7220,irq-gpio", &chip->irq_gpio);
	if (ret < 0) {
		pr_err("%s no sgm7220,irq_gpio info %d\n", __func__, ret);
		return ret;
	}
#endif
	return ret;
}

static int sgm7220_tcpcdev_init(struct sgm7220_chip *chip, struct device *dev)
{
	struct tcpc_desc *desc;
	struct device_node *np = dev->of_node;
	u32 val, len;
	const char *name = "type_c_port0";
	if (!np)
		return -EINVAL;

	pr_info("%s enter\n", __func__);

	desc = devm_kzalloc(dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	if (of_property_read_u32(np, "sgm7220-tcpc,role_def", &val) >= 0) {
		if (val >= TYPEC_ROLE_NR)
			desc->role_def = TYPEC_ROLE_DRP;
		else
			desc->role_def = val;
	} else {
		dev_info(dev, "use default Role DRP_TRY_SNK\n");
		desc->role_def = TYPEC_ROLE_TRY_SNK;
	}

	if (of_property_read_u32(np, "sgm7220-tcpc,notifier_supply_num",
				 &val) >= 0) {
		if (val < 0)
			desc->notifier_supply_num = 0;
		else
			desc->notifier_supply_num = val;
	} else
		desc->notifier_supply_num = 0;

	if (of_property_read_u32(np, "sgm7220-tcpc,rp_level", &val) >= 0) {
		switch (val) {
		case 0: /* RP Default */
			desc->rp_lvl = TYPEC_CC_RP_DFT;
			break;
		case 1: /* RP 1.5V */
			desc->rp_lvl = TYPEC_CC_RP_1_5;
			break;
		case 2: /* RP 3.0V */
			desc->rp_lvl = TYPEC_CC_RP_3_0;
			break;
		default:
			break;
		}
	}

#ifdef CONFIG_TCPC_VCONN_SUPPLY_MODE
	if (of_property_read_u32(np, "sgm7220-tcpc,vconn_supply", &val) >= 0) {
		if (val >= TCPC_VCONN_SUPPLY_NR)
			desc->vconn_supply = TCPC_VCONN_SUPPLY_ALWAYS;
		else
			desc->vconn_supply = val;
	} else {
		dev_info(dev, "use default Vconn Supply\n");
		desc->vconn_supply = TCPC_VCONN_SUPPLY_ALWAYS;
	}
#endif /* CONFIG_TCPC_VCONN_SUPPLY_MODE */

	of_property_read_string(np, "sgm7220-tcpc,name", (char const **)&name);
	len = strlen(name);
	desc->name = kzalloc(len + 1, GFP_KERNEL);
	if (!desc->name)
		return -ENOMEM;
	strncpy((char *)desc->name, name, strlen(name) + 1);

	chip->tcpc_desc = desc;
	chip->tcpc = tcpc_device_register(dev, desc, &sgm7220_tcpc_ops, chip);
	if (IS_ERR(chip->tcpc))
		return -EINVAL;

	chip->tcpc->typec_attach_old = TYPEC_UNATTACHED;
	chip->tcpc->typec_attach_new = TYPEC_UNATTACHED;
	//tcpci_report_usb_port_changed(chip->tcpc);

	pr_info("%s end\n", __func__);
	return 0;
}

static int sgm7220_check_revision(struct i2c_client *client)
{
	int ret = 0;

	ret = i2c_smbus_read_byte_data(client, SGM7220_DEVICE_ID6_ADDR);
	if (ret < 0) {
		dev_err(&client->dev, "read chip ID fail\n");
		return -EIO;
	}
	if (ret != 0x54) {
		pr_info("%s failed, reg06 = 0x%04x\n", __func__, ret);
		return -ENODEV;
	}

	ret = i2c_smbus_read_byte_data(client, SGM7220_DEVICE_ID5_ADDR);
	if (ret < 0) {
		dev_err(&client->dev, "read chip ID fail\n");
		return -EIO;
	}
	if (ret != 0x55) {
		pr_info("%s failed, reg05 = 0x%04x\n", __func__, ret);
		return -ENODEV;
	}

	ret = i2c_smbus_read_byte_data(client, SGM7220_DEVICE_ID4_ADDR);
	if (ret < 0) {
		dev_err(&client->dev, "read chip ID fail\n");
		return -EIO;
	}
	if (ret != 0x53) {
		pr_info("%s failed, reg04 = 0x%04x\n", __func__, ret);
		return -ENODEV;
	}

	return 0;
}
/*================================ Encapsulate interface functions (end)=================================*/
/*=======================================================================================================*/
/*===================================== create device attribution =======================================*/
static ssize_t current_advertise_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint8_t bitval = 0;
	int ret = 0;

	ret = sgm7220_read_config(chip->client, SGM7220_Reg08_ADDR,
				  CURRENT_MODE_ADVERTISE_MASK,
				  CURRENT_MODE_ADVERTISE_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}

	if (ADV_CUR_DEFAULT == bitval)
		return snprintf(
			buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
			"Default (500mA/900mA) initial value at startup");
	else if (ADV_CUR_1P5 == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"Medium (1.5A)");
	else if (ADV_CUR_3A == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"High (3A)");
	else
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"unknow");
}

static ssize_t current_advertise_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint bitval = 0;
	int ret = 0;

	if (kstrtouint(buf, 0, &bitval))
		return -EINVAL;

	ret = sgm7220_update_config(chip->client, SGM7220_Reg08_ADDR,
				    CURRENT_MODE_ADVERTISE_MASK,
				    CURRENT_MODE_ADVERTISE_SHIFT,
				    (uint8_t)bitval);
	if (ret < 0) {
		pr_err("%s: update reg fail!\n", __func__);
	}
	return count;
}

static ssize_t current_detect_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint8_t bitval = 0;
	int ret = 0;

	ret = sgm7220_read_config(chip->client, SGM7220_Reg08_ADDR,
				  CURRENT_MODE_DETECT_MASK,
				  CURRENT_MODE_DETECT_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}

	if (DET_CUR_DEFAULT == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"default current, 500 or 900mA");
	else if (DET_CUR_1P5 == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"middle current, 1.5A");
	else if (DET_CUR_3A == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"high current, 3A");
	else
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"unknow");
}

static ssize_t accessory_connected_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint8_t bitval = 0;
	int ret = 0;

	ret = sgm7220_read_config(chip->client, SGM7220_Reg08_ADDR,
				  ACCESSORY_CONNECTED_MASK,
				  ACCESSORY_CONNECTED_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}

	if (NO_ACCESSORY_ATTACHED == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"No accessory attached");
	else if (AUDIO_ACCESSOYR == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"Audio accessory");
	else if (AUDIO_CHARGED_THRU_ACCESSORY == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"Audio charged thru accessory");
	else if (DEBUG_ACCESSORY == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"Debug accessory");
	else
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"unknow");
}

static ssize_t active_cable_det_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint8_t bitval = 0;
	int ret = 0;

	ret = sgm7220_read_config(chip->client, SGM7220_Reg08_ADDR,
				  ACTIVE_CABLE_MASK, ACTIVE_CABLE_SHIFT,
				  &bitval);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}

	if (CABLE_NOT_ATTACHED == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"Not cable attached");
	else if (CABLE_ATTACHED == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"Active cable attached");
	else
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"unknow");
}

static ssize_t attached_state_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint8_t bitval = 0;
	int ret = 0;

	ret = sgm7220_read_config(chip->client, SGM7220_Reg09_ADDR,
				  ATTACHED_STATE_MASK, ATTACHED_STATE_SHIFT,
				  &bitval);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}

	if (NO_ATTACHED == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"Not attached (default)");
	else if (ATTACHED_DFP == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"Attached.SRC (DFP)");
	else if (ATTACHED_UFP == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"Attached.SNK (UFP)");
	else if (ATTACHED_TO_ACCESSORY == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"Attached to an accessory");
	else
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"unknow");
}

static ssize_t cable_dir_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint8_t bitval = 0;
	int ret = 0;

	ret = sgm7220_read_config(chip->client, SGM7220_Reg09_ADDR,
				  CABLE_DIR_MASK, CABLE_DIR_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}

	if (ORIENT_CC1 == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"ORIENT_CC1");
	else if (ORIENT_CC2 == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"ORIENT_CC2");
	else
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"unknow");
}

static ssize_t drp_duty_cycle_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint8_t bitval = 0;
	int ret = 0;

	ret = sgm7220_read_config(chip->client, SGM7220_Reg09_ADDR,
				  DRP_DUTY_CYCLE_MASK, DRP_DUTY_CYCLE_SHIFT,
				  &bitval);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}

	if (CYCLE_30 == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"DRP_CYCLE_30");
	else if (CYCLE_40 == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"DRP_CYCLE_40");
	else if (CYCLE_50 == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"DRP_CYCLE_50");
	else if (CYCLE_60 == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"DRP_CYCLE_60");
	else
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"unknow");
}

static ssize_t drp_duty_cycle_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint bitval = 0;
	int ret = 0;

	if (kstrtouint(buf, 0, &bitval))
		return -EINVAL;

	ret = sgm7220_update_config(chip->client, SGM7220_Reg09_ADDR,
				    DRP_DUTY_CYCLE_MASK, DRP_DUTY_CYCLE_SHIFT,
				    (uint8_t)bitval);
	if (ret < 0) {
		pr_err("%s: update reg fail!\n", __func__);
	}
	return count;
}

static ssize_t mode_select_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint8_t bitval = 0;
	int ret = 0;

	ret = sgm7220_read_config(chip->client, SGM7220_Reg0A_ADDR,
				  MODE_SELECT_MASK, MODE_SELECT_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}

	if (ACCORDING_TO_PORT == bitval)
		return snprintf(
			buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
			"Maintain mode according to PORT pin selection (default)");
	else if (MODE_UFP == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"UFP mode (unattached.SNK)");
	else if (MODE_DFP == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"DFP mode (unattached.SRC)");
	else if (MODE_DRP == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"DRP mode (start from unattached.SNK)");
	else
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval,
				"unknow");
}

static ssize_t mode_select_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint bitval = 0;
	int ret = 0;

	if (kstrtouint(buf, 0, &bitval))
		return -EINVAL;

	ret = sgm7220_update_config(chip->client, SGM7220_Reg0A_ADDR,
				    MODE_SELECT_MASK, MODE_SELECT_SHIFT,
				    (uint8_t)bitval);
	if (ret < 0) {
		pr_err("%s: update reg fail!\n", __func__);
	}
	return count;
}

static ssize_t show_registers(struct device *dev,
                struct device_attribute *attr, char *buf)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	u8 val;
	u8 tmpbuf[PAGE_SIZE];
	int len;
	int idx = 0;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sgm7220");

	sgm7220_read_reg(chip->client, 0x08, &val);
	len = snprintf(tmpbuf, PAGE_SIZE, "Reg[0x08] = 0x%.2x\n", val);
	memcpy(&buf[idx], tmpbuf, len);
	idx += len;
	sgm7220_read_reg(chip->client, 0x09, &val);
	len = snprintf(tmpbuf, PAGE_SIZE, "Reg[0x09] = 0x%.2x\n", val);
	memcpy(&buf[idx], tmpbuf, len);
	idx += len;
	sgm7220_read_reg(chip->client, 0x0A, &val);
	len = snprintf(tmpbuf, PAGE_SIZE, "Reg[0x0A] = 0x%.2x\n", val);
	memcpy(&buf[idx], tmpbuf, len);
	idx += len;
	sgm7220_read_reg(chip->client, 0x45, &val);
	len = snprintf(tmpbuf, PAGE_SIZE, "Reg[0x45] = 0x%.2x\n", val);
	memcpy(&buf[idx], tmpbuf, len);
	idx += len;

	return idx;
}

static ssize_t store_register(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if(ret == 2 && (reg == 0x08 || reg == 0x09 || reg == 0x0a || reg == 0x45)) {
		sgm7220_write_reg(chip->client, reg, val);
	}

	return count;
}

static DEVICE_ATTR(current_advertise, S_IRUGO | S_IWUSR, current_advertise_show,
		   current_advertise_store);
static DEVICE_ATTR(current_detect, S_IRUGO, current_detect_show, NULL);
static DEVICE_ATTR(accessory_connected, S_IRUGO, accessory_connected_show,
		   NULL);
static DEVICE_ATTR(active_cable_det, S_IRUGO, active_cable_det_show, NULL);
static DEVICE_ATTR(attached_state, S_IRUGO, attached_state_show, NULL);
static DEVICE_ATTR(cable_direction, S_IRUGO, cable_dir_show, NULL);
static DEVICE_ATTR(drp_duty_cycle, S_IRUGO | S_IWUSR, drp_duty_cycle_show,
		   drp_duty_cycle_store);
static DEVICE_ATTR(mode_select, S_IRUGO | S_IWUSR, mode_select_show,
		   mode_select_store);
static DEVICE_ATTR(registers, 0660, show_registers, store_register);

static struct device_attribute *sgm7220_attributes[] = {
	&dev_attr_current_advertise,
	&dev_attr_current_detect,
	&dev_attr_accessory_connected,
	&dev_attr_active_cable_det,
	&dev_attr_attached_state,
	&dev_attr_cable_direction,
	&dev_attr_drp_duty_cycle,
	&dev_attr_mode_select,
	&dev_attr_registers,
	NULL
};

static int sgm7220_create_device(struct sgm7220_chip *chip)
{
	struct device_attribute **attrs = sgm7220_attributes;
	struct device_attribute *attr = kzalloc(16, GFP_KERNEL);
	int err;

	pr_debug("%s:\n", __func__);
	chip->device_class = class_create(THIS_MODULE, "typec_device");
	if (IS_ERR(chip->device_class))
		return PTR_ERR(chip->device_class);

	chip->dev = device_create(chip->device_class, NULL, 0, NULL,
				  "cc_logic_sgm7220");
	if (IS_ERR(chip->dev))
		return PTR_ERR(chip->dev);

	dev_set_drvdata(chip->dev, chip);

	while ((attr = *attrs++) != NULL) {
		err = device_create_file(chip->dev, attr);
		if (err) {
			device_destroy(chip->device_class, 0);
			return err;
		}
	}
	kfree(attr);
	return 0;
}

static void sgm7220_destroy_device(struct sgm7220_chip *chip)
{
	struct device_attribute **attrs = sgm7220_attributes;
	struct device_attribute *attr = kzalloc(16, GFP_KERNEL);

	while ((attr = *attrs++) != NULL)
		device_remove_file(chip->dev, attr);

	kfree(attr);
	device_destroy(chip->device_class, 0);
	class_destroy(chip->device_class);
	chip->device_class = NULL;
}

static int sgm7220_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct sgm7220_chip *chip = NULL;
	int ret = 0, chip_id;
	bool use_dt = client->dev.of_node;

	pr_info("%s\n", __func__);
	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_I2C | I2C_FUNC_SMBUS_BYTE)) {
		pr_err("%s: checkint_functionality failed\n", __func__);
		return -ENODEV;
	}

	chip = devm_kzalloc(&client->dev, sizeof(struct sgm7220_chip),
			    GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	if (use_dt)
		sgm7220_parse_dt(chip, &client->dev);
	else {
		dev_err(&client->dev, "no dts node\n");
		return -ENODEV;
	}
	chip->dev = &client->dev;
	chip->client = client;
	sema_init(&chip->suspend_lock, 1);
	i2c_set_clientdata(client, chip);

	chip_id = sgm7220_check_revision(client);
	if (chip_id < 0)
		return chip_id;
	chip->irq_wake_lock =
		wakeup_source_register(chip->dev, "sgm7220_irq_wakelock");
	chip->i2c_wake_lock =
		wakeup_source_register(chip->dev, "sgm7220_i2c_wakelock");

	ret = sgm7220_tcpcdev_init(chip, &client->dev);
	if (ret < 0) {
		dev_err(&client->dev, "sgm7220 tcpc dev init fail\n");
		goto err_tcpc_reg;
	}

	ret = sgm7220_init_alert(chip->tcpc);
	if (ret < 0) {
		pr_err("%s: fails to initialize sgm7220 alert (ret = %d)\n",
		       __func__, ret);
		goto err_irq_init;
	}

	ret = sgm7220_create_device(chip);
	if (ret) {
		pr_err("%s: create device failed\n", __func__);
		goto err_device_create;
	}

	pr_info("%s:sgm7220 CC logic probe finished!\n", __func__);
	return 0;

err_device_create:
	sgm7220_destroy_device(chip);
err_irq_init:
	tcpc_device_unregister(chip->dev, chip->tcpc);
err_tcpc_reg:
	wakeup_source_unregister(chip->i2c_wake_lock);
	wakeup_source_unregister(chip->irq_wake_lock);
	return ret;
}

static int sgm7220_remove(struct i2c_client *client)
{
	struct sgm7220_chip *chip = i2c_get_clientdata(client);

	if (chip->irqnum) {
		disable_irq_wake(chip->irqnum);
		free_irq(chip->irqnum, chip);
	}

	if (gpio_is_valid(chip->irq_gpio))
		gpio_free(chip->irq_gpio);

	if (chip) {
		sgm7220_destroy_device(chip);
		tcpc_device_unregister(chip->dev, chip->tcpc);
		wakeup_source_unregister(chip->i2c_wake_lock);
		wakeup_source_unregister(chip->irq_wake_lock);
	}

	dev_info(&client->dev, "sgm7220 CC logic remove finished\n");
	return 0;
}

#ifdef CONFIG_PM
static int SGM7220_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sgm7220_chip *chip =
		(struct sgm7220_chip *)i2c_get_clientdata(client);

	pr_err("[sgm7220] %s enter\n", __func__);
	if (!chip) {
		pr_err("[sgm7220] suspend: No device is available!\n");
		return -EINVAL;
	}

	down(&chip->suspend_lock);

	return 0;
}

static int SGM7220_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sgm7220_chip *chip =
		(struct sgm7220_chip *)i2c_get_clientdata(client);

	pr_err("[sgm7220] %s enter\n", __func__);
	if (!chip) {
		pr_err("[sgm7220] suspend: No device is available!\n");
		return -EINVAL;
	}

	up(&chip->suspend_lock);

	return 0;
}

static void sgm7220_shutdown(struct i2c_client *client)
{
	struct sgm7220_chip *chip = i2c_get_clientdata(client);

	/* reset the INT */
	sgm7220_update_config(chip->client, SGM7220_Reg09_ADDR,
			      INTERRUPT_STATUS_MASK, INTERRUPT_STATUS_SHIFT,
			      0x01);
	if (chip != NULL) {
		if (chip->irqnum)
			disable_irq(chip->irqnum);
	}
}

#ifdef CONFIG_PM_RUNTIME
static int sgm7220_pm_suspend_runtime(struct device *device)
{
	dev_dbg(device, "pm_runtime: suspending...\n");
	return 0;
}

static int sgm7220_pm_resume_runtime(struct device *device)
{
	dev_dbg(device, "pm_runtime: resuming...\n");
	return 0;
}
#endif /* CONFIG_PM_RUNTIME */

static const struct dev_pm_ops sgm7220_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(SGM7220_suspend, SGM7220_resume)
#ifdef CONFIG_PM_RUNTIME
		SET_SYSTEM_SLEEP_PM_OPS(sgm7220_pm_suspend_runtime,
					sgm7220_pm_resume_runtime, NULL)
#endif /* CONFIG_PM_RUNTIME */
};
#define SGM7220_PM_OPS (&sgm7220_pm_ops)
#else
#define SGM7220_PM_OPS (NULL)
#endif /* CONFIG_PM */

static const struct i2c_device_id sgm7220_id_table[] = {
	{ "sgm7220", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, sgm7220_id_table);

static const struct of_device_id sgm_match_table[] = {
	{
		.compatible = "sgm,usb_type_c",
	},
	{},
};

static const unsigned short normal_i2c[] = { 0x47, 0x67, I2C_CLIENT_END };
/* i2c driver */
static struct i2c_driver sgm7220_driver = {
    .driver = {
        .name = "usb_type_c",
        .owner = THIS_MODULE,
		.of_match_table = sgm_match_table,
		.pm = SGM7220_PM_OPS,
    },
	.probe = sgm7220_probe,
    .remove = sgm7220_remove,
	.shutdown = sgm7220_shutdown,
    .id_table = sgm7220_id_table,
	.address_list = (const unsigned short *) normal_i2c,
};

static bool is_or_not_adv_configuration(void)
{
	bool isNot = true;
	char *ptr = NULL;
	int boardid_index;
	struct device_node *np = NULL;
	const char *bootparams;

	np = of_find_node_by_path("/chosen");
	of_property_read_string(np, "bootargs", &bootparams);
	if (!bootparams) {
		pr_err("%s: failed to get bootargs property\n", __func__);
		return 0;
	}

	ptr = strstr(bootparams, "hardware.boardid=");
	if (ptr) {
		ptr += strlen("hardware.boardid=");
		boardid_index = simple_strtol(ptr, NULL, 10);
		pr_err("%s:%d, boardid: %d", __func__, __LINE__, boardid_index);
	} else {
		pr_err("%s:%d, cannot find penang.boardid", __func__,
		       __LINE__);
		return false;
	}

	switch (boardid_index) {
	case PN_BOARDID_CONFIG_VA:
	case PN_BOARDID_CONFIG_VB:
	case PN_BOARDID_CONFIG_VC:
	case PN_BOARDID_CONFIG_VD:
	case PN_BOARDID_CONFIG_VE:
	case PN_BOARDID_CONFIG_VF:
	case PN_BOARDID_CONFIG_VN:
	case PN_BOARDID_CONFIG_VO:
	case PN_BOARDID_CONFIG_VQ:
	case PN_BOARDID_CONFIG_VR:
	case PN_BOARDID_CONFIG_VS:
		isNot = false;
		break;
	case PN_BOARDID_CONFIG_VG:
	case PN_BOARDID_CONFIG_VH:
	case PN_BOARDID_CONFIG_VI:
	case PN_BOARDID_CONFIG_VJ:
	case PN_BOARDID_CONFIG_VK:
	case PN_BOARDID_CONFIG_VL:
	case PN_BOARDID_CONFIG_VM:
	case PN_BOARDID_CONFIG_VP:
		isNot = true;
		break;
	default:
		break;
	}

	return isNot;
}

static int __init sgm7220_init(void)
{
	struct device_node *np;

	pr_info("sgm7220_init (%s): initializing...\n", SGM7220_DRV_VERSION);
	np = of_find_node_by_name(NULL, "usb_type_c");
	if (np != NULL)
		pr_info("usb_type_c node found...\n");
	else
		pr_info("usb_type_c node not found...\n");
	if (is_or_not_adv_configuration()) {
		return i2c_add_driver(&sgm7220_driver);
	}
	return false;
}
subsys_initcall(sgm7220_init);

static void __exit sgm7220_exit(void)
{
	i2c_del_driver(&sgm7220_driver);
}
module_exit(sgm7220_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("NULL");
MODULE_DESCRIPTION("SGM7220 TCPC Driver");
MODULE_VERSION(SGM7220_DRV_VERSION);
