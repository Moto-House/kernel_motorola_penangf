/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Jungchang Tsao <jungchang.tsao@mediatek.com>
 *	   Daniel Hsiao <daniel.hsiao@mediatek.com>
 *	   Tiffany Lin <tiffany.lin@mediatek.com>
 */

#ifndef _VENC_IPI_MSG_H_
#define _VENC_IPI_MSG_H_

#define AP_IPIMSG_VENC_BASE 0xC000
#define VPU_IPIMSG_VENC_BASE 0xD000

/**
 * enum venc_ipi_msg_id - message id between AP and VPU
 * (ipi stands for inter-processor interrupt)
 * @AP_IPIMSG_ENC_XXX:		AP to VPU cmd message id
 * @VPU_IPIMSG_ENC_XXX_DONE:	VPU ack AP cmd message id
 */
enum venc_ipi_msg_id {
	AP_IPIMSG_ENC_INIT = AP_IPIMSG_VENC_BASE,
	AP_IPIMSG_ENC_SET_PARAM,
	AP_IPIMSG_ENC_ENCODE,
	AP_IPIMSG_ENC_DEINIT,

	VPU_IPIMSG_ENC_INIT_DONE = VPU_IPIMSG_VENC_BASE,
	VPU_IPIMSG_ENC_SET_PARAM_DONE,
	VPU_IPIMSG_ENC_ENCODE_DONE,
	VPU_IPIMSG_ENC_DEINIT_DONE,
};

/**
 * struct venc_ap_ipi_msg_init - AP to VPU init cmd structure
 * @msg_id:	message id (AP_IPIMSG_XXX_ENC_INIT)
 * @reserved:	reserved for future use. vpu is running in 32bit. Without
 *		this reserved field, if kernel run in 64bit. this struct size
 *		will be different between kernel and vpu
 * @venc_inst:	AP encoder instance
 *		(struct venc_vp8_inst/venc_h264_inst *)
 */
struct venc_ap_ipi_msg_init {
	uint32_t msg_id;
	uint32_t reserved;
	uint64_t venc_inst;
};

/**
 * struct venc_ap_ipi_msg_set_param - AP to VPU set_param cmd structure
 * @msg_id:	message id (AP_IPIMSG_XXX_ENC_SET_PARAM)
 * @vpu_inst_addr:	VPU encoder instance addr
 *			(struct venc_vp8_vsi/venc_h264_vsi *)
 * @param_id:	parameter id (venc_set_param_type)
 * @data_item:	number of items in the data array
 * @data[8]:	data array to store the set parameters
 */
struct venc_ap_ipi_msg_set_param {
	uint32_t msg_id;
	uint32_t vpu_inst_addr;
	uint32_t param_id;
	uint32_t data_item;
	uint32_t data[8];
};

struct venc_ap_ipi_msg_set_param_ext {
	struct venc_ap_ipi_msg_set_param base;
	uint32_t data_ext[24];
};

/**
 * struct venc_ap_ipi_msg_enc - AP to VPU enc cmd structure
 * @msg_id:	message id (AP_IPIMSG_XXX_ENC_ENCODE)
 * @vpu_inst_addr:	VPU encoder instance addr
 *			(struct venc_vp8_vsi/venc_h264_vsi *)
 * @bs_mode:	bitstream mode for h264
 *		(H264_BS_MODE_SPS/H264_BS_MODE_PPS/H264_BS_MODE_FRAME)
 * @input_addr:	pointer to input image buffer plane
 * @bs_addr:	pointer to output bit stream buffer
 * @bs_size:	bit stream buffer size
 */
struct venc_ap_ipi_msg_enc {
	uint32_t msg_id;
	uint32_t vpu_inst_addr;
	uint32_t bs_mode;
	uint32_t input_addr[3];
	uint32_t bs_addr;
	uint32_t bs_size;
};

/**
 * struct venc_ap_ipi_msg_enc_ext - AP to SCP extended enc cmd structure
 *
 * @base:	base msg structure
 * @data_item:	number of items in the data array
 * @data[8]:	data array to store the set parameters
 */
struct venc_ap_ipi_msg_enc_ext {
	struct venc_ap_ipi_msg_enc base;
	uint32_t data_item;
	uint32_t data[32];
};

/**
 * struct venc_ap_ipi_msg_deinit - AP to VPU deinit cmd structure
 * @msg_id:	message id (AP_IPIMSG_XXX_ENC_DEINIT)
 * @vpu_inst_addr:	VPU encoder instance addr
 *			(struct venc_vp8_vsi/venc_h264_vsi *)
 */
struct venc_ap_ipi_msg_deinit {
	uint32_t msg_id;
	uint32_t vpu_inst_addr;
};

/**
 * enum venc_ipi_msg_status - VPU ack AP cmd status
 */
enum venc_ipi_msg_status {
	VENC_IPI_MSG_STATUS_OK,
	VENC_IPI_MSG_STATUS_FAIL,
};

/**
 * struct venc_vpu_ipi_msg_common - VPU ack AP cmd common structure
 * @msg_id:	message id (VPU_IPIMSG_XXX_DONE)
 * @status:	cmd status (venc_ipi_msg_status)
 * @venc_inst:	AP encoder instance (struct venc_vp8_inst/venc_h264_inst *)
 */
struct venc_vpu_ipi_msg_common {
	uint32_t msg_id;
	uint32_t status;
	uint64_t venc_inst;
};

/**
 * struct venc_vpu_ipi_msg_init - VPU ack AP init cmd structure
 * @msg_id:	message id (VPU_IPIMSG_XXX_ENC_SET_PARAM_DONE)
 * @status:	cmd status (venc_ipi_msg_status)
 * @venc_inst:	AP encoder instance (struct venc_vp8_inst/venc_h264_inst *)
 * @vpu_inst_addr:	VPU encoder instance addr
 *			(struct venc_vp8_vsi/venc_h264_vsi *)
 * @venc_abi_version:	ABI version of the firmware. Kernel can use it to
 *			ensure that it is compatible with the firmware.
 *			For MT8173 the value of this field is undefined and
 *			should not be used.
 */
struct venc_vpu_ipi_msg_init {
	uint32_t msg_id;
	uint32_t status;
	uint64_t venc_inst;
	uint32_t vpu_inst_addr;
	uint32_t venc_abi_version;
};

/**
 * struct venc_vpu_ipi_msg_set_param - VPU ack AP set_param cmd structure
 * @msg_id:	message id (VPU_IPIMSG_XXX_ENC_SET_PARAM_DONE)
 * @status:	cmd status (venc_ipi_msg_status)
 * @venc_inst:	AP encoder instance (struct venc_vp8_inst/venc_h264_inst *)
 * @param_id:	parameter id (venc_set_param_type)
 * @data_item:	number of items in the data array
 * @data[6]:	data array to store the return result
 */
struct venc_vpu_ipi_msg_set_param {
	uint32_t msg_id;
	uint32_t status;
	uint64_t venc_inst;
	uint32_t param_id;
	uint32_t data_item;
	uint32_t data[6];
};

/**
 * enum venc_ipi_msg_enc_state - Type of encode state
 * VEN_IPI_MSG_ENC_STATE_FRAME:	one frame being encoded
 * VEN_IPI_MSG_ENC_STATE_PART:	bit stream buffer full
 * VEN_IPI_MSG_ENC_STATE_SKIP:	encoded skip frame
 * VEN_IPI_MSG_ENC_STATE_ERROR:	encounter error
 */
enum venc_ipi_msg_enc_state {
	VEN_IPI_MSG_ENC_STATE_FRAME,
	VEN_IPI_MSG_ENC_STATE_PART,
	VEN_IPI_MSG_ENC_STATE_SKIP,
	VEN_IPI_MSG_ENC_STATE_ERROR,
};

/**
 * struct venc_vpu_ipi_msg_enc - VPU ack AP enc cmd structure
 * @msg_id:	message id (VPU_IPIMSG_XXX_ENC_ENCODE_DONE)
 * @status:	cmd status (venc_ipi_msg_status)
 * @venc_inst:	AP encoder instance (struct venc_vp8_inst/venc_h264_inst *)
 * @state:	encode state (venc_ipi_msg_enc_state)
 * @is_key_frm:	whether the encoded frame is key frame
 * @bs_size:	encoded bitstream size
 * @reserved:	reserved for future use. vpu is running in 32bit. Without
 *		this reserved field, if kernel run in 64bit. this struct size
 *		will be different between kernel and vpu
 */
struct venc_vpu_ipi_msg_enc {
	uint32_t msg_id;
	uint32_t status;
	uint64_t venc_inst;
	uint32_t state;
	uint32_t is_key_frm;
	uint32_t bs_size;
	uint32_t reserved;
};

/**
 * struct venc_vpu_ipi_msg_deinit - VPU ack AP deinit cmd structure
 * @msg_id:   message id (VPU_IPIMSG_XXX_ENC_DEINIT_DONE)
 * @status:   cmd status (venc_ipi_msg_status)
 * @venc_inst:	AP encoder instance (struct venc_vp8_inst/venc_h264_inst *)
 */
struct venc_vcu_ipi_msg_deinit {
	__u32 msg_id;
	__s32 status;
	__u64 venc_inst;
};

/**
 * struct venc_vcu_ipi_msg_waitisr - VCU to AP wait isr cmd structure
 * @msg_id:   message id (VCU_IPIMSG_XXX_ENC_DEINIT_DONE)
 * @status:   cmd status (venc_ipi_msg_status)
 * @venc_inst:  AP encoder instance (struct venc_vp8_inst/venc_h264_inst *)
 * @irq_status: encoder irq status
 * @timeout: 1 indicate encode timeout, 0 indicate no error
 */
struct venc_vcu_ipi_msg_waitisr {
	__u32 msg_id;
	__s32 status;
	__u64 venc_inst;
	__u32 irq_status;
	__u32 timeout;
};

/**
 * struct venc_vcu_ipi_mem_op -VCU/AP bi-direction memory operation cmd structure
 * @msg_id:   message id (VCU_IPIMSG_XXX_ENC_DEINIT_DONE)
 * @status:   cmd status (venc_ipi_msg_status)
 * @venc_inst:	AP encoder instance (struct venc_inst *)
 * @struct vcodec_mem_obj: encoder memories
 */
struct venc_vcu_ipi_mem_op {
	__u32 msg_id;
	__s32 status;
	__u64 venc_inst;
	struct vcodec_mem_obj mem;
	__u32 reserved[2];
};

/*
 * struct venc_vcu_config - Structure for encoder configuration
 *                               AP-W/R : AP is writer/reader on this item
 *                               VCU-W/R: VCU is write/reader on this item
 * @input_fourcc: input fourcc
 * @bitrate: target bitrate (in bps)
 * @pic_w: picture width. Picture size is visible stream resolution, in pixels,
 *         to be used for display purposes; must be smaller or equal to buffer
 *         size.
 * @pic_h: picture height
 * @buf_w: buffer width. Buffer size is stream resolution in pixels aligned to
 *         hardware requirements.
 * @buf_h: buffer height
 * @gop_size: group of picture size (idr frame)
 * @intra_period: intra frame period
 * @framerate: frame rate in fps
 * @profile: as specified in standard
 * @level: as specified in standard
 * @wfd: WFD mode 1:on, 0:off
 */
struct venc_vcu_config {
	__u32 input_fourcc;
	__u32 bitrate;
	__u32 pic_w;
	__u32 pic_h;
	__u32 buf_w;
	__u32 buf_h;
	__u32 gop_size;
	__u32 intra_period;
	__u32 framerate;
	__u32 profile;
	__u32 level;
	__u32 wfd;
	__u32 operationrate;
	__u32 scenario;
	__u32 prependheader;
	__u32 bitratemode;
	__u32 roi_rc_qp;
	__u32 roion;
	__u32 heif_grid_size;
	struct mtk_color_desc color_desc;
	__u32 resolutionChange;
	__u32 max_w;
	__u32 max_h;
	__u32 num_b_frame;
	__u32 slbc_ready;
	__u32 i_qp;
	__u32 p_qp;
	__u32 b_qp;
	__u32 svp_mode;
	__u32 svp_is_hal_secure_handle;
	__u32 tsvc;
	__u32 max_qp;
	__u32 min_qp;
	__u32 i_p_qp_delta;
	__u32 qp_control_mode;
	__u32 frame_level_qp;
	__u32 highquality;
	__u32 dummynal;
	__u32 slbc_addr;
};

/**
 * enum venc_bs_mode - for bs_mode argument in venc_bs_mode
 */
enum venc_bs_mode {
	VENC_BS_MODE_SPS = 0,
	VENC_BS_MODE_PPS,
	VENC_BS_MODE_SEQ_HDR,
	VENC_BS_MODE_FRAME,
	VENC_BS_MODE_FRAME_FINAL,
	VENC_BS_MODE_MAX
};

/**
 * struct venc_info - encode information
 * @bs_dma		: Input bit-stream buffer dma address
 * @bs_fd           : Input bit-stream buffer dmabuf fd
 * @fb_dma		    : Y frame buffer dma address
 * @fb_fd           : Y frame buffer dmabuf fd
 * @venc_bs_va		: VDEC bitstream buffer struct virtual address
 * @venc_fb_va		: VDEC frame buffer struct virtual address
 * @fb_num_planes	: frame buffer plane count
 * @reserved		: reserved variable for 64bit align
 */
struct venc_info {
	__u64 bs_dma;
	__u64 bs_fd;
	__u64 fb_dma[VIDEO_MAX_PLANES];
	__u64 fb_fd[VIDEO_MAX_PLANES];
	__u64 venc_bs_va;
	__u64 venc_fb_va;
	__u32 fb_num_planes;
	__u32 index;
	__u64 timestamp;
	__u32 qpmap;
};

/**
 * struct ring_input_list - ring input buffer list
 * @venc_bs_va_list   : bitstream buffer arrary
 * @venc_fb_va_list   : frame buffer arrary
 * @read_idx  : read index
 * @write_idx : write index
 * @count     : buffer count in list
 */
struct ring_input_list {
	__u64 venc_bs_va_list[VENC_MAX_FB_NUM];
	__u64 venc_fb_va_list[VENC_MAX_FB_NUM];
	__u32 bs_size[VENC_MAX_FB_NUM];
	__u32 is_key_frm[VENC_MAX_FB_NUM];
	__s32 read_idx;
	__s32 write_idx;
	__s32 count;
	__s32 reserved;
	__u32 flags[VENC_MAX_FB_NUM];
};

/*
 * struct venc_vsi - Structure for VCU driver control and info share
 *                        AP-W/R : AP is writer/reader on this item
 *                        VCU-W/R: VCU is write/reader on this item
 * This structure is allocated in VCU side and shared to AP side.
 * @config: h264 encoder configuration
 * @work_bufs: working buffer information in VCU side
 * The work_bufs here is for storing the 'size' info shared to AP side.
 * The similar item in struct venc_inst is for memory allocation
 * in AP side. The AP driver will copy the 'size' from here to the one in
 * struct mtk_vcodec_mem, then invoke mtk_vcodec_mem_alloc to allocate
 * the buffer. After that, bypass the 'dma_addr' to the 'iova' field here for
 * register setting in VCU side.
 */
struct venc_vsi {
	struct venc_vcu_config config;
	__u32  sizeimage[VIDEO_MAX_PLANES];
	struct ring_input_list list_free;
	struct venc_info       venc;
	__u32 sync_mode;
	__u64 meta_addr;
	__u32 meta_size;
	__u32 meta_offset;
	__u64 qpmap_addr;
	__u32 qpmap_size;
};

#endif /* _VENC_IPI_MSG_H_ */
