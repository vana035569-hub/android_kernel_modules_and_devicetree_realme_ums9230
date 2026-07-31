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

#ifndef _CAM_KERNEL_ADAPT_H_
#define _CAM_KERNEL_ADAPT_H_

#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
#include <linux/pm_runtime.h>
#include <linux/sprd_ion.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
#include <linux/dma-heap.h>
#include <uapi/linux/sprd_dmabuf.h>
#endif
#else
#include <video/sprd_mmsys_pw_domain.h>
#include "ion.h"
#endif

struct file *cam_kernel_adapt_filp_open(const char *, int, umode_t);
int cam_kernel_adapt_filp_close(struct file *, fl_owner_t id);
ssize_t cam_kernel_adapt_read(struct file *, void *, size_t, loff_t *);
ssize_t cam_kernel_adapt_write(struct file *, void *, size_t, loff_t *);
void cam_kernel_adapt_kproperty_get(const char *key, char *value, const char *default_value);
int cam_kernel_adapt_syscon_get_args_by_name(struct device_node *np, const char *name, int arg_count, unsigned int *out_args);
struct regmap *cam_kernel_adapt_syscon_regmap_lookup_by_name(struct device_node *np, const char *name);

#endif
