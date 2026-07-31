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

#ifndef _CAM_SCALER_H_
#define _CAM_SCALER_H_

#include "cam_hw.h"

#define ARC_32_COEF                             0x80000000

int cam_scaler_coeff_calc(struct yuv_scaler_info *scaler, uint32_t scale2yuv420);
int cam_scaler_coeff_calc_ex(struct yuv_scaler_info *scaler);
unsigned char cam_scaler_isp_scale_coeff_gen_ex(short i_w, short i_h,
		short o_w, short o_h,
		unsigned char i_pixfmt,
		unsigned char o_pixfmt,
		unsigned int *coeff_h_lum_ptr,
		unsigned int *coeff_h_ch_ptr,
		unsigned int *coeff_v_lum_ptr,
		unsigned int *coeff_v_ch_ptr,
		unsigned char *scaler_tap_hor,
		unsigned char *chroma_tap_hor,
		unsigned char *scaler_tap_ver,
		unsigned char *chroma_tap_ver,
		void *temp_buf_ptr,
		unsigned int temp_buf_size);

#endif
