// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#if IS_ENABLED(CONFIG_MSM_QMP)
#include <linux/mailbox_client.h>
#include <linux/mailbox/qmp.h>
#endif
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <linux/soc/qcom/smem.h>
#include <soc/qcom/soc_sleep_stats.h>
#include <clocksource/arm_arch_timer.h>
#include <soc/qcom/boot_stats.h>
#if IS_ENABLED(CONFIG_SEC_PM_LOG)
#include <linux/sec_pm_log.h>
#endif
#ifdef CONFIG_SEC_FACTORY
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/time64.h>
#include "../../adsp_factory/adsp.h"
#endif
#if IS_ENABLED(CONFIG_SEC_PM)
#include <trace/events/power.h>

#define MAX_BUF_LEN		512
#define MSM_ARCH_TIMER_FREQ	19200000
#define GET_SEC(A)		((A) / (MSM_ARCH_TIMER_FREQ))
#define GET_MSEC(A)		(((A) / (MSM_ARCH_TIMER_FREQ / 1000)) % 1000)

static DEFINE_MUTEX(sleep_stats_mutex);
#endif /* CONFIG_SEC_PM */

#define STAT_TYPE_ADDR		0x0
#define COUNT_ADDR		0x4
#define LAST_ENTERED_AT_ADDR	0x8
#define LAST_EXITED_AT_ADDR	0x10
#define ACCUMULATED_ADDR	0x18
#define CLIENT_VOTES_ADDR	0x1c

#define DDR_STATS_MAGIC_KEY	0xA1157A75
#define DDR_STATS_MAX_NUM_MODES	0x14
#define MAX_MSG_LEN		40
#define DRV_ABSENT		0xdeaddead
#define DRV_INVALID		0xffffdead
#define VOTE_MASK		0x3fff
#define VOTE_X_SHIFT		14

#define DDR_STATS_MAGIC_KEY_ADDR	0x0
#define DDR_STATS_NUM_MODES_ADDR	0x4
#define DDR_STATS_NAME_ADDR		0x0
#define DDR_STATS_COUNT_ADDR		0x4
#define DDR_STATS_DURATION_ADDR		0x8

#if IS_ENABLED(CONFIG_DEBUG_FS) && IS_ENABLED(CONFIG_QCOM_SMEM)
struct subsystem_data {
	const char *name;
	u32 smem_item;
	u32 pid;
};

static struct subsystem_data subsystems[] = {
	{ "modem", 605, 1 },
	{ "wpss", 605, 13 },
	{ "adsp", 606, 2 },
	{ "adsp_island", 613, 2 },
	{ "cdsp", 607, 5 },
	{ "slpi", 608, 3 },
	{ "slpi_island", 613, 3 },
	{ "gpu", 609, 0 },
	{ "display", 610, 0 },
	{ "apss", 631, QCOM_SMEM_HOST_ANY },
};
#endif

struct stats_config {
	unsigned int offset_addr;
	unsigned int ddr_offset_addr;
	unsigned int num_records;
	bool appended_stats_avail;
};

struct stats_entry {
	uint32_t name;
	uint32_t count;
	uint64_t duration;
};

struct stats_prv_data {
	const struct stats_config *config;
	void __iomem *reg;
	u32 drv_max;
};

struct sleep_stats {
	u32 stat_type;
	u32 count;
	u64 last_entered_at;
	u64 last_exited_at;
	u64 accumulated;
};

struct appended_stats {
	u32 client_votes;
	u32 reserved[3];
};

#if IS_ENABLED(CONFIG_MSM_QMP)
struct ddr_stats_g_data {
	bool read_vote_info;
	void __iomem *ddr_reg;
	u32 freq_count;
	u32 entry_count;
	u32 drv_max;
	struct mutex ddr_stats_lock;
	struct mbox_chan *stats_mbox_ch;
	struct mbox_client stats_mbox_cl;
};

struct ddr_stats_g_data *ddr_gdata;
#if IS_ENABLED(CONFIG_SEC_PM)
#define MAX_SUBSYS_LEN	16
static int max_subsys_count;
static int subsys_names[MAX_SUBSYS_LEN];
#endif
#endif

static bool ddr_freq_update;

#define DSP_SLEEP_DEBUG_ON

#if defined(DSP_SLEEP_DEBUG_ON)
#include <linux/samsung/debug/sec_debug.h>
#include <linux/workqueue.h>
#include <linux/soc/qcom/cdsp-loader.h>

#define MAX_COUNT 7

#ifdef CONFIG_SEC_FACTORY
#define MAX_DSP_ENTRY 2
#else
#define MAX_DSP_ENTRY 1
#endif

struct _dsp_entry {
	char name[4];
	uint64_t entry_sec;
	uint64_t entry_msec;
	uint64_t prev_exit_sec;
	uint64_t prev_exit_msec;
	uint64_t error_count;
#ifdef CONFIG_SEC_FACTORY
	u32 prev_count;
	struct timespec64 sleep_enter_kts;
#endif
	int (*ssr)(void);
} DSP_ENTRY[MAX_DSP_ENTRY];	// 0 : CDSP, 1 : ADSP - adsp is disabled for the time being.

static DECLARE_WORK(dsp_ssr, cdsp_restart);
#endif

#ifdef CONFIG_MSM_BOOT_TIME_MARKER
static struct stats_prv_data *gdata;
static u64 deep_sleep_last_exited_time;

uint64_t get_aosd_sleep_exit_time(void)
{
	int i;
	uint32_t offset;
	u64 last_exited_at;
	u32 count;
	static u32 saved_deep_sleep_count;
	u32 s_type = 0;
	char stat_type[5] = {0};
	struct stats_prv_data *drv = gdata;
	void __iomem *reg;

	for (i = 0; i < drv->config->num_records; i++) {
		offset = STAT_TYPE_ADDR + (i * sizeof(struct sleep_stats));

		if (drv[i].config->appended_stats_avail)
			offset += i * sizeof(struct appended_stats);

		reg = drv[i].reg + offset;

		s_type = readl_relaxed(reg);
		memcpy(stat_type, &s_type, sizeof(u32));
		strim(stat_type);

		if (!memcmp((const void *)stat_type, (const void *)"aosd", 4)) {
			count = readl_relaxed(reg + COUNT_ADDR);

			if (saved_deep_sleep_count == count)
				deep_sleep_last_exited_time = 0;
			else {
				saved_deep_sleep_count = count;
				last_exited_at = readq_relaxed(reg + LAST_EXITED_AT_ADDR);
				deep_sleep_last_exited_time = last_exited_at;
			}
			break;

		}
	}

	return deep_sleep_last_exited_time;
}
EXPORT_SYMBOL(get_aosd_sleep_exit_time);
#endif

static void print_sleep_stats(struct seq_file *s, struct sleep_stats *stat)
{
	u64 accumulated = stat->accumulated;
	/*
	 * If a subsystem is in sleep when reading the sleep stats adjust
	 * the accumulated sleep duration to show actual sleep time.
	 */
	if (stat->last_entered_at > stat->last_exited_at)
		accumulated += arch_timer_read_counter()
			       - stat->last_entered_at;

	seq_printf(s, "Count = %u\n", stat->count);
	seq_printf(s, "Last Entered At = %llu\n", stat->last_entered_at);
	seq_printf(s, "Last Exited At = %llu\n", stat->last_exited_at);
	seq_printf(s, "Accumulated Duration = %llu\n", accumulated);
}

static int subsystem_sleep_stats_show(struct seq_file *s, void *d)
{
#if IS_ENABLED(CONFIG_DEBUG_FS) && IS_ENABLED(CONFIG_QCOM_SMEM)
	struct subsystem_data *subsystem = s->private;
	struct sleep_stats *stat;

	stat = qcom_smem_get(subsystem->pid, subsystem->smem_item, NULL);
	if (IS_ERR(stat))
		return PTR_ERR(stat);

	print_sleep_stats(s, stat);

#endif
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(subsystem_sleep_stats);

static int soc_sleep_stats_show(struct seq_file *s, void *d)
{
	struct stats_prv_data *prv_data = s->private;
	void __iomem *reg = prv_data->reg;
	struct sleep_stats stat;

	memcpy_fromio(&stat, reg, sizeof(struct sleep_stats));

	print_sleep_stats(s, &stat);

	if (prv_data->config->appended_stats_avail) {
		struct appended_stats app_stat;

		app_stat.client_votes = readl_relaxed(reg + CLIENT_VOTES_ADDR);
		seq_printf(s, "Client_votes = %#x\n", app_stat.client_votes);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(soc_sleep_stats);

static void  print_ddr_stats(struct seq_file *s, int *count,
			     struct stats_entry *data, u64 accumulated_duration)
{

	u32 cp_idx = 0;
	u32 name;
	u64 duration = 0;

	if (accumulated_duration) {
		duration = data->duration * 100;
		do_div(duration, accumulated_duration);
	}

	name = (data->name >> 8) & 0xFF;
	if (name == 0x0) {
		name = (data->name) & 0xFF;
		*count = *count + 1;
		seq_printf(s,
		"LPM %d:\tName:0x%x\tcount:%u\tDuration (ticks):%ld (~%d%%)\n",
			*count, name, data->count, data->duration, duration);
	} else if (name == 0x1) {
		cp_idx = data->name & 0x1F;
		name = data->name >> 16;

		if (!name || !data->count)
			return;

		seq_printf(s,
		"Freq %dMhz:\tCP IDX:%u\tDuration (ticks):%ld (~%d%%)\n",
			name, cp_idx, data->duration, duration);
	}
}

static bool ddr_stats_is_freq_overtime(struct stats_entry *data)
{
	if ((data->count == 0) && (ddr_freq_update))
		return true;

	return false;
}

static void ddr_stats_fill_data(void __iomem *reg, u32 entry_count,
					struct stats_entry *data, u64 *accumulated_duration)
{
	int i;

	for (i = 0; i < entry_count; i++) {
		memcpy_fromio(&data[i], reg, sizeof(*data));
		*accumulated_duration += data[i].duration;
		reg += sizeof(struct stats_entry);
	}
}

static int ddr_stats_show(struct seq_file *s, void *d)
{
	struct stats_entry data[DDR_STATS_MAX_NUM_MODES];
	void __iomem *reg = s->private;
	u32 entry_count;
	u64 accumulated_duration = 0;
	int i, lpm_count = 0;

	entry_count = readl_relaxed(reg + DDR_STATS_NUM_MODES_ADDR);
	if (entry_count > DDR_STATS_MAX_NUM_MODES) {
		pr_err("Invalid entry count\n");
		return 0;
	}

	reg += DDR_STATS_NUM_MODES_ADDR + 0x4;
	ddr_stats_fill_data(reg, DDR_STATS_NUM_MODES_ADDR, data, &accumulated_duration);
	for (i = 0; i < DDR_STATS_NUM_MODES_ADDR; i++)
		print_ddr_stats(s, &lpm_count, &data[i], accumulated_duration);

	accumulated_duration = 0;
	reg += sizeof(struct stats_entry) * 0x4;
	for (i = DDR_STATS_NUM_MODES_ADDR; i < entry_count; i++) {
		memcpy_fromio(&data[i], reg, sizeof(*data));
		if (ddr_stats_is_freq_overtime(&data[i])) {
			seq_puts(s, "ddr_stats: Freq update failed.\n");
			return 0;
		}
		accumulated_duration += data[i].duration;
		reg += sizeof(struct stats_entry);
	}

	for (i = DDR_STATS_NUM_MODES_ADDR; i < entry_count; i++)
		print_ddr_stats(s, &lpm_count, &data[i], accumulated_duration);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ddr_stats);

#if IS_ENABLED(CONFIG_MSM_QMP)
static ktime_t send_msg_time;

int ddr_stats_freq_sync_send_msg(void)
{
	char buf[MAX_MSG_LEN] = {};
	struct qmp_pkt pkt;
	int ret = 0;

	mutex_lock(&ddr_gdata->ddr_stats_lock);
	ret = scnprintf(buf, MAX_MSG_LEN, "{class: ddr, action: freqsync}");
	pkt.size = (ret + 0x3) & ~0x3;
	pkt.data = buf;

	ret = mbox_send_message(ddr_gdata->stats_mbox_ch, &pkt);
	if (ret < 0) {
		pr_err("Error sending mbox message: %d\n", ret);
		mutex_unlock(&ddr_gdata->ddr_stats_lock);
		return ret;
	}
	mutex_unlock(&ddr_gdata->ddr_stats_lock);

	send_msg_time = ktime_get_boottime();

	return ret;
}
EXPORT_SYMBOL(ddr_stats_freq_sync_send_msg);

int ddr_stats_get_freq_count(void)
{
	if (!ddr_gdata)
		return -ENODEV;

	return ddr_gdata->freq_count;
}
EXPORT_SYMBOL(ddr_stats_get_freq_count);

int ddr_stats_get_residency(int freq_count, struct ddr_freq_residency *data)
{
	void __iomem *reg;
	u32 name;
	int i, j, num;
	uint64_t duration = 0;
	ktime_t now;
	struct stats_entry stats_data[DDR_STATS_MAX_NUM_MODES];

	if (freq_count < 0 || !data || !ddr_gdata || !ddr_gdata->ddr_reg)
		return -EINVAL;

	if (!ddr_gdata->entry_count)
		return -EINVAL;

	now = ktime_get_boottime();
	while (now < send_msg_time) {
		udelay(500);
		now = ktime_get_boottime();
	}

	mutex_lock(&ddr_gdata->ddr_stats_lock);
	num = freq_count > ddr_gdata->freq_count ? ddr_gdata->freq_count : freq_count;
	reg = ddr_gdata->ddr_reg + DDR_STATS_NUM_MODES_ADDR + 0x4;

	ddr_stats_fill_data(reg, ddr_gdata->entry_count, stats_data, &duration);

	/* Before get ddr residency, check ddr freq's count. */
	for (i = 0; i < ddr_gdata->entry_count; i++) {
		name = stats_data[i].name;
		if ((((name >> 8) & 0xFF) == 0x1) &&
				ddr_stats_is_freq_overtime(&stats_data[i])) {
			pr_err("ddr_stats: Freq update failed\n");
			mutex_unlock(&ddr_gdata->ddr_stats_lock);
			return -EINVAL;
		}
	}

	for (i = 0, j = 0; i < ddr_gdata->entry_count; i++) {
		name = stats_data[i].name;
		if (((name >> 8) & 0xFF) == 0x1) {
			data[j].freq = name >> 16;
			data[j].residency = stats_data[i].duration;
			if (++j > num)
				break;
		}
		reg += sizeof(struct stats_entry);
	}

	mutex_unlock(&ddr_gdata->ddr_stats_lock);

	return j;
}
EXPORT_SYMBOL(ddr_stats_get_residency);

int ddr_stats_get_ss_count(void)
{
	return ddr_gdata->read_vote_info ? ddr_gdata->drv_max : -EOPNOTSUPP;
}
EXPORT_SYMBOL(ddr_stats_get_ss_count);

int ddr_stats_get_ss_vote_info(int ss_count,
				struct ddr_stats_ss_vote_info *vote_info)
{
	char buf[MAX_MSG_LEN] = {};
	struct qmp_pkt pkt;
	void __iomem *reg;
	u32 vote_offset, *val;
	int ret, i;

	if (!vote_info || !ddr_gdata || (ddr_gdata->drv_max == -EINVAL) ||
			!(ss_count == ddr_gdata->drv_max))
		return -ENODEV;

	if (!ddr_gdata->read_vote_info)
		return -EOPNOTSUPP;

	val = kcalloc(ddr_gdata->drv_max, sizeof(u32), GFP_KERNEL);
	if (!val)
		return -ENOMEM;

	mutex_lock(&ddr_gdata->ddr_stats_lock);
	ret = scnprintf(buf, MAX_MSG_LEN, "{class: ddr, res: drvs_ddr_votes}");
	pkt.size = (ret + 0x3) & ~0x3;
	pkt.data = buf;

	ret = mbox_send_message(ddr_gdata->stats_mbox_ch, &pkt);
	if (ret < 0) {
		pr_err("Error sending mbox message: %d\n", ret);
		mutex_unlock(&ddr_gdata->ddr_stats_lock);
		kfree(val);
		return ret;
	}

	vote_offset = sizeof(u32) + sizeof(u32) +
			(ddr_gdata->entry_count * sizeof(struct stats_entry));
	reg = ddr_gdata->ddr_reg;

	for (i = 0; i < ss_count; i++, reg += sizeof(u32)) {
		val[i] = readl_relaxed(reg + vote_offset);
		if (val[i] == DRV_ABSENT) {
			vote_info[i].ab = DRV_ABSENT;
			vote_info[i].ib = DRV_ABSENT;
			continue;
		} else if (val[i] == DRV_INVALID) {
			vote_info[i].ab = DRV_INVALID;
			vote_info[i].ib = DRV_INVALID;
			continue;
		}

		vote_info[i].ab = (val[i] >> VOTE_X_SHIFT) & VOTE_MASK;
		vote_info[i].ib = val[i] & VOTE_MASK;
	}

	mutex_unlock(&ddr_gdata->ddr_stats_lock);

	kfree(val);
	return 0;

}
EXPORT_SYMBOL(ddr_stats_get_ss_vote_info);
#endif

#if IS_ENABLED(CONFIG_DEBUG_FS)
static struct dentry *create_debugfs_entries(void __iomem *reg,
					     void __iomem *ddr_reg,
					     struct stats_prv_data *prv_data,
					     struct device_node *node)
{
	struct dentry *root;
	char stat_type[sizeof(u32) + 1] = {0};
	u32 offset, type, key;
	int i;
#if IS_ENABLED(CONFIG_QCOM_SMEM)
	const char *name;
	int j, n_subsystems;
#endif

	root = debugfs_create_dir("qcom_sleep_stats", NULL);

	for (i = 0; i < prv_data[0].config->num_records; i++) {
		offset = STAT_TYPE_ADDR + (i * sizeof(struct sleep_stats));

		if (prv_data[0].config->appended_stats_avail)
			offset += i * sizeof(struct appended_stats);

		prv_data[i].reg = reg + offset;

		type = readl_relaxed(prv_data[i].reg);
		memcpy(stat_type, &type, sizeof(u32));
		strim(stat_type);

		debugfs_create_file(stat_type, 0444, root,
				    &prv_data[i],
				    &soc_sleep_stats_fops);
	}

#if IS_ENABLED(CONFIG_QCOM_SMEM)
	n_subsystems = of_property_count_strings(node, "ss-name");
	if (n_subsystems < 0)
		goto exit;

	for (i = 0; i < n_subsystems; i++) {
		of_property_read_string_index(node, "ss-name", i, &name);

		for (j = 0; j < ARRAY_SIZE(subsystems); j++) {
			if (!strcmp(subsystems[j].name, name)) {
				debugfs_create_file(subsystems[j].name, 0444,
						    root, &subsystems[j],
						    &subsystem_sleep_stats_fops);
#if IS_ENABLED(CONFIG_SEC_PM)
				subsys_names[max_subsys_count++] = j;
#endif
				break;
			}
		}
	}
#endif
	if (!ddr_reg)
		goto exit;

	key = readl_relaxed(ddr_reg + DDR_STATS_MAGIC_KEY_ADDR);
	if (key == DDR_STATS_MAGIC_KEY)
		debugfs_create_file("ddr_stats", 0444,
				     root, ddr_reg, &ddr_stats_fops);

exit:
	return root;
}
#endif

#if IS_ENABLED(CONFIG_SEC_PM)
u64 last_accumulated[5];
static void __iomem *global_reg_base;
static struct stats_prv_data *global_prv_data;
struct device_node *global_node;

static void sec_sleep_stats_show(const char *annotation)
{
	char buf[MAX_BUF_LEN];
	char *buf_ptr = buf;
	unsigned int duration_sec, duration_msec;

#if IS_ENABLED(CONFIG_SEC_PM_LOG)
	char pm_log_buf[MAX_BUF_LEN];
	char *pm_log_buf_ptr = pm_log_buf;
#endif

#if defined(DSP_SLEEP_DEBUG_ON)
	struct _dsp_entry *dsp_entry = NULL;
	int is_debug_low = 0;
	unsigned int debug_level = 0;
#endif

	char stat_type[sizeof(u32) + 1] = {0};
	u32 offset, type;
	int i, n_subsystems;
	bool is_exit = (!strcmp("exit", annotation)) ? true : false;

	mutex_lock(&sleep_stats_mutex);

	/* sleep_stats */
	buf_ptr += sprintf(buf_ptr, "PM: %s: ", annotation);
#if IS_ENABLED(CONFIG_SEC_PM_LOG)
	pm_log_buf_ptr += sprintf(pm_log_buf_ptr, "soc: %s: ", (is_exit ? "ex" : "en"));
#endif
	for (i = 0; i < global_prv_data[0].config->num_records; i++) {
		struct sleep_stats stat;
		u64 accumulated;

		offset = STAT_TYPE_ADDR + (i * sizeof(struct sleep_stats));

		if (global_prv_data[0].config->appended_stats_avail)
			offset += i * sizeof(struct appended_stats);

		global_prv_data[i].reg = global_reg_base + offset;

		type = readl_relaxed(global_prv_data[i].reg);
		memcpy(stat_type, &type, sizeof(u32));
		strim(stat_type);	// stat_type: sleep_stats name

		stat.count = readl_relaxed(global_prv_data[i].reg + COUNT_ADDR);
		stat.last_entered_at = readq(global_prv_data[i].reg + LAST_ENTERED_AT_ADDR);
		stat.last_exited_at = readq(global_prv_data[i].reg + LAST_EXITED_AT_ADDR);
		stat.accumulated = readq(global_prv_data[i].reg + ACCUMULATED_ADDR);
		accumulated = stat.accumulated;

		if (stat.last_entered_at > stat.last_exited_at)
			accumulated += arch_timer_read_counter()
					- stat.last_entered_at;

		if (is_exit && accumulated == last_accumulated[i])
			buf_ptr += sprintf(buf_ptr, "*");
		last_accumulated[i] = accumulated;

		duration_sec = GET_SEC(accumulated);
		duration_msec = GET_MSEC(accumulated);

		buf_ptr += sprintf(buf_ptr, "%s(%d, %u.%u), ",
				stat_type,
				stat.count,
				duration_sec, duration_msec);

#if IS_ENABLED(CONFIG_SEC_PM_LOG)
		pm_log_buf_ptr += sprintf(pm_log_buf_ptr, "(%d, %u.%u)",
					stat.count, duration_sec, (duration_msec / 100));
#endif
	}
	buf_ptr += sprintf(buf_ptr, "\n");

	/* subsystem stats */
	buf_ptr += sprintf(buf_ptr, "PM: %s: ", annotation);
#if IS_ENABLED(CONFIG_SEC_PM_LOG)
	pm_log_buf_ptr += sprintf(pm_log_buf_ptr, "/");
#endif
	n_subsystems = of_property_count_strings(global_node, "ss-name");
	if (n_subsystems < 0) {
		pr_err("%s: n_subsystems is under 0, ret=%d\n", __func__, n_subsystems);
		mutex_unlock(&sleep_stats_mutex);
		return;
	}

#if IS_ENABLED(CONFIG_DEBUG_FS) && IS_ENABLED(CONFIG_QCOM_SMEM)
	for (i = 0; i < max_subsys_count; i++) {
		struct subsystem_data *subsystem;
		struct sleep_stats *stat;
		u64 accumulated;
		int idx = subsys_names[i];

		subsystem = &subsystems[idx];
		stat = qcom_smem_get(subsystem->pid, subsystem->smem_item, NULL);
		if (IS_ERR(stat)) {
			pr_err("%s: Failed to get qcom_smem for %s, ret=%d\n", __func__,
				   subsystems[idx].name, PTR_ERR(stat));
			continue;
		}

		accumulated = stat->accumulated;

		if (stat->last_entered_at > stat->last_exited_at)
			accumulated += arch_timer_read_counter()
				- stat->last_entered_at;

		duration_sec = GET_SEC(accumulated);
		duration_msec = GET_MSEC(accumulated);

#if defined(DSP_SLEEP_DEBUG_ON)
#if 0
		dsp_entry = (!strcmp(subsystem->name, "cdsp")) ? &DSP_ENTRY[0] :
					(!strcmp(subsystem->name, "slpi") ? &DSP_ENTRY[1] : NULL);
#else
		dsp_entry = (!strcmp(subsystem->name, "cdsp")) ? &DSP_ENTRY[0] : NULL;
#endif
		if (dsp_entry != NULL) {
			if (!is_exit) {
				// entry
				dsp_entry->entry_sec = duration_sec;
				dsp_entry->entry_msec = duration_msec;
			} else {
				//exit
				/* Error detected if exit duration is same as entry */
				if((duration_sec == dsp_entry->entry_sec &&
							duration_msec == dsp_entry->entry_msec) &&
						(duration_sec == dsp_entry->prev_exit_sec &&
						 duration_msec == dsp_entry->prev_exit_msec)) {
					dsp_entry->error_count++;
					printk("entry error cnt : %d\n", dsp_entry->error_count);
				} else {
					dsp_entry->error_count = 0;
				}
				dsp_entry->prev_exit_sec = duration_sec;
				dsp_entry->prev_exit_msec = duration_msec;
			}
		}
#ifdef CONFIG_SEC_FACTORY
		dsp_entry = (!strcmp(subsystem->name, "adsp")) ? &DSP_ENTRY[1] : NULL;
		if (dsp_entry != NULL && !sns_check_ignore_crash()) {
			if (!is_exit) {
				// entry
				dsp_entry->sleep_enter_kts =
					ktime_to_timespec64(ktime_get_boottime());
				dsp_entry->prev_count = stat->count;
			} else {
				//exit
				struct timespec64 curr_kts =
					ktime_to_timespec64(ktime_get_boottime());
				time64_t diff_kts =
					curr_kts.tv_sec - dsp_entry->sleep_enter_kts.tv_sec;
				if (diff_kts > 10) {  // sleep more than 10s
					u32 diff_count = stat->count - dsp_entry->prev_count;
					int64_t wakeup_rate = 0;
					// more than about 200 wakeups in a sec.
					wakeup_rate = diff_count / diff_kts;
					if (wakeup_rate > 200) {
						dsp_entry->error_count = MAX_COUNT + 1;
						pr_err("%s frequent wakeup, %u, %u\n",
							dsp_entry->name,
							(u32)diff_kts,
							(u32)wakeup_rate);
					}
				}
			}
		}
#endif
#endif
		buf_ptr += sprintf(buf_ptr, "%s(%d, %u.%u), ",
						   subsystem->name,
						   stat->count,
						   duration_sec, duration_msec);
#if IS_ENABLED(CONFIG_SEC_PM_LOG)
		pm_log_buf_ptr += sprintf(pm_log_buf_ptr, "(%d, %u.%u)",
					stat->count, duration_sec, (duration_msec / 100));
#endif
	}
#endif	

	buf_ptr--;
	buf_ptr--;
	buf_ptr += sprintf(buf_ptr, "\n");

#if IS_ENABLED(CONFIG_SEC_PM_LOG)
	ss_power_print("%s\n", pm_log_buf);
#endif
	mutex_unlock(&sleep_stats_mutex);

	pr_info("%s", buf);

#if defined(DSP_SLEEP_DEBUG_ON)
	// 0 : CDSP, 1 : ADSP
	for (i = 0; i < sizeof(DSP_ENTRY) / sizeof(struct _dsp_entry); i++) {
		dsp_entry = &DSP_ENTRY[i];
		if(dsp_entry->error_count > MAX_COUNT) {
			debug_level = sec_debug_level();

			switch (debug_level) {
				case SEC_DEBUG_LEVEL_LOW:
					is_debug_low = 1;
					break;
				case SEC_DEBUG_LEVEL_MID:
					is_debug_low = 0;
					break;
			}

			if (!is_debug_low) {
				pr_err("entry error cnt : %d\n", dsp_entry->error_count);
				pr_err("Intentional crash for %s\n", dsp_entry->name);
				BUG_ON(1);
			} else {
				dsp_entry->error_count = 0;
				pr_err("reset entry error cnt : %d\n", dsp_entry->error_count);
				pr_err("Intentional cdsp subsystem restart\n");
				schedule_work(&dsp_ssr);
			}
		}
	}
#endif
}

static void soc_sleep_stats_debug_suspend_trace_probe(void *unused,
					const char *action, int val, bool start)
{
	/*
		SUSPEND
		start(1), val(1), action(machine_suspend)
	*/
	if (start && val > 0 && !strcmp("machine_suspend", action))
		sec_sleep_stats_show("entry");

	/*
		RESUME
		start(0), val(1), action(machine_suspend)
	*/
	if (!start && val > 0 && !strcmp("machine_suspend", action))
		sec_sleep_stats_show("exit");
}
#endif

static int soc_sleep_stats_probe(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *reg_base, *ddr_reg = NULL;
	void __iomem *offset_addr;
	phys_addr_t stats_base;
	resource_size_t stats_size;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry *root;
#endif
	const struct stats_config *config;
	struct stats_prv_data *prv_data;
	int i, ret;
#if IS_ENABLED(CONFIG_MSM_QMP)
	u32 name;
	void __iomem *reg;
#endif

#if IS_ENABLED(CONFIG_SEC_PM)
	/* Register callback for cheking subsystem stats */
	ret = register_trace_suspend_resume(
		soc_sleep_stats_debug_suspend_trace_probe, NULL);
	if (ret) {
		pr_err("%s: Failed to register suspend trace callback, ret=%d\n",
			__func__, ret);
	}
#endif

	config = device_get_match_data(&pdev->dev);
	if (!config)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return PTR_ERR(res);

	offset_addr = ioremap(res->start + config->offset_addr, sizeof(u32));
	if (IS_ERR(offset_addr))
		return PTR_ERR(offset_addr);

	stats_base = res->start | readl_relaxed(offset_addr);
	stats_size = resource_size(res);
	iounmap(offset_addr);

	reg_base = devm_ioremap(&pdev->dev, stats_base, stats_size);
	if (!reg_base)
		return -ENOMEM;

	prv_data = devm_kzalloc(&pdev->dev, config->num_records *
				sizeof(struct stats_prv_data), GFP_KERNEL);
	if (!prv_data)
		return -ENOMEM;

	for (i = 0; i < config->num_records; i++)
		prv_data[i].config = config;

	if (!config->ddr_offset_addr)
		goto skip_ddr_stats;

	offset_addr = ioremap(res->start + config->ddr_offset_addr,
								sizeof(u32));
	if (IS_ERR(offset_addr))
		return PTR_ERR(offset_addr);

	stats_base = res->start | readl_relaxed(offset_addr);
	iounmap(offset_addr);

	ddr_reg = devm_ioremap(&pdev->dev, stats_base, stats_size);
	if (!ddr_reg)
		return -ENOMEM;

	ret = of_property_read_u32(pdev->dev.of_node, "qcom,drv-max", &prv_data->drv_max);
	if (ret < 0)
		prv_data->drv_max = -EINVAL;

#if IS_ENABLED(CONFIG_MSM_QMP)
	ddr_gdata = devm_kzalloc(&pdev->dev, sizeof(*ddr_gdata), GFP_KERNEL);
	if (!ddr_gdata)
		return -ENOMEM;

	ddr_gdata->read_vote_info = false;
	ddr_gdata->ddr_reg = ddr_reg;

	mutex_init(&ddr_gdata->ddr_stats_lock);

	ddr_gdata->entry_count = readl_relaxed(ddr_gdata->ddr_reg + DDR_STATS_NUM_MODES_ADDR);
	if (ddr_gdata->entry_count > DDR_STATS_MAX_NUM_MODES) {
		pr_err("Invalid entry count\n");
		goto skip_ddr_stats;
	}

	reg = ddr_gdata->ddr_reg + DDR_STATS_NUM_MODES_ADDR + 0x4;

	for (i = 0; i < ddr_gdata->entry_count; i++) {
		name = readl_relaxed(reg + DDR_STATS_NAME_ADDR);
		name = (name >> 8) & 0xFF;
		if (name == 0x1)
			ddr_gdata->freq_count++;

		reg += sizeof(struct stats_entry);
	}

	ddr_gdata->stats_mbox_cl.dev = &pdev->dev;
	ddr_gdata->stats_mbox_cl.tx_block = true;
	ddr_gdata->stats_mbox_cl.tx_tout = 1000;
	ddr_gdata->stats_mbox_cl.knows_txdone = false;

	ddr_gdata->stats_mbox_ch = mbox_request_channel(&ddr_gdata->stats_mbox_cl, 0);
	if (IS_ERR(ddr_gdata->stats_mbox_ch))
		goto skip_ddr_stats;

	ddr_gdata->drv_max = prv_data->drv_max;
	ddr_gdata->read_vote_info = true;
#endif

	ddr_freq_update = of_property_read_bool(pdev->dev.of_node,
							"ddr-freq-update");

skip_ddr_stats:
#if IS_ENABLED(CONFIG_DEBUG_FS)
	root = create_debugfs_entries(reg_base, ddr_reg, prv_data,
				      pdev->dev.of_node);
	platform_set_drvdata(pdev, root);
#endif

#ifdef CONFIG_MSM_BOOT_TIME_MARKER
	gdata = prv_data;
#endif

#if IS_ENABLED(CONFIG_SEC_PM)
	global_reg_base = reg_base;
	global_prv_data = prv_data;
	global_node = pdev->dev.of_node;
#endif

#if defined(DSP_SLEEP_DEBUG_ON)
	strncpy(DSP_ENTRY[0].name, "cdsp", 4);
#if 0
	strncpy(DSP_ENTRY[1].name, "adsp", 4);
#endif
#endif

	return 0;
}

static int soc_sleep_stats_remove(struct platform_device *pdev)
{
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry *root = platform_get_drvdata(pdev);

	debugfs_remove_recursive(root);
#endif

#if IS_ENABLED(CONFIG_SEC_PM)
	unregister_trace_suspend_resume(
		soc_sleep_stats_debug_suspend_trace_probe, NULL);
#endif
	return 0;
}

static const struct stats_config rpm_data = {
	.offset_addr = 0x14,
	.num_records = 2,
	.appended_stats_avail = true,
};

static const struct stats_config rpmh_legacy_data = {
	.offset_addr = 0x4,
	.num_records = 3,
	.appended_stats_avail = false,
};

static const struct stats_config rpmh_data = {
	.offset_addr = 0x4,
	.ddr_offset_addr = 0x1c,
	.num_records = 3,
	.appended_stats_avail = false,
};

static const struct of_device_id soc_sleep_stats_table[] = {
	{ .compatible = "qcom,rpm-sleep-stats", .data = &rpm_data },
	{ .compatible = "qcom,rpmh-sleep-stats-legacy", .data = &rpmh_legacy_data },
	{ .compatible = "qcom,rpmh-sleep-stats", .data = &rpmh_data },
	{ }
};

static struct platform_driver soc_sleep_stats_driver = {
	.probe = soc_sleep_stats_probe,
	.remove = soc_sleep_stats_remove,
	.driver = {
		.name = "soc_sleep_stats",
		.of_match_table = soc_sleep_stats_table,
	},
};
module_platform_driver(soc_sleep_stats_driver);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. (QTI) SoC Sleep Stats driver");
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: smem  cdsp-loader");
