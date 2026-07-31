/* SPDX-License-Identifier: GPL-2.0-only*/
/*
 * audio/sprd/platform/deepbuffer/
 *
 * Front end cpu dai of sprd audio driver
 *
 * SPDX-FileCopyrightText: 2018 Unisoc (Shanghai) Technologies Co., Ltd
 *
 */

#ifndef __SPRD_DEEPBUFFER_H
#define __SPRD_DEEPBUFFER_H
#include <linux/list.h>

#define CHANNEL_NUM 8		//must be consistent with smsg channal.
#define SPRD_DEEPBUFFER_DRV_NAME "sprd-deepbuffer-driver"

struct deepbuffer_t;

struct deepbuffer_stream {
	u32 dst;
	int channel;		//sblock and smsg channel number

	struct snd_pcm_substream *substream;
	struct deepbuffer_t *deepbuffer;
	u32 stream_state;
	u32 cpu_dai_id;

	s32 period;
	s32 periods_avail;
	s32 periods_tosend;

	u32 hwptr_done;

	u32 last_elapsed_count;
	u32 last_getblk_count;

	u32 blk_count;

	struct mutex stream_mutex;
	struct list_head stream_list;
};

struct sblock_channel {
	int id;
	u32 status;
	u32 cpu_dai_id;
	int stream_id;
};

struct deepbuffer_t {
	struct deepbuffer_stream stream[2];
	struct deepbuffer_stream last_stream;
	struct device *dev;
	u32 deepbuffer_state;

	u32 dst;
	struct sblock_channel channel[CHANNEL_NUM];
	int (*channel_seek)(void *data, int stream_id, u32 cpu_dai_id);
	int stream_id;
	u32 usedmem_type;

	struct mutex channel_mutex;
	struct mutex deepbuffer_mutex;
};

#endif
