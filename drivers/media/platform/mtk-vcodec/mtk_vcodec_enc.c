// SPDX-License-Identifier: GPL-2.0
/*
* Copyright (c) 2016 MediaTek Inc.
* Author: PC Chen <pc.chen@mediatek.com>
*         Tiffany Lin <tiffany.lin@mediatek.com>
*/

#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>
#include <soc/mediatek/smi.h>

#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_enc.h"
#include "mtk_vcodec_intr.h"
#include "mtk_vcodec_util.h"
#include "venc_drv_if.h"

#define MTK_VENC_MIN_W	160U
#define MTK_VENC_MIN_H	128U
#define MTK_VENC_MAX_W	1920U
#define MTK_VENC_MAX_H	1088U
#define DFT_CFG_WIDTH	MTK_VENC_MIN_W
#define DFT_CFG_HEIGHT	MTK_VENC_MIN_H
#define MTK_MAX_CTRLS_HINT	20

#define MTK_DEFAULT_FRAMERATE_NUM 1001
#define MTK_DEFAULT_FRAMERATE_DENOM 30000

static void mtk_venc_worker(struct work_struct *work);

static const struct v4l2_frmsize_stepwise mtk_venc_framesizes = {
	MTK_VENC_MIN_W, MTK_VENC_MAX_W, 16,
	MTK_VENC_MIN_H, MTK_VENC_MAX_H, 16,
};

static int mtk_venc_sec_dc_map_dmabuf(void *mem_priv)
{
	struct vb2_dc_buf *buf = mem_priv;

	if (WARN_ON(!buf->db_attach)) {
		mtk_v4l2_err("trying to pin a non attached buffer\n");
		return -EINVAL;
	}

	if (WARN_ON(buf->dma_addr)) {
		mtk_v4l2_err("dmabuf buffer is already pinned\n");
		return 0;
	}

	buf->dma_addr = dmabuf_to_secure_handle(buf->db_attach->dmabuf);
	buf->dma_sgt = NULL;
	buf->vaddr = NULL;

	return 0;
}

static void mtk_venc_sec_dc_unmap_dmabuf(void *mem_priv)
{
	struct vb2_dc_buf *buf = mem_priv;

	if (WARN_ON(!buf->db_attach)) {
		mtk_v4l2_err("trying to unpin a not attached buffer\n");
		return;
	}

	if (WARN_ON(!buf->dma_addr)) {
		mtk_v4l2_err("dmabuf buffer is already unpinned\n");
		return;
	}

	if (buf->vaddr) {
		mtk_v4l2_err("dmabuf buffer vaddr not null\n");
		dma_buf_vunmap(buf->db_attach->dmabuf, buf->vaddr);
		buf->vaddr = NULL;
	}

	buf->dma_addr = 0;
	buf->dma_sgt = NULL;
}

static bool mtk_venc_is_vcu(void)
{
	if (VCU_FPTR(vcu_get_plat_device)) {
		if (mtk_vcodec_vcp & (1 << MTK_INST_ENCODER))
			return false;
		else
			return true;
	}
	return false;
}

static void set_venc_vcp_data(struct mtk_vcodec_ctx *ctx, enum vcp_reserve_mem_id_t id)
{
	struct venc_enc_param *enc_prm = NULL;

	enc_prm = kzalloc(sizeof(*enc_prm), GFP_KERNEL);
	if (!enc_prm)
		return;

	if (id == VENC_SET_PROP_MEM_ID) {

		sprintf(enc_prm->set_vcp_buf, "%s", mtk_venc_property);
		mtk_v4l2_debug(3, "[%d] mtk_venc_property %s", ctx->id, enc_prm->set_vcp_buf);
		mtk_v4l2_debug(3, "[%d] mtk_venc_property_prev %s",
					ctx->id, mtk_venc_property_prev);

		// set vcp log every time
		if (/* strcmp(mtk_venc_property_prev, enc_prm->set_vcp_buf) != 0 && */
			strlen(enc_prm->set_vcp_buf) != 0) {

			if (venc_if_set_param(ctx,
				VENC_SET_PARAM_PROPERTY,
				enc_prm) != 0) {
				mtk_v4l2_err("Error!! Cannot set venc property");
				goto err_set_vcp_data;
			}
			strcpy(mtk_venc_property_prev, enc_prm->set_vcp_buf);
		}
	} else if (id == VENC_VCP_LOG_INFO_ID) {

		sprintf(enc_prm->set_vcp_buf, "%s", mtk_venc_vcp_log);
		mtk_v4l2_debug(3, "[%d] mtk_venc_vcp_log %s", ctx->id, enc_prm->set_vcp_buf);
		mtk_v4l2_debug(3, "[%d] mtk_venc_vcp_log_prev %s", ctx->id, mtk_venc_vcp_log_prev);

		// set vcp log every time
		if (/* strcmp(mtk_venc_vcp_log_prev, enc_prm->set_vcp_buf) != 0 && */
			strlen(enc_prm->set_vcp_buf) != 0) {

			if (venc_if_set_param(ctx,
				VENC_SET_PARAM_VCP_LOG_INFO,
				enc_prm) != 0) {
				mtk_v4l2_err("Error!! Cannot set venc vcp log info");
				goto err_set_vcp_data;
			}
			strcpy(mtk_venc_vcp_log_prev, enc_prm->set_vcp_buf);
		}
	}

err_set_vcp_data:
	kfree(enc_prm);
}

static void set_vcu_vpud_log(struct mtk_vcodec_ctx *ctx, void *in)
{
	struct venc_enc_param *enc_prm = NULL;

	if (!mtk_venc_is_vcu()) {
		mtk_v4l2_err("only support on vcu enc path");
		return;
	}

	enc_prm = kzalloc(sizeof(*enc_prm), GFP_KERNEL);
	if (!enc_prm)
		return;

	enc_prm->log = (char *)in;
	venc_if_set_param(ctx, VENC_SET_PARAM_VCU_VPUD_LOG, enc_prm);
	kfree(enc_prm);
}

static void get_vcu_vpud_log(struct mtk_vcodec_ctx *ctx, void *out)
{
	if (!mtk_venc_is_vcu()) {
		mtk_v4l2_err("only support on vcu dec path");
		return;
	}

	venc_if_get_param(ctx, GET_PARAM_VENC_VCU_VPUD_LOG, out);
}


static void get_supported_format(struct mtk_vcodec_ctx *ctx)
{
	unsigned int i;

	if (mtk_venc_formats[0].fourcc == 0) {
		if (venc_if_get_param(ctx,
			GET_PARAM_VENC_CAP_SUPPORTED_FORMATS,
			&mtk_venc_formats) != 0) {
			mtk_v4l2_err("Error!! Cannot get supported format");
			return;
		}
		for (i = 0; i < MTK_MAX_ENC_CODECS_SUPPORT; i++) {
			if (mtk_venc_formats[i].fourcc != 0 &&
			    mtk_venc_formats[i].type == MTK_FMT_FRAME) {
				default_out_fmt_idx = i;
				break;
			}
		}
		for (i = 0; i < MTK_MAX_ENC_CODECS_SUPPORT; i++) {
			if (mtk_venc_formats[i].fourcc != 0 &&
			    mtk_venc_formats[i].type == MTK_FMT_ENC) {
				default_cap_fmt_idx = i;
				break;
			}
		}
	}
}

static void get_supported_framesizes(struct mtk_vcodec_ctx *ctx)
{
	unsigned int i;

	if (mtk_venc_framesizes[0].fourcc == 0) {
		if (venc_if_get_param(ctx, GET_PARAM_VENC_CAP_FRAME_SIZES,
				      &mtk_venc_framesizes) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot get frame size",
				ctx->id);
			return;
		}

		for (i = 0; i < MTK_MAX_ENC_CODECS_SUPPORT; i++) {
			if (mtk_venc_framesizes[i].fourcc != 0) {
				mtk_v4l2_debug(1,
				"venc_fs[%d] fourcc %d s %d %d %d %d %d %d\n",
				i, mtk_venc_framesizes[i].fourcc,
				mtk_venc_framesizes[i].stepwise.min_width,
				mtk_venc_framesizes[i].stepwise.max_width,
				mtk_venc_framesizes[i].stepwise.step_width,
				mtk_venc_framesizes[i].stepwise.min_height,
				mtk_venc_framesizes[i].stepwise.max_height,
				mtk_venc_framesizes[i].stepwise.step_height);
			}
		}
	}
}

static void get_free_buffers(struct mtk_vcodec_ctx *ctx,
				struct venc_done_result *pResult)
{
	venc_if_get_param(ctx,
		GET_PARAM_FREE_BUFFERS,
		pResult);
}

void mtk_enc_put_buf(struct mtk_vcodec_ctx *ctx)
{
	struct venc_done_result rResult;
	struct venc_frm_buf *pfrm;
	struct mtk_vcodec_mem *pbs;
	struct mtk_video_enc_buf *bs_info, *frm_info;
	struct vb2_v4l2_buffer *dst_vb2_v4l2, *src_vb2_v4l2;
	struct vb2_buffer *dst_buf;
	char *pbuf;
	int dump_size;
	int i;
	char debug_fb[200] = {0};

	mutex_lock(&ctx->buf_lock);
	do {
		dst_vb2_v4l2 = NULL;
		src_vb2_v4l2 = NULL;
		pfrm = NULL;
		pbs = NULL;

		memset(&rResult, 0, sizeof(rResult));
		get_free_buffers(ctx, &rResult);

		if (rResult.bs_va != 0 && virt_addr_valid(rResult.bs_va)) {
			pbs = (struct mtk_vcodec_mem *)rResult.bs_va;
			bs_info = container_of(pbs,
				struct mtk_video_enc_buf, bs_buf);
			dst_vb2_v4l2 = &bs_info->vb;
		}

		if (rResult.frm_va != 0 && virt_addr_valid(rResult.frm_va)) {
			pfrm = (struct venc_frm_buf *)rResult.frm_va;
			frm_info = container_of(pfrm,
				struct mtk_video_enc_buf, frm_buf);
			src_vb2_v4l2 = &frm_info->vb;

			if (rResult.flags & VENC_FLAG_ENCODE_TIMEOUT && pfrm->fb_addr[0].va != 0) {
				mtk_v4l2_err("Venc Timeout Dump Framebuf %d VA=%p PA=%llx Size=0x%zx =>",
				pfrm->index,
				pfrm->fb_addr[0].va,
				(u64)pfrm->fb_addr[0].dma_addr,
				pfrm->fb_addr[0].size);

				pbuf = (char *)pfrm->fb_addr[0].va;
				dump_size = pfrm->fb_addr[0].size < 256 ? pfrm->fb_addr[i].size: 256;
				for (i = 0; i < dump_size; i++) {
					SPRINTF(debug_fb, "%s %02x", debug_fb, pbuf[i]);
					if ((i + 1) % 16 == 0 || i == dump_size - 1) {
						mtk_v4l2_err("%s", debug_fb);
						memset(debug_fb, 0, ARRAY_SIZE(debug_fb));
					}
				}
			}
		}

		if (src_vb2_v4l2 != NULL && dst_vb2_v4l2 != NULL) {
			if (rResult.is_key_frm)
				dst_vb2_v4l2->flags |= V4L2_BUF_FLAG_KEYFRAME;

			dst_vb2_v4l2->vb2_buf.timestamp =
				src_vb2_v4l2->vb2_buf.timestamp;
			dst_vb2_v4l2->timecode = src_vb2_v4l2->timecode;
			dst_vb2_v4l2->sequence = src_vb2_v4l2->sequence;
			dst_buf = &dst_vb2_v4l2->vb2_buf;
			dst_buf->planes[0].bytesused = rResult.bs_size;
			v4l2_m2m_buf_done(src_vb2_v4l2, VB2_BUF_STATE_DONE);
			v4l2_m2m_buf_done(dst_vb2_v4l2, VB2_BUF_STATE_DONE);

			mtk_v4l2_debug(1, "venc_if_encode bs size=%d",
				rResult.bs_size);
		} else if (src_vb2_v4l2 == NULL && dst_vb2_v4l2 != NULL) {
			dst_buf = &dst_vb2_v4l2->vb2_buf;
			dst_buf->planes[0].bytesused = rResult.bs_size;
			v4l2_m2m_buf_done(dst_vb2_v4l2,
					VB2_BUF_STATE_DONE);
			mtk_v4l2_debug(0, "[Warning] bs size=%d, frm NULL!!",
				rResult.bs_size);
		} else {
			if (src_vb2_v4l2 == NULL)
				mtk_v4l2_debug(1, "NULL enc src buffer\n");

			if (dst_vb2_v4l2 == NULL)
				mtk_v4l2_debug(1, "NULL enc dst buffer\n");
		}
	} while (rResult.bs_va != 0 || rResult.frm_va != 0);
	mutex_unlock(&ctx->buf_lock);
}

static struct mtk_video_fmt *mtk_venc_find_format(struct v4l2_format *f,
						  unsigned int t)
{
	struct mtk_video_fmt *fmt;
	unsigned int k;

	mtk_v4l2_debug(3, "fourcc %d", f->fmt.pix_mp.pixelformat);
	for (k = 0; k < MTK_MAX_ENC_CODECS_SUPPORT &&
	     mtk_venc_formats[k].fourcc != 0; k++) {
		fmt = &mtk_venc_formats[k];
		if (fmt->fourcc == f->fmt.pix.pixelformat && fmt->type == t)
			return fmt;
	}

	return NULL;
}

static int vidioc_venc_check_supported_profile_level(__u32 fourcc,
	unsigned int pl, bool is_profile)
{
	struct v4l2_format f;
	int i = 0;

	f.fmt.pix.pixelformat = fourcc;
	if (mtk_venc_find_format(&f, MTK_FMT_ENC) == NULL)
		return false;

	for (i = 0; i < MTK_MAX_ENC_CODECS_SUPPORT; i++) {
		if (mtk_venc_framesizes[i].fourcc == fourcc) {
			if (is_profile) {
				if (mtk_venc_framesizes[i].profile & (1 << pl))
					return true;
				else
					return false;
			} else {
				if (mtk_venc_framesizes[i].level >= pl)
					return true;
				else
					return false;
			}
		}
	}
	return false;
}

static int vidioc_venc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mtk_vcodec_ctx *ctx = ctrl_to_ctx(ctrl);
	struct mtk_enc_params *p = &ctx->enc_params;
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_BITRATE val = %d",
			       ctrl->val);
		p->bitrate = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_BITRATE;
		break;
	case V4L2_CID_MPEG_VIDEO_B_FRAMES:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_B_FRAMES val = %d",
			       ctrl->val);
		p->num_b_frame = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE val = %d",
			       ctrl->val);
		p->rc_frame = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MAX_QP:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_H264_MAX_QP val = %d",
			       ctrl->val);
		p->h264_max_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEADER_MODE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_HEADER_MODE val = %d",
			       ctrl->val);
		p->seq_hdr_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE val = %d",
			       ctrl->val);
		p->rc_mb = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_H264_PROFILE val = %d",
			       ctrl->val);
		p->h264_profile = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_H264_LEVEL val = %d",
			       ctrl->val);
		p->h264_level = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_I_PERIOD:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_H264_I_PERIOD val = %d",
			       ctrl->val);
		p->intra_period = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_INTRA_PERIOD;
		break;
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_GOP_SIZE val = %d",
			       ctrl->val);
		p->gop_size = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_GOP_SIZE;
		break;
	case V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME");
		p->force_intra = 1;
		ctx->param_change |= MTK_ENCODE_PARAM_FORCE_INTRA;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops mtk_vcodec_enc_ctrl_ops = {
	.s_ctrl = vidioc_venc_s_ctrl,
};

static int vidioc_enum_fmt(struct v4l2_fmtdesc *f,
			   const struct mtk_video_fmt *formats,
			   size_t num_formats)
{
	if (f->index >= num_formats)
		return -EINVAL;

	f->pixelformat = formats[f->index].fourcc;
	memset(f->reserved, 0, sizeof(f->reserved));

	return 0;
}

static const struct mtk_video_fmt *
mtk_venc_find_format(u32 fourcc, const struct mtk_vcodec_enc_pdata *pdata)
{
	const struct mtk_video_fmt *fmt;
	unsigned int k;

	for (k = 0; k < pdata->num_capture_formats; k++) {
		fmt = &pdata->capture_formats[k];
		if (fmt->fourcc == fourcc)
			return fmt;
	}

	for (k = 0; k < pdata->num_output_formats; k++) {
		fmt = &pdata->output_formats[k];
		if (fmt->fourcc == fourcc)
			return fmt;
	}

	return NULL;
}

static int vidioc_enum_framesizes(struct file *file, void *fh,
				  struct v4l2_frmsizeenum *fsize)
{
	const struct mtk_video_fmt *fmt;

	if (fsize->index != 0)
		return -EINVAL;

	fmt = mtk_venc_find_format(fsize->pixel_format,
				   fh_to_ctx(fh)->dev->venc_pdata);
	if (!fmt)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise = mtk_venc_framesizes;

	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	const struct mtk_vcodec_enc_pdata *pdata =
		fh_to_ctx(priv)->dev->venc_pdata;

	return vidioc_enum_fmt(f, pdata->capture_formats,
			       pdata->num_capture_formats);
}

static int vidioc_enum_fmt_vid_out(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	const struct mtk_vcodec_enc_pdata *pdata =
		fh_to_ctx(priv)->dev->venc_pdata;

	return vidioc_enum_fmt(f, pdata->output_formats,
			       pdata->num_output_formats);
}

static int vidioc_venc_querycap(struct file *file, void *priv,
				struct v4l2_capability *cap)
{
	strscpy(cap->driver, MTK_VCODEC_ENC_NAME, sizeof(cap->driver));
	strscpy(cap->bus_info, MTK_PLATFORM_STR, sizeof(cap->bus_info));
	strscpy(cap->card, MTK_PLATFORM_STR, sizeof(cap->card));

	return 0;
}

static int vidioc_venc_s_parm(struct file *file, void *priv,
			      struct v4l2_streamparm *a)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct v4l2_fract *timeperframe = &a->parm.output.timeperframe;

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	if (timeperframe->numerator == 0 || timeperframe->denominator == 0) {
		timeperframe->numerator = MTK_DEFAULT_FRAMERATE_NUM;
		timeperframe->denominator = MTK_DEFAULT_FRAMERATE_DENOM;
	}

	ctx->enc_params.framerate_num = timeperframe->denominator;
	ctx->enc_params.framerate_denom = timeperframe->numerator;
	ctx->param_change |= MTK_ENCODE_PARAM_FRAMERATE;

	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;

	return 0;
}

static int vidioc_venc_g_parm(struct file *file, void *priv,
			      struct v4l2_streamparm *a)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	a->parm.output.timeperframe.denominator =
			ctx->enc_params.framerate_num;
	a->parm.output.timeperframe.numerator =
			ctx->enc_params.framerate_denom;

	return 0;
}

static struct mtk_q_data *mtk_venc_get_q_data(struct mtk_vcodec_ctx *ctx,
					      enum v4l2_buf_type type)
{
	if (V4L2_TYPE_IS_OUTPUT(type))
		return &ctx->q_data[MTK_Q_DATA_SRC];

	return &ctx->q_data[MTK_Q_DATA_DST];
}

/* V4L2 specification suggests the driver corrects the format struct if any of
 * the dimensions is unsupported
 */
static int vidioc_try_fmt(struct v4l2_format *f,
			  const struct mtk_video_fmt *fmt)
{
	struct v4l2_pix_format_mplane *pix_fmt_mp = &f->fmt.pix_mp;
	int i;

	pix_fmt_mp->field = V4L2_FIELD_NONE;

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		pix_fmt_mp->num_planes = 1;
		pix_fmt_mp->plane_fmt[0].bytesperline = 0;
	} else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		int tmp_w, tmp_h;

		pix_fmt_mp->height = clamp(pix_fmt_mp->height,
					MTK_VENC_MIN_H,
					MTK_VENC_MAX_H);
		pix_fmt_mp->width = clamp(pix_fmt_mp->width,
					MTK_VENC_MIN_W,
					MTK_VENC_MAX_W);

		/* find next closer width align 16, heign align 32, size align
		 * 64 rectangle
		 */
		tmp_w = pix_fmt_mp->width;
		tmp_h = pix_fmt_mp->height;
		v4l_bound_align_image(&pix_fmt_mp->width,
					MTK_VENC_MIN_W,
					MTK_VENC_MAX_W, 4,
					&pix_fmt_mp->height,
					MTK_VENC_MIN_H,
					MTK_VENC_MAX_H, 5, 6);

		if (pix_fmt_mp->width < tmp_w &&
			(pix_fmt_mp->width + 16) <= MTK_VENC_MAX_W)
			pix_fmt_mp->width += 16;
		if (pix_fmt_mp->height < tmp_h &&
			(pix_fmt_mp->height + 32) <= MTK_VENC_MAX_H)
			pix_fmt_mp->height += 32;

		mtk_v4l2_debug(0,
			"before resize width=%d, height=%d, after resize width=%d, height=%d, sizeimage=%d %d",
			tmp_w, tmp_h, pix_fmt_mp->width,
			pix_fmt_mp->height,
			pix_fmt_mp->plane_fmt[0].sizeimage,
			pix_fmt_mp->plane_fmt[1].sizeimage);

		pix_fmt_mp->num_planes = fmt->num_planes;
		pix_fmt_mp->plane_fmt[0].sizeimage =
				pix_fmt_mp->width * pix_fmt_mp->height +
				((ALIGN(pix_fmt_mp->width, 16) * 2) * 16);
		pix_fmt_mp->plane_fmt[0].bytesperline = pix_fmt_mp->width;

		if (pix_fmt_mp->num_planes == 2) {
			pix_fmt_mp->plane_fmt[1].sizeimage =
				(pix_fmt_mp->width * pix_fmt_mp->height) / 2 +
				(ALIGN(pix_fmt_mp->width, 16) * 16);
			pix_fmt_mp->plane_fmt[2].sizeimage = 0;
			pix_fmt_mp->plane_fmt[1].bytesperline =
							pix_fmt_mp->width;
			pix_fmt_mp->plane_fmt[2].bytesperline = 0;
		} else if (pix_fmt_mp->num_planes == 3) {
			pix_fmt_mp->plane_fmt[1].sizeimage =
			pix_fmt_mp->plane_fmt[2].sizeimage =
				(pix_fmt_mp->width * pix_fmt_mp->height) / 4 +
				((ALIGN(pix_fmt_mp->width, 16) / 2) * 16);
			pix_fmt_mp->plane_fmt[1].bytesperline =
				pix_fmt_mp->plane_fmt[2].bytesperline =
				pix_fmt_mp->width / 2;
		}
	}

	for (i = 0; i < pix_fmt_mp->num_planes; i++)
		memset(&(pix_fmt_mp->plane_fmt[i].reserved[0]), 0x0,
			   sizeof(pix_fmt_mp->plane_fmt[0].reserved));

	pix_fmt_mp->flags = 0;
	memset(&pix_fmt_mp->reserved, 0x0,
		sizeof(pix_fmt_mp->reserved));

	return 0;
}

static void mtk_venc_set_param(struct mtk_vcodec_ctx *ctx,
				struct venc_enc_param *param)
{
	struct mtk_q_data *q_data_src = &ctx->q_data[MTK_Q_DATA_SRC];
	struct mtk_enc_params *enc_params = &ctx->enc_params;

	switch (q_data_src->fmt->fourcc) {
	case V4L2_PIX_FMT_YUV420M:
		param->input_yuv_fmt = VENC_YUV_FORMAT_I420;
		break;
	case V4L2_PIX_FMT_YVU420M:
		param->input_yuv_fmt = VENC_YUV_FORMAT_YV12;
		break;
	case V4L2_PIX_FMT_NV12M:
		param->input_yuv_fmt = VENC_YUV_FORMAT_NV12;
		break;
	case V4L2_PIX_FMT_NV21M:
		param->input_yuv_fmt = VENC_YUV_FORMAT_NV21;
		break;
	default:
		mtk_v4l2_err("Unsupported fourcc =%d", q_data_src->fmt->fourcc);
		break;
	}
	param->h264_profile = enc_params->h264_profile;
	param->h264_level = enc_params->h264_level;

	/* Config visible resolution */
	param->width = q_data_src->visible_width;
	param->height = q_data_src->visible_height;
	/* Config coded resolution */
	param->buf_width = q_data_src->coded_width;
	param->buf_height = q_data_src->coded_height;
	param->frm_rate = enc_params->framerate_num /
			enc_params->framerate_denom;
	param->intra_period = enc_params->intra_period;
	param->gop_size = enc_params->gop_size;
	param->bitrate = enc_params->bitrate;

	mtk_v4l2_debug(0,
		"fmt 0x%x, P/L %d/%d, w/h %d/%d, buf %d/%d, fps/bps %d/%d, gop %d, i_period %d",
		param->input_yuv_fmt, param->h264_profile,
		param->h264_level, param->width, param->height,
		param->buf_width, param->buf_height,
		param->frm_rate, param->bitrate,
		param->gop_size, param->intra_period);
}

static int vidioc_venc_s_fmt_cap(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	const struct mtk_vcodec_enc_pdata *pdata = ctx->dev->venc_pdata;
	struct vb2_queue *vq;
	struct mtk_q_data *q_data;
	int i, ret;
	const struct mtk_video_fmt *fmt;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq) {
		mtk_v4l2_err("fail to get vq");
		return -EINVAL;
	}

	if (vb2_is_busy(vq)) {
		mtk_v4l2_err("queue busy");
		return -EBUSY;
	}

	q_data = mtk_venc_get_q_data(ctx, f->type);
	if (!q_data) {
		mtk_v4l2_err("fail to get q data");
		return -EINVAL;
	}

	fmt = mtk_venc_find_format(f->fmt.pix.pixelformat, pdata);
	if (!fmt) {
		fmt = &ctx->dev->venc_pdata->capture_formats[0];
		f->fmt.pix.pixelformat = fmt->fourcc;
	}

	q_data->fmt = fmt;
	ret = vidioc_try_fmt(f, q_data->fmt);
	if (ret)
		return ret;

	q_data->coded_width = f->fmt.pix_mp.width;
	q_data->coded_height = f->fmt.pix_mp.height;
	q_data->field = f->fmt.pix_mp.field;

	for (i = 0; i < f->fmt.pix_mp.num_planes; i++) {
		struct v4l2_plane_pix_format	*plane_fmt;

		plane_fmt = &f->fmt.pix_mp.plane_fmt[i];
		q_data->bytesperline[i]	= plane_fmt->bytesperline;
		q_data->sizeimage[i] = plane_fmt->sizeimage;
	}

	if (ctx->state == MTK_STATE_FREE) {
		ret = venc_if_init(ctx, q_data->fmt->fourcc);
		if (ret) {
			mtk_v4l2_err("venc_if_init failed=%d, codec type=%x",
					ret, q_data->fmt->fourcc);
			return -EBUSY;
		}
		ctx->state = MTK_STATE_INIT;
	}

	return 0;
}

static int vidioc_venc_s_fmt_out(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	const struct mtk_vcodec_enc_pdata *pdata = ctx->dev->venc_pdata;
	struct vb2_queue *vq;
	struct mtk_q_data *q_data;
	int ret, i;
	const struct mtk_video_fmt *fmt;
	struct v4l2_pix_format_mplane *pix_fmt_mp = &f->fmt.pix_mp;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq) {
		mtk_v4l2_err("fail to get vq");
		return -EINVAL;
	}

	if (vb2_is_busy(vq)) {
		mtk_v4l2_err("queue busy");
		return -EBUSY;
	}

	q_data = mtk_venc_get_q_data(ctx, f->type);
	if (!q_data) {
		mtk_v4l2_err("fail to get q data");
		return -EINVAL;
	}

	fmt = mtk_venc_find_format(f->fmt.pix.pixelformat, pdata);
	if (!fmt) {
		fmt = &ctx->dev->venc_pdata->output_formats[0];
		f->fmt.pix.pixelformat = fmt->fourcc;
	}

	pix_fmt_mp->height = clamp(pix_fmt_mp->height,
				MTK_VENC_MIN_H,
				MTK_VENC_MAX_H);
	pix_fmt_mp->width = clamp(pix_fmt_mp->width,
				MTK_VENC_MIN_W,
				MTK_VENC_MAX_W);

	q_data->visible_width = f->fmt.pix_mp.width;
	q_data->visible_height = f->fmt.pix_mp.height;
	q_data->fmt = fmt;
	ret = vidioc_try_fmt(f, q_data->fmt);
	if (ret)
		return ret;

	q_data->coded_width = f->fmt.pix_mp.width;
	q_data->coded_height = f->fmt.pix_mp.height;

	q_data->field = f->fmt.pix_mp.field;
	ctx->colorspace = f->fmt.pix_mp.colorspace;
	ctx->ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
	ctx->quantization = f->fmt.pix_mp.quantization;
	ctx->xfer_func = f->fmt.pix_mp.xfer_func;

	for (i = 0; i < f->fmt.pix_mp.num_planes; i++) {
		struct v4l2_plane_pix_format *plane_fmt;

		plane_fmt = &f->fmt.pix_mp.plane_fmt[i];
		q_data->bytesperline[i] = plane_fmt->bytesperline;
		q_data->sizeimage[i] = plane_fmt->sizeimage;
	}

	return 0;
}

static int vidioc_venc_g_fmt(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *vq;
	struct mtk_q_data *q_data;
	int i;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = mtk_venc_get_q_data(ctx, f->type);

	pix->width = q_data->coded_width;
	pix->height = q_data->coded_height;
	pix->pixelformat = q_data->fmt->fourcc;
	pix->field = q_data->field;
	pix->num_planes = q_data->fmt->num_planes;
	for (i = 0; i < pix->num_planes; i++) {
		pix->plane_fmt[i].bytesperline = q_data->bytesperline[i];
		pix->plane_fmt[i].sizeimage = q_data->sizeimage[i];
		memset(&(pix->plane_fmt[i].reserved[0]), 0x0,
		       sizeof(pix->plane_fmt[i].reserved));
	}

	pix->flags = 0;
	pix->colorspace = ctx->colorspace;
	pix->ycbcr_enc = ctx->ycbcr_enc;
	pix->quantization = ctx->quantization;
	pix->xfer_func = ctx->xfer_func;

	return 0;
}

static int vidioc_try_fmt_vid_cap_mplane(struct file *file, void *priv,
					 struct v4l2_format *f)
{
	const struct mtk_video_fmt *fmt;
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	const struct mtk_vcodec_enc_pdata *pdata = ctx->dev->venc_pdata;

	fmt = mtk_venc_find_format(f->fmt.pix.pixelformat, pdata);
	if (!fmt) {
		fmt = &ctx->dev->venc_pdata->capture_formats[0];
		f->fmt.pix.pixelformat = fmt->fourcc;
	}
	f->fmt.pix_mp.colorspace = ctx->colorspace;
	f->fmt.pix_mp.ycbcr_enc = ctx->ycbcr_enc;
	f->fmt.pix_mp.quantization = ctx->quantization;
	f->fmt.pix_mp.xfer_func = ctx->xfer_func;

	return vidioc_try_fmt(f, fmt);
}

static int vidioc_try_fmt_vid_out_mplane(struct file *file, void *priv,
					 struct v4l2_format *f)
{
	const struct mtk_video_fmt *fmt;
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	const struct mtk_vcodec_enc_pdata *pdata = ctx->dev->venc_pdata;

	fmt = mtk_venc_find_format(f->fmt.pix.pixelformat, pdata);
	if (!fmt) {
		fmt = &ctx->dev->venc_pdata->output_formats[0];
		f->fmt.pix.pixelformat = fmt->fourcc;
	}
	if (!f->fmt.pix_mp.colorspace) {
		f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_REC709;
		f->fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
		f->fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
		f->fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
	}

	return vidioc_try_fmt(f, fmt);
}

static int vidioc_venc_g_selection(struct file *file, void *priv,
				     struct v4l2_selection *s)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct mtk_q_data *q_data;

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	q_data = mtk_venc_get_q_data(ctx, s->type);
	if (!q_data)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		s->r.top = 0;
		s->r.left = 0;
		s->r.width = q_data->coded_width;
		s->r.height = q_data->coded_height;
		break;
	case V4L2_SEL_TGT_CROP:
		s->r.top = 0;
		s->r.left = 0;
		s->r.width = q_data->visible_width;
		s->r.height = q_data->visible_height;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int vidioc_venc_s_selection(struct file *file, void *priv,
				     struct v4l2_selection *s)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct mtk_q_data *q_data;

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	q_data = mtk_venc_get_q_data(ctx, s->type);
	if (!q_data)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
		/* Only support crop from (0,0) */
		s->r.top = 0;
		s->r.left = 0;
		s->r.width = min(s->r.width, q_data->coded_width);
		s->r.height = min(s->r.height, q_data->coded_height);
		q_data->visible_width = s->r.width;
		q_data->visible_height = s->r.height;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int vidioc_venc_qbuf(struct file *file, void *priv,
			    struct v4l2_buffer *buf)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);

	if (ctx->state == MTK_STATE_ABORT) {
		mtk_v4l2_err("[%d] Call on QBUF after unrecoverable error",
				ctx->id);
		return -EIO;
	}

	// Check if need to proceed cache operations
	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, buf->type);
	if (buf->index >= vq->num_buffers) {
		mtk_v4l2_err("[%d] buffer index %d out of range %d",
			ctx->id, buf->index, vq->num_buffers);
		return -EINVAL;
	}
	vb = vq->bufs[buf->index];
	vb2_v4l2 = container_of(vb, struct vb2_v4l2_buffer, vb2_buf);
	mtkbuf = container_of(vb2_v4l2, struct mtk_video_enc_buf, vb);

	if (buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		if (IS_ERR_OR_NULL(buf->m.planes)) {
			mtk_v4l2_err("[%d] buffer planes address %p %llx can not access",
				ctx->id, buf->m.planes, buf->m.planes);
			return -EIO;
		}
		if (buf->m.planes[0].bytesused == 0) {
			mtkbuf->lastframe = EOS;
			mtk_v4l2_debug(1, "[%d] index=%d Eos FB(%d,%d) vb=%p pts=%llu",
				ctx->id, buf->index,
				buf->bytesused,
				buf->length, vb, vb->timestamp);
		} else if (buf->flags & V4L2_BUF_FLAG_LAST) {
			mtkbuf->lastframe = EOS_WITH_DATA;
			mtk_v4l2_debug(1, "[%d] id=%d EarlyEos FB(%d,%d) vb=%p pts=%llu",
				ctx->id, buf->index, buf->m.planes[0].bytesused,
				buf->length, vb, vb->timestamp);
		} else {
			mtkbuf->lastframe = NON_EOS;
			mtk_v4l2_debug(1, "[%d] id=%d getdata FB(%d,%d) vb=%p pts=%llu ",
				ctx->id, buf->index,
				buf->m.planes[0].bytesused,
				buf->length, mtkbuf, vb->timestamp);
		}
	} else
		mtk_v4l2_debug(1, "[%d] id=%d BS (%d) vb=%p",
				ctx->id, buf->index,
				buf->length, mtkbuf);

	if (buf->flags & V4L2_BUF_FLAG_NO_CACHE_CLEAN) {
		mtk_v4l2_debug(4, "[%d] No need for Cache clean, buf->index:%d. mtkbuf:%p",
		   ctx->id, buf->index, mtkbuf);
		mtkbuf->flags |= NO_CAHCE_CLEAN;
	}

	if (buf->flags & V4L2_BUF_FLAG_NO_CACHE_INVALIDATE) {
		mtk_v4l2_debug(4, "[%d] No need for Cache invalidate, buf->index:%d. mtkbuf:%p",
		   ctx->id, buf->index, mtkbuf);
		mtkbuf->flags |= NO_CAHCE_INVALIDATE;
	}

	mtkbuf->frm_buf.has_qpmap = 0;
	mtkbuf->frm_buf.has_meta = 0;
	mtkbuf->frm_buf.qpmap_dma = 0;
	mtkbuf->frm_buf.metabuffer_dma = 0;

	if (buf->flags & V4L2_BUF_FLAG_HDR_META && buf->reserved != 0) {
		mtkbuf->frm_buf.has_meta = 1;
		mtkbuf->frm_buf.meta_dma = dma_buf_get(buf->reserved);

		if (IS_ERR(mtkbuf->frm_buf.meta_dma)) {
			mtk_v4l2_err("%s meta_dma is err 0x%p.\n", __func__,
				mtkbuf->frm_buf.meta_dma);

			mtk_venc_queue_error_event(ctx);
			return -EINVAL;
		}

		mtkbuf->frm_buf.buf_att = dma_buf_attach(mtkbuf->frm_buf.meta_dma,
			&ctx->dev->plat_dev->dev);
		mtkbuf->frm_buf.sgt = dma_buf_map_attachment(mtkbuf->frm_buf.buf_att,
			DMA_TO_DEVICE);
		if (IS_ERR_OR_NULL(mtkbuf->frm_buf.sgt)) {
			mtk_v4l2_err("dma_buf_map_attachment fail %d.\n",
				mtkbuf->frm_buf.sgt);
			dma_buf_detach(mtkbuf->frm_buf.meta_dma, mtkbuf->frm_buf.buf_att);
			return -EINVAL;
		}
		mtkbuf->frm_buf.meta_addr = sg_dma_address(mtkbuf->frm_buf.sgt->sgl);

		mtk_v4l2_debug(1, "[%d] Have HDR info meta fd, buf->index:%d. mtkbuf:%p, fd:%u",
			ctx->id, buf->index, mtkbuf, buf->reserved);
	}

	if (buf->flags & V4L2_BUF_FLAG_QP_META &&
		buf->reserved > 0 &&
		buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		struct device *dev = NULL;

		mtkbuf->frm_buf.qpmap_dma = dma_buf_get(buf->reserved);
		if (IS_ERR(mtkbuf->frm_buf.qpmap_dma)) {
			mtk_v4l2_err("%s qpmap_dma is err 0x%p.\n", __func__,
				mtkbuf->frm_buf.qpmap_dma);
			mtk_venc_queue_error_event(ctx);
			return -EINVAL;
		}

		dev = ctx->m2m_ctx->cap_q_ctx.q.dev;
		/* use vcp & vcu compatible access device */

		mtkbuf->frm_buf.qpmap_dma_att = dma_buf_attach(mtkbuf->frm_buf.qpmap_dma,
			dev);
		mtkbuf->frm_buf.qpmap_sgt = dma_buf_map_attachment(mtkbuf->frm_buf.qpmap_dma_att,
			DMA_TO_DEVICE);
		if (IS_ERR_OR_NULL(mtkbuf->frm_buf.qpmap_sgt)) {
			mtk_v4l2_err("dma_buf_map_attachment fail %d.\n",
				mtkbuf->frm_buf.qpmap_sgt);
			dma_buf_detach(mtkbuf->frm_buf.qpmap_dma, mtkbuf->frm_buf.qpmap_dma_att);
			return -EINVAL;
		}
		mtkbuf->frm_buf.qpmap_dma_addr = sg_dma_address(mtkbuf->frm_buf.qpmap_sgt->sgl);
		mtkbuf->frm_buf.has_qpmap = 1;
		mtk_v4l2_debug(1, "[%d] Have Qpmap fd, buf->index:%d, qpmap_dma:%p, fd:%u",
			ctx->id, buf->index, mtkbuf->frm_buf.qpmap_dma, buf->reserved);
	}

	if (buf->flags & V4L2_BUF_FLAG_HAS_META &&
		buf->reserved > 0 &&
		buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		struct dma_buf_attachment *meta_buf_att;
		struct sg_table *meta_sgt;
		struct metadata_info *meta_info;
		void *meta_va = NULL;
		int index = 0;
		struct meta_describe meta_desc;
		struct device *dev = NULL;

		mtkbuf->frm_buf.metabuffer_dma = dma_buf_get(buf->reserved);
		if (IS_ERR(mtkbuf->frm_buf.metabuffer_dma)) {
			mtk_v4l2_err("%s metabuffer_dma is err 0x%p.\n", __func__,
				mtkbuf->frm_buf.metabuffer_dma);
			mtk_venc_queue_error_event(ctx);
			return -EINVAL;
		}

		dev = ctx->m2m_ctx->cap_q_ctx.q.dev;
		/* use vcp & vcu compatible access device */

		meta_buf_att = dma_buf_attach(mtkbuf->frm_buf.metabuffer_dma,
			dev);
		meta_sgt = dma_buf_map_attachment(meta_buf_att, DMA_TO_DEVICE);
		if (IS_ERR_OR_NULL(meta_sgt)) {
			mtk_v4l2_err("dma_buf_map_attachment fail %d.\n", meta_sgt);
			dma_buf_detach(mtkbuf->frm_buf.metabuffer_dma, meta_buf_att);
			dma_buf_put(mtkbuf->frm_buf.metabuffer_dma);
			return -EINVAL;
		}

		mtkbuf->frm_buf.metabuffer_addr = sg_dma_address(meta_sgt->sgl);
		//check required size before doing va mapping
		if (mtkbuf->frm_buf.metabuffer_dma->size < sizeof(struct metadata_info)) {
			mtk_v4l2_err("V4L2_BUF_FLAG_HAS_META dma size check failed");
			dma_buf_unmap_attachment(meta_buf_att, meta_sgt, DMA_TO_DEVICE);
			dma_buf_detach(mtkbuf->frm_buf.metabuffer_dma, meta_buf_att);
			dma_buf_put(mtkbuf->frm_buf.metabuffer_dma);
			return -EINVAL;
		}
		meta_va = dma_buf_vmap(mtkbuf->frm_buf.metabuffer_dma);

		mtk_v4l2_debug(2, "V4L2_BUF_FLAG_HAS_META  buf->reserved:%d dma_buf=%p, DMA=%lx",
			buf->reserved, mtkbuf->frm_buf.metabuffer_dma,
			(unsigned long)mtkbuf->frm_buf.metabuffer_addr);

		if (meta_va) {
			meta_info = (struct metadata_info *)meta_va;
		} else {
			mtk_v4l2_err("V4L2_BUF_FLAG_HAS_META meta_va is NULL");
			dma_buf_unmap_attachment(meta_buf_att, meta_sgt, DMA_TO_DEVICE);
			dma_buf_detach(mtkbuf->frm_buf.metabuffer_dma, meta_buf_att);
			dma_buf_put(mtkbuf->frm_buf.metabuffer_dma);
			return -EINVAL;
		}

		for (; index < MTK_MAX_METADATA_NUM; index++) {
			memset(&meta_desc, 0, sizeof(meta_desc));
			meta_desc.invalid = meta_info->metadata_dsc[index].invalid;
			if (!meta_desc.invalid)
				break;

			meta_desc.fd_flag = meta_info->metadata_dsc[index].fd_flag;
			meta_desc.type = meta_info->metadata_dsc[index].type;
			meta_desc.size = meta_info->metadata_dsc[index].size;
			meta_desc.value = meta_info->metadata_dsc[index].value;

			mtk_v4l2_debug(1, "meta data info,index:%d fd_flag:%u type:%u size:%u val:%u)",
				index, meta_desc.fd_flag, meta_desc.type,
				meta_desc.size, meta_desc.value);

			if (meta_desc.type == METADATA_QPMAP && !meta_desc.fd_flag) {
				mtk_v4l2_err("qpmap should provide buffer fd");
				continue;
			} else if (meta_desc.type == METADATA_HDR && meta_desc.fd_flag) {
				mtk_v4l2_err("hdr should not provide buffer fd");
				continue;
			}

			if (meta_desc.fd_flag) {
				if (meta_desc.type == METADATA_QPMAP) {
					struct dma_buf_attachment *qpmap_buf_att;
					struct sg_table *qpmap_meta_sgt;

					mtkbuf->frm_buf.qpmap_dma = dma_buf_get(meta_desc.value);

					if (IS_ERR(mtkbuf->frm_buf.qpmap_dma)) {
						mtk_v4l2_err("%s qpmap_dma is err 0x%p.\n",
							__func__, mtkbuf->frm_buf.qpmap_dma);
						mtk_venc_queue_error_event(ctx);
						continue;
					}

					qpmap_buf_att = dma_buf_attach(mtkbuf->frm_buf.qpmap_dma,
						dev);
					qpmap_meta_sgt = dma_buf_map_attachment(qpmap_buf_att,
						DMA_TO_DEVICE);
					if (IS_ERR_OR_NULL(qpmap_meta_sgt)) {
						mtk_v4l2_err("dma_buf_map_attachment fail %d.\n",
							qpmap_meta_sgt);
						dma_buf_detach(mtkbuf->frm_buf.qpmap_dma,
							qpmap_buf_att);
						dma_buf_put(mtkbuf->frm_buf.qpmap_dma);
						continue;
					}
					mtkbuf->frm_buf.qpmap_dma_addr =
						sg_dma_address(qpmap_meta_sgt->sgl);
					mtkbuf->frm_buf.has_qpmap = 1;
					dma_buf_unmap_attachment(qpmap_buf_att,
						qpmap_meta_sgt, DMA_TO_DEVICE);
					dma_buf_detach(mtkbuf->frm_buf.qpmap_dma, qpmap_buf_att);
					mtk_v4l2_debug(2, "[%d] Have Qpmap fd, buf->index:%d. qpmap_dma:%p, fd:%u",
						ctx->id, buf->index,
						mtkbuf->frm_buf.qpmap_dma, meta_desc.value);
				}
			} else {
				if (meta_desc.type == METADATA_HDR) {
					mtkbuf->frm_buf.has_meta = 1;
					mtkbuf->frm_buf.meta_dma = mtkbuf->frm_buf.metabuffer_dma;
					mtkbuf->frm_buf.meta_addr =
						mtkbuf->frm_buf.metabuffer_addr + meta_desc.value;
					//vpud use fd to get va and pa,we should add a
					//offset to get real address of hdr
					mtkbuf->frm_buf.meta_offset = meta_desc.value;
				}
			}
		}
		dma_buf_vunmap(mtkbuf->frm_buf.metabuffer_dma, meta_va);
		dma_buf_unmap_attachment(meta_buf_att, meta_sgt, DMA_TO_DEVICE);
		dma_buf_detach(mtkbuf->frm_buf.metabuffer_dma, meta_buf_att);
	}

	return v4l2_m2m_qbuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_venc_dqbuf(struct file *file, void *priv,
			     struct v4l2_buffer *buf)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);

	if (ctx->state == MTK_STATE_ABORT) {
		mtk_v4l2_err("[%d] Call on QBUF after unrecoverable error",
				ctx->id);
		return -EIO;
	}

	return v4l2_m2m_dqbuf(file, ctx->m2m_ctx, buf);
}

const struct v4l2_ioctl_ops mtk_venc_ioctl_ops = {
	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,

	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf			= vidioc_venc_qbuf,
	.vidioc_dqbuf			= vidioc_venc_dqbuf,

	.vidioc_querycap		= vidioc_venc_querycap,
	.vidioc_enum_fmt_vid_cap	= vidioc_enum_fmt_vid_cap,
	.vidioc_enum_fmt_vid_out	= vidioc_enum_fmt_vid_out,
	.vidioc_enum_framesizes		= vidioc_enum_framesizes,

	.vidioc_try_fmt_vid_cap_mplane	= vidioc_try_fmt_vid_cap_mplane,
	.vidioc_try_fmt_vid_out_mplane	= vidioc_try_fmt_vid_out_mplane,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,

	.vidioc_s_parm			= vidioc_venc_s_parm,
	.vidioc_g_parm			= vidioc_venc_g_parm,
	.vidioc_s_fmt_vid_cap_mplane	= vidioc_venc_s_fmt_cap,
	.vidioc_s_fmt_vid_out_mplane	= vidioc_venc_s_fmt_out,

	.vidioc_g_fmt_vid_cap_mplane	= vidioc_venc_g_fmt,
	.vidioc_g_fmt_vid_out_mplane	= vidioc_venc_g_fmt,

	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf		= v4l2_m2m_ioctl_prepare_buf,

	.vidioc_g_selection		= vidioc_venc_g_selection,
	.vidioc_s_selection		= vidioc_venc_s_selection,
};

static int vb2ops_venc_queue_setup(struct vb2_queue *vq,
				   unsigned int *nbuffers,
				   unsigned int *nplanes,
				   unsigned int sizes[],
				   struct device *alloc_devs[])
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(vq);
	struct mtk_q_data *q_data;
	unsigned int i;

	q_data = mtk_venc_get_q_data(ctx, vq->type);

	if (q_data == NULL)
		return -EINVAL;

	if (*nplanes) {
		if (*nplanes != q_data->fmt->num_planes)
			return -EINVAL;
		for (i = 0; i < *nplanes; i++)
			if (sizes[i] < q_data->sizeimage[i])
				return -EINVAL;
	} else {
		*nplanes = q_data->fmt->num_planes;
		for (i = 0; i < *nplanes; i++) {
			sizes[i] = q_data->sizeimage[i];
			if (sizes[i] == 0) {
				mtk_v4l2_err("plane size[%d] is 0", i);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int vb2ops_venc_buf_prepare(struct vb2_buffer *vb)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct mtk_q_data *q_data;
	int i;

	q_data = mtk_venc_get_q_data(ctx, vb->vb2_queue->type);

	for (i = 0; i < q_data->fmt->num_planes; i++) {
		if (vb2_plane_size(vb, i) < q_data->sizeimage[i]) {
			mtk_v4l2_err("data will not fit into plane %d (%lu < %d)",
				i, vb2_plane_size(vb, i),
				q_data->sizeimage[i]);
			return -EINVAL;
		}
	}

	return 0;
}

static void vb2ops_venc_buf_queue(struct vb2_buffer *vb)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vb2_v4l2 =
			container_of(vb, struct vb2_v4l2_buffer, vb2_buf);

	struct mtk_video_enc_buf *mtk_buf =
			container_of(vb2_v4l2, struct mtk_video_enc_buf,
				     m2m_buf.vb);

	if ((vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) &&
	    (ctx->param_change != MTK_ENCODE_PARAM_NONE)) {
		mtk_v4l2_debug(1, "[%d] Before id=%d encode parameter change %x",
			       ctx->id,
			       vb2_v4l2->vb2_buf.index,
			       ctx->param_change);
		mtk_buf->param_change = ctx->param_change;
		mtk_buf->enc_params = ctx->enc_params;
		ctx->param_change = MTK_ENCODE_PARAM_NONE;
	}

	v4l2_m2m_buf_queue(ctx->m2m_ctx, to_vb2_v4l2_buffer(vb));
}

static int vb2ops_venc_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(q);
	struct venc_enc_param param;
	int ret;
	int i;

	/* Once state turn into MTK_STATE_ABORT, we need stop_streaming
	  * to clear it
	  */
	if ((ctx->state == MTK_STATE_ABORT) || (ctx->state == MTK_STATE_FREE)) {
		ret = -EIO;
		goto err_set_param;
	}

	/* Do the initialization when both start_streaming have been called */
	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		if (!vb2_start_streaming_called(&ctx->m2m_ctx->cap_q_ctx.q))
			return 0;
	} else {
		if (!vb2_start_streaming_called(&ctx->m2m_ctx->out_q_ctx.q))
			return 0;
	}

	mtk_venc_set_param(ctx, &param);
	ret = venc_if_set_param(ctx, VENC_SET_PARAM_ENC, &param);
	if (ret) {
		mtk_v4l2_err("venc_if_set_param failed=%d", ret);
		ctx->state = MTK_STATE_ABORT;
		goto err_set_param;
	}
	ctx->param_change = MTK_ENCODE_PARAM_NONE;

	if ((ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc == V4L2_PIX_FMT_H264) &&
	    (ctx->enc_params.seq_hdr_mode !=
				V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE)) {
		ret = venc_if_set_param(ctx,
					VENC_SET_PARAM_PREPEND_HEADER,
					NULL);
		if (ret) {
			mtk_v4l2_err("venc_if_set_param failed=%d", ret);
			ctx->state = MTK_STATE_ABORT;
			goto err_set_param;
		}
		ctx->state = MTK_STATE_HEADER;
	}

	mutex_lock(&ctx->dev->enc_dvfs_mutex);
	mtk_venc_dvfs_begin_inst(ctx);
	mtk_venc_pmqos_begin_inst(ctx);
	mutex_unlock(&ctx->dev->enc_dvfs_mutex);

	kfree(param);
	return 0;

err_set_param:
	for (i = 0; i < q->num_buffers; ++i) {
		struct vb2_buffer *buf = vb2_get_buffer(q, i);

		/*
		 * FIXME: This check is not needed as only active buffers
		 * can be marked as done.
		 */
		if (buf->state == VB2_BUF_STATE_ACTIVE) {
			mtk_v4l2_debug(0, "[%d] id=%d, type=%d, %d -> VB2_BUF_STATE_QUEUED",
					ctx->id, i, q->type,
					(int)buf->state);
			v4l2_m2m_buf_done(to_vb2_v4l2_buffer(buf),
					  VB2_BUF_STATE_QUEUED);
		}
	}

	return ret;
}

static void vb2ops_venc_stop_streaming(struct vb2_queue *q)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(q);
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	int ret;

	mtk_v4l2_debug(2, "[%d]-> type=%d", ctx->id, q->type);

	if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		while ((dst_buf = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx))) {
			dst_buf->vb2_buf.planes[0].bytesused = 0;
			v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_ERROR);
		}
	} else {
		while ((src_buf = v4l2_m2m_src_buf_remove(ctx->m2m_ctx)))
			v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
	}

	if ((q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
	     vb2_is_streaming(&ctx->m2m_ctx->out_q_ctx.q)) ||
	    (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE &&
	     vb2_is_streaming(&ctx->m2m_ctx->cap_q_ctx.q))) {
		mtk_v4l2_debug(1, "[%d]-> q type %d out=%d cap=%d",
			       ctx->id, q->type,
			       vb2_is_streaming(&ctx->m2m_ctx->out_q_ctx.q),
			       vb2_is_streaming(&ctx->m2m_ctx->cap_q_ctx.q));
		return;
	}

	/* Release the encoder if both streams are stopped. */
	ret = venc_if_deinit(ctx);
	if (ret)
		mtk_v4l2_err("venc_if_deinit failed=%d", ret);

	ctx->state = MTK_STATE_FREE;
}

static int vb2ops_venc_buf_out_validate(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	vbuf->field = V4L2_FIELD_NONE;
	return 0;
}

static const struct vb2_ops mtk_venc_vb2_ops = {
	.queue_setup		= vb2ops_venc_queue_setup,
	.buf_out_validate	= vb2ops_venc_buf_out_validate,
	.buf_prepare		= vb2ops_venc_buf_prepare,
	.buf_queue		= vb2ops_venc_buf_queue,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
	.start_streaming	= vb2ops_venc_start_streaming,
	.stop_streaming		= vb2ops_venc_stop_streaming,
};

static int mtk_venc_encode_header(void *priv)
{
	struct mtk_vcodec_ctx *ctx = priv;
	int ret;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct mtk_vcodec_mem bs_buf;
	struct venc_done_result enc_result;

	dst_buf = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);
	if (!dst_buf) {
		mtk_v4l2_debug(1, "No dst buffer");
		return -EINVAL;
	}

	bs_buf.va = vb2_plane_vaddr(&dst_buf->vb2_buf, 0);
	bs_buf.dma_addr = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	bs_buf.size = (size_t)dst_buf->vb2_buf.planes[0].length;

	mtk_v4l2_debug(1,
			"[%d] buf id=%d va=0x%p dma_addr=0x%llx size=%zu",
			ctx->id,
			dst_buf->vb2_buf.index, bs_buf.va,
			(u64)bs_buf.dma_addr,
			bs_buf.size);

	ret = venc_if_encode(ctx,
			VENC_START_OPT_ENCODE_SEQUENCE_HEADER,
			NULL, &bs_buf, &enc_result);

	if (ret) {
		dst_buf->vb2_buf.planes[0].bytesused = 0;
		ctx->state = MTK_STATE_ABORT;
		v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_ERROR);
		mtk_v4l2_err("venc_if_encode failed=%d", ret);
		return -EINVAL;
	}
	src_buf = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	if (src_buf) {
		dst_buf->vb2_buf.timestamp = src_buf->vb2_buf.timestamp;
		dst_buf->timecode = src_buf->timecode;
	} else {
		mtk_v4l2_err("No timestamp for the header buffer.");
	}

	ctx->state = MTK_STATE_HEADER;
	dst_buf->vb2_buf.planes[0].bytesused = enc_result.bs_size;
	v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_DONE);

	return 0;
}

static int mtk_venc_param_change(struct mtk_vcodec_ctx *ctx)
{
	struct venc_enc_param enc_prm;
	struct vb2_v4l2_buffer *vb2_v4l2 = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	struct mtk_video_enc_buf *mtk_buf =
			container_of(vb2_v4l2, struct mtk_video_enc_buf,
				     m2m_buf.vb);

	int ret = 0;

	memset(&enc_prm, 0, sizeof(enc_prm));
	if (mtk_buf->param_change == MTK_ENCODE_PARAM_NONE)
		return 0;

	if (mtk_buf->param_change & MTK_ENCODE_PARAM_BITRATE) {
		enc_prm.bitrate = mtk_buf->enc_params.bitrate;
		mtk_v4l2_debug(1, "[%d] id=%d, change param br=%d",
				ctx->id,
				vb2_v4l2->vb2_buf.index,
				enc_prm.bitrate);
		ret |= venc_if_set_param(ctx,
					 VENC_SET_PARAM_ADJUST_BITRATE,
					 &enc_prm);
	}
	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_FRAMERATE) {
		enc_prm.frm_rate = mtk_buf->enc_params.framerate_num /
				   mtk_buf->enc_params.framerate_denom;
		mtk_v4l2_debug(1, "[%d] id=%d, change param fr=%d",
			       ctx->id,
			       vb2_v4l2->vb2_buf.index,
			       enc_prm.frm_rate);
		ret |= venc_if_set_param(ctx,
					 VENC_SET_PARAM_ADJUST_FRAMERATE,
					 &enc_prm);
	}
	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_GOP_SIZE) {
		enc_prm.gop_size = mtk_buf->enc_params.gop_size;
		mtk_v4l2_debug(1, "change param intra period=%d",
			       enc_prm.gop_size);
		ret |= venc_if_set_param(ctx,
					 VENC_SET_PARAM_GOP_SIZE,
					 &enc_prm);
	}
	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_FORCE_INTRA) {
		mtk_v4l2_debug(1, "[%d] id=%d, change param force I=%d",
				ctx->id,
				vb2_v4l2->vb2_buf.index,
				mtk_buf->enc_params.force_intra);
		if (mtk_buf->enc_params.force_intra)
			ret |= venc_if_set_param(ctx,
						 VENC_SET_PARAM_FORCE_INTRA,
						 NULL);
	}

	mtk_buf->param_change = MTK_ENCODE_PARAM_NONE;

	if (ret) {
		ctx->state = MTK_STATE_ABORT;
		mtk_v4l2_err("venc_if_set_param %d failed=%d",
				mtk_buf->param_change, ret);
		return -1;
	}

	return 0;
}

/*
 * v4l2_m2m_streamoff() holds dev_mutex and waits mtk_venc_worker()
 * to call v4l2_m2m_job_finish().
 * If mtk_venc_worker() tries to acquire dev_mutex, it will deadlock.
 * So this function must not try to acquire dev->dev_mutex.
 * This means v4l2 ioctls and mtk_venc_worker() can run at the same time.
 * mtk_venc_worker() should be carefully implemented to avoid bugs.
 */
static void mtk_venc_worker(struct work_struct *work)
{
	struct mtk_vcodec_ctx *ctx = container_of(work, struct mtk_vcodec_ctx,
				    encode_work);
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct venc_frm_buf frm_buf;
	struct mtk_vcodec_mem bs_buf;
	struct venc_done_result enc_result;
	int ret, i;

	/* check dst_buf, dst_buf may be removed in device_run
	 * to stored encdoe header so we need check dst_buf and
	 * call job_finish here to prevent recursion
	 */
	dst_buf = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);
	if (!dst_buf) {
		v4l2_m2m_job_finish(ctx->dev->m2m_dev_enc, ctx->m2m_ctx);
		return;
	}

	src_buf = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
	memset(&frm_buf, 0, sizeof(frm_buf));
	for (i = 0; i < src_buf->vb2_buf.num_planes ; i++) {
		frm_buf.fb_addr[i].dma_addr =
				vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, i);
		frm_buf.fb_addr[i].size =
				(size_t)src_buf->vb2_buf.planes[i].length;
	}
	src_buf = &src_vb2_v4l2->vb2_buf;
	dst_buf = &dst_vb2_v4l2->vb2_buf;

	src_buf_info = container_of(src_vb2_v4l2, struct mtk_video_enc_buf, vb);
	dst_buf_info = container_of(dst_vb2_v4l2, struct mtk_video_enc_buf, vb);

	pbs_buf = &dst_buf_info->bs_buf;
	pfrm_buf = &src_buf_info->frm_buf;

	if (mtk_v4l2_dbg_level > 0)
		pbs_buf->va = vb2_plane_vaddr(dst_buf, 0);
	pbs_buf->dma_addr = vb2_dma_contig_plane_dma_addr(dst_buf, 0);
	pbs_buf->size = (size_t)dst_buf->planes[0].length;
	pbs_buf->dmabuf = dst_buf->planes[0].dbuf;
	pbs_buf->index = dst_buf->index;
	ctx->bs_list[pbs_buf->index + 1] = (uintptr_t)pbs_buf;

	if (src_buf_info->lastframe == EOS) {
		src_buf_info->lastframe = NON_EOS;
		if (ctx->oal_vcodec == 1) {
			ret = venc_if_encode(ctx,
					 VENC_START_OPT_ENCODE_FRAME_FINAL,
					 NULL, pbs_buf, &enc_result);

			pend_src_vb2_v4l2 =
				to_vb2_v4l2_buffer(ctx->pend_src_buf);
			dst_vb2_v4l2->flags |= pend_src_vb2_v4l2->flags;
			dst_vb2_v4l2->vb2_buf.timestamp =
				pend_src_vb2_v4l2->vb2_buf.timestamp;
			dst_vb2_v4l2->timecode = pend_src_vb2_v4l2->timecode;
			dst_vb2_v4l2->sequence = pend_src_vb2_v4l2->sequence;
			dst_vb2_v4l2->flags |= V4L2_BUF_FLAG_LAST;
			if (enc_result.is_key_frm)
				dst_vb2_v4l2->flags |= V4L2_BUF_FLAG_KEYFRAME;

			if (ret) {
				dst_buf->planes[0].bytesused = 0;
				v4l2_m2m_buf_done(pend_src_vb2_v4l2,
						VB2_BUF_STATE_ERROR);
				v4l2_m2m_buf_done(dst_vb2_v4l2,
						VB2_BUF_STATE_ERROR);
				mtk_v4l2_err("last venc_if_encode failed=%d",
									ret);
				if (ret == -EIO) {
					ctx->state = MTK_STATE_ABORT;
					mtk_venc_queue_error_event(ctx);
					venc_check_release_lock(ctx);
				}
			} else {
				dst_buf->planes[0].bytesused =
							enc_result.bs_size;
				v4l2_m2m_buf_done(pend_src_vb2_v4l2,
							VB2_BUF_STATE_DONE);
				v4l2_m2m_buf_done(dst_vb2_v4l2,
							VB2_BUF_STATE_DONE);
			}

			ctx->pend_src_buf = NULL;
		} else {
			ret = venc_if_encode(ctx,
					VENC_START_OPT_ENCODE_FRAME_FINAL,
					NULL, NULL, &enc_result);
			dst_vb2_v4l2->vb2_buf.timestamp =
				src_vb2_v4l2->vb2_buf.timestamp;
			dst_vb2_v4l2->timecode = src_vb2_v4l2->timecode;
			dst_vb2_v4l2->flags |= V4L2_BUF_FLAG_LAST;
			dst_buf->planes[0].bytesused = 0;

			if (ret) {
				mtk_v4l2_err("last venc_if_encode failed=%d",
									ret);
				if (ret == -EIO) {
					ctx->state = MTK_STATE_ABORT;
					mtk_venc_queue_error_event(ctx);
					venc_check_release_lock(ctx);
				}
			} else if (!ctx->async_mode)
				mtk_enc_put_buf(ctx);

			mtk_venc_check_queue_cnt(ctx, src_buf->vb2_queue);
			mtk_venc_check_queue_cnt(ctx, dst_buf->vb2_queue);

			v4l2_m2m_buf_done(dst_vb2_v4l2,
				VB2_BUF_STATE_DONE);
		}
		mtk_vdec_queue_stop_enc_event(ctx);

		if (src_buf->planes[0].bytesused == 0U) {
			src_vb2_v4l2->flags |= V4L2_BUF_FLAG_LAST;
			vb2_set_plane_payload(&src_buf_info->vb.vb2_buf, 0, 0);
			v4l2_m2m_buf_done(src_vb2_v4l2,
				VB2_BUF_STATE_DONE);
		}
		v4l2_m2m_job_finish(ctx->dev->m2m_dev_enc, ctx->m2m_ctx);
		mutex_unlock(&ctx->worker_lock);
		return;
	} else if (src_buf_info->lastframe == EOS_WITH_DATA) {
		/*
		 * Getting early eos frame buffer, after encode this
		 * buffer, need to flush encoder. Use the flush_buf
		 * as normal EOS, and flush encoder.
		 */
		mtk_v4l2_debug(0, "[%d] EarlyEos: encode last frame %d",
			ctx->id, src_buf->planes[0].bytesused);
		if (ctx->enc_flush_buf->lastframe == NON_EOS) {
			ctx->enc_flush_buf->lastframe = EOS;
			src_vb2_v4l2->flags |= V4L2_BUF_FLAG_LAST;
			dst_vb2_v4l2->flags |= V4L2_BUF_FLAG_LAST;
			v4l2_m2m_buf_queue_check(ctx->m2m_ctx, &ctx->enc_flush_buf->vb);
		} else {
			mtk_v4l2_debug(1, "Stopping no need to queue enc_flush_buf.");
		}
	}

	for (i = 0; i < src_buf->num_planes ; i++) {
		// always map va for fb dump when encode timeout
		pfrm_buf->fb_addr[i].va = vb2_plane_vaddr(src_buf, i) +
			(size_t)src_buf->planes[i].data_offset;
		pfrm_buf->fb_addr[i].dma_addr =
			vb2_dma_contig_plane_dma_addr(src_buf, i) +
			(size_t)src_buf->planes[i].data_offset;
		pfrm_buf->fb_addr[i].size =
				(size_t)(src_buf->planes[i].bytesused-
				src_buf->planes[i].data_offset);
		pfrm_buf->fb_addr[i].dmabuf =
				src_buf->planes[i].dbuf;
		pfrm_buf->fb_addr[i].data_offset =
				src_buf->planes[i].data_offset;

		mtk_v4l2_debug(2, "fb_addr[%d].va %p, offset %d, dma_addr %p, size %d\n",
			i, pfrm_buf->fb_addr[i].va,
			src_buf->planes[i].data_offset,
			(void *)pfrm_buf->fb_addr[i].dma_addr,
			(int)pfrm_buf->fb_addr[i].size);
	}
	pfrm_buf->num_planes = src_buf->num_planes;
	pfrm_buf->timestamp = src_vb2_v4l2->vb2_buf.timestamp;
	pfrm_buf->index = src_buf->index;
	ctx->fb_list[pfrm_buf->index + 1] = (uintptr_t)pfrm_buf;
	length = q_data_src->coded_width * q_data_src->coded_height;

	mtk_v4l2_debug(2,
			"Framebuf PA=%llx Size=0x%zx;PA=0x%llx Size=0x%zx;PA=0x%llx Size=%zu",
			(u64)frm_buf.fb_addr[0].dma_addr,
			frm_buf.fb_addr[0].size,
			(u64)frm_buf.fb_addr[1].dma_addr,
			frm_buf.fb_addr[1].size,
			(u64)frm_buf.fb_addr[2].dma_addr,
			frm_buf.fb_addr[2].size);

	ret = venc_if_encode(ctx, VENC_START_OPT_ENCODE_FRAME,
			     &frm_buf, &bs_buf, &enc_result);

	dst_buf->vb2_buf.timestamp = src_buf->vb2_buf.timestamp;
	dst_buf->timecode = src_buf->timecode;

	if (enc_result.is_key_frm)
		dst_buf->flags |= V4L2_BUF_FLAG_KEYFRAME;

	if (ret) {
		v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
		dst_buf->vb2_buf.planes[0].bytesused = 0;
		v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_ERROR);
		mtk_v4l2_err("venc_if_encode failed=%d", ret);
	} else {
		v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);
		dst_buf->vb2_buf.planes[0].bytesused = enc_result.bs_size;
		v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_DONE);
		mtk_v4l2_debug(2, "venc_if_encode bs size=%d",
				 enc_result.bs_size);
	}

	v4l2_m2m_job_finish(ctx->dev->m2m_dev_enc, ctx->m2m_ctx);

	mtk_v4l2_debug(1, "<=== src_buf[%d] dst_buf[%d] venc_if_encode ret=%d Size=%u===>",
			src_buf->vb2_buf.index, dst_buf->vb2_buf.index, ret,
			enc_result.bs_size);
}

static void m2mops_venc_device_run(void *priv)
{
	struct mtk_vcodec_ctx *ctx = priv;

	if ((ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc == V4L2_PIX_FMT_H264) &&
	    (ctx->state != MTK_STATE_HEADER)) {
		/* encode h264 sps/pps header */
		mtk_venc_encode_header(ctx);
		queue_work(ctx->dev->encode_workqueue, &ctx->encode_work);
		return;
	}

	mtk_venc_param_change(ctx);
	queue_work(ctx->dev->encode_workqueue, &ctx->encode_work);
}

static int m2mops_venc_job_ready(void *m2m_priv)
{
	struct mtk_vcodec_ctx *ctx = m2m_priv;

	if (ctx->state == MTK_STATE_ABORT || ctx->state == MTK_STATE_FREE) {
		mtk_v4l2_debug(3, "[%d]Not ready: state=0x%x.",
			       ctx->id, ctx->state);
		return 0;
	}

	return 1;
}

static void m2mops_venc_job_abort(void *priv)
{
	struct mtk_vcodec_ctx *ctx = priv;

	ctx->state = MTK_STATE_ABORT;
}

const struct v4l2_m2m_ops mtk_venc_m2m_ops = {
	.device_run	= m2mops_venc_device_run,
	.job_ready	= m2mops_venc_job_ready,
	.job_abort	= m2mops_venc_job_abort,
};

void mtk_vcodec_enc_set_default_params(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_q_data *q_data;

	ctx->m2m_ctx->q_lock = &ctx->dev->dev_mutex;
	ctx->fh.m2m_ctx = ctx->m2m_ctx;
	ctx->fh.ctrl_handler = &ctx->ctrl_hdl;
	INIT_WORK(&ctx->encode_work, mtk_venc_worker);

	ctx->colorspace = V4L2_COLORSPACE_REC709;
	ctx->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	ctx->quantization = V4L2_QUANTIZATION_DEFAULT;
	ctx->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	q_data = &ctx->q_data[MTK_Q_DATA_SRC];
	memset(q_data, 0, sizeof(struct mtk_q_data));
	q_data->visible_width = DFT_CFG_WIDTH;
	q_data->visible_height = DFT_CFG_HEIGHT;
	q_data->coded_width = DFT_CFG_WIDTH;
	q_data->coded_height = DFT_CFG_HEIGHT;
	q_data->field = V4L2_FIELD_NONE;

	q_data->fmt = &ctx->dev->venc_pdata->output_formats[0];

	v4l_bound_align_image(&q_data->coded_width,
				MTK_VENC_MIN_W,
				MTK_VENC_MAX_W, 4,
				&q_data->coded_height,
				MTK_VENC_MIN_H,
				MTK_VENC_MAX_H, 5, 6);

	if (q_data->coded_width < DFT_CFG_WIDTH &&
		(q_data->coded_width + 16) <= MTK_VENC_MAX_W)
		q_data->coded_width += 16;
	if (q_data->coded_height < DFT_CFG_HEIGHT &&
		(q_data->coded_height + 32) <= MTK_VENC_MAX_H)
		q_data->coded_height += 32;

	q_data->sizeimage[0] =
		q_data->coded_width * q_data->coded_height+
		((ALIGN(q_data->coded_width, 16) * 2) * 16);
	q_data->bytesperline[0] = q_data->coded_width;
	q_data->sizeimage[1] =
		(q_data->coded_width * q_data->coded_height) / 2 +
		(ALIGN(q_data->coded_width, 16) * 16);
	q_data->bytesperline[1] = q_data->coded_width;

	q_data = &ctx->q_data[MTK_Q_DATA_DST];
	memset(q_data, 0, sizeof(struct mtk_q_data));
	q_data->coded_width = DFT_CFG_WIDTH;
	q_data->coded_height = DFT_CFG_HEIGHT;
	q_data->fmt = &ctx->dev->venc_pdata->capture_formats[0];
	q_data->field = V4L2_FIELD_NONE;
	ctx->q_data[MTK_Q_DATA_DST].sizeimage[0] =
		DFT_CFG_WIDTH * DFT_CFG_HEIGHT;
	ctx->q_data[MTK_Q_DATA_DST].bytesperline[0] = 0;

	ctx->enc_params.framerate_num = MTK_DEFAULT_FRAMERATE_NUM;
	ctx->enc_params.framerate_denom = MTK_DEFAULT_FRAMERATE_DENOM;
}

int mtk_vcodec_enc_ctrls_setup(struct mtk_vcodec_ctx *ctx)
{
	const struct v4l2_ctrl_ops *ops = &mtk_vcodec_enc_ctrl_ops;
	struct v4l2_ctrl_handler *handler = &ctx->ctrl_hdl;

	v4l2_ctrl_handler_init(handler, MTK_MAX_CTRLS_HINT);

	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MIN_BUFFERS_FOR_OUTPUT,
			  1, 1, 1, 1);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_BITRATE,
			  ctx->dev->venc_pdata->min_bitrate,
			  ctx->dev->venc_pdata->max_bitrate, 1, 4000000);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_B_FRAMES,
			0, 2, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE,
			0, 1, 1, 1);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_H264_MAX_QP,
			0, 51, 1, 51);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_H264_I_PERIOD,
			0, 65535, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_GOP_SIZE,
			0, 65535, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE,
			0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME,
			0, 0, 0, 0);
	v4l2_ctrl_new_std_menu(handler, ops,
			V4L2_CID_MPEG_VIDEO_HEADER_MODE,
			V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
			0, V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE);
	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_H264_PROFILE,
		V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10,
		0, V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE);
	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_HEVC_PROFILE,
		V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
		0, V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN);
	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE,
		V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE,
		0, V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE);
	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_H264_LEVEL,
		V4L2_MPEG_VIDEO_H264_LEVEL_6_2,
		0, V4L2_MPEG_VIDEO_H264_LEVEL_1_0);
	v4l2_ctrl_new_std_menu(handler, ops,
		V4L2_CID_MPEG_VIDEO_HEVC_LEVEL,
		V4L2_MPEG_VIDEO_HEVC_LEVEL_6_2,
		0, V4L2_MPEG_VIDEO_HEVC_LEVEL_1);
	v4l2_ctrl_new_std_menu(handler, ops,
		V4L2_CID_MPEG_VIDEO_HEVC_TIER,
		V4L2_MPEG_VIDEO_HEVC_TIER_HIGH,
		0, V4L2_MPEG_VIDEO_HEVC_TIER_MAIN);
	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL,
		V4L2_MPEG_VIDEO_MPEG4_LEVEL_5,
		0, V4L2_MPEG_VIDEO_MPEG4_LEVEL_0);
	if (handler->error)
		mtk_v4l2_debug(0, "Adding control failed V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL %x %d",
			 V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL, handler->error);

	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
		V4L2_MPEG_VIDEO_BITRATE_MODE_CQ,
		0, V4L2_MPEG_VIDEO_BITRATE_MODE_VBR);
	if (handler->error)
		mtk_v4l2_debug(0, "Adding control failed V4L2_CID_MPEG_VIDEO_BITRATE_MODE %x %d",
			 V4L2_CID_MPEG_VIDEO_BITRATE_MODE, handler->error);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_SCENARIO;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode scenario";
	cfg.min = 0;
	cfg.max = 32;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_NONREFP;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode nonrefp";
	cfg.min = 0;
	cfg.max = 32;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_NONREFP_FREQ;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode nonrefp";
	cfg.min = 0;
	cfg.max = 32;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_DETECTED_FRAMERATE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode detect framerate";
	cfg.min = 0;
	cfg.max = 32;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_RFS_ON;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode slice loss indication";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode slice loss indication";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_OPERATION_RATE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode operation rate";
	cfg.min = 0;
	cfg.max = 2048;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_ROI_ON;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode roi switch";
	cfg.min = 0;
	cfg.max = 8;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_GRID_SIZE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode heif grid size";
	cfg.min = 0;
	cfg.max = (3840<<16)+2176;
	cfg.step = 16;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_COLOR_DESC;
	cfg.type = V4L2_CTRL_TYPE_U32;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode color description for HDR";
	cfg.min = 0x00000000;
	cfg.max = 0xffffffff;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	cfg.dims[0] = (sizeof(struct mtk_color_desc)/sizeof(u32));
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_MAX_WIDTH;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode max width";
	cfg.min = 0;
	cfg.max = 3840;
	cfg.step = 16;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_MAX_HEIGHT;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode max height";
	cfg.min = 0;
	cfg.max = 3840;
	cfg.step = 16;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.i_qp = 51;
	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_RC_I_FRAME_QP;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "I-Frame QP Value";
	cfg.min = 0;
	cfg.max = 51;
	cfg.step = 1;
	cfg.def = ctx->enc_params.i_qp;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.p_qp = 51;
	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_RC_P_FRAME_QP;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "P-Frame QP Value";
	cfg.min = 0;
	cfg.max = 51;
	cfg.step = 1;
	cfg.def = ctx->enc_params.p_qp;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.b_qp = 51;
	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_RC_B_FRAME_QP;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "B-Frame QP Value";
	cfg.min = 0;
	cfg.max = 51;
	cfg.step = 1;
	cfg.def = ctx->enc_params.b_qp;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_SEC_ENCODE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video Sec Encode path";
	cfg.min = 0;
	cfg.max = 2;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	/* g_volatile_ctrl */
	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_ROI_RC_QP;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_VOLATILE |
		V4L2_CTRL_FLAG_READ_ONLY;
	cfg.name = "Video encode roi rc qp";
	cfg.min = 0;
	cfg.max = 2048;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_RESOLUTION_CHANGE;
	cfg.type = V4L2_CTRL_TYPE_U32;
	cfg.flags = V4L2_CTRL_FLAG_VOLATILE |
		V4L2_CTRL_FLAG_READ_ONLY;
	cfg.name = "Video encode resolution change";
	cfg.min = 0x00000000;
	cfg.max = 0x00ffffff;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	cfg.dims[0] = sizeof(struct venc_resolution_change)/sizeof(u32);
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_VIDEO_ENABLE_TSVC;
	cfg.type = V4L2_CTRL_TYPE_U32;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode tsvc";
	cfg.min = 0;
	cfg.max = 15;
	cfg.step = 1;
	cfg.def = 0;
	cfg.dims[0] = 2;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_ENABLE_HIGHQUALITY;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode enable highquality";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.max_qp = -1;
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_RC_MAX_QP;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode max qp";
	cfg.min = -1;
	cfg.max = 51;
	cfg.step = 1;
	cfg.def = ctx->enc_params.max_qp;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.min_qp = -1;
	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_RC_MIN_QP;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode min qp";
	cfg.min = -1;
	cfg.max = 51;
	cfg.step = 1;
	cfg.def = ctx->enc_params.min_qp;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.ip_qpdelta = -1;
	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_RC_I_P_QP_DELTA;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode ip qp delta";
	cfg.min = -1;
	cfg.max = 51;
	cfg.step = 1;
	cfg.def = ctx->enc_params.ip_qpdelta;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	ctx->enc_params.framelvl_qp = -1;
	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_RC_FRAME_LEVEL_QP;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode frame level qp";
	cfg.min = -1;
	cfg.max = 51;
	cfg.step = 1;
	cfg.def = ctx->enc_params.framelvl_qp;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_RC_QP_CONTROL_MODE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode qp control mode";
	cfg.min = 0;
	cfg.max = 8;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ENCODE_ENABLE_DUMMY_NAL;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video encode enable dummynal";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_LOG;
	cfg.type = V4L2_CTRL_TYPE_STRING;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video Log";
	cfg.min = 0;
	cfg.max = 255;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_VCP_PROP;
	cfg.type = V4L2_CTRL_TYPE_STRING;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video VCP Property";
	cfg.min = 0;
	cfg.max = 255;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_GET_LOG;
	cfg.type = V4L2_CTRL_TYPE_STRING;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "Get Video Log";
	cfg.min = 0;
	cfg.max = LOG_PROPERTY_SIZE;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_GET_VCP_PROP;
	cfg.type = V4L2_CTRL_TYPE_STRING;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "Get Video VCP Property";
	cfg.min = 0;
	cfg.max = LOG_PROPERTY_SIZE;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_enc_custom_ctrls_check(handler, &cfg, NULL);

	if (handler->error) {
		mtk_v4l2_err("Init control handler fail %d",
				handler->error);
		return handler->error;
	}

	v4l2_ctrl_handler_setup(&ctx->ctrl_hdl);

	return 0;
}

int mtk_vcodec_enc_queue_init(void *priv, struct vb2_queue *src_vq,
			      struct vb2_queue *dst_vq)
{
	struct mtk_vcodec_ctx *ctx = priv;
	int ret;

	/* Note: VB2_USERPTR works with dma-contig because mt8173
	 * support iommu
	 * https://patchwork.kernel.org/patch/8335461/
	 * https://patchwork.kernel.org/patch/7596181/
	 */
	src_vq->type		= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes	= VB2_DMABUF | VB2_MMAP | VB2_USERPTR;
	src_vq->drv_priv	= ctx;
	src_vq->buf_struct_size = sizeof(struct mtk_video_enc_buf);
	src_vq->ops		= &mtk_venc_vb2_ops;
	src_vq->mem_ops		= &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock		= &ctx->dev->dev_mutex;
	src_vq->dev		= &ctx->dev->plat_dev->dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes	= VB2_DMABUF | VB2_MMAP | VB2_USERPTR;
	dst_vq->drv_priv	= ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops		= &mtk_venc_vb2_ops;
	dst_vq->mem_ops		= &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock		= &ctx->dev->dev_mutex;
	dst_vq->dev		= &ctx->dev->plat_dev->dev;

	return vb2_queue_init(dst_vq);
}

int mtk_venc_unlock(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_vcodec_dev *dev = ctx->dev;

	mutex_unlock(&dev->enc_mutex);
	return 0;
}

int mtk_venc_lock(struct mtk_vcodec_ctx *ctx, u32 hw_id)
{
	struct mtk_vcodec_dev *dev = ctx->dev;

	if (hw_id >= MTK_VENC_HW_NUM)
		return ret;

	if (sec != 0)
		lock = VENC_LOCK_SEC;
	else
		lock = VENC_LOCK_NORMAL;

	mtk_v4l2_debug(4, "ctx %p [%d] hw_id %d sem_cnt %d, sec: %d, lock: %d",
		ctx, ctx->id, hw_id, ctx->dev->enc_sem[hw_id].count, sec,
		ctx->dev->enc_hw_locked[hw_id]);




	if (lock != ctx->dev->enc_hw_locked[hw_id])
		ret = down_trylock(&ctx->dev->enc_sem[hw_id]);
	else
		ret = 0;

	if (ret == 0) {
		ctx->dev->enc_hw_locked[hw_id] = lock;
		ctx->core_locked[hw_id] = 1;
	}

	return ret;

}

void mtk_vcodec_enc_empty_queues(struct file *file, struct mtk_vcodec_ctx *ctx)
{
	struct vb2_buffer *dst_buf = NULL;
	struct vb2_v4l2_buffer *src_vb2_v4l2, *dst_vb2_v4l2;
	struct v4l2_fh *fh = file->private_data;

	// error handle for release before stream-off
	//  stream off both queue mannually.
	v4l2_m2m_streamoff(file, fh->m2m_ctx,
		V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	v4l2_m2m_streamoff(file, fh->m2m_ctx,
		V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	while ((src_vb2_v4l2 = v4l2_m2m_src_buf_remove(ctx->m2m_ctx))) {
		if (src_vb2_v4l2 != &ctx->enc_flush_buf->vb &&
			src_vb2_v4l2->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
			v4l2_m2m_buf_done(src_vb2_v4l2, VB2_BUF_STATE_ERROR);
	}

	while ((dst_vb2_v4l2 = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx))) {
		dst_buf = &dst_vb2_v4l2->vb2_buf;
		dst_buf->planes[0].bytesused = 0;
		if (dst_vb2_v4l2->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
			v4l2_m2m_buf_done(dst_vb2_v4l2, VB2_BUF_STATE_ERROR);
	}

	ctx->state = MTK_STATE_FREE;
}

void mtk_vcodec_enc_release(struct mtk_vcodec_ctx *ctx)
{
	int ret = venc_if_deinit(ctx);

	if (ret)
		mtk_v4l2_err("venc_if_deinit failed=%d", ret);

	ctx->state = MTK_STATE_FREE;
}
