#include "wcn_chr.h"

static struct task_struct *client_rx_task;
static atomic_t wcn_chr_enable = ATOMIC_INIT(0);
static struct semaphore wcn_chr_sema;

struct socket *cl_sock;

static struct wcn_chr_event_list event_list[1] = {
		{"assert", 0x2501, 0},
};

static int wcn_chr_wr2serv(char *buf, size_t len)
{
	int ret;
	struct msghdr send_msg = {0};
	struct kvec send_vec = {0};

	send_vec.iov_base = buf;
	send_vec.iov_len = len;

	ret = kernel_sendmsg(cl_sock, &send_msg, &send_vec, 1, len);
	if (ret < 0) {
		WCN_ERR("%s: kernel_sendmsg failed with %d\n", __func__, ret);
		return -EINVAL;
	}

	return len;
}

static bool wcn_chr_is_enable(char *buf, size_t len)
{
	int ret;
	struct sockaddr_in s_addr;

	if (!cl_sock) {
		WCN_ERR("%s: invalid cl_sock\n", __func__);
		return false;
	}

	if (atomic_read(&wcn_chr_enable) == 1)
		return true;

	if (strncmp(buf, WCN_CHR_SOCKET_CMD_ENABLE, MIN(len, strlen(WCN_CHR_SOCKET_CMD_ENABLE))) == 0) {
		s_addr.sin_family = AF_INET;
		s_addr.sin_port = htons(4756);
		s_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

		ret = cl_sock->ops->connect(cl_sock, (struct sockaddr *)&s_addr, sizeof(s_addr), 0);
		if (ret == 0) {
			up(&wcn_chr_sema);
			return true;
		}
	}

	return false;
}

int wcn_chr_write(char *buf, size_t len)
{
	if (wcn_chr_is_enable(buf, len) == false)
		return -EINVAL;

	return wcn_chr_wr2serv(buf, len);
}

int wcn_chr_read(void)
{
	int ret = 0;
	struct sockaddr_in s_addr;

	if (!cl_sock) {
		WCN_ERR("%s: invalid cl_sock\n", __func__);
		return -EINVAL;
	}

	if (atomic_read(&wcn_chr_enable) == 1)
		return ret;

	s_addr.sin_family = AF_INET;
	s_addr.sin_port = htons(4756);
	s_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	ret = cl_sock->ops->connect(cl_sock, (struct sockaddr *)&s_addr, sizeof(s_addr), 0);
	if (ret < 0) {
		WCN_ERR("%s: connect failed with %d\n", __func__, ret);
		return ret;
	}

	up(&wcn_chr_sema);
	ret = wcn_chr_wr2serv(WCN_CHR_SOCKET_CMD_ENABLE, strlen(WCN_CHR_SOCKET_CMD_ENABLE));
	if (ret < 0) {
		WCN_ERR("%s: wr2serv failed with %d\n", __func__, ret);
		return ret;
	}

	return ret;

}


int wcn_chr_report_event(char *str, u32 index)
{
	wcn_bsp_chr_cp2_assert_t chr_event = {0};
	char send_buf[BUF_SIZE] = {0};
	struct msghdr send_msg = {0};
	struct kvec send_vec = {0};
	int ret;
	char char_info[BUF_SIZE] = {0};

	if (event_list[index].enable == 0) {
		WCN_INFO("event[%u]:%s not enable\n", index, event_list[index].name);
		return 0;
	}

	if (atomic_read(&wcn_chr_enable) == 0 || cl_sock == NULL) {
		WCN_INFO("%s: wcn chr not ready\n", __func__);
		return 0;
	}

	send_vec.iov_base = send_buf;
	send_vec.iov_len = BUF_SIZE;

	chr_event.cp_log_level = sysfs_info.loglevel;
	chr_event.ap_log_level = console_loglevel;
	snprintf(chr_event.error_dscp, sizeof(chr_event.error_dscp), str);
	snprintf(chr_event.cp_version, sizeof(chr_event.cp_version), sysfs_info.sw_ver_buf);

	ret = snprintf(char_info, sizeof(char_info), "0x%x,0x%x,%s,%s",
				chr_event.cp_log_level, chr_event.ap_log_level,
				chr_event.error_dscp, chr_event.cp_version);

	snprintf(send_buf, BUF_SIZE, "wcn_chr_ind_event,module=bsp,"
				"ref_count=%d,event_id=0x%x,"
				"version=0x%x,event_content_len=%d,"
				"char_info=%s",
			1, event_list[index].event_id, 1, ret, char_info);

	ret = kernel_sendmsg(cl_sock, &send_msg, &send_vec, 1, BUF_SIZE);
	if (ret < 0) {
		WCN_ERR("kernel_sendmsg failed with %d\n", ret);
		return ret;
	}

	return 0;
}

static int parse_event(char *buf, int len)
{
	char *tmp, *tmp1, *tmp2;
	long event_id, set, i;
	int ret;

	if (strncmp(buf, WCN_CHR_SOCKET_CMD_DISABLE,
					MIN(len, strlen(WCN_CHR_SOCKET_CMD_DISABLE))) == 0) {

		WCN_INFO("%s: %s\n", __func__, WCN_CHR_SOCKET_CMD_DISABLE);
		atomic_set(&wcn_chr_enable, 0);
		return 0;
	}

	if (strncmp(buf, WCN_CHR_SET_EVENT_HEAD,
					MIN(len, strlen(WCN_CHR_SET_EVENT_HEAD))) == 0) {

		WCN_INFO("%s: %s\n", __func__, buf);

		tmp1 = strstr(buf, "event_id=");
		tmp1 += strlen("event_id=");

		tmp2 = strstr(buf, "set=");
		tmp2 += strlen("set=");

		tmp = strchr(tmp2, ',');
		if (!tmp)
			return -EINVAL;
		*tmp = '\0';

		tmp = strchr(tmp1, ',');
		if (!tmp)
			return -EINVAL;
		*tmp = '\0';

		ret = kstrtol(tmp1, 16, &event_id);
		if (ret < 0) {
			WCN_ERR("kstrtol event_id failed with %d, %s\n", ret, tmp1);
			return ret;
		}

		ret = kstrtol(tmp2, 10, &set);
		if (ret < 0) {
                        WCN_ERR("kstrtol set failed with %d, %s\n", ret, tmp2);
                        return ret;
                }

		WCN_INFO("%s: %s: 0x%lx-%ld\n", __func__, WCN_CHR_SET_EVENT_HEAD, event_id, set);

		for (i = 0; i < ARRAY_SIZE(event_list); i++) {
			if (event_list[i].event_id == event_id) {
				event_list[i].enable = set;
				break;
			}
		}

		return 0;
	}

	return 0;
}

static int client_rx_thread(void *data)
{
	int ret;
	char recv_buf[BUF_SIZE] = {0};
	struct msghdr recv_msg = {0};
	struct kvec recv_vec = {0};

	cl_sock = kzalloc(sizeof(struct socket), GFP_KERNEL);
	if (!cl_sock) {
		WCN_ERR("kzalloc cl_sock failed\n");
		return -ENOMEM;
	}

retry:

	ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, 0, &cl_sock);
	if (ret < 0) {
		WCN_ERR("cl_sock create failed %d\n", ret);
		return -EINVAL;
	}

	WCN_INFO("wait for chr server ready\n");

	down(&wcn_chr_sema);

	WCN_INFO("chr server connected\n");
	atomic_set(&wcn_chr_enable, 1);

	recv_vec.iov_base = recv_buf;
	recv_vec.iov_len = BUF_SIZE;

	while (atomic_read(&wcn_chr_enable)) {
		memset(recv_buf, 0, sizeof(recv_buf));
		ret = kernel_recvmsg(cl_sock, &recv_msg, &recv_vec, 1, BUF_SIZE, 0);
		if (ret < 0) {
			WCN_ERR("kernel_recvmsg failed with %d\n", ret);
			continue;
		}
		WCN_INFO("%s\n", recv_buf);
		parse_event(recv_buf, strlen(recv_buf));
	}

	sock_release(cl_sock);

	goto retry;
}

int wcn_chr_init(void)
{
	WCN_INFO("%s entry\n", __func__);

	sema_init(&wcn_chr_sema, 0);

	client_rx_task = kthread_create(client_rx_thread, NULL, "wcn_chr_rx");
	if (IS_ERR_OR_NULL(client_rx_task)) {
		WCN_ERR("client rx thread create failed\n");
		return -1;
	}

	wake_up_process(client_rx_task);

	return 0;
}
