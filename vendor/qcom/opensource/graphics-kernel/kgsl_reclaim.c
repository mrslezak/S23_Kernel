// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kthread.h>
#include <linux/notifier.h>
#include <linux/shmem_fs.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/sched/clock.h>
#include <linux/sched/cputime.h>

#include "kgsl_reclaim.h"
#include "kgsl_sharedmem.h"
#include "kgsl_trace.h"

/*
 * Reclaiming excessive number of pages from a process will impact launch
 * latency for the subsequent launch of the process. After measuring the
 * launch latencies by having various maximum limits, it has been decided
 * that allowing 30MB (7680 pages) of relcaim per process will have little
 * impact and the latency will be within acceptable limit.
 */
static u32 kgsl_reclaim_max_page_limit = 7680;

/* Setting this to 0 means we reclaim pages as specified in shrinker call */
static u32 kgsl_nr_to_scan;

struct work_struct reclaim_work;

static atomic_t kgsl_nr_to_reclaim;

struct swap_stat {
	pid_t pid;
	int nr_pages;
	u64 nsec_start;
	u64 nsec_end;
	u64 nsec_cputime;	// use the highest bit for in or out
};

#define SWAP_STAT_HISTORY_SIZE (128)
static struct swap_stat swap_stat_hist[SWAP_STAT_HISTORY_SIZE];
static unsigned int swap_stat_new_idx;
struct mutex swap_stat_lock;

static void *swap_hist_buf;
static bool swap_hist_enabled;

#define SWAP_HIST_BUF_SIZE	(16 << 10)	/* 16 KB */

static int graphics_swap_proc_init(void)
{
	swap_hist_buf = vmalloc(SWAP_HIST_BUF_SIZE);
	if (!swap_hist_buf)
		return -ENOMEM;

	mutex_init(&swap_stat_lock);
	swap_hist_enabled = true;
	return 0;
}

static void swap_stat_save(struct swap_stat *swap_stat)
{
	unsigned int stat_idx;

	if (!swap_hist_enabled)
		return;

	mutex_lock(&swap_stat_lock);
	stat_idx = swap_stat_new_idx;
	swap_stat_new_idx = (swap_stat_new_idx + 1) % SWAP_STAT_HISTORY_SIZE;
	mutex_unlock(&swap_stat_lock);

	swap_stat_hist[stat_idx] = *swap_stat;
}

#define SWAP_STAT_SWAPIN_BIT	63
#define SWAP_STAT_SWAPIN	(1UL << SWAP_STAT_SWAPIN_BIT)

static int graphics_swap_proc_show(struct seq_file *m, void *v)
{
	unsigned int stat_idx;
	struct swap_stat *swap_stat;

	int cnt;
	u64 wtime_s, wtime_e, wtime_d, cputime_in, cputime;
	u64 wtime_first = 0;
	unsigned long wtime_s_rem, wtime_e_rem;
	int nr_pages;
	pid_t pid;
	bool is_swapin;

	stat_idx = swap_stat_new_idx;

	for (cnt = 1; cnt <= SWAP_STAT_HISTORY_SIZE; cnt++) {
		if (!stat_idx)
			stat_idx = SWAP_STAT_HISTORY_SIZE - 1;
		else
			stat_idx--;
		swap_stat = &swap_stat_hist[stat_idx];

		wtime_s = swap_stat->nsec_start;
		if (!wtime_first)
			wtime_first = wtime_s;
		if (!wtime_s) /* no data in it */
			break;
		if (wtime_first < wtime_s) /* newer data after read */
			break;
		wtime_e = swap_stat->nsec_end;
		wtime_d = wtime_e - wtime_s;
		wtime_s_rem = do_div(wtime_s, 1000000000);
		wtime_e_rem = do_div(wtime_e, 1000000000);

		cputime_in = swap_stat->nsec_cputime;
		cputime = cputime_in & ~SWAP_STAT_SWAPIN;
		pid = swap_stat->pid;
		is_swapin = !!(cputime_in & SWAP_STAT_SWAPIN);
		nr_pages = swap_stat->nr_pages;

		seq_printf(m, "[%03d] [%5lu.%06lu] [%5lu.%06lu] %6u %s %6lu %6lu %6d\n",
			  cnt,
			  (unsigned long)wtime_s, wtime_s_rem / 1000,
			  (unsigned long)wtime_e, wtime_e_rem / 1000,
			  pid, is_swapin ? "IN " : "OUT",
			  (unsigned long)wtime_d / 1000000,
			  cputime / NSEC_PER_MSEC,
			  nr_pages << (PAGE_SHIFT - 10));
	}

	return 0;
}

static int kgsl_memdesc_get_reclaimed_pages(struct kgsl_mem_entry *entry)
{
	struct kgsl_memdesc *memdesc = &entry->memdesc;
	int i, ret;
	struct page *page;

	for (i = 0; i < memdesc->page_count; i++) {
		if (memdesc->pages[i])
			continue;

		page = shmem_read_mapping_page_gfp(
			memdesc->shmem_filp->f_mapping, i, kgsl_gfp_mask(0));

		if (IS_ERR(page))
			return PTR_ERR(page);

		kgsl_page_sync_for_device(memdesc->dev, page, PAGE_SIZE);

		/*
		 * Update the pages array only if vmfault has not
		 * updated it meanwhile
		 */
		spin_lock(&memdesc->lock);
		if (!memdesc->pages[i]) {
			memdesc->pages[i] = page;
			atomic_dec(&entry->priv->unpinned_page_count);
		} else
			put_page(page);
		spin_unlock(&memdesc->lock);
	}

	ret = kgsl_mmu_map(memdesc->pagetable, memdesc);
	if (ret)
		return ret;

	trace_kgsl_reclaim_memdesc(entry, false);

	memdesc->priv &= ~KGSL_MEMDESC_RECLAIMED;
	memdesc->priv &= ~KGSL_MEMDESC_SKIP_RECLAIM;

	return 0;
}

int kgsl_reclaim_to_pinned_state(
		struct kgsl_process_private *process)
{
	struct kgsl_mem_entry *entry, *valid_entry;
	int next = 0, ret = 0, count = 0;
	unsigned long jiffies_s = jiffies;
	u64 utime, stime_s, stime_e, stime_d;
	pid_t pid;
	struct swap_stat swap_stat;

	mutex_lock(&process->reclaim_lock);

	if (test_bit(KGSL_PROC_PINNED_STATE, &process->state))
		goto done;

	swap_stat.nsec_start = local_clock();
	task_cputime(current, &utime, &stime_s);

	count = atomic_read(&process->unpinned_page_count);

	for ( ; ; ) {
		valid_entry = NULL;
		spin_lock(&process->mem_lock);
		entry = idr_get_next(&process->mem_idr, &next);
		if (entry == NULL) {
			spin_unlock(&process->mem_lock);
			break;
		}

		if (entry->memdesc.priv & KGSL_MEMDESC_RECLAIMED)
			valid_entry = kgsl_mem_entry_get(entry);
		spin_unlock(&process->mem_lock);

		if (valid_entry) {
			ret = kgsl_memdesc_get_reclaimed_pages(entry);
			kgsl_mem_entry_put(entry);
			if (ret)
				goto done;
		}

		next++;
	}

	trace_kgsl_reclaim_process(process, count, false);
	set_bit(KGSL_PROC_PINNED_STATE, &process->state);

	task_cputime(current, &utime, &stime_e);
	stime_d = stime_e - stime_s;
	pid = pid_nr(process->pid);
	swap_stat.nsec_end = local_clock();
	swap_stat.nsec_cputime = stime_d | SWAP_STAT_SWAPIN;
	swap_stat.pid = pid;
	swap_stat.nr_pages = count;

	pr_info("kgsl swapin timeJS(ms):%u|%llu tgid:%u nr_unpinned_page:%d->%d\n",
		jiffies_to_msecs(jiffies - jiffies_s),
		stime_d / NSEC_PER_MSEC, pid, count,
		atomic_read(&process->unpinned_page_count));

	swap_stat_save(&swap_stat);

done:
	mutex_unlock(&process->reclaim_lock);
	return ret;
}

static void kgsl_reclaim_foreground_work(struct work_struct *work)
{
	struct kgsl_process_private *process =
		container_of(work, struct kgsl_process_private, fg_work);

	if (test_bit(KGSL_PROC_STATE, &process->state))
		kgsl_reclaim_to_pinned_state(process);
	kgsl_process_private_put(process);
}

static ssize_t kgsl_proc_state_show(struct kobject *kobj,
		struct kgsl_process_attribute *attr, char *buf)
{
	struct kgsl_process_private *process =
		container_of(kobj, struct kgsl_process_private, kobj);

	if (test_bit(KGSL_PROC_STATE, &process->state))
		return scnprintf(buf, PAGE_SIZE, "foreground\n");
	else
		return scnprintf(buf, PAGE_SIZE, "background\n");
}

static ssize_t kgsl_proc_state_store(struct kobject *kobj,
	struct kgsl_process_attribute *attr, const char *buf, ssize_t count)
{
	struct kgsl_process_private *process =
		container_of(kobj, struct kgsl_process_private, kobj);

	if (sysfs_streq(buf, "foreground")) {
		if (!test_and_set_bit(KGSL_PROC_STATE, &process->state) &&
			kgsl_process_private_get(process))
			kgsl_schedule_work_highprio(&process->fg_work);
	} else if (sysfs_streq(buf, "background")) {
		if (test_and_clear_bit(KGSL_PROC_STATE, &process->state)
				&& kgsl_process_private_get(process)) {
			kgsl_schedule_work_highprio(&process->bg_work);
		} else {
			return -EINVAL;
		}
	} else
		return -EINVAL;

	return count;
}

static ssize_t gpumem_reclaimed_show(struct kobject *kobj,
		struct kgsl_process_attribute *attr, char *buf)
{
	struct kgsl_process_private *process =
		container_of(kobj, struct kgsl_process_private, kobj);

	return scnprintf(buf, PAGE_SIZE, "%d\n",
		atomic_read(&process->unpinned_page_count) << PAGE_SHIFT);
}

PROCESS_ATTR(state, 0644, kgsl_proc_state_show, kgsl_proc_state_store);
PROCESS_ATTR(gpumem_reclaimed, 0444, gpumem_reclaimed_show, NULL);

static const struct attribute *proc_reclaim_attrs[] = {
	&attr_state.attr,
	&attr_gpumem_reclaimed.attr,
	NULL,
};

void kgsl_reclaim_proc_sysfs_init(struct kgsl_process_private *process)
{
	WARN_ON(sysfs_create_files(&process->kobj, proc_reclaim_attrs));
}

ssize_t kgsl_proc_max_reclaim_limit_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;

	ret = kstrtou32(buf, 0, &kgsl_reclaim_max_page_limit);
	return ret ? ret : count;
}

ssize_t kgsl_proc_max_reclaim_limit_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", kgsl_reclaim_max_page_limit);
}

ssize_t kgsl_nr_to_scan_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;

	ret = kstrtou32(buf, 0, &kgsl_nr_to_scan);
	return ret ? ret : count;
}

ssize_t kgsl_nr_to_scan_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", kgsl_nr_to_scan);
}

static u32 kgsl_reclaim_process(struct kgsl_process_private *process,
		u32 pages_to_reclaim)
{
	struct kgsl_memdesc *memdesc;
	struct kgsl_mem_entry *entry, *valid_entry;
	u32 next = 0, remaining = pages_to_reclaim;

	/*
	 * If we do not get the lock here, it means that the buffers are
	 * being pinned back. So do not keep waiting here as we would anyway
	 * return empty handed once the lock is acquired.
	 */
	if (!mutex_trylock(&process->reclaim_lock))
		return 0;

	while (remaining) {

		if (atomic_read(&process->unpinned_page_count) >=
				kgsl_reclaim_max_page_limit)
			break;

		/* Abort reclaim if process submitted work. */
		if (atomic_read(&process->cmd_count))
			break;

		/* Abort reclaim if process foreground hint is received. */
		if (test_bit(KGSL_PROC_STATE, &process->state))
			break;

		valid_entry = NULL;
		spin_lock(&process->mem_lock);
		entry = idr_get_next(&process->mem_idr, &next);
		if (entry == NULL) {
			spin_unlock(&process->mem_lock);
			break;
		}

		memdesc = &entry->memdesc;
		if (!entry->pending_free &&
				(memdesc->priv & KGSL_MEMDESC_CAN_RECLAIM) &&
				!(memdesc->priv & KGSL_MEMDESC_RECLAIMED) &&
				!(memdesc->priv & KGSL_MEMDESC_SKIP_RECLAIM))
			valid_entry = kgsl_mem_entry_get(entry);
		spin_unlock(&process->mem_lock);

		if (!valid_entry) {
			next++;
			continue;
		}

		if ((atomic_read(&process->unpinned_page_count) +
			memdesc->page_count) > kgsl_reclaim_max_page_limit) {
			kgsl_mem_entry_put(entry);
			next++;
			continue;
		}

		if (memdesc->page_count > remaining) {
			kgsl_mem_entry_put(entry);
			next++;
			continue;
		}

		if (!kgsl_mmu_unmap(memdesc->pagetable, memdesc)) {
			int i;

			for (i = 0; i < memdesc->page_count; i++) {
				set_page_dirty_lock(memdesc->pages[i]);
				spin_lock(&memdesc->lock);
				put_page(memdesc->pages[i]);
				memdesc->pages[i] = NULL;
				atomic_inc(&process->unpinned_page_count);
				spin_unlock(&memdesc->lock);
				remaining--;
			}

			mapping_clear_unevictable(memdesc->shmem_filp->f_mapping);
			reclaim_shmem_address_space(memdesc->shmem_filp->f_mapping);
			mapping_set_unevictable(memdesc->shmem_filp->f_mapping);
			memdesc->priv |= KGSL_MEMDESC_RECLAIMED;
			trace_kgsl_reclaim_memdesc(entry, true);

		}

		kgsl_mem_entry_put(entry);
		next++;
	}

	if (next)
		clear_bit(KGSL_PROC_PINNED_STATE, &process->state);

	trace_kgsl_reclaim_process(process, pages_to_reclaim - remaining, true);
	mutex_unlock(&process->reclaim_lock);

	return (pages_to_reclaim - remaining);
}

static void ppr_kgsl_reclaim_background_work(struct work_struct *work)
{
	u64 utime, stime_s, stime_e, stime_d;
	u64 nr_reclaimed;
	pid_t pid;
	struct kgsl_process_private *process =
		container_of(work, struct kgsl_process_private, bg_work);

	if (!test_bit(KGSL_PROC_STATE, &process->state)) {
		struct swap_stat swap_stat;

		swap_stat.nsec_start = local_clock();
		task_cputime(current, &utime, &stime_s);

		nr_reclaimed = kgsl_reclaim_process(process, kgsl_reclaim_max_page_limit);

		task_cputime(current, &utime, &stime_e);
		stime_d = stime_e - stime_s;
		pid = pid_nr(process->pid);
		swap_stat.nsec_end = local_clock();
		swap_stat.nsec_cputime = stime_d;
		swap_stat.pid = pid;
		swap_stat.nr_pages = nr_reclaimed;
		swap_stat_save(&swap_stat);
	}

	kgsl_process_private_put(process);
}

static void kgsl_reclaim_background_work(struct work_struct *work)
{
	u32 bg_proc = 0, nr_pages = atomic_read(&kgsl_nr_to_reclaim);
	u64 pp_nr_pages;
	struct list_head kgsl_reclaim_process_list;
	struct kgsl_process_private *process, *next;

	INIT_LIST_HEAD(&kgsl_reclaim_process_list);
	read_lock(&kgsl_driver.proclist_lock);
	list_for_each_entry(process, &kgsl_driver.process_list, list) {
		if (test_bit(KGSL_PROC_STATE, &process->state) ||
				!kgsl_process_private_get(process))
			continue;

		bg_proc++;
		list_add(&process->reclaim_list, &kgsl_reclaim_process_list);
	}
	read_unlock(&kgsl_driver.proclist_lock);

	list_for_each_entry(process, &kgsl_reclaim_process_list, reclaim_list) {
		if (!nr_pages)
			break;

		pp_nr_pages = nr_pages;
		do_div(pp_nr_pages, bg_proc--);
		nr_pages -= kgsl_reclaim_process(process, pp_nr_pages);
	}

	list_for_each_entry_safe(process, next,
			&kgsl_reclaim_process_list, reclaim_list) {
		list_del(&process->reclaim_list);
		kgsl_process_private_put(process);
	}
}

/* Shrinker callback functions */
static unsigned long
kgsl_reclaim_shrink_scan_objects(struct shrinker *shrinker,
		struct shrink_control *sc)
{
	if (!current_is_kswapd())
		return 0;

	atomic_set(&kgsl_nr_to_reclaim, kgsl_nr_to_scan ?
					kgsl_nr_to_scan : sc->nr_to_scan);
	kgsl_schedule_work(&reclaim_work);

	return atomic_read(&kgsl_nr_to_reclaim);
}

static unsigned long
kgsl_reclaim_shrink_count_objects(struct shrinker *shrinker,
		struct shrink_control *sc)
{
	struct kgsl_process_private *process;
	unsigned long count_reclaimable = 0;

	if (!current_is_kswapd())
		return 0;

	read_lock(&kgsl_driver.proclist_lock);
	list_for_each_entry(process, &kgsl_driver.process_list, list) {
		if (!test_bit(KGSL_PROC_STATE, &process->state))
			count_reclaimable += kgsl_reclaim_max_page_limit -
				atomic_read(&process->unpinned_page_count);
	}
	read_unlock(&kgsl_driver.proclist_lock);

	return count_reclaimable;
}

/* Shrinker callback data*/
static struct shrinker kgsl_reclaim_shrinker = {
	.count_objects = kgsl_reclaim_shrink_count_objects,
	.scan_objects = kgsl_reclaim_shrink_scan_objects,
	.seeks = DEFAULT_SEEKS,
	.batch = 0,
};

void kgsl_reclaim_proc_private_init(struct kgsl_process_private *process)
{
	mutex_init(&process->reclaim_lock);
	INIT_WORK(&process->fg_work, kgsl_reclaim_foreground_work);
	INIT_WORK(&process->bg_work, ppr_kgsl_reclaim_background_work);
	set_bit(KGSL_PROC_PINNED_STATE, &process->state);
	set_bit(KGSL_PROC_STATE, &process->state);
	atomic_set(&process->unpinned_page_count, 0);
}

int kgsl_reclaim_init(void)
{
	int ret;

	/* Initialize shrinker */
	ret = register_shrinker(&kgsl_reclaim_shrinker);
	if (ret) {
		pr_err("kgsl: reclaim: Failed to register shrinker\n");
		return ret;
	}

	INIT_WORK(&reclaim_work, kgsl_reclaim_background_work);

	if (!graphics_swap_proc_init())
		proc_create_single("graphics_swap", 0444, NULL,
				   graphics_swap_proc_show);
	return ret;
}

void kgsl_reclaim_close(void)
{
	/* Unregister shrinker */
	unregister_shrinker(&kgsl_reclaim_shrinker);
}
