/*
* SPDX-FileCopyrightText: 2020-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#ifndef __FCC_H__
#define __FCC_H__

#define MAX_PHY_MODE	7
#define MAX_POWER_BACKOFF_RULE	100
#define MAX_FCC_COUNTRY_NUM	91
#define MAX_FCC_COUNTRY_NUM_HULK	81

struct sprd_priv;

struct sprd_power_backoff {
	u8 subtype;
	u8 channel;
	u8 bw;
	u8 power_rule[MAX_PHY_MODE][2];
} __packed;

struct fcc_power_bo {
	char country[3];
	u8 num;
	struct sprd_power_backoff power_backoff[MAX_POWER_BACKOFF_RULE];
} __packed;

struct fresh_bo_info {
	u8 pw_channel;
	u8 pw_bw;
};

struct sprd_fcc_priv {
	struct mutex lock;/* protects the FCC data */
	struct fcc_power_bo *cur_power_bo;
	bool flag;
	u8 channel;
	u8 bw;
};

void sc2355_fcc_fresh_bo_work(struct sprd_priv *priv, void *data, u16 len);
void sc2355_fcc_match_country(struct sprd_priv *priv, const char *alpha2);
void sc2355_fcc_reset_bo(void);
void sc2355_fcc_init(void);
void sc2355_init_5g_sar_info(void);
void sc2355_reset_5g_sar_info(void);
void sc2355_set_5g_sar_info(unsigned char *data);
u8 sc2355_pw_backoff_band2value(u8 channel);
#endif

