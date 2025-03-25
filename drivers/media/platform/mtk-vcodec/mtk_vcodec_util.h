/* SPDX-License-Identifier: GPL-2.0 */
/*
* Copyright (c) 2016 MediaTek Inc.
* Author: PC Chen <pc.chen@mediatek.com>
*	Tiffany Lin <tiffany.lin@mediatek.com>
*/

#ifndef _MTK_VCODEC_UTIL_H_
#define _MTK_VCODEC_UTIL_H_

#include <linux/types.h>
#include <linux/dma-direction.h>
#include <linux/math64.h>
#include <linux/mtk_vcu_controls.h>
#include <linux/timer.h>
#include "vcodec_ipi_msg.h"
// include vcp header to make build pass even if CONFIG_MTK_TINYSYS_VCP_SUPPORT is off
// dynamic to switch vcp or vcu path by "mtk_vcodec_vcp"
//#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
#include "vcp_helper.h"
//#endif
#if IS_ENABLED(CONFIG_VIDEO_MEDIATEK_VCU)
#include "mtk_vcu.h"
#endif
#include <linux/trace_events.h>

/* #define FPGA_PWRCLK_API_DISABLE */
/* #define FPGA_INTERRUPT_API_DISABLE */

#define mem_slot_range 100*1024ULL //100KB

#define CODEC_ALLOCATE_MAX_BUFFER_SIZE 0x10000000UL /*256MB, sync with mtk_vcodec_mem.h*/

#define LOG_PARAM_INFO_SIZE 64
#define LOG_PROPERTY_SIZE 1024
#define ROUND_N(X, N)   (((X) + ((N)-1)) & (~((N)-1)))    //only for N is exponential of 2

#define MTK_VDEC_CHECK_ACTIVE_INTERVAL 2000 // ms

#define SNPRINTF(args...)							\
	do {											\
		if (snprintf(args) < 0)						\
			pr_notice("[ERROR] %s(),%d: snprintf error\n", __func__, __LINE__);	\
	} while (0)
#define SPRINTF(args...)							\
	do {											\
		if (sprintf(args) < 0)						\
			pr_notice("[ERROR] %s(),%d: sprintf error\n", __func__, __LINE__);	\
	} while (0)

struct mtk_vcodec_mem {
	size_t size;
	void *va;
	dma_addr_t dma_addr;
};

struct mtk_vcodec_fb {
	size_t size;
	dma_addr_t dma_addr;
};

struct mtk_vcodec_ctx;
struct mtk_vcodec_dev;

extern int mtk_v4l2_dbg_level;
extern bool mtk_vcodec_dbg;


#define mtk_v4l2_err(fmt, args...)                \
	pr_err("[MTK_V4L2][ERROR] %s:%d: " fmt "\n", __func__, __LINE__, \
	       ##args)

#define mtk_vcodec_err(h, fmt, args...)					\
	pr_err("[MTK_VCODEC][ERROR][%d]: %s() " fmt "\n",		\
	       ((struct mtk_vcodec_ctx *)h->ctx)->id, __func__, ##args)


#if defined(DEBUG)

#define mtk_v4l2_debug(level, fmt, args...)				 \
	do {								 \
		if (mtk_v4l2_dbg_level >= level)			 \
			pr_info("[MTK_V4L2] level=%d %s(),%d: " fmt "\n",\
				level, __func__, __LINE__, ##args);	 \
	} while (0)

#define mtk_v4l2_debug_enter()  mtk_v4l2_debug(8, "+")
#define mtk_v4l2_debug_leave()  mtk_v4l2_debug(8, "-")

#define mtk_vcodec_debug(h, fmt, args...)                     \
	do {                                                      \
		if (mtk_vcodec_dbg && h != NULL && h->ctx != NULL)    \
			pr_notice("[MTK_VCODEC][%d]: %s() " fmt "\n",     \
				((struct mtk_vcodec_ctx *)h->ctx)->id,        \
				__func__, ##args);                            \
	} while (0)

#define mtk_vcodec_perf_log(fmt, args...)                               \
	do {                                                            \
		if (mtk_vcodec_perf)                          \
			pr_info("[MTK_PERF] " fmt "\n", ##args);        \
	} while (0)


#define mtk_vcodec_err(h, fmt, args...)                                      \
	do {                                                                     \
		if (h != NULL && h->ctx != NULL)                                     \
			pr_info("[MTK_VCODEC][ERROR][%d]: %s() " fmt "\n",               \
				((struct mtk_vcodec_ctx *)h->ctx)->id, __func__, ##args);    \
	} while (0)

#define mtk_vcodec_debug_enter(h)  mtk_vcodec_debug(h, "+")
#define mtk_vcodec_debug_leave(h)  mtk_vcodec_debug(h, "-")

#else

#define mtk_v4l2_debug(level, fmt, args...) {}
#define mtk_v4l2_debug_enter() {}
#define mtk_v4l2_debug_leave() {}

#define mtk_vcodec_debug(h, fmt, args...) {}
#define mtk_vcodec_debug_enter(h) {}
#define mtk_vcodec_debug_leave(h) {}

#endif

void __iomem *mtk_vcodec_get_reg_addr(struct mtk_vcodec_ctx *data,
				unsigned int reg_idx);
int mtk_vcodec_mem_alloc(struct mtk_vcodec_ctx *data,
				struct mtk_vcodec_mem *mem);
void mtk_vcodec_mem_free(struct mtk_vcodec_ctx *data,
				struct mtk_vcodec_mem *mem);
void mtk_vcodec_set_curr_ctx(struct mtk_vcodec_dev *dev,
	struct mtk_vcodec_ctx *ctx);
struct mtk_vcodec_ctx *mtk_vcodec_get_curr_ctx(struct mtk_vcodec_dev *dev);

#endif /* _MTK_VCODEC_UTIL_H_ */
