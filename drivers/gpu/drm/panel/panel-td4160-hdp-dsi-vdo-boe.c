// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/backlight.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_modes.h>
#include <linux/delay.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mediatek_v2/mtk_panel_ext.h"
#include "../mediatek/mediatek_v2/mtk_drm_graphics_base.h"
#endif

#include "../mediatek/mediatek_v2/mtk_corner_pattern/mtk_data_hw_roundedpattern.h"

#include "lcd_bias.h"
/*penang code for EKPENAN4GU-1490 by p-lizongrui at 2024/02/26 start*/
extern bool get_hbm_status(void);
extern void set_hbm_status(bool status);
/*penang code for EKPENAN4GU-1490 by p-lizongrui at 2024/02/26 end*/
#define BIAS_OUT_VALUE 6000
static char bl_tb0[] = { 0x51, 0x01, 0xff };

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct gpio_desc *reset_gpio;
	bool prepared;
	bool enabled;
	int error;
};
static bool first_cmd;
#define lcm_dcs_write_seq(ctx, seq...)                                         \
	({                                                                     \
		const u8 d[] = { seq };                                        \
		BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64,                           \
				 "DCS sequence too big for stack");            \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                          \
	})

#define lcm_dcs_write_seq_static(ctx, seq...)                                  \
	({                                                                     \
		static const u8 d[] = { seq };                                 \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                          \
	})

static inline struct lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
}

#ifdef PANEL_SUPPORT_READBACK
static int lcm_dcs_read(struct lcm *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error < 0)
		return 0;

	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		dev_info(ctx->dev, "error %d reading dcs seq:(%#x)\n", ret,
			 cmd);
		ctx->error = ret;
	}

	return ret;
}

static void lcm_panel_get_data(struct lcm *ctx)
{
	u8 buffer[3] = { 0 };
	static int ret;

	pr_info("%s+\n", __func__);

	if (ret == 0) {
		ret = lcm_dcs_read(ctx, 0x0A, buffer, 1);
		pr_info("%s  0x%08x\n", __func__, buffer[0] | (buffer[1] << 8));
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			ret, buffer[0] | (buffer[1] << 8));
	}
}
#endif

static void lcm_dcs_write(struct lcm *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	char *addr;

	if (ctx->error < 0)
		return;

	addr = (char *)data;
	if ((int)*addr < 0xB0)
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	else
		ret = mipi_dsi_generic_write(dsi, data, len);
	if (ret < 0) {
		dev_info(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}

static void lcm_panel_init(struct lcm *ctx)
{
	lcm_dcs_write_seq_static(ctx, 0x53, 0x04);
	lcm_dcs_write_seq_static(ctx, 0x55, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x11);
	mdelay(110);
	lcm_dcs_write_seq_static(ctx, 0x29);
	mdelay(10);
	// lcm_dcs_write_seq(ctx, bl_tb0[0], bl_tb0[1], bl_tb0[2]);
	first_cmd = 1;
	pr_info("%s-\n", __func__);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;

	ctx->enabled = false;

	return 0;
}

static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->prepared)
		return 0;
	lcm_dcs_write_seq_static(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	msleep(20);
	lcm_dcs_write_seq_static(ctx, MIPI_DCS_ENTER_SLEEP_MODE);
	msleep(120);
	set_hbm_status(0);
	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(3);
	lcd_bias_set_vspn(0, 1, BIAS_OUT_VALUE);
	msleep(3);
	tp_reset_en(0);
	ctx->error = 0;
	ctx->prepared = false;

	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	pr_info("%s+\n", __func__);
	if (ctx->prepared)
		return 0;
	tp_reset_en(1);
	mdelay(10);
	lcd_bias_set_vspn(1, 0, BIAS_OUT_VALUE);
	mdelay(15);
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(2);
	gpiod_set_value(ctx->reset_gpio, 0);
	mdelay(5);
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(20);
	lcm_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		lcm_unprepare(panel);

	ctx->prepared = true;

	pr_info("%s-\n", __func__);
	return ret;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (ctx->enabled)
		return 0;

	ctx->enabled = true;

	return 0;
}

#define FRAME_WIDTH                 720
#define FRAME_HEIGHT                1600

#define PHYSICAL_WIDTH              64500
#define PHYSICAL_HEIGHT             129000

#define DATA_RATE                  912
#define HSA                         10
#define HBP                         66
#define VSA                        4
#define VBP                         20
#define HFP                         90
#define MODE_0_FPS                  60
#define MODE_0_VFP                  1010
#define MODE_0_HFP                  HFP

#define MODE_1_FPS                  90
#define MODE_1_VFP                  132
#define MODE_1_HFP                  HFP

static const struct drm_display_mode default_mode = {
	.clock = (FRAME_WIDTH + MODE_0_HFP + HSA + HBP) *
		(FRAME_HEIGHT + MODE_0_VFP + VSA + VBP) * MODE_0_FPS / 1000, //htotal*vtotal*fps/1000
	.hdisplay = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH + MODE_0_HFP,
	.hsync_end = FRAME_WIDTH + MODE_0_HFP + HSA,
	.htotal = FRAME_WIDTH + MODE_0_HFP + HSA + HBP,
	.vdisplay = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + MODE_0_VFP,
	.vsync_end = FRAME_HEIGHT + MODE_0_VFP + VSA,
	.vtotal = FRAME_HEIGHT + MODE_0_VFP + VSA + VBP,
};

static const struct drm_display_mode performance_mode_90hz = {
	.clock = (FRAME_WIDTH + MODE_1_HFP + HSA + HBP) *
		(FRAME_HEIGHT + MODE_1_VFP + VSA + VBP) * MODE_1_FPS / 1000,
	.hdisplay = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH + MODE_1_HFP,
	.hsync_end = FRAME_WIDTH + MODE_1_HFP + HSA,
	.htotal = FRAME_WIDTH + MODE_1_HFP + HSA + HBP,
	.vdisplay = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + MODE_1_VFP,
	.vsync_end = FRAME_HEIGHT + MODE_1_VFP + VSA,
	.vtotal = FRAME_HEIGHT + MODE_1_VFP + VSA + VBP,
};

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params = {
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a, .count = 1, .para_list[0] = 0x9c,
	},
	// .lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.data_rate = DATA_RATE,
/*penang code for EKPENAN4GU-1490 by p-lizongrui at 2024/02/26 start*/
	.dyn = {
		.switch_en = 1,
		.pll_clk = 442,
		.hbp = 120
	},
/*penang code for EKPENAN4GU-1490 by p-lizongrui at 2024/02/26 end*/
};

static struct mtk_panel_params ext_params_90hz = {
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a, .count = 1, .para_list[0] = 0x9c,
	},
	// .lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.data_rate = DATA_RATE,
/*penang code for EKPENAN4GU-1490 by p-lizongrui at 2024/02/26 start*/
	.dyn = {
		.switch_en = 1,
		.pll_clk = 442,
		.hbp = 120
	},
/*penang code for EKPENAN4GU-1490 by p-lizongrui at 2024/02/26 end*/
};

static int panel_ata_check(struct drm_panel *panel)
{
	/* Customer test by own ATA tool */
	return 1;
}

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb, void *handle,
				 unsigned int level)
{
/*penang code for EKPENAN4GU-1490 by p-lizongrui at 2024/02/26 start*/
	int hbm = get_hbm_status();
	if (hbm) {
		pr_info("%s do hbm\b", __func__);
		level = 2047 * 8 / 10;
	} else if (level > 2047) {
/*penang code for EKPENAN4GU-1490 by p-lizongrui at 2024/02/26 end*/
		level = 2047;
	} else if (level < 19) {
		level = 15;
	} else {
		level = level * 65 / 100;
	}
	pr_info("%s backlight = -%d\n", __func__, level);
	bl_tb0[1] = level >> 8;
	bl_tb0[2] = level & 0xFF;

	if (!cb)
		return -1;
	if (first_cmd) {
		usleep_range(30 * 1000, 31 * 1000);
		first_cmd = 0;
	}
	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));
	return 0;
}

struct drm_display_mode *get_mode_by_id_hfp(struct drm_connector *connector,
	unsigned int mode)
{
	struct drm_display_mode *m;
	unsigned int i = 0;

	list_for_each_entry(m, &connector->modes, head) {
		if (i == mode)
			return m;
		i++;
	}
	return NULL;
}

static int mtk_panel_ext_param_set(struct drm_panel *panel,
			struct drm_connector *connector, unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id_hfp(connector, mode);

	if (m == NULL) {
		pr_err("%s:%d invalid display_mode\n", __func__, __LINE__);
		return -1;
	}

	if (drm_mode_vrefresh(m) == 60)
		ext->params = &ext_params;
	else if (drm_mode_vrefresh(m) == 90)
		ext->params = &ext_params_90hz;
	else
		ret = 1;

	return ret;
}

// static void mode_switch_to_90(struct drm_panel *panel)
// {
// 	struct lcm *ctx = panel_to_lcm(panel);

// 	pr_info("%s\n", __func__);

// 	lcm_dcs_write_seq_static(ctx, 0xFF, 0x25);
// 	lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
// 	lcm_dcs_write_seq_static(ctx, 0x18, 0x20);//90hz
// 	lcm_dcs_write_seq_static(ctx, 0xFF, 0x10);
// 	lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);

// }

// static void mode_switch_to_60(struct drm_panel *panel)
// {
// 	struct lcm *ctx = panel_to_lcm(panel);

// 	pr_info("%s\n", __func__);

// 	lcm_dcs_write_seq_static(ctx, 0xFF, 0x25);
// 	lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
// 	lcm_dcs_write_seq_static(ctx, 0x18, 0x21);
// 	lcm_dcs_write_seq_static(ctx, 0xFF, 0x10);
// 	lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
// }

// static int mode_switch(struct drm_panel *panel,
// 		struct drm_connector *connector, unsigned int cur_mode,
// 		unsigned int dst_mode, enum MTK_PANEL_MODE_SWITCH_STAGE stage)
// {
// 	int ret = 0;
// 	struct drm_display_mode *m = get_mode_by_id_hfp(connector, dst_mode);
// 	if (!m)
// 		return ret;
// 	pr_info("%s cur_mode = %d dst_mode %d\n", __func__, cur_mode, dst_mode);

// 	if (m == NULL) {
// 		pr_err("%s:%d invalid display_mode\n", __func__, __LINE__);
// 		return -1;
// 	}

// 	if (drm_mode_vrefresh(m) == 60) {
// 		mode_switch_to_60(panel);
// 	} else if (drm_mode_vrefresh(m) == 90) {
// 		mode_switch_to_90(panel);
// 	} else
// 		ret = 1;

// 	return ret;
// }

static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);
	gpiod_set_value(ctx->reset_gpio, on);

	return 0;
}

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ext_param_set = mtk_panel_ext_param_set,
	// .mode_switch = mode_switch,
	.ata_check = panel_ata_check,
};
#endif

static int lcm_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct drm_display_mode *mode2;
	// struct drm_display_mode *mode3;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 default_mode.hdisplay, default_mode.vdisplay,
			 drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	mode2 = drm_mode_duplicate(connector->dev, &performance_mode_90hz);
	if (!mode2) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode_90hz.hdisplay, performance_mode_90hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_90hz));
		return -ENOMEM;
	}

	drm_mode_set_name(mode2);
	mode2->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode2);

	// connector->display_info.width_mm = PHYSICAL_WIDTH / 1000;
	// connector->display_info.height_mm = PHYSICAL_HEIGHT / 1000;

	return 1;
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;
	struct lcm *ctx;
	unsigned int value;
	int ret;

	pr_info("%s+ lcm,td4160,vdo\n", __func__);

	dsi_node = of_get_parent(dev->of_node);
	if (dsi_node) {
		endpoint = of_graph_get_next_endpoint(dsi_node, NULL);
		if (endpoint) {
			remote_node = of_graph_get_remote_port_parent(endpoint);
			if (!remote_node) {
				pr_info("No panel connected,skip probe lcm\n");
				return -ENODEV;
			}
			pr_info("device node name:%s\n", remote_node->name);
		}
	}
	if (remote_node != dev->of_node) {
		pr_info("%s+ skip probe due to not current lcm\n", __func__);
		return -ENODEV;
	}

	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET | MIPI_DSI_CLOCK_NON_CONTINUOUS;

	value = 0;
	ret = of_property_read_u32(dev->of_node, "rc-enable", &value);
	if (ret < 0)
		value = 0;
	else {
		ext_params.round_corner_en = value;
		ext_params_90hz.round_corner_en = value;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(dev, "cannot get reset-gpios %ld\n",
			 PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}

	ctx->prepared = true;
	ctx->enabled = true;
	drm_panel_init(&ctx->panel, dev, &lcm_drm_funcs, DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_handle_reg(&ctx->panel);
	ret = mtk_panel_ext_create(dev, &ext_params, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;

#endif

	pr_info("%s- lcm\n", __func__);

	return ret;
}

static int lcm_remove(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);
#if defined(CONFIG_MTK_PANEL_EXT)
	struct mtk_panel_ctx *ext_ctx = find_panel_ctx(&ctx->panel);
#endif

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_detach(ext_ctx);
	mtk_panel_remove(ext_ctx);
#endif

	return 0;
}

static const struct of_device_id lcm_of_match[] = {
	{
	    .compatible = "lcm,td4160",
	},
	{}
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-td4160-hdp-dsi-vdo-boe",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("lizongrui");
MODULE_DESCRIPTION("lcm panel-td4160-hdp-dsi-vdo-boe Panel Driver");
MODULE_LICENSE("GPL v2");
