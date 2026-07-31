/*
 * SPDX-FileCopyrightText: 2015 Spreadtrum Communications (Shanghai) Co., Ltd
 * SPDX-FileCopyrightText: 2016 Unisoc (Shanghai) Technologies Co., Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#ifndef __AUDIO_SBLOCK_H
#define __AUDIO_SBLOCK_H

#include <linux/poll.h>
#include <linux/types.h>

#define	SBLOCK_NOTIFY_GET	0x01
#define	SBLOCK_NOTIFY_RECV	0x02
#define	SBLOCK_NOTIFY_STATUS	0x04
#define	SBLOCK_NOTIFY_OPEN	0x08
#define	SBLOCK_NOTIFY_CLOSE	0x10

/* flag for CMD/DONE msg type */
#define SMSG_CMD_SBLOCK_INIT		0x0001
#define SMSG_DONE_SBLOCK_INIT		0x0002

/* flag for EVENT msg type */
#define SMSG_EVENT_SBLOCK_SEND		0x0001
#define SMSG_EVENT_SBLOCK_RELEASE	0x0002


struct sblock {
	void		*addr;
	u32	length;
};

int audio_sblock_poll_wait(uint8_t dst, uint8_t channel,
		struct file *filp, poll_table *wait);

int audio_sblock_create(uint8_t dst, uint8_t channel,
		u32 txblocknum, u32 txblocksize,
		u32 rxblocknum, u32 rxblocksize, int mem_type);

void audio_sblock_destroy(uint8_t dst, uint8_t channel);
int audio_sblock_get(uint8_t dst, uint8_t channel, struct sblock *blk,
		     int timeout);
int audio_sblock_send(uint8_t dst, uint8_t channel, struct sblock *blk);
int audio_sblock_receive(uint8_t dst, uint8_t channel, struct sblock *blk,
			 int timeout);
int audio_sblock_release(uint8_t dst, uint8_t channel, struct sblock *blk);
void audio_sblock_put(uint8_t dst, uint8_t channel, struct sblock *blk);
int audio_sblock_init(uint8_t dst, uint8_t channel,
		u32 txblocknum, u32 txblocksize,
		u32 rxblocknum, u32 rxblocksize, int mem_type);
void audio_sblock_info_print(void);
int audio_sblock_get_free_count(uint8_t dst, uint8_t channel);
int audio_sblock_register_notifier(uint8_t dst, uint8_t channel,
		void (*handler)(int event, void *data, void *smsg), void *data);
#endif
