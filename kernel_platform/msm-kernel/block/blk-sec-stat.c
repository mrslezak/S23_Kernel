// SPDX-License-Identifier: GPL-2.0
/*
 *  Samsung Block Statistics
 *
 *  Copyright (C) 2021 Manjong Lee <mj0123.lee@samsung.com>
 *  Copyright (C) 2021 Junho Kim <junho89.kim@samsung.com>
 *  Copyright (C) 2021 Changheun Lee <nanich.lee@samsung.com>
 *  Copyright (C) 2021 Seunghwan Hyun <seunghwan.hyun@samsung.com>
 *  Copyright (C) 2021 Tran Xuan Nam <nam.tx2@samsung.com>
 */

#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/part_stat.h>

#include "blk-sec-stats.h"

struct disk_info {
	/* fields related with target device itself */
	struct gendisk *gd;
	struct request_queue *queue;
};

struct accumulated_stat {
	struct timespec64 uptime;
	unsigned long sectors[3];       /* READ, WRITE, DISCARD */
	unsigned long ios[3];
	unsigned long iot;
};

static struct disk_info internal_disk;
static unsigned int internal_min_size_mb = 10 * 1024; /* 10GB */
static struct accumulated_stat old, new;

extern int blk_sec_stat_pio_init(struct kobject *kobj);
extern void blk_sec_stat_pio_exit(struct kobject *kobj);
extern struct pio_node *get_pio_node(struct request *rq);
extern void update_pio_node(struct request *rq,
		unsigned int data_size, struct pio_node *pio);
extern void put_pio_node(struct pio_node *pio);

extern int blk_sec_stat_traffic_init(struct kobject *kobj);
extern void blk_sec_stat_traffic_exit(struct kobject *kobj);
extern void blk_sec_stat_traffic_update(struct request *rq,
		unsigned int data_size);

#define SECTORS2MB(x) ((x) / 2 / 1024)

static struct gendisk *get_internal_disk(void)
{
	struct gendisk *gd = NULL;
	struct block_device *bdev;
	int idx;
	int size_mb;

	/* In some project which is powered by MediaTek AP, the internal
	 * storage device is "sdc / MKDEV(8, 32)". So we have to try (8, 32)
	 * before trying (179, 0).
	 */
	dev_t devno[] = {
		MKDEV(8, 0),
		MKDEV(8, 32),
		MKDEV(179, 0),
		MKDEV(0, 0)
	};

	for (idx = 0; devno[idx] != MKDEV(0, 0); idx++) {
		bdev = blkdev_get_by_dev(devno[idx], FMODE_READ, NULL);
		if (!IS_ERR(bdev) && bdev->bd_disk) {
			size_mb = SECTORS2MB(get_capacity(bdev->bd_disk));

			if (size_mb >= internal_min_size_mb) {
				gd = bdev->bd_disk;
				break;
			}
		}
	}

	return gd;
}

static inline int init_internal_disk_info(void)
{
	/* it only sets internal_disk.gd info.
	 * internal_disk.rq_infos have to be allocated later.
	 */
	if (!internal_disk.gd) {
		internal_disk.gd = get_internal_disk();
		if (unlikely(!internal_disk.gd)) {
			pr_err("%s: can't find internal disk\n", __func__);
			return -ENODEV;
		}
	}
	internal_disk.queue = internal_disk.gd->queue;

	return 0;
}

static inline void clear_internal_disk_info(void)
{
	internal_disk.gd = NULL;
	internal_disk.queue = NULL;
}

bool is_internal_disk(struct gendisk *gd)
{
	struct gendisk *igd = get_internal_disk();

	if (gd == igd)
		return true;

	return false;
}
EXPORT_SYMBOL(is_internal_disk);

static inline bool has_valid_disk_info(void)
{
	return !!internal_disk.queue;
}

void blk_sec_stat_account_init(struct request_queue *q)
{
	int ret;

	if (!has_valid_disk_info()) {
		ret = init_internal_disk_info();
		if (ret) {
			clear_internal_disk_info();
			pr_err("%s: Can't find internal disk info!", __func__);
			return;
		}
	}
}
EXPORT_SYMBOL(blk_sec_stat_account_init);

void blk_sec_stat_account_exit(struct elevator_queue *eq)
{
}
EXPORT_SYMBOL(blk_sec_stat_account_exit);

#define UNSIGNED_DIFF(n, o) (((n) >= (o)) ? ((n) - (o)) : ((n) + (0 - (o))))
#define SECTORS2KB(x) ((x) / 2)

static inline void get_monotonic_boottime(struct timespec64 *ts)
{
	*ts = ktime_to_timespec64(ktime_get_boottime());
}

static ssize_t diskios_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int ret;
	struct block_device *bdev;
	long hours;

	if (unlikely(!has_valid_disk_info()))
		return -EINVAL;

	bdev = internal_disk.gd->part0;

	new.ios[STAT_READ] = part_stat_read(bdev, ios[STAT_READ]);
	new.ios[STAT_WRITE] = part_stat_read(bdev, ios[STAT_WRITE]);
	new.ios[STAT_DISCARD] = part_stat_read(bdev, ios[STAT_DISCARD]);
	new.sectors[STAT_READ] = part_stat_read(bdev, sectors[STAT_READ]);
	new.sectors[STAT_WRITE] = part_stat_read(bdev, sectors[STAT_WRITE]);
	new.sectors[STAT_DISCARD] = part_stat_read(bdev, sectors[STAT_DISCARD]);
	new.iot = jiffies_to_msecs(part_stat_read(bdev, io_ticks)) / 1000;

	get_monotonic_boottime(&(new.uptime));
	hours = (new.uptime.tv_sec - old.uptime.tv_sec) / 60;
	hours = (hours + 30) / 60;

	ret = sprintf(buf, "\"ReadC\":\"%lu\",\"ReadKB\":\"%lu\","
			"\"WriteC\":\"%lu\",\"WriteKB\":\"%lu\","
			"\"DiscardC\":\"%lu\",\"DiscardKB\":\"%lu\","
			"\"IOT\":\"%lu\","
			"\"Hours\":\"%ld\"\n",
			UNSIGNED_DIFF(new.ios[STAT_READ], old.ios[STAT_READ]),
			SECTORS2KB(UNSIGNED_DIFF(new.sectors[STAT_READ], old.sectors[STAT_READ])),
			UNSIGNED_DIFF(new.ios[STAT_WRITE], old.ios[STAT_WRITE]),
			SECTORS2KB(UNSIGNED_DIFF(new.sectors[STAT_WRITE], old.sectors[STAT_WRITE])),
			UNSIGNED_DIFF(new.ios[STAT_DISCARD], old.ios[STAT_DISCARD]),
			SECTORS2KB(UNSIGNED_DIFF(new.sectors[STAT_DISCARD], old.sectors[STAT_DISCARD])),
			UNSIGNED_DIFF(new.iot, old.iot),
			hours);

	old.ios[STAT_READ] = new.ios[STAT_READ];
	old.ios[STAT_WRITE] = new.ios[STAT_WRITE];
	old.ios[STAT_DISCARD] = new.ios[STAT_DISCARD];
	old.sectors[STAT_READ] = new.sectors[STAT_READ];
	old.sectors[STAT_WRITE] = new.sectors[STAT_WRITE];
	old.sectors[STAT_DISCARD] = new.sectors[STAT_DISCARD];
	old.uptime = new.uptime;
	old.iot = new.iot;

	return ret;
}

static inline bool may_account_rq(struct request *rq)
{
	if (unlikely(!has_valid_disk_info()))
		return false;

	if (internal_disk.queue != rq->q)
		return false;

	return true;
}

void blk_sec_stat_account_io_prepare(struct request *rq, void *ptr_pio)
{
	if (unlikely(!may_account_rq(rq)))
		return;

	*(struct pio_node **)ptr_pio = get_pio_node(rq);
}
EXPORT_SYMBOL(blk_sec_stat_account_io_prepare);

void blk_sec_stat_account_io_complete(struct request *rq,
		unsigned int data_size, void *pio)
{
	if (unlikely(!may_account_rq(rq)))
		return;

	blk_sec_stat_traffic_update(rq, data_size);
	update_pio_node(rq, data_size, (struct pio_node *)pio);
}
EXPORT_SYMBOL(blk_sec_stat_account_io_complete);

void blk_sec_stat_account_io_finish(struct request *rq, void *ptr_pio)
{
	if (unlikely(!may_account_rq(rq)))
		return;

	put_pio_node(*(struct pio_node **)ptr_pio);
	*(struct pio_node **)ptr_pio = NULL;
}
EXPORT_SYMBOL(blk_sec_stat_account_io_finish);

static struct kobj_attribute diskios_attr = __ATTR(diskios, 0444, diskios_show,  NULL);

static const struct attribute *blk_sec_stat_attrs[] = {
	&diskios_attr.attr,
	NULL,
};

static struct kobject *blk_sec_stats_kobj;

static int __init blk_sec_stats_init(void)
{
	int retval;

	blk_sec_stats_kobj = kobject_create_and_add("blk_sec_stats", kernel_kobj);
	if (!blk_sec_stats_kobj)
		return -ENOMEM;

	retval = sysfs_create_files(blk_sec_stats_kobj, blk_sec_stat_attrs);
	if (retval) {
		kobject_put(blk_sec_stats_kobj);
		return retval;
	}

	retval = blk_sec_stat_pio_init(blk_sec_stats_kobj);
	if (retval)
		pr_err("%s: fail to initialize PIO sub module", __func__);

	retval = blk_sec_stat_traffic_init(blk_sec_stats_kobj);
	if (retval)
		pr_err("%s: fail to initialize TRAFFIC sub module", __func__);

	retval = init_internal_disk_info();
	if (retval) {
		clear_internal_disk_info();
		pr_err("%s: Can't find internal disk info!", __func__);
	}

	return 0;
}

static void __exit blk_sec_stats_exit(void)
{
	clear_internal_disk_info();
	blk_sec_stat_traffic_exit(blk_sec_stats_kobj);
	blk_sec_stat_pio_exit(blk_sec_stats_kobj);
	sysfs_remove_files(blk_sec_stats_kobj, blk_sec_stat_attrs);
	kobject_put(blk_sec_stats_kobj);
}

module_init(blk_sec_stats_init);
module_exit(blk_sec_stats_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Manjong Lee <mj0123.lee@samsung.com>");
MODULE_DESCRIPTION("Samsung block layer statistics module for various purposes");
MODULE_VERSION("1.0");
