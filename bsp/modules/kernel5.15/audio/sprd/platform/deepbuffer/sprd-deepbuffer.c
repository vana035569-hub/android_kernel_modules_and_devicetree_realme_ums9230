// SPDX-License-Identifier: GPL-2.0-only
/*
 * audio/sprd/platform/deepbuffer/
 *
 * Front end cpu dai of sprd audio driver
 *
 * SPDX-FileCopyrightText: 2018 Unisoc (Shanghai) Technologies Co., Ltd
 *
 */

#include "sprd-asoc-debug.h"
#define pr_fmt(fmt) pr_sprd_fmt(" DEEPBUFFER ")""fmt

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/memory.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/suspend.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include <linux/io.h>
#include <linux/workqueue.h>

#include <sound/control.h>
#include <sound/tlv.h>

#include <linux/fs.h>

// #include "audio-platform.h"
#include "sprd-deepbuffer.h"
#include "audio_sblock.h"
#include "audio_mem.h"
#include "audio-sipc.h"
#include "audio-smsg.h"
#include "sprd-platform-pcm-routing.h"

#define ETRACE(x...) pr_err("Error: " x)
#define WTRACE(x...) pr_warn(x)
#define ADEBUG() pr_debug("sprd-deepbuffer: function: %s,line %d\n", \
	__func__, __LINE__)

#define NOT_AVAILABLE		0
#define AVAILABLE		1

#define STREAM_BLOCK_COUNT	4

#define TX_DATA_BLOCK_SIZE	80
#define RX_DATA_BLOCK_SIZE	0
#define MAX_BUFFER_SIZE		(64 * 1024)

#define MAX_PERIOD_SIZE		(MAX_BUFFER_SIZE)
#define USE_FORMATS	(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | \
			SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE)
#define USE_RATE	(SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000)
#define USE_RATE_MIN		5500
#define USE_RATE_MAX		48000
#define USE_CHANNELS_MIN		1
#define USE_CHANNELS_MAX		8
#define USE_PERIODS_MIN		1
#define USE_PERIODS_MAX		1024

static int deepbuffer_pcm_lib_preallocate_pages(struct snd_pcm *pcm,
			int type, void *data, u32 size, size_t max, int stream_id);
static void deepbuffer_pcm_lib_preallocate_free(struct snd_pcm *pcm, int stream_id);

struct buff_info {
	unsigned int buff_addr;
	unsigned int buff_length;
	unsigned int channel;
};

struct deepbuffer_msg {
	u32 command;
	u32 stream_id;
	u32 reserved1;
	u32 reserved2;
	void *param;
};

enum snd_status {
	DEEPBUFFER_IDLE,
	DEEPBUFFER_OPENNED,
	DEEPBUFFER_CLOSED,
	DEEPBUFFER_STOPPED,
	DEEPBUFFER_PREPARED,
	DEEPBUFFER_TRIGGERED,
	DEEPBUFFER_PARAMIZED,
	DEEPBUFFER_ABORT,
};

static struct snd_pcm_hardware deepbuffer_playback = {
	.info = (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP_VALID),
	.formats = USE_FORMATS,
	.rates = USE_RATE,
	.rate_min = USE_RATE_MIN,
	.rate_max = USE_RATE_MAX,
	.channels_min = USE_CHANNELS_MIN,
	.channels_max = USE_CHANNELS_MAX,
	.buffer_bytes_max = MAX_BUFFER_SIZE,
	.period_bytes_min = 64,
	.period_bytes_max = MAX_PERIOD_SIZE,
	.periods_min = USE_PERIODS_MIN,
	.periods_max = USE_PERIODS_MAX,
	.fifo_size = 0,
};

static struct snd_pcm_hardware deepbuffer_capture = {
	.info = (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP_VALID),
	.formats = USE_FORMATS,
	.rates = USE_RATE,
	.rate_min = USE_RATE_MIN,
	.rate_max = USE_RATE_MAX,
	.channels_min = USE_CHANNELS_MIN,
	.channels_max = USE_CHANNELS_MAX,
	.buffer_bytes_max = MAX_BUFFER_SIZE,
	.period_bytes_min = 64,
	.period_bytes_max = MAX_PERIOD_SIZE,
	.periods_min = USE_PERIODS_MIN,
	.periods_max = USE_PERIODS_MAX,
	.fifo_size = 0,
};

static void deepbuffer_lock(struct deepbuffer_t *deepbuffer)
{
	if (deepbuffer)
		mutex_lock(&deepbuffer->deepbuffer_mutex);
}

static void deepbuffer_unlock(struct deepbuffer_t *deepbuffer)
{
	if (deepbuffer)
		mutex_unlock(&deepbuffer->deepbuffer_mutex);
}

static void deepbuffer_stream_lock(struct deepbuffer_stream *stream)
{
	if (stream)
		mutex_lock(&stream->stream_mutex);
}

static void deepbuffer_stream_unlock(struct deepbuffer_stream *stream)
{
	if (stream)
		mutex_unlock(&stream->stream_mutex);
}

static int get_sblock_channel(struct deepbuffer_t *deepbuffer, int stream_id, u32 cpu_dai_id)
{
	int ch;

#if 0
	for (ch = 0; ch < CHANNEL_NUM; ch++) {
		if (deepbuffer->channel[ch].status == AVAILABLE) {
			deepbuffer->channel[ch].status = NOT_AVAILABLE;
			deepbuffer->channel[ch].cpu_dai_id = cpu_dai_id;
			deepbuffer->channel[ch].stream_id = stream_id;
			return deepbuffer->channel[ch].id;
		}
	}

	return -1;
#else

	if (cpu_dai_id == FE_DAI_ID_SPATIAL_AUD) {
		ch = 0;
	} else if (cpu_dai_id == FE_DAI_ID_DPBF_2) {
		ch = 1;
	} else if (cpu_dai_id == FE_DAI_ID_DPBF_3) {
		ch = 2;
	} else {
		pr_err("cpu_dai_id: %d", cpu_dai_id);
		return -1;
	}

	if (deepbuffer->channel[ch].status == AVAILABLE) {
		deepbuffer->channel[ch].status = NOT_AVAILABLE;
		deepbuffer->channel[ch].cpu_dai_id = cpu_dai_id;
		deepbuffer->channel[ch].stream_id = stream_id;
		return deepbuffer->channel[ch].id;
	} else {
		pr_err("channel%d is not available", deepbuffer->channel[ch].id);
		return -1;
	}
#endif

}

static int release_sblock_channel(struct deepbuffer_t *deepbuffer, int channel_id)
{
	int ch;

	for (ch = 0; ch < CHANNEL_NUM; ch++) {
		if (channel_id == deepbuffer->channel[ch].id) {
			deepbuffer->channel[ch].status = AVAILABLE;
			deepbuffer->channel[ch].cpu_dai_id = -1;
			deepbuffer->channel[ch].stream_id = -1;
			return 0;
		}
	}

	return -1;
}

static int get_channel_direction(struct deepbuffer_t *deepbuffer, int channel_id)
{
	int ch;

	for (ch = 0; ch < CHANNEL_NUM; ch++) {
		if (channel_id == deepbuffer->channel[ch].id)
			return deepbuffer->channel[ch].stream_id;
	}
	return -1;
}

static void check_channel_status(struct deepbuffer_t *deepbuffer)
{
	int ch, stream_id;

	pr_info("deepbuffer channel status:\n");
	pr_info("channel ID |     status    | FE ID | direction\n");
	for (ch = 0; ch < CHANNEL_NUM; ch++) {
		stream_id = deepbuffer->channel[ch].stream_id;
		pr_info("%10d | %13s | %5d | %8s", deepbuffer->channel[ch].id,
			deepbuffer->channel[ch].status?"AVAILABLE":"NOT AVAILABLE",
			deepbuffer->channel[ch].cpu_dai_id,
			stream_id < 0?"NA":(stream_id?"capture":"playback"));
	}
}

static int seek_fe_channel(void *data, int stream_id, u32 cpu_dai_id)
{
	int ch;
	struct deepbuffer_t *deepbuffer = data;

	for (ch = 0; ch < CHANNEL_NUM; ch++) {
		if (cpu_dai_id == deepbuffer->channel[ch].cpu_dai_id &&
			stream_id == deepbuffer->channel[ch].stream_id)
			return deepbuffer->channel[ch].id;
	}

	return -1;
}

static int deepbuffer_pcm_open(struct snd_soc_component *component,
		struct snd_pcm_substream *substream)
{
	int stream_channel, ret = 0;
	struct snd_soc_pcm_runtime *srtd = substream->private_data;
	const int stream_id = substream->pstr->stream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_pcm *pcm = substream->pcm;
	struct deepbuffer_stream *stream;
	u32 cpu_dai_id = asoc_rtd_to_cpu(srtd, 0)->id;
	struct deepbuffer_t *deepbuffer = NULL;
	struct deepbuffer_stream *stream_head = NULL;
	struct snd_soc_component *platform =
		snd_soc_rtdcom_lookup(srtd, SPRD_DEEPBUFFER_DRV_NAME);

	if (platform == NULL) {
		pr_err("%s can't get plaatform", __func__);
		return -ENODEV;
	}
	deepbuffer = snd_soc_component_get_drvdata(platform);
	if (deepbuffer == NULL) {
		pr_err("%s drvdata is NULL", __func__);
		return -EINVAL;
	}
	stream_head = &deepbuffer->stream[substream->stream];

	deepbuffer_lock(deepbuffer);
	if (!deepbuffer->deepbuffer_state) {
		deepbuffer_unlock(deepbuffer);
		pr_err("pcm_open error, deepbuffer state: %u\n",
			deepbuffer->deepbuffer_state);
		return -EIO;
	}


	stream_channel = get_sblock_channel(deepbuffer, stream_id, cpu_dai_id);
	deepbuffer_unlock(deepbuffer);
	if (stream_channel < 0) {
		pr_err("there is no sblock channel for %s of FE(%d)",
				stream_id?"capture":"playback", cpu_dai_id);
		return -EBUSY;
	}

	stream = kmalloc(sizeof(struct deepbuffer_stream), GFP_KERNEL);
	if (stream == NULL)
		return -ENOMEM;

	ret = deepbuffer_pcm_lib_preallocate_pages(
		pcm, SNDRV_DMA_TYPE_CONTINUOUS,
		((struct device *)(__force unsigned long)(GFP_KERNEL)),
		MAX_BUFFER_SIZE, MAX_BUFFER_SIZE, stream_id);
	if (ret) {
		pr_err("%s: prealloc pages error.\n", __func__);
		kfree(stream);
		stream = NULL;
		return -ENOMEM;
	}

	stream->dst = stream_head->dst;
	stream->channel = stream_channel;
	stream->substream = substream;
	stream->deepbuffer = deepbuffer;
	stream->cpu_dai_id = asoc_rtd_to_cpu(srtd, 0)->id;
	stream->period = 0;
	stream->periods_tosend = 0;
	stream->periods_avail = 0;
	stream->hwptr_done = 0;
	stream->last_getblk_count = 0;
	stream->last_elapsed_count = 0;
	stream->blk_count = STREAM_BLOCK_COUNT;
	mutex_init(&stream->stream_mutex);

	deepbuffer_lock(deepbuffer);
	INIT_LIST_HEAD(&(stream->stream_list));
	list_add(&(stream->stream_list), &(stream_head->stream_list));
	deepbuffer_unlock(deepbuffer);

	if (stream_id == SNDRV_PCM_STREAM_PLAYBACK)
		runtime->hw = deepbuffer_playback;
	else
		runtime->hw = deepbuffer_capture;

	runtime->private_data = stream;
	check_channel_status(deepbuffer);

	substream->wait_time = 500;
	return ret;
}

static int deepbuffer_pcm_close(struct snd_soc_component *component,
		struct snd_pcm_substream *substream)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *srtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct deepbuffer_stream *stream = runtime->private_data;
	struct snd_pcm *pcm = substream->pcm;
	const int stream_id = substream->pstr->stream;
	struct deepbuffer_t *deepbuffer = NULL;
	struct snd_soc_component *platform =
		snd_soc_rtdcom_lookup(srtd, SPRD_DEEPBUFFER_DRV_NAME);

	if (platform == NULL) {
		pr_err("%s can't get plaatform", __func__);
		return -ENODEV;
	}
	deepbuffer = snd_soc_component_get_drvdata(platform);
	if (deepbuffer == NULL) {
		pr_err("%s drvdata is NULL", __func__);
		return -EINVAL;
	}

	if (stream == NULL) {
		pr_err("%s error, no such substream!\n", __func__);
		return -ENODEV;
	}

	deepbuffer_lock(deepbuffer);
	if (!deepbuffer->deepbuffer_state) {
		deepbuffer_unlock(deepbuffer);
		pr_err("pcm_close error, deepbuffer state: %u\n",
			deepbuffer->deepbuffer_state);
		return -EIO;
	}

	if (release_sblock_channel(deepbuffer, stream->channel)) {
		deepbuffer_unlock(deepbuffer);
		pr_err("pcm_close error, wrong stream channel!\n");
		return -1;
	}

	list_del(&(stream->stream_list));
	stream->substream = NULL;
	kfree(stream);
	runtime->private_data = NULL;
	deepbuffer_unlock(deepbuffer);

	deepbuffer_pcm_lib_preallocate_free(pcm, stream_id);
	check_channel_status(deepbuffer);
	return ret;
}

static int deepbuffer_pcm_lib_malloc_pages(struct snd_pcm_substream *substream,
			size_t size)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_dma_buffer *dmab = &substream->dma_buffer;

	pr_debug("%s: dmab addr 0x%llx", __func__, dmab->addr);

	/* Use the pre-allocated buffer */
	snd_pcm_set_runtime_buffer(substream, dmab);
	runtime->dma_bytes = size;

	return 1;
}

static int deepbuffer_pcm_lib_free_pages(struct snd_pcm_substream *substream)
{
	snd_pcm_set_runtime_buffer(substream, NULL);
	return 0;
}

static int deepbuffer_hw_params(struct snd_soc_component *component,
			struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *hw_params)
{
	s32 ret = 0;

	ADEBUG();
	ret = deepbuffer_pcm_lib_malloc_pages(substream,
			params_buffer_bytes(hw_params));
	pr_debug("hw_params result is %d", ret);
	return ret;
}

static int deepbuffer_hw_free(struct snd_soc_component *component,
			struct snd_pcm_substream *substream)
{
	int ret = 0;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct deepbuffer_stream *stream = runtime->private_data;

	if (stream == NULL) {
		pr_err("%s error, no such substream!\n", __func__);
		return -ENODEV;
	}

	deepbuffer_stream_lock(stream);
	ret = deepbuffer_pcm_lib_free_pages(substream);
	deepbuffer_stream_unlock(stream);

	return ret;
}

static int deepbuffer_pcm_prepare(struct snd_soc_component *component,
			struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *srtd = substream->private_data;
	struct deepbuffer_t *deepbuffer = NULL;
	struct snd_soc_component *platform =
		snd_soc_rtdcom_lookup(srtd, SPRD_DEEPBUFFER_DRV_NAME);

	if (platform == NULL) {
		pr_err("%s can't get plaatform", __func__);
		return -ENODEV;
	}
	deepbuffer = snd_soc_component_get_drvdata(platform);
	if (deepbuffer == NULL) {
		pr_err("%s drvdata is NULL", __func__);
		return -EINVAL;
	}

	ADEBUG();
	deepbuffer_lock(deepbuffer);
	if (!deepbuffer->deepbuffer_state) {
		deepbuffer_unlock(deepbuffer);
		pr_err("pcm_prepare error, deepbuffer state: %u\n",
			deepbuffer->deepbuffer_state);
		return -EIO;
	}
	deepbuffer_unlock(deepbuffer);
	return 0;
}


static int deepbuffer_data_trigger_process(struct deepbuffer_stream *stream,
			int stream_id)
{
	struct snd_pcm_runtime *runtime = stream->substream->runtime;
	struct sblock blk = { 0 };
	struct buff_info *buff = NULL;
	int ret = 0;

	if (stream_id == SNDRV_PCM_STREAM_PLAYBACK) {
		stream->periods_avail = snd_pcm_playback_avail(runtime) /
			runtime->period_size;
	} else {
		stream->periods_avail = snd_pcm_capture_avail(runtime) /
			runtime->period_size;
	}

	stream->periods_tosend = runtime->periods - stream->periods_avail;

	pr_debug("%s: stream is %s, periods tosend:%d", __func__,
		stream_id?"capture":"playback", stream->periods_tosend);

	while (stream->periods_tosend) {
		ret = audio_sblock_get(stream->dst, stream->channel, &blk, 0);
		if (ret)
			break;
		stream->last_getblk_count++;

		buff = (struct buff_info *)blk.addr;
		buff->buff_length = frames_to_bytes(runtime, runtime->period_size);
		buff->buff_addr = stream->substream->dma_buffer.addr +
			stream->period * buff->buff_length;
		buff->channel = runtime->channels;
		ret = audio_sblock_send(stream->dst, stream->channel, &blk);
		if (ret) {
			pr_err("sblock send failed, ret = %d!\n", ret);
			audio_sblock_put(stream->dst, stream->channel, &blk);
		}
		stream->period++;
		stream->period = stream->period % runtime->periods;
		stream->periods_tosend--;
	}

	pr_info("sblock_getblock_count trigger is %d\n", stream->last_getblk_count);

	return ret;
}

static int deepbuffer_pcm_trigger(struct snd_soc_component *component,
			struct snd_pcm_substream *substream, int cmd)
{
	int err = 0;
	int ret = 0;
	struct snd_pcm_runtime *runtime = substream->runtime;
	const int stream_id = substream->pstr->stream;
	struct deepbuffer_stream *stream = runtime->private_data;

	if (stream == NULL) {
		pr_err("%s error, no such substream!\n", __func__);
		return -ENODEV;
	}

	ADEBUG();

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		pr_debug("%s in, TRIGGER_START, stream_id=%d\n", __func__, stream_id);
		stream->stream_state = DEEPBUFFER_TRIGGERED;
		ret = deepbuffer_data_trigger_process(stream, stream_id);
		pr_info("%s out, TRIGGER_START, ret=%d\n", __func__, ret);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		pr_info("%s in, TRIGGER_STOP, stream_id=%d\n", __func__, stream_id);
		stream->stream_state = DEEPBUFFER_STOPPED;
		/*
		 * Change the pointer here because DSP can't read all the sblocks that
		 * have been sent. If you do not update the pointer, the DMA buffer pointer
		 * will not updated, and there will be a duplicate sound when you start playing.
		 */
		//ret = audio_sblock_change_ptr(stream->dst, stream->channel);
		pr_info("%s out, TRIGGER_STOP, ret=%d\n", __func__, ret);
		pr_info("%s: stream is %s, hw_ptr = %ld, appl_ptr = %ld\n", __func__,
			stream_id?"capture":"playback",
			runtime->status->hw_ptr, runtime->control->appl_ptr);
		break;
	default:
		err = -EINVAL;
		break;
	}
	return 0;
}

static snd_pcm_uframes_t deepbuffer_pcm_pointer(struct snd_soc_component *component,
			struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct deepbuffer_stream *stream = runtime->private_data;
	unsigned int offset;

	if (stream == NULL) {
		pr_err("%s error, no such substream!\n", __func__);
		return -ENODEV;
	}

	pr_debug("stream is :%s, hwptr_done: %u", substream->stream?"capture":"playback",
			stream->hwptr_done);
	offset =
	    stream->hwptr_done * frames_to_bytes(runtime, runtime->period_size);

	return bytes_to_frames(runtime, offset);
}

static int deepbuffer_mmap(struct snd_soc_component *component,
			struct snd_pcm_substream *substream,
			struct vm_area_struct *vma)
{
	ADEBUG();
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	return remap_pfn_range(vma, vma->vm_start,
			       substream->dma_buffer.addr >> PAGE_SHIFT,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

/*
 * called by sblock thread, which is created in audio_sblock.c
 */
static int deepbuffer_data_transfer_process(struct deepbuffer_stream *stream,
			int stream_id)
{
	struct snd_pcm_runtime *runtime = stream->substream->runtime;
	struct sblock blk = { 0 };
	struct buff_info *buff = NULL;
	int ret = 0;
	s32 elapsed_blks = 0;
	s32 periods_avail;
	s32 periods_tosend;
	s32 cur_blk_count = 0;

	ADEBUG();

	cur_blk_count = audio_sblock_get_free_count(stream->dst, stream->channel);

	/* calculating blks have not yet been released */
	elapsed_blks =
		(cur_blk_count + stream->last_getblk_count - stream->blk_count) -
		stream->last_elapsed_count;

	if (stream_id == SNDRV_PCM_STREAM_PLAYBACK) {
		periods_avail = snd_pcm_playback_avail(runtime) /
			runtime->period_size;
	} else {
		periods_avail = snd_pcm_capture_avail(runtime) /
			runtime->period_size;
	}
	periods_tosend = stream->periods_avail - periods_avail;

	if (periods_tosend > 0)
		stream->periods_tosend += periods_tosend;

	pr_debug("%s: runtime->periods = %d, stream->periods_avail = %d, priods_to_sned = %d\n",
		__func__, runtime->periods, stream->periods_avail, stream->periods_tosend);

	pr_debug("%s: stream is %s, hw_ptr = %ld, appl_ptr = %ld\n", __func__,
		stream_id ? "capture" : "playback",
		runtime->status->hw_ptr, runtime->control->appl_ptr);

	pr_debug("hw = %ld, appl = %ld\n", runtime->status->hw_ptr, runtime->control->appl_ptr);

	if (stream->periods_tosend) {
		while (stream->periods_tosend) {
			ret = audio_sblock_get(stream->dst, stream->channel, &blk, 0);
			if (ret)
				break;
			stream->last_getblk_count++;
			buff = (struct buff_info *)blk.addr;
			buff->buff_length = frames_to_bytes(runtime, runtime->period_size);
			buff->buff_addr = stream->substream->dma_buffer.addr +
				stream->period * buff->buff_length;
			buff->channel = runtime->channels;
			ret = audio_sblock_send(stream->dst, stream->channel, &blk);
			if (ret) {
				pr_err("sblock send failed, ret = %d!\n", ret);
				audio_sblock_put(stream->dst, stream->channel, &blk);
			}
			stream->periods_tosend--;
			stream->period++;
			stream->period = stream->period % runtime->periods;
		}
	} else {
		pr_info("deepbuffer no data to send ");
		if (audio_sblock_get_free_count(stream->dst, stream->channel) ==
		    STREAM_BLOCK_COUNT) {
			pr_debug
			    ("sprd-deepbuffer.c: deepbuffer no data to send and is empty ");
		}
	}

	while (elapsed_blks > 0) {
		elapsed_blks--;
		stream->hwptr_done++;
		stream->hwptr_done %= runtime->periods;
		snd_pcm_period_elapsed(stream->substream);
		periods_avail++;
		stream->last_elapsed_count++;
	}
	stream->periods_avail = periods_avail;

	return 0;
}

static void sblock_notifier(int event, void *data, void *smsg_t)
{
	struct deepbuffer_t *deepbuffer = data;
	struct aud_smsg *smsg = smsg_t;
	struct deepbuffer_stream *stream = NULL, *tmp_stream, *n;
	u32 channel_id = smsg->channel;
	struct deepbuffer_stream *stream_head;
	int ret = 0;
	int stream_id = get_channel_direction(deepbuffer, channel_id);

	if (stream_id < 0) {
		pr_err("channel id is wrong!/n");
		return;
	}

	deepbuffer_lock(deepbuffer);
	stream_head = &deepbuffer->stream[stream_id];

	if (list_empty(&(stream_head->stream_list))) {
		pr_err("%s, stream_head list is empty!!\n", __func__);
		deepbuffer_unlock(deepbuffer);
		return;
	}
	list_for_each_entry_safe(tmp_stream, n, &(stream_head->stream_list), stream_list) {
		if (channel_id == tmp_stream->channel) {
			stream = tmp_stream;
			break;
		}
	}

	if (stream == NULL) {
		pr_err("%s error, no such substream!\n", __func__);
		deepbuffer_unlock(deepbuffer);
		return;
	}

	if (event == SBLOCK_NOTIFY_GET) {
		if (stream->stream_state == DEEPBUFFER_TRIGGERED) {
			deepbuffer_stream_lock(stream);
			ret = deepbuffer_data_transfer_process(stream, stream_id);
			deepbuffer_stream_unlock(stream);
		} else {
			pr_debug("\n: deepbuffer is stopped\n");
		}
	}
	deepbuffer_unlock(deepbuffer);
}

/*
 * deepbuffer_sblock_create()
 *	creat sblocks
 *	stream channel blocks used to send PCM data to DSP.
 */
static int deepbuffer_sblock_create(struct deepbuffer_t *deepbuffer)
{
	int ch, ret = 0;

	for (ch = 0; ch < CHANNEL_NUM; ch++) {
		pr_info("deepbuffer->channel[%d].id:%d", ch, deepbuffer->channel[ch].id);
		ret = audio_sblock_init(deepbuffer->dst, deepbuffer->channel[ch].id,
			STREAM_BLOCK_COUNT, TX_DATA_BLOCK_SIZE,
			STREAM_BLOCK_COUNT, RX_DATA_BLOCK_SIZE, deepbuffer->usedmem_type);
		if (ret) {
			ETRACE("deepbuffer stream channel(%d) sblock create failed, err:%d\n",
					deepbuffer->channel[ch].id, ret);
			goto fail;
		}
		ret = audio_sblock_register_notifier(deepbuffer->dst, deepbuffer->channel[ch].id,
			sblock_notifier, deepbuffer);
		if (ret) {
			ETRACE("sblock create register notifier failed ret:%d\n.", ret);
			goto fail;
		}
		deepbuffer->channel[ch].status = AVAILABLE;
	}

	pr_info("sblock create success!\n");
	return ret;

fail:
	ETRACE("sblock create failed!\n");
	return ret;
}

/*
 * deepbuffer_sblock_destory()
 * destroy sblocks
 */
static void deepbuffer_sblock_destory(struct deepbuffer_t *deepbuffer)
{
	int ch;

	for (ch = 0; ch < CHANNEL_NUM; ch++)
		audio_sblock_destroy(deepbuffer->dst, deepbuffer->channel[ch].id);
}

/*
 * sprd_snd_platform_probe()
 * create sblock, create kernel thread do handshake with DSP.
 */
static int sprd_snd_platform_probe(struct snd_soc_component *platform)
{
	int ret = 0;
	struct deepbuffer_t *deepbuffer = snd_soc_component_get_drvdata(platform);

	ret = deepbuffer_sblock_create(deepbuffer);
	if (ret) {
		pr_err("deepbuffer: sblock create error:%d!\n", ret);
		return -1;
	}

	deepbuffer_lock(deepbuffer);
	deepbuffer->deepbuffer_state = AVAILABLE;
	deepbuffer_unlock(deepbuffer);
	return 0;
}

/*
 * sprd_snd_platform_remove()
 * destroy sblock
 */
static void sprd_snd_platform_remove(struct snd_soc_component *platform)
{
	struct deepbuffer_t *deepbuffer = snd_soc_component_get_drvdata(platform);

	deepbuffer_sblock_destory(deepbuffer);
}

static void deepbuffer_pcm_lib_preallocate_free(struct snd_pcm *pcm, int stream_id)
{
	struct snd_pcm_substream *substream;

	for (substream = pcm->streams[stream_id].substream; substream;
		substream = substream->next) {
		if (substream->dma_buffer.area) {
			audio_mem_unmap(substream->dma_buffer.area);
			substream->dma_buffer.area = (int8_t *)NULL;
		}
		if (substream->dma_buffer.addr) {
			audio_mem_free(DDR32, substream->dma_buffer.addr,
				substream->dma_buffer.bytes);
			substream->dma_buffer.addr = (dma_addr_t)NULL;
			substream->dma_buffer.bytes = 0;
		}
	}
}

static int deepbuffer_pcm_lib_preallocate_pages(struct snd_pcm *pcm,
			int type, void *data, u32 size, size_t max, int stream_id)
{
	struct snd_pcm_substream *substream;

	/*
	 * alloc room for playback and record.
	 */
	for (substream = pcm->streams[stream_id].substream; substream;
		substream = substream->next) {
		struct snd_dma_buffer *dmab = &substream->dma_buffer;
		u32 addr = audio_mem_alloc(DDR32, &size);

		if (addr) {
			dmab->dev.type = type;
			dmab->dev.dev = data;
			dmab->area = (char *)audio_mem_vmap(addr, size, 1);
			if (dmab->area == NULL) {
				pr_err("%s audio_mem_vmap error", __func__);
				return -ENOMEM;
			}
			dmab->addr = addr;
			dmab->bytes = size;
			memset((void *)dmab->area, 0x5a, size);
			pr_info("%s: dmab addr is %x, area is %lx,size is %d, substream: %d",
				 __func__,
				 (uint32_t)dmab->addr,
				(unsigned long)dmab->area,
				(int)size, substream->stream);
			if (substream->dma_buffer.bytes > 0)
				substream->buffer_bytes_max =
					substream->dma_buffer.bytes;
			substream->dma_max = max;
		} else {
			pr_err("prealloc smem error\n");
			memset(dmab, 0, sizeof(struct snd_dma_buffer));
			substream->dma_max = 0;
			substream->buffer_bytes_max = 0;
			goto ERR;
		}
	}

	return 0;
ERR:
	deepbuffer_pcm_lib_preallocate_free(pcm, stream_id);
	return -1;
}

/*
 * sprd_pcm_new()
 * Do not alloc dma buffer here, otherwise it will cause
 * kernel crash during recording. You can alloc dma buffer in
 * pcm_open(), and free them in pcm_close().
 */
static int sprd_pcm_new(struct snd_soc_component *component,
		struct snd_soc_pcm_runtime *rtd)
{
	int ret = 0;
	struct snd_card *card = rtd->card->snd_card;

	if (!card->dev->dma_mask) {
		ret = dma_set_mask(card->dev, 0ULL);
		if (ret)
			return ret;
	}
#ifdef CONFIG_ARM64
	card->dev->coherent_dma_mask = 0ULL;
#else
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(32);
#endif

	return ret;
}

static void sprd_pcm_free(struct snd_soc_component *component,
		struct snd_pcm *pcm)
{

}

static const struct snd_soc_component_driver sprd_soc_platform = {
	.name = SPRD_DEEPBUFFER_DRV_NAME,
	.probe = sprd_snd_platform_probe,
	.remove = sprd_snd_platform_remove,
	.pcm_construct = sprd_pcm_new,
	.pcm_destruct = sprd_pcm_free,

	/* pcm ops */
	.open = deepbuffer_pcm_open,
	.close = deepbuffer_pcm_close,
	.hw_params = deepbuffer_hw_params,
	.hw_free = deepbuffer_hw_free,
	.prepare = deepbuffer_pcm_prepare,
	.trigger = deepbuffer_pcm_trigger,
	.pointer = deepbuffer_pcm_pointer,
	.mmap = deepbuffer_mmap,
};

static int deepbuffer_init(struct platform_device *pdev,
			struct deepbuffer_t *deepbuffer)
{
	int ret, ch;
	u32 dst_id, mem_type;
	u32 stream_ch;
	const char *deepbuffer_dst_id = "sprd,deepbuffer-dst-id";
	const char *stream_channel = "sprd,stream_channel";
	const char *usedmem_type = "sprd,usedmem_type";
	struct device_node *np = pdev->dev.of_node;

	if (np == NULL) {
		pr_err("ERR: device_node is NULL!\n");
		return -1;
	}
	pr_info("%s: parse device node data in\n", __func__);

	ADEBUG();

	ret = of_property_read_u32(np, deepbuffer_dst_id, &dst_id);
	if (ret) {
		pr_warn("deepbuffer: missing %s in dt node\n", deepbuffer_dst_id);
		return ret;
	}

	ret = of_property_read_u32(np, stream_channel, &stream_ch);
	if (ret) {
		pr_warn("deepbuffer: missing %s in dt node\n", stream_channel);
		// stream_ch = 31;
		return ret;
	}

	ret = of_property_read_u32(np, usedmem_type, &mem_type);
	if (ret) {
		pr_warn("deepbuffer: missing %s in dt node\n", usedmem_type);
		return ret;
	}

	deepbuffer->dst = dst_id;
	for (ch = 0; ch < CHANNEL_NUM; ch++, stream_ch++)
		deepbuffer->channel[ch].id = stream_ch;

	deepbuffer->stream[0].dst = dst_id;
	deepbuffer->stream[1].dst = dst_id;
	deepbuffer->usedmem_type = mem_type;
	deepbuffer->channel_seek = seek_fe_channel;
	pr_info("dts data parse done!\n");
	mutex_init(&deepbuffer->deepbuffer_mutex);
	INIT_LIST_HEAD(&(deepbuffer->stream[0].stream_list));
	INIT_LIST_HEAD(&(deepbuffer->stream[1].stream_list));
	pr_info("%s: parse device node data out\n", __func__);
	return 0;
}

static int sprd_soc_platform_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct deepbuffer_t *deepbuffer;

	deepbuffer = devm_kzalloc(&pdev->dev, sizeof(struct deepbuffer_t), GFP_KERNEL);
	if (!deepbuffer)
		return -1;

	pr_info("%s: alloc memory success!\n", __func__);

	ret = deepbuffer_init(pdev, deepbuffer);
	if (ret) {
		pr_info("%s: parse dts data failed\n", __func__);
		return -1;
	}
	deepbuffer->dev = (struct device *)&pdev->dev;
	pr_info("%s: deepbuffer init success\n", __func__);
	platform_set_drvdata(pdev, deepbuffer);
	return snd_soc_register_component(&pdev->dev, &sprd_soc_platform, NULL, 0);
}

static int sprd_soc_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id sprd_deepbuffer_of_match[] = {
	{.compatible = "unisoc,sharkl5pro-deepbuffer-platform",},
	{.compatible = "unisoc,qogirl6-deepbuffer-platform",},
	{.compatible = "unisoc,qogirn6pro-deepbuffer-platform",},
	{.compatible = "unisoc,kongurk8-deepbuffer-platform",},
	{.compatible = "unisoc,qogirn5-deepbuffer-platform",},
	{.compatible = "unisoc,qogirl8-deepbuffer-platform",},
	{},
};

MODULE_DEVICE_TABLE(of, sprd_deepbuffer_of_match);
#endif

struct platform_driver sprd_deepbuffer_pcm_driver = {
	.driver = {
		.name = "sprd-deepbuffer",
		.owner = THIS_MODULE,
		.of_match_table = sprd_deepbuffer_of_match,
	},
	.probe = sprd_soc_platform_probe,
	.remove = sprd_soc_platform_remove,
};

module_platform_driver(sprd_deepbuffer_pcm_driver);

MODULE_DESCRIPTION("SPRD ASoC DEEPBUFFER driver");
MODULE_LICENSE("GPL");
