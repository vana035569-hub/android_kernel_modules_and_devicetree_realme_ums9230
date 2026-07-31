/*
 * Copyright (C) 2021-2022 UNISOC Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __CAM_DEBUGGER_H__
#define __CAM_DEBUGGER_H__

#include "dcam_interface.h"
#include "cam_dump_node.h"
#include "cam_replace_node.h"

typedef void (*debug_write_ioctl)(struct cam_hw_info *hw, char *name, char * val, uint32_t idx);
typedef void (*debug_read_ioctl)(struct seq_file *s, uint32_t idx);

enum {
	CH_PRE = 0,
	CH_CAP = 1,
	CH_VID = 2,
	CH_MAX = 3,

	FBC_DCAM = 0,
	FBC_3DNR = 1,
	FBC_ISP = 2,
	FBC_MAX = 3,
};

enum fbc_crl_type {
	DEBUG_FBC_CRL_BIN = 0x1,
	DEBUG_FBC_CRL_FULL = 0x2,
	DEBUG_FBC_CRL_RAW = 0x4,
	DEBUG_FBC_CRL_MAX,
};

enum dcam_recovery_switch {
	DEBUG_DCAM_RECOVERY_IDLE,
	DEBUG_DCAM_RECOVERY_BYPASS,
	DEBUG_DCAM_RECOVERY_OPEN,
	DEBUG_DCAM_RECOVERY_MAX,
};

enum dbg_rawcap_frgb_switch {
	DEBUG_RAWCAP_MODE,
	DEBUG_FRGB_MODE,
	DEBUG_MODE_MAX,
};

enum hw_ctx_index {
	HW_CTX_0,
	HW_CTX_1,
	HW_CTX_2,
	HW_CTX_3,
	HW_CTX_MAX,
};

enum ip_type {
	IP_DCAM,
	IP_ISP,
	IP_MAX,
};

struct debug_cmd {
	char *name;
	debug_write_ioctl write_fun;
	debug_read_ioctl read_fun;
	uint32_t hw_ctx;
	uint32_t ip_info;
};

struct debug_ops {
	char name[20];
	char val[16];
	char tag[16];
};

#define CAM_DEBUGGER_IF_GET_CORRECT_KEY(val) ( { \
	if (!val) { \
		pr_err("fail to get correct key\n"); \
		return -EFAULT; \
	} \
})

int cam_debugger_init(struct cam_hw_info *hw);
int cam_debugger_deinit(void);
void cam_debugger_uevent_notify(struct platform_device *pdev, char *str);

void camdebugger_dcam_bypass_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx);
void camdebugger_dcam_bypass_read(struct seq_file *s, uint32_t idx);
void camdebugger_isp_bypass_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx);
void camdebugger_isp_bypass_read(struct seq_file *s, uint32_t idx);
void camdebugger_pyr_dec_bypass_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx);
void camdebugger_pyr_dec_bypass_read(struct seq_file *s, uint32_t idx);

void camdebugger_recovery_bypass_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx);
void camdebugger_recovery_bypass_read(struct seq_file *s, uint32_t idx);
void camdebugger_isp_pyr_dec_bypass_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx);
void camdebugger_isp_pyr_dec_bypass_read(struct seq_file *s, uint32_t idx);

void camdebugger_dcam_reg_read(struct seq_file *s, uint32_t idx);

void camdebugger_zoom_mode_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx);
void camdebugger_zoom_mode_read(struct seq_file *s, uint32_t idx);

void camdebugger_fbc_control_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx);
void camdebugger_fbc_control_read(struct seq_file *s, uint32_t idx);

void camdebugger_replace_control_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx);
void camdebugger_replace_control_read(struct seq_file *s, uint32_t idx);
void camdebugger_replace_file_fmt_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx);
void camdebugger_replace_switch_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx);
void camdebugger_replace_switch_read(struct seq_file *s, uint32_t idx);

void camdebugger_dump_switch_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx);
void camdebugger_dump_switch_read(struct seq_file *s, uint32_t idx);
void camdebugger_dump_control_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx);
void camdebugger_dump_control_read(struct seq_file *s, uint32_t idx);
void camdebugger_dump_count_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx);

void camdebugger_dcamint_eof_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx);
void camdebugger_dcamint_eof_read(struct seq_file *s, uint32_t idx);

void camdebugger_memory_leak_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx);

void camdebugger_rawcap_frgb_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx);
void camdebugger_rawcap_frgb_read(struct seq_file *s, uint32_t idx);

void camdebugger_pipeline_log_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx);
void camdebugger_pipeline_log_read(struct seq_file *s, uint32_t idx);

void camdebugger_reg_buf_read(struct seq_file *s, uint32_t idx);

void camdebugger_iommu_mode_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx);
void camdebugger_iommu_mode_read(struct seq_file *s, uint32_t idx);

void camdebugger_lbuf_len_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx);
void camdebugger_lbuf_len_read(struct seq_file *s, uint32_t idx);
#endif
