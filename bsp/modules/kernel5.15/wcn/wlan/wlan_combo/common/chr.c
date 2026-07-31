/*
* SPDX-FileCopyrightText: 2020-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#include "common/common.h"
#include "common/chip_ops.h"

/*
 * Sendmsg style is as follows:
 * "wcn_chr_ind_event,module=WIFI,
 * ref_count=%d,event_id=0x%x,
 * version=0x%x,event_content_len=%d,
 * char_info=**********"
 */
static int sprd_chr_sock_sendmsg(struct sprd_chr *chr, u8 *data)
{
	int ret;
	struct msghdr send_msg = {0};
	struct kvec send_vec = {0};

	send_vec.iov_base = data;
	send_vec.iov_len = 1024;

	wl_debug("CHR: ready to sendmsg: %s\n", data);
	ret = kernel_sendmsg(chr->chr_sock, &send_msg, &send_vec, 1, CHR_BUF_SIZE);
	if (ret < 0) {
		wl_err("%s, CHR: sendmsg failed, restore chr status\n", __func__);
		chr->chr_status = CHR_UNDEFINE;
		return -EINVAL;
	}

	return 0;
}

/* This function is used to fill chr_driver_params and sendbuf */
static void sprd_fill_chr_driver(struct chr_driver_params *chr_driver, u16 refcnt, u32 id,
				 u8 version, u8 content_len, u8 *content, u8 *buf)
{
	char temp[64] = {0};
	chr_driver->refcnt = refcnt;
	chr_driver->evt_id = id;
	chr_driver->version = version;
	chr_driver->evt_content_len = content_len;
	chr_driver->evt_content = content;

	if (id == EVT_CHR_DISC_LINK_LOSS || id == EVT_CHR_DISC_SYS_ERR
	    || id == EVT_CHR_OPEN_ERR) {
		snprintf(temp, sizeof(temp), "%u", (*content));
		snprintf(buf, CHR_BUF_SIZE, "wcn_chr_ind_event,module=WIFI,"
			"ref_count=%u,event_id=0x%x,version=0x%x,event_content_len=%d,"
			"char_info=%s", refcnt, id, version, (int)strlen(temp), temp);
	}
	return;
}

/* This function is used to report chr_disconnect evt from CP2 */
void sprd_chr_report_disconnect(struct sprd_vif *vif, u8 version,
				u32 evt_id, u32 evt_id_subtype,
				u8 evt_content_len, u8 *evt_content)
{
	int ret;
	u16 refcnt = 0;
	struct chr_linkloss_disc_error link_loss = {0};
	struct chr_system_disc_error system_err = {0};
	struct chr_driver_params chr_driver = {0};
	struct sprd_chr *chr = vif->priv->chr;
	u8 sendbuf[CHR_BUF_SIZE] = {0};
	u8 *pos = evt_content;

	if (*pos >= CHR_ARR_SIZE) {
		wl_err("%s, CHR: the content: %u is invalid, reporting not allowed",
			__func__, *pos);
		return;
	}

	if (evt_id == EVT_CHR_DISC_LINK_LOSS) {
		refcnt = ++chr->chr_refcnt->disc_linkloss_cnt[*pos];
		memcpy(&link_loss.reason_code, pos, sizeof(link_loss.reason_code));
		wl_debug("%s: CHR: %s, ref_cnt=%u\n", __func__,
			link_loss.reason_code == 1 ? "Power off AP" : "Beacon Loss",
			refcnt);
	} else if (evt_id == EVT_CHR_DISC_SYS_ERR) {
		/*
		 * cp2 does not implement this event, and the driver reserves the
		 * event interface first.
		 */
		refcnt = ++chr->chr_refcnt->disc_systerr_cnt[*pos];
		memcpy(&system_err.reason_code, pos, sizeof(system_err.reason_code));
		wl_debug("%s: CHR: SYSTEM_ERR_DISCONNECT, ref_cnt=%u\n", __func__, refcnt);
	}

	sprd_fill_chr_driver(&chr_driver, refcnt, evt_id, version,
			     evt_content_len, evt_content, sendbuf);

	mutex_lock(&chr->sock_lock);
	if (chr->chr_sock && chr->chr_status == CHR_ENABLE) {
		ret = sprd_chr_sock_sendmsg(chr, sendbuf);
		if (ret)
			wl_err("CHR: wifi_driver_sendmsg failed with 0x%x\n", evt_id);
	} else {
		wl_err("CHR: connect been closed, can not send msg to server");
	}
	mutex_unlock(&chr->sock_lock);

	return;
}

/* This function is used to report chr_open_error evt from driver */
void sprd_chr_report_open_error(struct sprd_chr *chr, u32 evt_id, u8 err_code)
{
	int ret;
	u16 refcnt = 0;
	struct chr_open_error open_error = {0};
	struct chr_driver_params chr_driver = {0};
	u8 sendbuf[CHR_BUF_SIZE] = {0};

	if (err_code >= CHR_ARR_SIZE) {
		wl_err("%s, CHR: the err_code: %u is invalid, reporting not allowed",
			__func__, err_code);
		return;
	}

	refcnt = ++chr->chr_refcnt->open_err_cnt[err_code];
	open_error.reason_code = err_code;

	sprd_fill_chr_driver(&chr_driver, refcnt, evt_id, CHR_VERSION, 1, &err_code, sendbuf);

	mutex_lock(&chr->sock_lock);
	if (chr->chr_sock && chr->chr_status == CHR_ENABLE) {
		ret = sprd_chr_sock_sendmsg(chr, sendbuf);
		if (ret)
			wl_err("CHR: wifi_driver_sendmsg failed with 0x%x\n", evt_id);
	} else {
		wl_err("CHR: connect been closed, can not send msg to server");
	}
	mutex_unlock(&chr->sock_lock);

	wl_debug("%s: CHR: %s, ref_cnt=%u\n", __func__,
		open_error.reason_code == 0 ? "Power_on Err" : "Download_ini Err",
		refcnt);
	return;
}

/* This function is used to get the key val from string */
static inline int sprd_chr_get_cmdval(u32 *val, u8 *pos, u8 *key, int octal)
{
	u8 *temp = strstr(pos, key);

	if (!temp) {
		wl_err("%s, CHR: %s failed\n", __func__, key);
		return -1;
	}
	temp += strlen(key);

	*val = simple_strtoul(temp, NULL, octal);
	return 0;
}

/* this function is used to decode string */
static int sprd_chr_decode_str(struct chr_cmd *cmd_set, u8 *data)
{
	char temp_str[CHR_BUF_SIZE] = {0};
	u8 *pos = data;

	if (strstr(pos, "wcn_chr_set_event")) {
		memcpy(cmd_set->evt_type, pos, strlen("wcn_chr_set_event"));
		pos += strlen("wcn_chr_set_event,");
	} else {
		return -1;
	}
	if (strstr(pos, "module=WIFI")) {
		memcpy(cmd_set->module, pos + strlen("module="), strlen("WIFI"));
		pos += strlen("module=WIFI,");
	} else {
		return -1;
	}

	if (sprd_chr_get_cmdval(&cmd_set->evt_id, pos, "event_id=", 16) ||
	    sprd_chr_get_cmdval(&cmd_set->set, pos, "set=", 10) ||
	    sprd_chr_get_cmdval(&cmd_set->maxcount, pos, "maxcount=", 16) ||
	    sprd_chr_get_cmdval(&cmd_set->timerlimit, pos, "tlimit=", 16))
		return -1;

	snprintf(temp_str, CHR_BUF_SIZE, "wcn_chr_set_event,module=WIFI,"
		"event_id=0x%x,set=%d,maxcount=0x%x,tlimit=0x%x", cmd_set->evt_id,
		cmd_set->set, cmd_set->maxcount, cmd_set->timerlimit);

	return strlen(temp_str);
}

/* this function is used to reset chr->cmd_list*/
static void sprd_chr_rebuild_cmdlist(struct sprd_chr *chr, struct chr_cmd *cmd_set)
{
	u32 index;

	if (cmd_set->evt_id >= EVT_CHR_DRV_MIN && cmd_set->evt_id <= EVT_CHR_DRV_MAX) {
		index = cmd_set->evt_id - EVT_CHR_DRV_MIN;

		if (index >= CHR_ARR_SIZE) {
			wl_err("%s, CHR: index:%u is invalid\n", __func__, index);
			return;
		}

		if (cmd_set->set == 1 && !chr->drv_cmd_list[index].set) {
			chr->drv_len = chr->drv_len + 1;
			wl_debug("%s, CHR: start monitoring evt_id:%#x, drv_len: %u",
				__func__, cmd_set->evt_id, chr->drv_len);
		} else if (cmd_set->set == 0 && chr->drv_cmd_list[index].set) {
			chr->drv_len = chr->drv_len - 1;
			wl_debug("%s, CHR: stop monitoring evt_id:%#x, drv_len: %u",
				__func__, cmd_set->evt_id, chr->drv_len);
		} else {
			wl_debug("%s, CHR: adjust evt's params evt_id:%#x, drv_len: %u",
				__func__, cmd_set->evt_id, chr->drv_len);
		}
		memcpy(&chr->drv_cmd_list[index], cmd_set, sizeof(struct chr_cmd));

	} else if (cmd_set->evt_id >= EVT_CHR_FW_MIN && cmd_set->evt_id <= EVT_CHR_FW_MAX) {
		index = cmd_set->evt_id - EVT_CHR_FW_MIN;

		if (index >= CHR_ARR_SIZE) {
			wl_err("%s, CHR: index:%u is invalid\n", __func__, index);
			return;
		}

		if (cmd_set->set == 1 && !chr->fw_cmd_list[index].set) {
			chr->fw_len = chr->fw_len + 1;
			wl_debug("%s, CHR: start monitoring evt_id:%#x, fw_len: %u",
				__func__, cmd_set->evt_id, chr->fw_len);
		} else if (cmd_set->set == 0 && chr->fw_cmd_list[index].set) {
			chr->fw_len = chr->fw_len - 1;
			wl_debug("%s, CHR: stop monitoring evt_id:%#x, fw_len: %u",
				__func__, cmd_set->evt_id, chr->fw_len);
		} else {
			wl_debug("%s, CHR: adjust evt's params evt_id:%#x, fw_len: %u",
				__func__, cmd_set->evt_id, chr->fw_len);
		}
		memcpy(&chr->fw_cmd_list[index], cmd_set, sizeof(struct chr_cmd));
	}

	return;
}

/* this function is used to determing whether CHR is disable*/
static inline int sprd_chr_set_sockflag(struct sprd_chr *chr, u8 *data)
{
	if (strstr(data, "wcn_chr_disable")) {
		chr->fw_len = 0;
		chr->drv_len = 0;
		chr->chr_status = CHR_DISABLE;
		memset(&chr->fw_cmd_list, 0, sizeof(chr->fw_cmd_list));
		memset(&chr->drv_cmd_list, 0, sizeof(chr->drv_cmd_list));
		return -1;
	}

	return 0;
}

static int sprd_chr_connect_server(struct sprd_chr *chr, struct sockaddr_in *s_addr)
{
	int ret = 0;
	int connect_limit = 0;
	struct socket *sock = chr->chr_sock;

	wl_debug("%s, CHR: wait the server starting", __func__);
	/* Optimize:block here while server not ready */
	while (1) {
		if (chr->thread_exit || connect_limit++ >= CHR_CONNECT_LIMIT) {
			wl_err("%s, CHR: stop wait connect, go exit!", __func__);
			return -1;
		}
		complete(&chr->socket_completed);
		msleep(1000);

		ret = sock->ops->connect(sock, (struct sockaddr *)s_addr, sizeof(*s_addr), 0);
		if (!ret)
			break;
	}
	wl_debug("CHR: wifi_client connected\n");

	return ret;
}

static void sprd_send_chr_cmd(struct sprd_priv *priv, struct sprd_chr *chr)
{
	struct sprd_vif *chr_vif;
	bool is_open = false;
	int ret;

	spin_lock_bh(&priv->list_lock);
	list_for_each_entry(chr_vif, &priv->vif_list, vif_node) {
		if (chr_vif->state & VIF_STATE_OPEN) {
			is_open = true;
			break;
		}
	}
	spin_unlock_bh(&priv->list_lock);

	/*
	 * only when cp2 is open state, cmd will be sent to CP2,
	 * otherwise cmd will be saved.
	 */
	if (is_open) {
		if (chr->fw_len != 0 || chr->drv_len == 0) {
			ret = sprd_set_chr(chr);
			if (ret)
				wl_err("%s, CHR: set chr_cmd to CP2 failed", __func__);
		} else {
			wl_debug("%s, CHR: the msg belongs to driver monitoring"
				 "event, don't set to cp2", __func__);
		}
	} else {
		wl_warn("%s, CHR: Drop set_chr_cmd in case of power off, "
			"save buf in chr_cmdlist", __func__);
	}
}

static int sprd_chr_client_thread(void *params)
{
	int ret, sbuf_len, buf_pos;
	struct sockaddr_in s_addr;
	char *recv_buf;
	struct msghdr recv_msg = {0};
	struct kvec recv_vec = {0};
	struct chr_cmd command = {0};
	struct sprd_chr *chr;
	struct sprd_priv *priv;

	chr = (struct sprd_chr *)params;
	priv = chr->priv;
	recv_buf = kmalloc(CHR_BUF_SIZE, GFP_KERNEL);
	if (!recv_buf) {
		wl_err("%s, CHR: recv_buf malloc failed\n", __func__);
		chr->chr_client_thread = NULL;
		return -ENOMEM;
	}
/*
 * After receiving disable_chr each time,it's necessary
 * to establish a new connection with the upper.
 */
retry:
	ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, 0, &chr->chr_sock);
	if (ret < 0 || chr->chr_sock == NULL) {
		wl_err("CHR: sock_client create failed %d\n", ret);
		kfree(recv_buf);
		recv_buf = NULL;
		chr->chr_client_thread = NULL;
		return -EINVAL;
	}

	s_addr.sin_family = AF_INET;
	s_addr.sin_port = htons(4758);
	s_addr.sin_addr.s_addr = in_aton("127.0.0.1");

	ret = sprd_chr_connect_server(chr, &s_addr);
	if (ret == -1)
		goto exit;

	recv_vec.iov_base = recv_buf;
	recv_vec.iov_len = CHR_BUF_SIZE;
/*
 * chr_status = CHR_DISABLE means have recevied disable_chr,
 * it will break out here to establish
 * a new connection with upper
 */
	while (chr->chr_status != CHR_DISABLE) {
		buf_pos = 0;
		memset(recv_buf, 0, CHR_BUF_SIZE);
		memset(&recv_msg, 0, sizeof(recv_msg));
		ret = kernel_recvmsg(chr->chr_sock, &recv_msg, &recv_vec, 1, CHR_BUF_SIZE, 0);

		if (unlikely(chr->thread_exit))
			goto exit;
		/* when an unknown err occurs in kernel_recvmsg,
		* a large amount of information will be printfed
		* cyclically, affecting the use of "kernel.log".
		* So go to "exit".
		*/
		if (unlikely(ret <= 0 || recv_buf[0] == 0)) {
			wl_err("%s, CHR: kernel_recvmsg faild, go to exit", __func__);
			goto exit;
		}

		wl_debug("%s, CHR: recvmsg: %s", __func__, recv_buf);

		/* Multiple chr_evt may be sended through a single string */
		while (ret && recv_buf[buf_pos]) {
			if (sprd_chr_set_sockflag(chr, recv_buf))
				break;

			sbuf_len = sprd_chr_decode_str(&command, recv_buf + buf_pos);
			if (sbuf_len == -1) {
				wl_err("%s, CHR: msg content error", __func__);
				goto exit;
			}
			chr->chr_status = CHR_ENABLE;
			sprd_chr_rebuild_cmdlist(chr, &command);

			buf_pos += sbuf_len;
			memset(&command, 0, sizeof(command));
		}

		sprd_send_chr_cmd(priv, chr);
	}

	mutex_lock(&chr->sock_lock);
	sock_release(chr->chr_sock);
	chr->chr_sock = NULL;
	chr->chr_status = CHR_UNDEFINE;
	mutex_unlock(&chr->sock_lock);
	wl_debug("%s, CHR: init socket, try to connect server\n", __func__);

	goto retry;

exit:
	/*thread exit is the only exit of chr*/
	chr->chr_status = CHR_UNDEFINE;
	chr->thread_exit = 0;
	mutex_lock(&chr->sock_lock);
	sock_release(chr->chr_sock);
	chr->chr_sock = NULL;
	mutex_unlock(&chr->sock_lock);
	kfree(recv_buf);
	recv_buf = NULL;
	complete(&chr->thread_completed);
	wl_debug("%s, CHR: exit client_thread\n", __func__);

	return 0;
}

void sprd_chr_handle_power(struct sprd_chr *chr)
{
/*
 * If driver have receied enable_chr and
 * required to monitor open_err evt then
 * start reporting the evt when power on err
 */
	if (chr->open_err_flag) {
		if (chr->chr_status == CHR_ENABLE && chr->drv_cmd_list[0].set)
			sprd_chr_report_open_error(chr, EVT_CHR_OPEN_ERR, chr->open_err_flag - 1);
		else
			wl_debug("%s, CHR: open err appears, but chr module is closed\n", __func__);

		CHR_OPENERR_FLAGSET(&chr->open_err_flag, OPEN_ERR_INIT);
	}
	return;
}

void sprd_chr_handle_open(struct sprd_chr *chr)
{
	int ret;
	/*
	 * Every time Wi-Fi is turned off,
	 * CP2 will clean up the global valrables
	 * that record the chr_evt to be monitored
	 */
	if (chr->fw_len) {
		wl_debug("%s, CHR: set chr to CP2 each time open", __func__);
		ret = sprd_set_chr(chr);
		if (ret)
			wl_err("%s, CHR: set chr_cmd to CP2 failed", __func__);
	}

	/* if created chr_client_thread falied in sprd_iface_probe, try to create here */
	if (!chr->chr_sock) {
		wl_debug("CHR: Creating chr_client_thread\n");
		ret = sprd_chr_init(chr);
		if (ret) {
			wl_err("%s chr init failed: %d\n", __func__, ret);
		}
	}
	return;
}

struct sprd_chr *sprd_chr_handle_probe(struct sprd_hif *hif)
{
	int ret;
	struct sprd_chr *chr = NULL;
	struct sprd_priv *priv = hif->priv;

	/* int the chr struct */
	chr = kzalloc(sizeof(*chr), GFP_KERNEL);
	if (!chr) {
		wl_err("%s, CHR: kzalloc chr failed", __func__);
		return NULL;
	}
	hif->chr = chr;
	chr->hif = hif;

	priv->chr = chr;
	chr->priv = priv;

	if (!chr->chr_sock) {
		wl_debug("CHR: Creating chr_client_thread\n");
		ret = sprd_chr_init(chr);
		if (ret) {
			wl_err("%s, CHR: chr init failed: %d\n", __func__, ret);
		}
	}
	return chr;
}

int sprd_chr_init(struct sprd_chr *chr)
{
	struct chr_refcnt_arr *refcnt = NULL;

	/* Only init chr_refcnt one time */
	if (!chr->chr_refcnt) {
		refcnt = kzalloc(sizeof(*refcnt), GFP_KERNEL);
		if (!refcnt) {
			wl_err("%s, kzalloc refcnt failed", __func__);
			return -ENOMEM;
		}
		chr->chr_refcnt = refcnt;
	}

	chr->chr_client_thread = NULL;
	chr->chr_sock = NULL;
	mutex_init(&chr->sock_lock);

	wl_debug("%s, CHR: ready to init the chr_client_thread", __func__);
	chr->chr_client_thread = kthread_create(sprd_chr_client_thread, chr, "wifi_driver_chr");
	if (IS_ERR_OR_NULL(chr->chr_client_thread)) {
		wl_err("CHR: client thread create failed\n");
		return -1;
	}
	init_completion(&chr->socket_completed);
	init_completion(&chr->thread_completed);
	wake_up_process(chr->chr_client_thread);

	return 0;
}

void sprd_chr_deinit(struct sprd_chr *chr, int exit_type)
{
	int ret;

	if (!chr) {
		wl_err("%s, CHR: struct chr has been free!", __func__);
		return;
	}

	/* wait the sprd_chr_client_thread entering the connect blocking status */
	ret = wait_for_completion_timeout(&chr->socket_completed, CHR_WAIT_TIMEOUT);

	if (!ret) {
		wl_err("%s, CHR: don't wait for the chr-thread to"
			"enter the connect blocking state", __func__);
		goto exit;
	}

	if (chr->chr_client_thread) {
		reinit_completion(&chr->thread_completed);
		chr->thread_exit = 1;
		/*
		 * when sprd_iface_remove is running, sprd_chr_thread may have just
		 * received the msg from upper and is processing it at this time,
		 * and it needs to wait for its processing to complete before
		 * re-entering blocking.Only REMOVE_DEINIT need to be do this.
		 * The max long time is 20ms;
		 */
		if (exit_type == REMOVE_DEINIT) {
			msleep(100);
			mutex_lock(&chr->sock_lock);
			if (chr->chr_sock) {
				kernel_sock_shutdown(chr->chr_sock, SHUT_RDWR);
			}
			mutex_unlock(&chr->sock_lock);
		}
		/* wait the sprd_chr_client_thread exit */
		ret = wait_for_completion_timeout(&chr->thread_completed, CHR_WAIT_TIMEOUT);

		if (!ret) {
			wl_err("%s, CHR: don't wait for the chr-thread to"
				"enter the recvmsg blocking state", __func__);
			return;
		}
	}
	chr->chr_client_thread = NULL;

exit:
	mutex_destroy(&chr->sock_lock);
	if (chr->chr_refcnt) {
		kfree(chr->chr_refcnt);
		chr->chr_refcnt = NULL;
	}
	kfree(chr);
	chr = NULL;
	wl_debug("%s, CHR: stop chr_client_thread!\n", __func__);
	return;
}

