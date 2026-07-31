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

#ifdef CAM_RAWCAP

int camrawcap_raw_pre_proc(struct camera_module *module,
		struct isp_raw_proc_info *proc_info)
{
	int ret = 0;
	uint32_t raw2yuv_path_id = 0;
	uint32_t pipeline_type = 0, pyrdec_support = 0;
	struct cfg_param_status cfg_param = {0};
	struct cam_hw_info *hw = NULL;
	struct channel_context *ch = NULL;
	struct cam_frame *param_frame = NULL;
	struct cam_pipeline_desc *pipeline_desc = NULL;
	struct pyr_dec_node_desc *pyr_dec_desc = NULL;
	struct isp_node_desc *isp_node_description = NULL;
	struct dcam_offline_node_desc *dcam_offline_desc = NULL;

	pr_debug("cam%d in. module:%px, fd:%x, endian:%d\n", module->idx, module, proc_info->fd_pm, proc_info->src_y_endian);
	pr_debug("size:%d, %d, partten:%d.\n", proc_info->src_size.width, proc_info->src_size.height, proc_info->src_pattern);
	hw = module->grp->hw_info;
	module->capture_type = CAM_CAPTURE_RAWPROC;
	module->cam_uinfo.dcam_slice_mode = proc_info->src_size.width > DCAM_24M_WIDTH ? CAM_OFFLINE_SLICE_HW : 0;
	module->cam_uinfo.is_pyr_dec = hw->ip_isp->isphw_abt->pyr_dec_support;
	raw2yuv_path_id = camcore_dcampath_id_convert(hw->ip_dcam[0]->dcamhw_abt->aux_dcam_path);
	pyrdec_support = hw->ip_isp->isphw_abt->pyr_dec_support;
	ch = &module->channel[CAM_CH_CAP];
	ch->dcam_port_id = -1;
	ch->isp_port_id = PORT_CAP_OUT;
	ch->aux_dcam_port_id = -1;
	ch->ch_uinfo.dcam_raw_fmt = -1;
	ch->ch_uinfo.sensor_raw_fmt = -1;
	ch->ch_uinfo.src_size.w = proc_info->src_size.width;
	ch->ch_uinfo.src_size.h = proc_info->src_size.height;
	pipeline_type = CAM_PIPELINE_OFFLINE_RAW2YUV;
	pipeline_desc = cam_buf_kernel_sys_vzalloc(sizeof(struct cam_pipeline_desc));
	if (!pipeline_desc) {
		pr_err("fail to alloc pipeline_des\n");
		return -ENOMEM;
	}
	dcam_offline_desc = &pipeline_desc->dcam_offline_desc;
	isp_node_description = &pipeline_desc->isp_node_description;
	pyr_dec_desc = &pipeline_desc->pyr_dec_desc;
	pipeline_desc->pipeline_graph = &module->static_topology->pipeline_list[pipeline_type];
	pipeline_desc->nodes_dev = &module->nodes_dev;
	pipeline_desc->data_cb_handle = module;
	pipeline_desc->buf_manager_handle =  module->grp->global_buf_manager;
	pipeline_desc->data_cb_func = camcore_pipeline_callback;
	pipeline_desc->zoom_cb_func = camcore_zoom_param_get_callback;
	pipeline_desc->slice_cb_func = camcore_nonzsl_frame_slice;

	dcam_offline_desc->dev = module->dcam_dev_handle;
	dcam_offline_desc->buf_manager_handle = pipeline_desc->buf_manager_handle;
	if (hw->prj_id == SHARKL3)
		dcam_offline_desc->csi_controller_idx = CSI_ID_0;
	else
		dcam_offline_desc->csi_controller_idx = module->csi_controller_idx;
	dcam_offline_desc->dcam_slice_mode = module->cam_uinfo.dcam_slice_mode;
	dcam_offline_desc->endian = proc_info->src_y_endian;
	dcam_offline_desc->port_desc.endian = ENDIAN_LITTLE;
	dcam_offline_desc->pattern = proc_info->src_pattern;
	dcam_offline_desc->port_desc.src_sel = PROCESS_RAW_SRC_SEL;
	dcam_offline_desc->statis_en = 1;
	cam_valid_fmt_get((uint32_t*)&module->raw_cap_fetch_fmt, hw->ip_dcam[0]->dcamhw_abt->sensor_raw_fmt);
	dcam_offline_desc->fetch_fmt = module->raw_cap_fetch_fmt;
	cam_valid_fmt_get(&ch->ch_uinfo.dcam_raw_fmt, hw->ip_dcam[0]->dcampath_abt[raw2yuv_path_id]->format[0]);
	dcam_offline_desc->port_desc.dcam_out_fmt = ch->ch_uinfo.dcam_raw_fmt;
	if (hw->ip_dcam[0]->dcamhw_abt->sensor_raw_path_id == DCAM_PATH_VCH2 &&
		proc_info->src_size.height > ISP_SLCIE_HEIGHT_MAX) {
		dcam_offline_desc->fetch_fmt = CAM_RAW_PACK_10;
		dcam_offline_desc->port_desc.dcam_out_fmt = CAM_RAW_PACK_10;
	}
	if (module->cam_uinfo.dcam_slice_mode && hw->ip_dcam[0]->dcamhw_abt->save_band_for_bigsize)
		dcam_offline_desc->port_desc.dcam_out_fmt = CAM_RAW_PACK_10;
	if (hw->ip_isp->isphw_abt->fetch_raw_support == 0)
		dcam_offline_desc->port_desc.dcam_out_fmt = CAM_YVU420_2FRAME_MIPI;

	/* isp offline param desc */
	isp_node_description->in_fmt = dcam_offline_desc->port_desc.dcam_out_fmt;
	isp_node_description->pyr_out_fmt = hw->ip_dcam[0]->dcamhw_abt->store_pyr_fmt;
	isp_node_description->store_3dnr_fmt = hw->ip_dcam[0]->dcamhw_abt->store_3dnr_fmt[0];
	if (module->cam_uinfo.is_pyr_dec)
		isp_node_description->pyr_layer_num = ISP_PYR_DEC_LAYER_NUM;
	else
		isp_node_description->pyr_layer_num = 0;
	isp_node_description->ltm_rgb = hw->ip_isp->isphw_abt->rgb_ltm_support;
	isp_node_description->mode_3dnr = MODE_3DNR_OFF;
	isp_node_description->mode_ltm = MODE_LTM_CAP;
	isp_node_description->mode_gtm = MODE_GTM_CAP;
	isp_node_description->ch_id = ch->ch_id;
	isp_node_description->bayer_pattern = proc_info->src_pattern;
	isp_node_description->enable_slowmotion = 0;
	isp_node_description->is_high_fps = 0;
	isp_node_description->cam_id = module->idx;
	isp_node_description->buf_manager_handle = pipeline_desc->buf_manager_handle;
	isp_node_description->dev = module->isp_dev_handle;
	isp_node_description->port_desc.out_fmt = CAM_YVU420_2FRAME;
	isp_node_description->port_desc.endian = ENDIAN_LITTLE;
	isp_node_description->port_desc.output_size.w = proc_info->dst_size.width;
	isp_node_description->port_desc.output_size.h = proc_info->dst_size.height;
	isp_node_description->blkparam_node_num = ISP_NONZSL_PARAM_BUF_NUM;
	isp_node_description->port_desc.resbuf_get_cb = isp_node_description->resbuf_get_cb;
	isp_node_description->port_desc.resbuf_cb_data = isp_node_description->resbuf_cb_data;
	isp_node_description->port_desc.in_fmt = dcam_offline_desc->port_desc.dcam_out_fmt;
	isp_node_description->port_desc.bayer_pattern = isp_node_description->bayer_pattern;
	isp_node_description->port_desc.pyr_out_fmt = isp_node_description->pyr_out_fmt;
	isp_node_description->port_desc.store_3dnr_fmt = isp_node_description->store_3dnr_fmt;
	isp_node_description->port_desc.sn_size = isp_node_description->sn_size;
	isp_node_description->port_desc.hw = hw;
	isp_node_description->ultra_cap_en = CAM_ENABLE;

	pyr_dec_desc->hw = hw;
	pyr_dec_desc->layer_num = PYR_DEC_LAYER_NUM;
	pyr_dec_desc->node_idx = module->idx;
	pyr_dec_desc->buf_manager_handle = pipeline_desc->buf_manager_handle;
	pyr_dec_desc->dcam_slice_mode = module->cam_uinfo.dcam_slice_mode;
	pyr_dec_desc->is_4in1 = module->cam_uinfo.is_4in1;
	pyr_dec_desc->sn_size = module->cam_uinfo.sn_size;
	pyr_dec_desc->dev = module->isp_dev_handle;
	pyr_dec_desc->pyrdec_dev = module->isp_dev_handle->pyr_dec_handle;
	pyr_dec_desc->in_fmt = dcam_offline_desc->port_desc.dcam_out_fmt;
	pyr_dec_desc->pyr_out_fmt = hw->ip_dcam[0]->dcamhw_abt->store_pyr_fmt;
	pyr_dec_desc->blkparam_buf_num = 1;
	ch->pipeline_handle = cam_pipeline_creat(pipeline_desc);
	if (!ch->pipeline_handle) {
		pr_err("fail to get cam pipeline.\n");
		ret = -ENOMEM;
		goto fail;
	}
	ch->enable = 1;
	ch->dcam_port_id = dcamoffline_pathid_convert_to_portid(hw->ip_dcam[0]->dcamhw_abt->aux_dcam_path);
	if (ch->dcam_port_id >= PORT_DCAM_OFFLINE_OUT_MAX) {
		pr_err("fail to get port id\n");
		cam_pipeline_destory(ch->pipeline_handle);
		memset(pipeline_desc->nodes_dev, 0, sizeof(struct cam_nodes_dev));
		ch->pipeline_handle = NULL;
		ret = -ENOMEM;
		goto fail;
	}
	ch->pipeline_type = pipeline_type;
	CAM_QUEUE_INIT(&ch->zoom_param_q, CAM_ZOOM_COEFF_Q_LEN, NULL);
	CAM_QUEUE_INIT(&ch->nonzsl_pre.zoom_param_q, CAM_ZOOM_COEFF_Q_LEN, NULL);

	/*TEMP: config ispctxid to pyrdec node*/
	if (pipeline_desc->pipeline_graph->need_pyr_dec && pyrdec_support)
		camcore_pyrdec_ctxid_cfg(ch, isp_node_description->isp_node);

	{
		uint32_t statis_pipe_type = CAM_PIPELINE_ONLINERAW_2_OFFLINEPREVIEW;
		if ((ch->ch_uinfo.src_size.w / 2) > g_camctrl.isp_linebuf_len)
			ch->ch_uinfo.nonzsl_pre_ratio = 4;
		else
			ch->ch_uinfo.nonzsl_pre_ratio = 2;
		dcam_offline_desc->offline_pre_en = CAM_ENABLE;
		isp_node_description->ch_id = CAM_CH_PRE;
		isp_node_description->mode_ltm = MODE_LTM_PRE;
		isp_node_description->port_desc.output_size.w = ch->ch_uinfo.dst_size.w / ch->ch_uinfo.nonzsl_pre_ratio;
		isp_node_description->port_desc.output_size.h = ch->ch_uinfo.dst_size.h / ch->ch_uinfo.nonzsl_pre_ratio;
		pipeline_desc->pipeline_graph = &module->static_topology->pipeline_list[statis_pipe_type];
		ch->nonzsl_pre_pipeline = cam_pipeline_creat(pipeline_desc);
		ch->nonzsl_statis_pipeline_type = statis_pipe_type;
		if (!ch->nonzsl_pre_pipeline) {
			pr_err("fail to get statis pipeline.\n");
			goto fail;
		}
	}

	pr_debug("size:%d, %d, dst:%d, %d, pm offset:%d.\n", proc_info->src_size.width, proc_info->src_size.height,
		proc_info->dst_size.width, proc_info->dst_size.height, proc_info->pm_offset);
	ch->trim_dcam.size_x = proc_info->src_size.width;
	ch->trim_dcam.size_y = proc_info->src_size.height;
	ch->dst_dcam.w = ch->trim_dcam.size_x;
	ch->dst_dcam.h = ch->trim_dcam.size_y;
	ch->trim_isp.size_x = ch->trim_dcam.size_x;
	ch->trim_isp.size_y = proc_info->src_size.height;
	ch->ch_uinfo.dst_size.w = proc_info->dst_size.width;
	ch->ch_uinfo.dst_size.h = proc_info->dst_size.height;
	ret = cam_zoom_channel_size_config(module, ch);

	param_frame = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
	param_frame->common.buf.status = CAM_BUF_ALLOC;
	param_frame->common.buf.type = CAM_BUF_USER;
	param_frame->common.buf.mfd = proc_info->fd_pm;
	ret = cam_buf_manager_buf_status_cfg(&param_frame->common.buf, CAM_BUF_STATUS_GET_K_ADDR, CAM_BUF_IOMMUDEV_MAX);
	if (ret) {
		pr_err("fail to change non zsl param buf status.\n");
		goto fail;
	}
	param_frame->common.buf.addr_k += proc_info->pm_offset;
	cfg_param.status = 1;
	cfg_param.scene_id = PM_SCENE_CAP;
	cfg_param.frame_id = proc_info->frame_id;
	cfg_param.update = 1;
	cfg_param.blkpm_ptr = (struct dcam_isp_k_block *)param_frame->common.buf.addr_k;
	if (cfg_param.blkpm_ptr)
		CAM_PIPEINE_DCAM_OFFLINE_NODE_CFG(ch, CAM_PIPELINE_CFG_PARAM_SWITCH, &cfg_param);
	if (pyrdec_support)
		CAM_PIPEINE_PYR_DEC_NODE_CFG(ch, CAM_PIPELINE_CFG_PARAM_SWITCH, &cfg_param);
	CAM_PIPELINE_NONZSL_ISP_PRE_NODE_CFG(ch, CAM_PIPELINE_CFG_PARAM_SWITCH, ISP_NODE_MODE_PRE_ID, &cfg_param);
	cam_queue_empty_frame_put(param_frame);

	atomic_set(&module->state, CAM_CFG_CH);
fail:
	cam_buf_kernel_sys_vfree(pipeline_desc);

	return ret;
}

int camrawcap_raw_post_proc(struct camera_module *module,
		struct isp_raw_proc_info *proc_info)
{
	int ret = 0;
	timespec cur_ts = {0};
	uint32_t width = 0, height = 0;
	uint32_t format = 0, size = 0, i = 0;
	uint32_t ratio = 4, bin_width = 0, bin_height = 0, bin_size = 0;
	enum cam_node_type node_type = CAM_NODE_TYPE_MAX;
	struct cam_hw_info *hw = NULL;
	struct channel_context *ch = NULL;
	struct isp_node *isp_node = NULL;
	struct dcam_pipe_dev *dev = NULL;
	struct cam_frame *src_frame = NULL;
	struct cam_frame *dst_frame = NULL;
	struct cam_frame *bin_frame = NULL;
	struct cam_frame *res_frame = NULL;
	struct cam_frame *mid_frame = NULL;
	struct cam_frame *param_frame = NULL;
	struct isp_rec_ctx_desc *rec_ctx = NULL;
	struct isp_ltm_ctx_desc *rgb_ltm = NULL;
	struct pyr_dec_node *pyrdec_node = NULL;
	struct cfg_param_status cfg_param = {0};
	struct cam_buf_alloc_desc alloc_param = {0};
	struct cam_node_cfg_param node_param = {0};
	struct cam_pipeline_cfg_param param_cfg = {0};

	pr_info("start\n");
	atomic_set(&module->state, CAM_RUNNING);
	hw = module->grp->hw_info;
	memset(&cur_ts, 0, sizeof(timespec));
	dev = (struct dcam_pipe_dev *)module->dcam_dev_handle;
	ch = &module->channel[CAM_CH_CAP];
	if (ch->enable == 0) {
		pr_err("fail to get channel enable state\n");
		return -EFAULT;
	}

	isp_node = module->nodes_dev.isp_node_dev[ISP_NODE_MODE_CAP_ID];
	pr_debug("postproc scene:%d, src %d 0x%x, mid %d, 0x%x, dst %d, 0x%x\n",
		proc_info->scene, proc_info->fd_src, proc_info->src_offset,
		proc_info->fd_dst, proc_info->dst_offset,
		proc_info->fd_pm, proc_info->pm_offset);
	pr_debug("size:%d, %d.\n", proc_info->src_size.width, proc_info->src_size.height);
	src_frame = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
	src_frame->common.buf.type = CAM_BUF_USER;
	src_frame->common.link_from.node_type = CAM_NODE_TYPE_USER;
	src_frame->common.buf.mfd = proc_info->fd_src;
	src_frame->common.buf.offset[0] = proc_info->src_offset;
	src_frame->common.channel_id = ch->ch_id;
	src_frame->common.width = proc_info->src_size.width;
	src_frame->common.height = proc_info->src_size.height;
	os_adapt_time_get_ts(&cur_ts);
	src_frame->common.sensor_time.tv_sec = cur_ts.tv_sec;
	src_frame->common.sensor_time.tv_usec = cur_ts.tv_nsec / NSEC_PER_USEC;
	src_frame->common.boot_sensor_time = os_adapt_time_get_boottime();
	src_frame->common.buf.status = CAM_BUF_ALLOC;
	src_frame->common.xtm_conflict.need_ltm_map = 1;
	src_frame->common.xtm_conflict.need_gtm_map = 1;
	src_frame->common.xtm_conflict.gtm_mod_en = 1;
	dst_frame = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
	dst_frame->common.buf.type = CAM_BUF_USER;
	dst_frame->common.buf.mfd = proc_info->fd_dst;
	dst_frame->common.buf.offset[0] = proc_info->dst_offset;
	dst_frame->common.channel_id = ch->ch_id;
	dst_frame->common.img_fmt = ch->ch_uinfo.dst_fmt;
	dst_frame->common.sensor_time = src_frame->common.sensor_time;
	dst_frame->common.boot_sensor_time = src_frame->common.boot_sensor_time;
	dst_frame->common.width = proc_info->src_size.width;
	dst_frame->common.height = proc_info->src_size.height;
	dst_frame->common.buf.status = CAM_BUF_ALLOC;

	if (proc_info->scene == CAMRAW_POSTPROC_PIPELINE_PROCESS) {
		ret = CAM_PIPEINE_ISP_OUT_PORT_CFG(ch, ch->isp_port_id, CAM_PIPELINE_CFG_BUF, ISP_NODE_MODE_CAP_ID, dst_frame);
		if (ret) {
			pr_err("fail to cfg isp out buffer.\n");
			goto dstbuf_cfg_fail;
		}
		width = proc_info->src_size.width;
		height = proc_info->src_size.height;
		format = ch->ch_uinfo.dcam_raw_fmt;
		mid_frame = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
		mid_frame->common.channel_id = ch->ch_id;
		mid_frame->common.buf.status = CAM_BUF_ALLOC;
		mid_frame->common.width = width;
		mid_frame->common.height = height;
		size = cal_sprd_pitch(width, format) * height;
		size = ALIGN(size, CAM_BUF_ALIGN_SIZE);
		ret = cam_buf_alloc(&mid_frame->common.buf, (size_t)size, module->iommu_enable);
		if (ret) {
			pr_err("fail to get mid_frame.\n");
			goto midbuf_cfg_fail;
		}
		mid_frame->common.sensor_time = src_frame->common.sensor_time;
		mid_frame->common.boot_sensor_time = src_frame->common.boot_sensor_time;
		CAM_QUEUE_FRAME_FLAG_RESET(&mid_frame->common);
		ret = CAM_PIPEINE_DCAM_OFFLINE_OUT_PORT_CFG(ch, ch->dcam_port_id, CAM_PIPELINE_CFG_BUF,
			mid_frame, CAM_NODE_TYPE_DCAM_OFFLINE);
		if (ret) {
			pr_err("fail to cfg dcam out buffer.\n");
			goto midbuf_cfg_fail;
		}
		node_type = CAM_NODE_TYPE_DCAM_OFFLINE;
	} else if (proc_info->scene == CAMRAW_POSTPROC_PIPELINE_DCAM_PROCESS) {
		ret = CAM_PIPEINE_DCAM_OFFLINE_OUT_PORT_CFG(ch, ch->dcam_port_id, CAM_PIPELINE_CFG_BUF,
			dst_frame, CAM_NODE_TYPE_DCAM_OFFLINE);
		if (ret) {
			pr_err("fail to cfg isp out buffer.\n");
			goto dstbuf_cfg_fail;
		}
		node_type = CAM_NODE_TYPE_DCAM_OFFLINE;
	} else if (proc_info->scene == CAMRAW_POSTPROC_PIPELINE_ISP_PROCESS) {
		complete(&isp_node->ultra_cap_com);
		ret = CAM_PIPEINE_ISP_OUT_PORT_CFG(ch, ch->isp_port_id, CAM_PIPELINE_CFG_BUF, ISP_NODE_MODE_CAP_ID, dst_frame);
		if (ret) {
			pr_err("fail to cfg isp out buffer.\n");
			goto dstbuf_cfg_fail;
		}
		param_frame = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
		param_frame->common.buf.status = CAM_BUF_ALLOC;
		param_frame->common.buf.type = CAM_BUF_USER;
		param_frame->common.buf.mfd = proc_info->fd_pm;
		ret = cam_buf_manager_buf_status_cfg(&param_frame->common.buf, CAM_BUF_STATUS_GET_K_ADDR, CAM_BUF_IOMMUDEV_MAX);
		if (ret) {
			pr_err("fail to change non zsl param buf status.\n");
			goto dstbuf_cfg_fail;
		}
		param_frame->common.buf.addr_k += proc_info->pm_offset;
		cfg_param.status = 1;
		cfg_param.scene_id = PM_SCENE_CAP;
		cfg_param.frame_id = proc_info->frame_id;
		cfg_param.update = 1;
		cfg_param.blkpm_ptr = (struct dcam_isp_k_block *)param_frame->common.buf.addr_k;
		CAM_PIPEINE_ISP_NODE_CFG(ch, CAM_PIPELINE_CFG_PARAM_SWITCH, ISP_NODE_MODE_CAP_ID, &cfg_param);
		CAM_PIPEINE_ISP_NODE_CFG(ch, CAM_PIPELINE_CFG_PARAM_SWITCH, ISP_NODE_MODE_CAP_ID, &cfg_param);
		CAM_PIPEINE_ISP_NODE_CFG(ch, CAM_PIPELINE_CFG_PARAM_SWITCH, ISP_NODE_MODE_CAP_ID, &cfg_param);
		CAM_PIPEINE_ISP_NODE_CFG(ch, CAM_PIPELINE_CFG_PARAM_SWITCH, ISP_NODE_MODE_CAP_ID, &cfg_param);
		node_type = CAM_NODE_TYPE_ISP_OFFLINE;
		ret = cam_buf_manager_buf_status_cfg(&src_frame->common.buf, CAM_BUF_STATUS_GET_IOVA, CAM_BUF_IOMMUDEV_ISP);
		if (ret) {
			pr_err("fail to change dst buf status.\n");
			goto dstbuf_cfg_fail;
		}
		if (module->cam_uinfo.is_pyr_dec) {
			node_type = CAM_NODE_TYPE_PYR_DEC;
			memset(&src_frame->common.zoom_data, 0 , sizeof(struct cam_zoom_frame));
			camcore_zoom_param_get_callback(ch->pipeline_type, module, &src_frame->common.zoom_data);
		} else {
			if (proc_info->src_size.height > ISP_SLCIE_HEIGHT_MAX) {
				src_frame->common.width = proc_info->src_size.width;
				src_frame->common.height = proc_info->src_size.height;
				src_frame->common.link_to.node_type = CAM_NODE_TYPE_ISP_OFFLINE;
				src_frame->common.nonzsl_xtm.full_size.h = proc_info->src_size.height;
				ret = camcore_nonzsl_frame_slice(src_frame, module);
				return ret;
			}
			memset(&src_frame->common.zoom_data, 0 , sizeof(struct cam_zoom_frame));
			camcore_zoom_param_get_callback(ch->pipeline_type, module, &src_frame->common.zoom_data);
		}
	}

	if (ch->nonzsl_pre_pipeline && proc_info->scene != CAMRAW_POSTPROC_PIPELINE_ISP_PROCESS) {
		bin_width = cal_sprd_pitch(proc_info->src_size.width / ratio, ch->ch_uinfo.dcam_raw_fmt);
		bin_height = proc_info->src_size.height / ratio;
		bin_size = bin_width * bin_height;
		if (!hw->ip_isp->isphw_abt->fetch_raw_support)
			bin_size = bin_size * 3 / 2;
		bin_size = ALIGN(bin_size, CAM_BUF_ALIGN_SIZE);
		bin_frame = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
		bin_frame->common.channel_id = CAM_CH_PRE;
		bin_frame->common.buf.status = CAM_BUF_ALLOC;
		bin_frame->common.buf.type = CAM_BUF_KERNEL;
		bin_frame->common.nonzsl_xtm.hist_eb = CAM_ENABLE;
		bin_frame->common.nonzsl_xtm.full_size.w = proc_info->src_size.width;
		bin_frame->common.nonzsl_xtm.full_size.h = proc_info->src_size.height;
		bin_frame->common.sensor_time = src_frame->common.sensor_time;
		bin_frame->common.boot_sensor_time = src_frame->common.boot_sensor_time;
		bin_frame->common.width = proc_info->src_size.width / ratio;
		bin_frame->common.height = proc_info->src_size.height / ratio;
		ret = cam_buf_alloc(&bin_frame->common.buf, (size_t)bin_size, module->iommu_enable);
		if (ret) {
			pr_err("fail to alloc bin buf\n");
			goto binbuf_cfg_fail;
		}
		CAM_QUEUE_FRAME_FLAG_RESET(&bin_frame->common);
		param_cfg.node_type = CAM_NODE_TYPE_DCAM_OFFLINE;
		param_cfg.node_param.param = bin_frame;
		param_cfg.node_param.port_type = PORT_TRANSFER_OUT;
		param_cfg.node_param.port_id = PORT_OFFLINE_BIN_OUT;
		param_cfg.node_id = DCAM_OFFLINE_NODE_ID;
		cam_buf_manager_buf_status_cfg(&bin_frame->common.buf, CAM_BUF_STATUS_GET_IOVA, CAM_BUF_IOMMUDEV_DCAM);
		cam_buf_manager_buf_status_cfg(&bin_frame->common.buf, CAM_BUF_STATUS_GET_IOVA, CAM_BUF_IOMMUDEV_ISP);
		bin_frame->common.buf.bypass_iova_ops = CAM_ENABLE;
		ret = ch->pipeline_handle->ops.cfg_param(ch->nonzsl_pre_pipeline, CAM_PIPELINE_CFG_BUF, &param_cfg);
		if (ret) {
			pr_err("fail to cfg dcam out buffer.\n");
			goto binbuf_cfg_fail;
		}

		res_frame = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
		res_frame->common.channel_id = CAM_CH_PRE;
		res_frame->common.buf.type = CAM_BUF_KERNEL;
		if (hw->ip_isp->isphw_abt->fetch_raw_support)
			bin_size = bin_size * 3 / 2;
		bin_size = ALIGN(bin_size, CAM_BUF_ALIGN_SIZE);
		ret = cam_buf_alloc(&res_frame->common.buf, bin_size, module->iommu_enable);
		if (ret) {
			pr_err("fail to alloc bin isp buf\n");
			cam_queue_empty_frame_put(res_frame);
			goto resbuf_cfg_fail;
		}
		param_cfg.node_type = CAM_NODE_TYPE_ISP_OFFLINE;
		param_cfg.node_param.param = res_frame;
		param_cfg.node_param.port_type = PORT_TRANSFER_OUT;
		param_cfg.node_param.port_id = PORT_PRE_OUT;
		param_cfg.node_id = ISP_NODE_MODE_PRE_ID;
		ret = ch->pipeline_handle->ops.cfg_param(ch->nonzsl_pre_pipeline, CAM_PIPELINE_CFG_BUF, &param_cfg);
		if (ret) {
			pr_err("fail to set yuv buf to isp\n");
			goto resbuf_cfg_fail;
		}
		if (isp_node->uinfo.mode_ltm != MODE_LTM_OFF) {
			/* todo: ltm buffer size needs to be refined.*/
			/* size = ((width + 1) & (~1)) * height * 3 / 2; */
			/*
			 * sizeof histo from 1 tile: 128 * 16 bit
			 * MAX tile num: 8 * 8
			 */
			size = 64 * 128 * 2;
			size = ALIGN(size, CAM_BUF_ALIGN_SIZE);

			if (isp_node->uinfo.ltm_rgb) {
				rgb_ltm = (struct isp_ltm_ctx_desc *)isp_node->rgb_ltm_handle;
				if (!rgb_ltm) {
					pr_err("fail to get rgb ltm.\n");
					return -1;
				}
				for (i = 0; i < ISP_LTM_BUF_NUM; i++) {
					ret = cam_buf_alloc(&rgb_ltm->ltm_frame[i].buf, size, module->iommu_enable);
					if (ret) {
						pr_err("fail to alloc ltm buf: %d ch %d\n", i, isp_node->ch_id);
						return -1;
					}

					ret = cam_buf_manager_buf_status_cfg(&rgb_ltm->ltm_frame[i].buf, CAM_BUF_STATUS_GET_IOVA_K_ADDR, CAM_BUF_IOMMUDEV_ISP);
					if (ret) {
						pr_err("fail to map isp ltm iommu buf.\n");
						cam_buf_free(&rgb_ltm->ltm_frame[i].buf);
						return -1;
					}
					rgb_ltm->ltm_frame[i].buf.bypass_iova_ops = CAM_ENABLE;
					pr_debug("ctx id %d, LTM CFGB[%d][0x%p] = 0x%lx\n", rgb_ltm->ctx_id, i, rgb_ltm->ltm_frame[i], rgb_ltm->ltm_frame[i].buf.iova);
				}
			}
		}
	}

	atomic_set(&module->state, CAM_RUNNING);

	if (module->cam_uinfo.is_pyr_dec && ch->nonzsl_pre_pipeline && proc_info->scene != CAMRAW_POSTPROC_PIPELINE_ISP_PROCESS) {
		pyrdec_node = module->nodes_dev.pyr_dec_node_dev[PYR_DEC_NODE_ID];
		rec_ctx = (struct isp_rec_ctx_desc *)isp_node->rec_handle;

		alloc_param.ch_id = ch->ch_id;
		alloc_param.width = proc_info->src_size.width;
		alloc_param.height = proc_info->src_size.height;
		alloc_param.iommu_enable = module->iommu_enable;
		alloc_param.is_pyr_dec = module->cam_uinfo.is_pyr_dec;
		alloc_param.pyr_out_fmt = hw->ip_dcam[0]->dcampath_abt[DCAM_BIN_OUT_PATH]->format[0];
		alloc_param.pyr_layer_num = ISP_PYR_DEC_LAYER_NUM;

		node_param.port_id = PORT_DEC_OUT;
		node_param.param = &alloc_param;
		pyrdec_node->port_cfg_cb_func(&node_param, PORT_CFG_BUFFER_ALLOC, pyrdec_node->port_cfg_cb_handle);
		isp_pyr_rec_buffer_alloc(rec_ctx, &alloc_param, isp_node->buf_manager_handle);
	}

	ret = camcore_frame_start_proc(src_frame, node_type, ch);
	if (ret) {
		pr_err("fail to start dcam/isp for raw proc\n");
		goto resbuf_cfg_fail;

	}

	atomic_set(&module->timeout_flag, 1);
	ret = camcore_timer_start(&module->cam_timer, CAMERA_TIMEOUT);

	return ret;

resbuf_cfg_fail:
	if (ch->nonzsl_pre_pipeline && proc_info->scene != CAMRAW_POSTPROC_PIPELINE_ISP_PROCESS)
		cam_queue_empty_frame_put(res_frame);
binbuf_cfg_fail:
	if (ch->nonzsl_pre_pipeline && proc_info->scene != CAMRAW_POSTPROC_PIPELINE_ISP_PROCESS)
		cam_queue_empty_frame_put(bin_frame);
midbuf_cfg_fail:
	if (proc_info->scene == CAMRAW_POSTPROC_PIPELINE_PROCESS)
		cam_queue_empty_frame_put(mid_frame);
dstbuf_cfg_fail:
	cam_queue_empty_frame_put(dst_frame);
	cam_queue_empty_frame_put(src_frame);

	pr_err("fail to call post raw proc\n");
	return ret;
}

int camrawcap_raw_proc_done(struct camera_module *module)
{
	int ret = 0;
	struct channel_context *ch = NULL;
	struct dcam_pipe_dev *dev = NULL;
	struct cam_nodes_dev *nodes_dev = NULL;

	pr_info("cam%d start\n", module->idx);
	module->capture_type = CAM_CAPTURE_STOP;
	atomic_set(&module->state, CAM_STREAM_OFF);
	dev = (struct dcam_pipe_dev *)module->dcam_dev_handle;

	if (atomic_read(&module->timeout_flag) == 1)
		pr_err("fail to raw proc, timeout\n");

	nodes_dev = &module->nodes_dev;
	ch = &module->channel[CAM_CH_CAP];

	camcore_timer_deinit(&module->cam_timer);

	ch->enable = 0;
	ch->dcam_port_id = -1;
	ch->isp_port_id = -1;
	ch->aux_dcam_port_id = -1;
	ch->ch_uinfo.dcam_raw_fmt = -1;
	ch->ch_uinfo.sensor_raw_fmt = -1;
	module->cam_uinfo.slice_num = 0;
	module->cam_uinfo.dcam_slice_mode = CAM_SLICE_NONE;

	atomic_set(&module->state, CAM_IDLE);
	camcore_pipeline_deinit(module, ch);

	pr_info("camera%d rawproc done.\n", module->idx);
	return ret;
}
#endif
