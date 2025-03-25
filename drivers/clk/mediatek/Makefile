# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_COMMON_CLK_MEDIATEK) += clk-mtk.o clk-pll.o clk-gate.o clk-apmixed.o clk-cpumux.o reset.o clk-mux.o

ifneq ($(wildcard $(srctree)/$(src)/clk-mt6739.c),)
obj-$(CONFIG_COMMON_CLK_MT6739) += clk-mt6739.o
obj-$(CONFIG_COMMON_CLK_MT6739) += mt6739_clkmgr.o
obj-$(CONFIG_COMMON_CLK_MT6739) += clk-mt6739-pg.o
clk-clkmgr-mt6739-$(CONFIG_COMMON_CLK_MT6739) += mt6739_clkmgr.o
obj-$(CONFIG_COMMON_CLK_MT6739) += clk-fmeter-mt6739.o
clk-fmeter-mt6739-$(CONFIG_COMMON_CLK_MT6739) += clk-fmeter-mt6739.o
obj-$(CONFIG_COMMON_CLK_MT6739) += clk-fixed-factor-mt6739.o
clk-fixed-factor-mt6739-$(CONFIG_COMMON_CLK_MT6739) += clk-fixed-factor-mt6739.o
$(warning go this for clk CONFIG_COMMON_CLK_MT6739="$(CONFIG_COMMON_CLK_MT6739)")
$(warning go this for clk CONFIG_MTK_HANG_DETECT="$(CONFIG_MTK_HANG_DETECT)")

ifeq ($(CONFIG_DEBUG_FS), y)
obj-$(CONFIG_COMMON_CLK_MT6739) += clkdbg_mt6739.o
clk-dbg-mt6739-$(CONFIG_COMMON_CLK_MT6739) += clkdbg_mt6739.o
endif
endif

ifneq ($(wildcard $(srctree)/$(src)/clk-mt6761.c),)
obj-$(CONFIG_COMMON_CLK_MT6761) += clk-mt6761.o
obj-$(CONFIG_COMMON_CLK_MT6761_AUDIOSYS) += clk-mt6761-audio.o
obj-$(CONFIG_COMMON_CLK_MT6761_CAMSYS) += clk-mt6761-cam.o
obj-$(CONFIG_COMMON_CLK_MT6761_MIPI0ASYS) += clk-mt6761-mipi0a.o
obj-$(CONFIG_COMMON_CLK_MT6761_MMSYS) += clk-mt6761-mm.o
obj-$(CONFIG_COMMON_CLK_MT6761_VCODECSYS) += clk-mt6761-vcodec.o
obj-$(CONFIG_COMMON_CLK_MT6761) += clk-mt6761-pg.o
obj-$(CONFIG_COMMON_CLK_MT6761) += clkchk-mt6761.o
obj-$(CONFIG_COMMON_CLK_MT6761) += mt6761_clkmgr.o
obj-$(CONFIG_COMMON_CLK_MT6761) += clk-fmeter-mt6761.o
obj-$(CONFIG_COMMON_CLK_MT6761) += pd-chk-mt6761.o
pd-chk-mt6761-$(CONFIG_COMMON_CLK_MT6761) += mtk-pd-chk-mt6761.o
$(warning go this for clk CONFIG_COMMON_CLK_MT6761="$(CONFIG_COMMON_CLK_MT6761)")
$(warning go this for clk CONFIG_MTK_HANG_DETECT="$(CONFIG_MTK_HANG_DETECT)")

ifeq ($(CONFIG_DEBUG_FS), y)
obj-$(CONFIG_COMMON_CLK_MT6761) += clkdbg-mt6761.o
clk-dbg-mt6761-$(CONFIG_COMMON_CLK_MT6761) += clkdbg-mt6761.o
endif
endif

ifneq ($(wildcard $(srctree)/$(src)/clk-mt6765.c),)
obj-$(CONFIG_COMMON_CLK_MT6765) += clk-mt6765.o
obj-$(CONFIG_COMMON_CLK_MT6765_AUDIOSYS) += clk-mt6765-audio.o
obj-$(CONFIG_COMMON_CLK_MT6765_CAMSYS) += clk-mt6765-cam.o
obj-$(CONFIG_COMMON_CLK_MT6765_IMGSYS) += clk-mt6765-img.o
obj-$(CONFIG_COMMON_CLK_MT6765_MIPI0ASYS) += clk-mt6765-mipi0a.o
obj-$(CONFIG_COMMON_CLK_MT6765_MMSYS) += clk-mt6765-mm.o
obj-$(CONFIG_COMMON_CLK_MT6765_VCODECSYS) += clk-mt6765-vcodec.o
obj-$(CONFIG_COMMON_CLK_MT6779) += clk-mt6779.o
obj-$(CONFIG_COMMON_CLK_MT6779_MMSYS) += clk-mt6779-mm.o
obj-$(CONFIG_COMMON_CLK_MT6779_IMGSYS) += clk-mt6779-img.o
obj-$(CONFIG_COMMON_CLK_MT6779_IPESYS) += clk-mt6779-ipe.o
obj-$(CONFIG_COMMON_CLK_MT6779_CAMSYS) += clk-mt6779-cam.o
obj-$(CONFIG_COMMON_CLK_MT6779_VDECSYS) += clk-mt6779-vdec.o
obj-$(CONFIG_COMMON_CLK_MT6779_VENCSYS) += clk-mt6779-venc.o
obj-$(CONFIG_COMMON_CLK_MT6779_MFGCFG) += clk-mt6779-mfg.o
obj-$(CONFIG_COMMON_CLK_MT6779_AUDSYS) += clk-mt6779-aud.o
obj-$(CONFIG_COMMON_CLK_MT6797) += clk-mt6797.o
obj-$(CONFIG_COMMON_CLK_MT6797_IMGSYS) += clk-mt6797-img.o
obj-$(CONFIG_COMMON_CLK_MT6797_MMSYS) += clk-mt6797-mm.o
obj-$(CONFIG_COMMON_CLK_MT6797_VDECSYS) += clk-mt6797-vdec.o
obj-$(CONFIG_COMMON_CLK_MT6797_VENCSYS) += clk-mt6797-venc.o
obj-$(CONFIG_COMMON_CLK_MT2701) += clk-mt2701.o
obj-$(CONFIG_COMMON_CLK_MT2701_AUDSYS) += clk-mt2701-aud.o
obj-$(CONFIG_COMMON_CLK_MT2701_BDPSYS) += clk-mt2701-bdp.o
obj-$(CONFIG_COMMON_CLK_MT2701_ETHSYS) += clk-mt2701-eth.o
obj-$(CONFIG_COMMON_CLK_MT2701_G3DSYS) += clk-mt2701-g3d.o
obj-$(CONFIG_COMMON_CLK_MT2701_HIFSYS) += clk-mt2701-hif.o
obj-$(CONFIG_COMMON_CLK_MT2701_IMGSYS) += clk-mt2701-img.o
obj-$(CONFIG_COMMON_CLK_MT2701_MMSYS) += clk-mt2701-mm.o
obj-$(CONFIG_COMMON_CLK_MT2701_VDECSYS) += clk-mt2701-vdec.o
obj-$(CONFIG_COMMON_CLK_MT2712) += clk-mt2712.o
obj-$(CONFIG_COMMON_CLK_MT2712_BDPSYS) += clk-mt2712-bdp.o
obj-$(CONFIG_COMMON_CLK_MT2712_IMGSYS) += clk-mt2712-img.o
obj-$(CONFIG_COMMON_CLK_MT2712_JPGDECSYS) += clk-mt2712-jpgdec.o
obj-$(CONFIG_COMMON_CLK_MT2712_MFGCFG) += clk-mt2712-mfg.o
obj-$(CONFIG_COMMON_CLK_MT2712_MMSYS) += clk-mt2712-mm.o
obj-$(CONFIG_COMMON_CLK_MT2712_VDECSYS) += clk-mt2712-vdec.o
obj-$(CONFIG_COMMON_CLK_MT2712_VENCSYS) += clk-mt2712-venc.o
ifneq ($(wildcard $(srctree)/$(src)/clk-mt6789.c),)
obj-$(CONFIG_COMMON_CLK_MT6789) += clk-mt6789.o clk-bringup.o
obj-$(CONFIG_COMMON_CLK_MT6789_AUDIOSYS) += clk-mt6789-audiosys.o
obj-$(CONFIG_COMMON_CLK_MT6789_CAM) += clk-mt6789-cam.o
obj-$(CONFIG_COMMON_CLK_MT6789_MMSYS) += clk-mt6789-mmsys.o
obj-$(CONFIG_COMMON_CLK_MT6789_DISP_DSC) += clk-mt6789-disp_dsc.o
obj-$(CONFIG_COMMON_CLK_MT6789_IMG) += clk-mt6789-img.o
obj-$(CONFIG_COMMON_CLK_MT6789_I2C) += clk-mt6789-i2c.o
obj-$(CONFIG_COMMON_CLK_MT6789_IMP_IIC_WRAP_EN) += clk-mt6789-impen.o
obj-$(CONFIG_COMMON_CLK_MT6789_BUS) += clk-mt6789-bus.o
obj-$(CONFIG_COMMON_CLK_MT6789_MFG) += clk-mt6789-mfgcfg.o
obj-$(CONFIG_COMMON_CLK_MT6789_VCODEC) += clk-mt6789-vcodec.o
endif
ifneq ($(wildcard $(srctree)/$(src)/clk-mt6833.c),)
obj-$(CONFIG_COMMON_CLK_MT6833) += clk-mt6833.o clk-bringup.o
obj-$(CONFIG_COMMON_CLK_MT6833_AUDIOSYS) += clk-mt6833-audiosys.o
obj-$(CONFIG_COMMON_CLK_MT6833_CAM) += clk-mt6833-cam.o
obj-$(CONFIG_COMMON_CLK_MT6833_MMSYS) += clk-mt6833-mmsys.o
obj-$(CONFIG_COMMON_CLK_MT6833_IMG) += clk-mt6833-img.o
obj-$(CONFIG_COMMON_CLK_MT6833_I2C) += clk-mt6833-i2c.o
obj-$(CONFIG_COMMON_CLK_MT6833_BUS) += clk-mt6833-bus.o
obj-$(CONFIG_COMMON_CLK_MT6833_MFG) += clk-mt6833-mfgcfg.o
obj-$(CONFIG_COMMON_CLK_MT6833_VCODEC) += clk-mt6833-vcodec.o
endif
ifneq ($(wildcard $(srctree)/$(src)/clk-mt6853.c),)
obj-$(CONFIG_COMMON_CLK_MT6853) += clk-mt6853.o
obj-$(CONFIG_COMMON_CLK_MT6853_APU0) += clk-mt6853-apu0.o
obj-$(CONFIG_COMMON_CLK_MT6853_APU1) += clk-mt6853-apu1.o
obj-$(CONFIG_COMMON_CLK_MT6853_APU_VCORE) += clk-mt6853-apuv.o
obj-$(CONFIG_COMMON_CLK_MT6853_APU_CONN) += clk-mt6853-apuc.o
obj-$(CONFIG_COMMON_CLK_MT6853_AUDIOSYS) += clk-mt6853-audsys.o
obj-$(CONFIG_COMMON_CLK_MT6853_CAMSYS_MAIN) += clk-mt6853-cam_m.o
obj-$(CONFIG_COMMON_CLK_MT6853_CAMSYS_RAWA) += clk-mt6853-cam_ra.o
obj-$(CONFIG_COMMON_CLK_MT6853_CAMSYS_RAWB) += clk-mt6853-cam_rb.o
obj-$(CONFIG_COMMON_CLK_MT6853_IMGSYS1) += clk-mt6853-imgsys1.o
obj-$(CONFIG_COMMON_CLK_MT6853_IMGSYS2) += clk-mt6853-imgsys2.o
obj-$(CONFIG_COMMON_CLK_MT6853_IMP_IIC_WRAP_C) += clk-mt6853-impc.o
obj-$(CONFIG_COMMON_CLK_MT6853_IMP_IIC_WRAP_E) += clk-mt6853-impe.o
obj-$(CONFIG_COMMON_CLK_MT6853_IMP_IIC_WRAP_N) += clk-mt6853-impn.o
obj-$(CONFIG_COMMON_CLK_MT6853_IMP_IIC_WRAP_S) += clk-mt6853-imps.o
obj-$(CONFIG_COMMON_CLK_MT6853_IMP_IIC_WRAP_W) += clk-mt6853-impw.o
obj-$(CONFIG_COMMON_CLK_MT6853_IMP_IIC_WRAP_WS) += clk-mt6853-impws.o
obj-$(CONFIG_COMMON_CLK_MT6853_IPESYS) += clk-mt6853-ipe.o
obj-$(CONFIG_COMMON_CLK_MT6853_MDPSYS_CONFIG) += clk-mt6853-mdp.o
obj-$(CONFIG_COMMON_CLK_MT6853_MFGCFG) += clk-mt6853-mfg.o
obj-$(CONFIG_COMMON_CLK_MT6853_MMSYS_CONFIG) += clk-mt6853-mm.o
obj-$(CONFIG_COMMON_CLK_MT6853_SCP_ADSP) += clk-mt6853-scp_par.o
obj-$(CONFIG_COMMON_CLK_MT6853_VDEC_GCON) += clk-mt6853-vdec.o
obj-$(CONFIG_COMMON_CLK_MT6853_VENC_GCON) += clk-mt6853-venc.o
endif
ifneq ($(wildcard $(srctree)/$(src)/clk-mt6855.c),)
obj-$(CONFIG_COMMON_CLK_MT6855) += clk-mt6855.o clk-bringup.o
obj-$(CONFIG_COMMON_CLK_MT6855_ADSP) += clk-mt6855-adsp.o
obj-$(CONFIG_COMMON_CLK_MT6855_CAMSYS_MAIN) += clk-mt6855-cam_m.o
obj-$(CONFIG_COMMON_CLK_MT6855_CAM) += clk-mt6855-cam.o
obj-$(CONFIG_COMMON_CLK_MT6855_MMSYS) += clk-mt6855-mmsys.o
obj-$(CONFIG_COMMON_CLK_MT6855_IMGSYS1) += clk-mt6855-imgsys1.o
obj-$(CONFIG_COMMON_CLK_MT6855_IMGSYS2) += clk-mt6855-imgsys2.o
obj-$(CONFIG_COMMON_CLK_MT6855_PERI) += clk-mt6855-peri.o
obj-$(CONFIG_COMMON_CLK_MT6855_BUS) += clk-mt6855-bus.o
obj-$(CONFIG_COMMON_CLK_MT6855_IMG) += clk-mt6855-img.o
obj-$(CONFIG_COMMON_CLK_MT6855_MDPSYS1_CONFIG) += clk-mt6855-mdp1.o
obj-$(CONFIG_COMMON_CLK_MT6855_MDPSYS) += clk-mt6855-mdp.o
obj-$(CONFIG_COMMON_CLK_MT6855_VCODEC) += clk-mt6855-vcodec.o
endif
ifneq ($(wildcard $(srctree)/$(src)/clk-mt6879.c),)
obj-$(CONFIG_COMMON_CLK_MT6879) += clk-mt6879.o clk-bringup.o
obj-$(CONFIG_COMMON_CLK_MT6879_ADSP) += clk-mt6879-adsp.o
obj-$(CONFIG_COMMON_CLK_MT6879_CAM) += clk-mt6879-cam.o
obj-$(CONFIG_COMMON_CLK_MT6879_CCU) += clk-mt6879-ccu.o
obj-$(CONFIG_COMMON_CLK_MT6879_IMG) += clk-mt6879-img.o
obj-$(CONFIG_COMMON_CLK_MT6879_MMSYS) += clk-mt6879-mmsys.o
obj-$(CONFIG_COMMON_CLK_MT6879_PERI) += clk-mt6879-peri.o
obj-$(CONFIG_COMMON_CLK_MT6879_BUS) += clk-mt6879-bus.o
obj-$(CONFIG_COMMON_CLK_MT6879_MDPSYS) += clk-mt6879-mdp.o
obj-$(CONFIG_COMMON_CLK_MT6879_MFG) += clk-mt6879-mfgcfg.o
obj-$(CONFIG_COMMON_CLK_MT6879_VCODEC) += clk-mt6879-vcodec.o
endif
ifneq ($(wildcard $(srctree)/$(src)/clk-mt6893.c),)
obj-$(CONFIG_COMMON_CLK_MT6893) += clk-mt6893.o clk-bringup.o
obj-$(CONFIG_COMMON_CLK_MT6893_APU0) += clk-mt6893-apu0.o
obj-$(CONFIG_COMMON_CLK_MT6893_APU1) += clk-mt6893-apu1.o
obj-$(CONFIG_COMMON_CLK_MT6893_APU2) += clk-mt6893-apu2.o
obj-$(CONFIG_COMMON_CLK_MT6893_APU_VCORE) += clk-mt6893-apuv.o
obj-$(CONFIG_COMMON_CLK_MT6893_APU_CONN) += clk-mt6893-apuc.o
obj-$(CONFIG_COMMON_CLK_MT6893_APU_MDLA0) += clk-mt6893-apum0.o
obj-$(CONFIG_COMMON_CLK_MT6893_APU_MDLA1) += clk-mt6893-apum1.o
obj-$(CONFIG_COMMON_CLK_MT6893_AUDIOSYS) += clk-mt6893-audsys.o
obj-$(CONFIG_COMMON_CLK_MT6893_CAMSYS_MAIN) += clk-mt6893-cam_m.o
obj-$(CONFIG_COMMON_CLK_MT6893_CAMSYS_RAWA) += clk-mt6893-cam_ra.o
obj-$(CONFIG_COMMON_CLK_MT6893_CAMSYS_RAWB) += clk-mt6893-cam_rb.o
obj-$(CONFIG_COMMON_CLK_MT6893_CAMSYS_RAWC) += clk-mt6893-cam_rc.o
obj-$(CONFIG_COMMON_CLK_MT6893_IMGSYS1) += clk-mt6893-imgsys1.o
obj-$(CONFIG_COMMON_CLK_MT6893_IMGSYS2) += clk-mt6893-imgsys2.o
obj-$(CONFIG_COMMON_CLK_MT6893_IMP_IIC_WRAP_C) += clk-mt6893-impc.o
obj-$(CONFIG_COMMON_CLK_MT6893_IMP_IIC_WRAP_E) += clk-mt6893-impe.o
obj-$(CONFIG_COMMON_CLK_MT6893_IMP_IIC_WRAP_N) += clk-mt6893-impn.o
obj-$(CONFIG_COMMON_CLK_MT6893_IMP_IIC_WRAP_S) += clk-mt6893-imps.o
obj-$(CONFIG_COMMON_CLK_MT6893_IPESYS) += clk-mt6893-ipe.o
obj-$(CONFIG_COMMON_CLK_MT6893_MDPSYS) += clk-mt6893-mdp.o
obj-$(CONFIG_COMMON_CLK_MT6893_MFGCFG) += clk-mt6893-mfgcfg.o
obj-$(CONFIG_COMMON_CLK_MT6893_MMSYS) += clk-mt6893-mm.o
obj-$(CONFIG_COMMON_CLK_MT6893_SCP_ADSP) += clk-mt6893-scp_adsp.o
obj-$(CONFIG_COMMON_CLK_MT6893_VDECSYS) += clk-mt6893-vde2.o
obj-$(CONFIG_COMMON_CLK_MT6893_VDECSYS_SOC) += clk-mt6893-vde1.o
obj-$(CONFIG_COMMON_CLK_MT6893_VENCSYS_C1) += clk-mt6893-ven2.o
obj-$(CONFIG_COMMON_CLK_MT6893_VENCSYS) += clk-mt6893-ven1.o
endif
ifneq ($(wildcard $(srctree)/$(src)/clk-mt6895.c),)
obj-$(CONFIG_COMMON_CLK_MT6895) += clk-mt6895.o clk-bringup.o
obj-$(CONFIG_COMMON_CLK_MT6895_ADSP) += clk-mt6895-adsp.o
obj-$(CONFIG_COMMON_CLK_MT6895_CAM) += clk-mt6895-cam.o
obj-$(CONFIG_COMMON_CLK_MT6895_CCU) += clk-mt6895-ccu.o
obj-$(CONFIG_COMMON_CLK_MT6895_IMG) += clk-mt6895-img.o
obj-$(CONFIG_COMMON_CLK_MT6895_MMSYS) += clk-mt6895-mmsys.o
obj-$(CONFIG_COMMON_CLK_MT6895_PERI) += clk-mt6895-peri.o
obj-$(CONFIG_COMMON_CLK_MT6895_BUS) += clk-mt6895-bus.o
obj-$(CONFIG_COMMON_CLK_MT6895_MDPSYS) += clk-mt6895-mdpsys.o
obj-$(CONFIG_COMMON_CLK_MT6895_MFG) += clk-mt6895-mfgcfg.o
obj-$(CONFIG_COMMON_CLK_MT6895_VCODEC) += clk-mt6895-vcodec.o
endif
ifneq ($(wildcard $(srctree)/$(src)/clk-mt6983.c),)
obj-$(CONFIG_COMMON_CLK_MT6983) += clk-mt6983.o clk-bringup.o
obj-$(CONFIG_COMMON_CLK_MT6983_ADSP) += clk-mt6983-adsp_grp.o
obj-$(CONFIG_COMMON_CLK_MT6983_CCU) += clk-mt6983-ccu_main.o
obj-$(CONFIG_COMMON_CLK_MT6983_IMG) += clk-mt6983-img.o
obj-$(CONFIG_COMMON_CLK_MT6983_IMP_IIC_WRAP) += clk-mt6983-imp_iic_wrap.o
obj-$(CONFIG_COMMON_CLK_MT6983_VCODEC) += clk-mt6983-vcodec.o
obj-$(CONFIG_COMMON_CLK_MT6983_MDPSYS) += clk-mt6983-mdp_grp.o
obj-$(CONFIG_COMMON_CLK_MT6983_MFG) += clk-mt6983-mfg_top_config.o
obj-$(CONFIG_COMMON_CLK_MT6983_MMSYS) += clk-mt6983-mm.o
obj-$(CONFIG_COMMON_CLK_MT6983_CAM) += clk-mt6983-cam.o
endif
obj-$(CONFIG_COMMON_CLK_MT7622) += clk-mt7622.o
obj-$(CONFIG_COMMON_CLK_MT7622_ETHSYS) += clk-mt7622-eth.o
obj-$(CONFIG_COMMON_CLK_MT7622_HIFSYS) += clk-mt7622-hif.o
obj-$(CONFIG_COMMON_CLK_MT7622_AUDSYS) += clk-mt7622-aud.o
obj-$(CONFIG_COMMON_CLK_MT7629) += clk-mt7629.o
obj-$(CONFIG_COMMON_CLK_MT7629_ETHSYS) += clk-mt7629-eth.o
obj-$(CONFIG_COMMON_CLK_MT7629_HIFSYS) += clk-mt7629-hif.o
obj-$(CONFIG_COMMON_CLK_MT8135) += clk-mt8135.o
obj-$(CONFIG_COMMON_CLK_MT8167) += clk-mt8167.o
obj-$(CONFIG_COMMON_CLK_MT8167_AUDSYS) += clk-mt8167-aud.o
obj-$(CONFIG_COMMON_CLK_MT8167_IMGSYS) += clk-mt8167-img.o
obj-$(CONFIG_COMMON_CLK_MT8167_MFGCFG) += clk-mt8167-mfgcfg.o
obj-$(CONFIG_COMMON_CLK_MT8167_MMSYS) += clk-mt8167-mm.o
obj-$(CONFIG_COMMON_CLK_MT8167_VDECSYS) += clk-mt8167-vdec.o
obj-$(CONFIG_COMMON_CLK_MT8173) += clk-mt8173.o
obj-$(CONFIG_COMMON_CLK_MT8173_MMSYS) += clk-mt8173-mm.o
obj-$(CONFIG_COMMON_CLK_MT8183) += clk-mt8183.o
obj-$(CONFIG_COMMON_CLK_MT8183_AUDIOSYS) += clk-mt8183-audio.o
obj-$(CONFIG_COMMON_CLK_MT8183_CAMSYS) += clk-mt8183-cam.o
obj-$(CONFIG_COMMON_CLK_MT8183_IMGSYS) += clk-mt8183-img.o
obj-$(CONFIG_COMMON_CLK_MT8183_IPU_CORE0) += clk-mt8183-ipu0.o
obj-$(CONFIG_COMMON_CLK_MT8183_IPU_CORE1) += clk-mt8183-ipu1.o
obj-$(CONFIG_COMMON_CLK_MT8183_IPU_ADL) += clk-mt8183-ipu_adl.o
obj-$(CONFIG_COMMON_CLK_MT8183_IPU_CONN) += clk-mt8183-ipu_conn.o
obj-$(CONFIG_COMMON_CLK_MT8183_MFGCFG) += clk-mt8183-mfgcfg.o
obj-$(CONFIG_COMMON_CLK_MT8183_MMSYS) += clk-mt8183-mm.o
obj-$(CONFIG_COMMON_CLK_MT8183_VDECSYS) += clk-mt8183-vdec.o
obj-$(CONFIG_COMMON_CLK_MT8183_VENCSYS) += clk-mt8183-venc.o
obj-$(CONFIG_COMMON_CLK_MT8516) += clk-mt8516.o
obj-$(CONFIG_COMMON_CLK_MT8516_AUDSYS) += clk-mt8516-aud.o
