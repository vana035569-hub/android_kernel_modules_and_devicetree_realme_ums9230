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

#include <linux/err.h>
#include <sprd_mm.h>

#include "dcam_core.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "DCAM_SLICE: %d %d %s : " fmt, current->pid, __LINE__, __func__

#define ISP_SLICE_OVERLAP_W_MAX         64

uint32_t dcam_slice_needed_info_get(struct dcam_hw_context *hw_ctx, uint32_t *dev_lbuf, uint32_t in_width)
{
	uint32_t out_width = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_lbuf_share lbufarg;
	struct cam_hw_lbuf_share camarg;

	hw = hw_ctx->hw;
	lbufarg.idx = hw_ctx->hw_ctx_id;
	lbufarg.width = 0;
	hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_LBUF_SHARE_GET, &lbufarg);
	out_width = lbufarg.width;

	camarg.idx = hw_ctx->hw_ctx_id;
	camarg.width = in_width;
	camarg.offline_flag = 1;
	pr_debug("Dcam%d, sw_ctx%d, new width %d old linebuf %d\n", camarg.idx, hw_ctx->node_id, camarg.width, lbufarg.width);
	if (hw->ip_dcam[hw_ctx->hw_ctx_id]->dcamhw_abt->lbuf_share_support && (lbufarg.width < camarg.width)) {
		hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_LBUF_SHARE_SET, &camarg);
		lbufarg.idx = hw_ctx->hw_ctx_id;
		lbufarg.width = 0;
		hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_LBUF_SHARE_GET, &lbufarg);
		out_width = lbufarg.width;
	}
	if (!out_width)
		*dev_lbuf = DCAM_PATH_WMAX * 2;
	else
		*dev_lbuf = out_width;
	if (in_width > *dev_lbuf)
		return 1;
	else
		return 0;
}

int dcam_slice_hw_info_set(struct dcam_offline_slice_info *slice, struct cam_frame *pframe,
	uint32_t slice_wmax, struct dcam_isp_k_block *pm)
{
	int ret = 0, i = 0, j = 0;
	uint32_t w = 0, offset = 0;
	uint32_t slc_w = 0, f_align = 0;
	uint32_t slc_h1 = 0, slc_h2 = 0;
	uint32_t h_slice_num = 0;
	uint32_t index = 0;
	struct dcam_dev_lsc_info *info = NULL;

	info = &pm->lsc.lens_info;
	if (pframe->common.height > DCAM_SW_SLICE_HEIGHT_MAX)
		h_slice_num = 2;
	else
		h_slice_num = 1;

	if (h_slice_num == 2 && !info->bypass) {
		/* Cut two graphs up and down according to the lsc table partitioning rules
		 * index is the height of each cell in the table,
		 * index = ceil((height/2/2)/grid_width-1); a "2" is half the height,
		 * other "2" is two channels in the high direction
		 */
		index = 2 * 2 * info->grid_width;
		if (pframe->common.height % index == 0)
			index = pframe->common.height / index - 1;
		else
			index = pframe->common.height / index;
		index = (index >> 1) << 1;
		slc_h2 = pframe->common.height - 2 * index * info->grid_width;
		slc_h1 = pframe->common.height + 3 * info->grid_width - slc_h2;
		pm->lsc.grid_x_index = index;
	} else {
		slc_h1 = slc_h2 = pframe->common.height / h_slice_num;
		slc_h1 = slc_h2 = ALIGN(slc_h1, 2);
	}

	w = pframe->common.width;
	if (w <= slice_wmax) {
		slc_w = w;
		goto slices;
	}

	if (slice->fetch_fmt == CAM_RAW_14)
		f_align = 8;/* 8p * 16bits/p = 128 bits fetch aligned */
	else
		f_align = 64;/* 64p * 10bits/p = 128bits * 5 */

	slc_w = slice_wmax / f_align * f_align;

	/* can not get valid slice w aligned  */
	if ((slc_w > slice_wmax) ||
		(slc_w * DCAM_OFFLINE_SLC_MAX / 2) < pframe->common.width) {
		pr_err("dcam failed, pic_w %d, slc_limit %d, algin %d\n",
				pframe->common.width, slice_wmax, f_align);
		return -EFAULT;
	}

slices:
	while (j < h_slice_num) {
		offset = 0;
		w = pframe->common.width;
		while (w > 0) {
			slice->slice_trim[i].start_x = offset;
			slice->slice_trim[i].start_y = (pframe->common.height - slc_h2) * j;
			if ((offset == 0) || ((w + DCAM_OVERLAP) <= slc_w)) /* left slice || right slice*/
				slice->slice_trim[i].size_x = (w > slc_w) ? (slc_w - DCAM_OVERLAP) : w;
			else
				slice->slice_trim[i].size_x = (w > slc_w) ? (slc_w - 2* DCAM_OVERLAP) : w;
			slice->slice_trim[i].size_y = slc_h1 * (1 - j) + slc_h2 * j;
			pr_info("slc%d, (%d %d %d %d), limit %d\n", i,
				slice->slice_trim[i].start_x, slice->slice_trim[i].start_y,
				slice->slice_trim[i].size_x, slice->slice_trim[i].size_y,
				slice_wmax);

			w -= slice->slice_trim[i].size_x;
			offset += slice->slice_trim[i].size_x;
			i++;
		}
		j++;
		slice->slice_num = i;
		slice->slice_count = i;
	}
	slice->w_slice_num = slice->slice_num / h_slice_num;

	return ret;
}
