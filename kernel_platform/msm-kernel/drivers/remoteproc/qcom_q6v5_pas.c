// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm ADSP/SLPI Peripheral Image Loader for MSM8974 and MSM8996
 *
 * Copyright (C) 2016 Linaro Ltd
 * Copyright (C) 2014 Sony Mobile Communications AB
 * Copyright (c) 2012-2013, 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/qcom_scm.h>
#include <linux/regulator/consumer.h>
#include <linux/remoteproc.h>
#include <linux/interconnect.h>
#include <linux/soc/qcom/mdt_loader.h>
#include <linux/soc/qcom/smem.h>
#include <linux/soc/qcom/smem_state.h>
#include <linux/soc/qcom/qcom_aoss.h>
#include <soc/qcom/secure_buffer.h>
#include <trace/events/rproc_qcom.h>
#include <soc/qcom/qcom_ramdump.h>

#include "qcom_common.h"
#include "qcom_pil_info.h"
#include "qcom_q6v5.h"
#include "remoteproc_internal.h"

#define XO_FREQ		19200000
#define PIL_TZ_AVG_BW	UINT_MAX
#define PIL_TZ_PEAK_BW	UINT_MAX
#define QMP_MSG_LEN	64
#if IS_ENABLED(CONFIG_SEC_SENSORS_SSC)
#define SENSOR_SUPPLY_NAME "sensor_vdd"
#define SUBSENSOR_SUPPLY_NAME "subsensor_vdd"
#define PROX_VDD_NAME "prox_vdd"
#endif

static struct icc_path *scm_perf_client;
static int scm_pas_bw_count;
static DEFINE_MUTEX(scm_pas_bw_mutex);
bool timeout_disabled;
static bool mpss_dsm_mem_setup;
#if IS_ENABLED(CONFIG_SEC_SENSORS_SSC)
static int sensor_supply_reg_idx = -1;
static int subsensor_supply_reg_idx = -1;
static int prox_vdd_reg_idx = -1;
#endif

struct adsp_data {
	int crash_reason_smem;
	const char *firmware_name;
	const char *dtb_firmware_name;
	int pas_id;
	int dtb_pas_id;
	bool free_after_auth_reset;
	unsigned int minidump_id;
	bool uses_elf64;
	bool has_aggre2_clk;
	bool auto_boot;
	bool dma_phys_below_32b;
	bool needs_dsm_mem_setup;

	char **active_pd_names;
	char **proxy_pd_names;

	const char *ssr_name;
	const char *sysmon_name;
	const char *qmp_name;
	int ssctl_id;
};

struct qcom_adsp {
	struct device *dev;
	struct device *minidump_dev;
	struct rproc *rproc;

	struct qcom_q6v5 q6v5;

	struct clk *xo;
	struct clk *aggre2_clk;

	struct regulator *cx_supply;
	struct regulator *px_supply;
	struct reg_info *regs;
	int reg_cnt;

	struct device *active_pds[1];
	struct device *proxy_pds[3];
	const char *qmp_name;
	struct qmp *qmp;

	int active_pd_count;
	int proxy_pd_count;

	int pas_id;
	int dtb_pas_id;
	const char *dtb_fw_name;
	struct qcom_mdt_metadata *mdata;
	struct qcom_mdt_metadata dtb_mdata;
	unsigned int minidump_id;
	bool retry_shutdown;
	struct icc_path *bus_client;
	int crash_reason_smem;
	bool has_aggre2_clk;
	bool dma_phys_below_32b;
	const char *info_name;

	struct completion start_done;
	struct completion stop_done;

	phys_addr_t dtb_mem_phys;
	phys_addr_t dtb_mem_reloc;
	void *dtb_mem_region;
	size_t dtb_mem_size;

	phys_addr_t mem_phys;
	phys_addr_t mem_reloc;
	void *mem_region;
	size_t mem_size;

	struct qcom_rproc_glink glink_subdev;
	struct qcom_rproc_subdev smd_subdev;
	struct qcom_rproc_ssr ssr_subdev;
	struct qcom_sysmon *sysmon;
	const struct firmware *dtb_firmware;

	struct workqueue_struct *adsp_wq;
	struct work_struct ssr_handler;
};

struct msm_ipc_subsys_request {
	char name[16];
	char reason[16];
	int request_id;
};

enum {
	SUBSYS_CR_REQ = 0,
	SUBSYS_RES_REQ,
};

static ssize_t ssr_store(struct device *dev, struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct msm_ipc_subsys_request *req = (struct msm_ipc_subsys_request *)buf;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct qcom_adsp *adsp = (struct qcom_adsp *)platform_get_drvdata(pdev);
	struct rproc *rproc = adsp->rproc;

	if (count < sizeof(struct msm_ipc_subsys_request)) {
		dev_err(&rproc->dev, "Invalid argument for SSR (%d!=%d)\n",
			count, sizeof(struct msm_ipc_subsys_request));
		return -EINVAL;
	}

	/* NOTE: it supports only "modem" */
	if (strncmp(req->name, "modem", 5) != 0) {
		dev_err(&rproc->dev, "unsupported subsys: %s\n", req->name);
		return -EPERM;
	}

	switch (req->request_id) {
	case SUBSYS_CR_REQ:
		panic("RIL triggered %s crash %s", req->name, req->reason);
		break;
	case SUBSYS_RES_REQ:
		dev_info(&rproc->dev, "silent_ssr: %s\n", req->name);

		/* Prevent suspend while the remoteproc is being recovered */
		pm_stay_awake(rproc->dev.parent);

		queue_work(adsp->adsp_wq, &adsp->ssr_handler);
		break;
	default:
		dev_err(&rproc->dev, "Invalid request %d\n", req->request_id);
		return -EINVAL;
	}

	return count;
}
static DEVICE_ATTR_WO(ssr);

static void adsp_ssr_handler_work(struct work_struct *work)
{
	struct qcom_adsp *adsp = container_of(work, struct qcom_adsp, ssr_handler);
	struct qcom_q6v5 *q6v5 = &adsp->q6v5;
	struct rproc *rproc = adsp->rproc;
	struct rproc_subdev *subdev;
	const struct firmware *firmware_p;
	int ret;

	dev_info(&rproc->dev, "trigger sussystem restart - %s\n", rproc->firmware);

	ret = mutex_lock_interruptible(&rproc->lock);
	if (ret) {
		dev_err(&rproc->dev, "can't lock rproc %s: %d\n", rproc->name, ret);
		goto exit;
	}

	if (!atomic_read(&rproc->power)) {
		dev_err(&rproc->dev, "already offline rproc %s: %d\n", rproc->name, ret);
		goto proc_unlock;
	}

	spin_lock_irq(&q6v5->silent_ssr_lock);
	atomic_set(&q6v5->ssr_in_prog, 1);
	spin_unlock_irq(&q6v5->silent_ssr_lock);

	/* Stop any subdevices for the remote processor */
	list_for_each_entry_reverse(subdev, &rproc->subdevs, node) {
		if (subdev->stop)
			subdev->stop(subdev, false);
	}

	/* power off the remote processor */
	ret = rproc->ops->stop(rproc);
	if (ret) {
		dev_err(&rproc->dev, "can't stop rproc: %d\n", ret);
		goto proc_unlock;
	}

	list_for_each_entry_reverse(subdev, &rproc->subdevs, node) {
		if (subdev->unprepare)
			subdev->unprepare(subdev);
	}

	rproc->state = RPROC_OFFLINE;

	/* load firmware */
	ret = request_firmware(&firmware_p, rproc->firmware, &rproc->dev);
	if (ret < 0) {
		dev_err(&rproc->dev, "request_firmware failed: %d\n", ret);
		goto proc_unlock;
	}

	if (rproc->ops->load) {
		ret = rproc->ops->load(rproc, firmware_p);
		if (ret) {
			dev_err(&rproc->dev, "failed to load fw: %d\n", ret);
			goto out;
		}
	}

	list_for_each_entry(subdev, &rproc->subdevs, node) {
		if (subdev->prepare) {
			ret = subdev->prepare(subdev);
			if (ret)
				goto unroll_preparation;
		}
	}

	/* power up the remote processor */
	ret = rproc->ops->start(rproc);
	if (ret) {
		dev_err(&rproc->dev, "can't start rproc %s: %d\n", rproc->name, ret);
		goto unroll_preparation;
	}

	list_for_each_entry(subdev, &rproc->subdevs, node) {
		if (subdev->start) {
			ret = subdev->start(subdev);
			if (ret)
				goto unroll_registration;
		}
	}

	rproc->state = RPROC_RUNNING;

	dev_info(&rproc->dev, "remote processor %s is now up\n", rproc->name);

	goto out;

unroll_registration:
	list_for_each_entry_continue_reverse(subdev, &rproc->subdevs, node) {
		if (subdev->stop)
			subdev->stop(subdev, true);
	}

	rproc->ops->stop(rproc);

unroll_preparation:
	list_for_each_entry_continue_reverse(subdev, &rproc->subdevs, node) {
		if (subdev->unprepare)
			subdev->unprepare(subdev);
	}
out:
	release_firmware(firmware_p);
proc_unlock:
	spin_lock_irq(&q6v5->silent_ssr_lock);
	atomic_set(&q6v5->ssr_in_prog, 0);
	spin_unlock_irq(&q6v5->silent_ssr_lock);
	mutex_unlock(&rproc->lock);
exit:
	pm_relax(rproc->dev.parent);
}

static ssize_t txn_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct qcom_adsp *adsp = (struct qcom_adsp *)platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%zu\n", qcom_sysmon_get_txn_id(adsp->sysmon));
}
static DEVICE_ATTR_RO(txn_id);

void adsp_segment_dump(struct rproc *rproc, struct rproc_dump_segment *segment,
		     void *dest, size_t offset, size_t size)
{
	struct qcom_adsp *adsp = rproc->priv;
	int total_offset;
	void __iomem *base;
	int len = strlen("md_dbg_buf");

	if (strnlen(segment->priv, len + 1) == len &&
		    !strcmp(segment->priv, "md_dbg_buf")) {
		base = ioremap((unsigned long)le64_to_cpu(segment->da), size);
		if (!base) {
			pr_err("failed to map md_dbg_buf region\n");
			return;
		}

		memcpy_fromio(dest, base, size);
		iounmap(base);
		return;
	}

	total_offset = segment->da + segment->offset + offset - adsp->mem_phys;
	if (total_offset < 0 || total_offset + size > adsp->mem_size) {
		dev_err(adsp->dev,
			"invalid copy request for segment %pad with offset %zu and size %zu)\n",
			&segment->da, offset, size);
		memset(dest, 0xff, size);
		return;
	}

	memcpy_fromio(dest, adsp->mem_region + total_offset, size);
}

static void adsp_minidump(struct rproc *rproc)
{
	struct qcom_adsp *adsp = rproc->priv;

	trace_rproc_qcom_event(dev_name(adsp->dev), "adsp_minidump", "enter");

	if (rproc->dump_conf == RPROC_COREDUMP_DISABLED)
		goto exit;

	qcom_minidump(rproc, adsp->minidump_dev, adsp->minidump_id, adsp_segment_dump);

exit:
	trace_rproc_qcom_event(dev_name(adsp->dev), "adsp_minidump", "exit");
}

static int adsp_toggle_load_state(struct qmp *qmp, const char *name, bool enable)
{
	char buf[QMP_MSG_LEN] = {};

	snprintf(buf, sizeof(buf),
		 "{class: image, res: load_state, name: %s, val: %s}",
		 name, enable ? "on" : "off");
	return qmp_send(qmp, buf, sizeof(buf));
}

static int adsp_pds_enable(struct qcom_adsp *adsp, struct device **pds,
			   size_t pd_count)
{
	int ret;
	int i;

	for (i = 0; i < pd_count; i++) {
		dev_pm_genpd_set_performance_state(pds[i], INT_MAX);
		ret = pm_runtime_get_sync(pds[i]);
		if (ret < 0) {
			pm_runtime_put_noidle(pds[i]);
			dev_pm_genpd_set_performance_state(pds[i], 0);
			goto unroll_pd_votes;
		}
	}

	return 0;

unroll_pd_votes:
	for (i--; i >= 0; i--) {
		dev_pm_genpd_set_performance_state(pds[i], 0);
		pm_runtime_put(pds[i]);
	}

	return ret;
};

static void adsp_pds_disable(struct qcom_adsp *adsp, struct device **pds,
			     size_t pd_count)
{
	int i;

	for (i = 0; i < pd_count; i++) {
		dev_pm_genpd_set_performance_state(pds[i], 0);
		pm_runtime_put(pds[i]);
	}
}

static int scm_pas_enable_bw(void)
{
	int ret = 0;

	if (IS_ERR(scm_perf_client))
		return -EINVAL;

	mutex_lock(&scm_pas_bw_mutex);
	if (!scm_pas_bw_count) {
		ret = icc_set_bw(scm_perf_client, PIL_TZ_AVG_BW,
						PIL_TZ_PEAK_BW);
		if (ret)
			goto err_bus;
	}

	scm_pas_bw_count++;
	mutex_unlock(&scm_pas_bw_mutex);
	return ret;

err_bus:
	pr_err("scm-pas: Bandwidth request failed (%d)\n", ret);
	icc_set_bw(scm_perf_client, 0, 0);

	mutex_unlock(&scm_pas_bw_mutex);
	return ret;
}

static void scm_pas_disable_bw(void)
{
	if (IS_ERR(scm_perf_client))
		return;

	mutex_lock(&scm_pas_bw_mutex);
	if (scm_pas_bw_count-- == 1)
		icc_set_bw(scm_perf_client, 0, 0);
	mutex_unlock(&scm_pas_bw_mutex);
}

static void adsp_add_coredump_segments(struct qcom_adsp *adsp, const struct firmware *fw)
{
	struct rproc *rproc = adsp->rproc;
	struct rproc_dump_segment *entry;
	struct elf32_hdr *ehdr = (struct elf32_hdr *)fw->data;
	struct elf32_phdr *phdr, *phdrs = (struct elf32_phdr *)(fw->data + ehdr->e_phoff);
	uint32_t elf_min_addr = U32_MAX;
	bool relocatable = false;
	int ret;
	int i;

	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr = &phdrs[i];
		if (phdr->p_type != PT_LOAD ||
		   (phdr->p_flags & QCOM_MDT_TYPE_MASK) == QCOM_MDT_TYPE_HASH ||
		   !phdr->p_memsz)
			continue;

		if (phdr->p_flags & QCOM_MDT_RELOCATABLE)
			relocatable = true;

		elf_min_addr = min(phdr->p_paddr, elf_min_addr);

		ret = rproc_coredump_add_segment(rproc, phdr->p_paddr, phdr->p_memsz);
		if (ret) {
			dev_err(adsp->dev, "failed to add rproc segment: %d\n", ret);
			rproc_coredump_cleanup(adsp->rproc);
			return;
		}
	}

	list_for_each_entry(entry, &rproc->dump_segments, node)
		entry->da = adsp->mem_phys + entry->da - elf_min_addr;

	if (relocatable)
		adsp->mem_reloc = adsp->mem_phys + adsp->mem_reloc - elf_min_addr;
}

static int adsp_load(struct rproc *rproc, const struct firmware *fw)
{
	struct qcom_adsp *adsp = (struct qcom_adsp *)rproc->priv;
	int ret;

	trace_rproc_qcom_event(dev_name(adsp->dev), "adsp_load", "enter");

	rproc_coredump_cleanup(adsp->rproc);

	scm_pas_enable_bw();

	if (!adsp->dtb_pas_id || !adsp->dtb_fw_name) {
		scm_pas_disable_bw();
		return 0;
	}

	ret = request_firmware(&adsp->dtb_firmware, adsp->dtb_fw_name, adsp->dev);
	if (ret) {
		dev_err(adsp->dev, "request_firmware failed for %s: %d\n", adsp->dtb_fw_name, ret);
		goto exit;
	}

	ret = qcom_mdt_load_no_free(adsp->dev, adsp->dtb_firmware, adsp->dtb_fw_name,
				adsp->dtb_pas_id, adsp->dtb_mem_region, adsp->dtb_mem_phys,
				adsp->dtb_mem_size, &adsp->dtb_mem_reloc, adsp->dma_phys_below_32b,
				&adsp->dtb_mdata);
	if (ret) {
		dev_err(adsp->dev, "failed to load %s: %d\n", adsp->dtb_fw_name, ret);
		release_firmware(adsp->dtb_firmware);
		goto exit;
	}

exit:
	trace_rproc_qcom_event(dev_name(adsp->dev), "adsp_load", "exit");
	scm_pas_disable_bw();

	return ret;
}
#if IS_ENABLED(CONFIG_SEC_SENSORS_SSC)
static void disable_regulators_sensor_vdd(struct qcom_adsp *adsp)
{
	dev_info(adsp->dev, "%s Regulator disable: %s %d uV %d uA\n", __func__,
		adsp->info_name, adsp->regs[sensor_supply_reg_idx].uV,
		adsp->regs[sensor_supply_reg_idx].uA);
	regulator_set_voltage(adsp->regs[sensor_supply_reg_idx].reg, 0, INT_MAX);
	regulator_set_load(adsp->regs[sensor_supply_reg_idx].reg, 0);
	regulator_disable(adsp->regs[sensor_supply_reg_idx].reg);

	if (subsensor_supply_reg_idx > 0) {
		dev_info(adsp->dev, "%s Regulator disable: %s %d uV %d uA\n", __func__,
			adsp->info_name, adsp->regs[subsensor_supply_reg_idx].uV,
			adsp->regs[subsensor_supply_reg_idx].uA);
		regulator_set_voltage(adsp->regs[subsensor_supply_reg_idx].reg, 0, INT_MAX);
		regulator_set_load(adsp->regs[subsensor_supply_reg_idx].reg, 0);
		regulator_disable(adsp->regs[subsensor_supply_reg_idx].reg);
	}

	if (prox_vdd_reg_idx > 0) {
		dev_info(adsp->dev, "%s Regulator disable: %s %d uV %d uA\n", __func__,
			adsp->info_name, adsp->regs[prox_vdd_reg_idx].uV,
			adsp->regs[prox_vdd_reg_idx].uA);
		regulator_set_voltage(adsp->regs[prox_vdd_reg_idx].reg, 0, INT_MAX);
		regulator_set_load(adsp->regs[prox_vdd_reg_idx].reg, 0);
		regulator_disable(adsp->regs[prox_vdd_reg_idx].reg);
	}
}
#endif

static void disable_regulators(struct qcom_adsp *adsp)
{
	int i;

	for (i = (adsp->reg_cnt - 1); i >= 0; i--) {
#if IS_ENABLED(CONFIG_SEC_SENSORS_SSC)
		if (!strcmp(adsp->info_name, "adsp")) {
			if ((i == sensor_supply_reg_idx)
				|| (i == subsensor_supply_reg_idx)
				|| (i == prox_vdd_reg_idx)) {
				dev_info(adsp->dev, "skip disabling %s, idx: %d",
					SENSOR_SUPPLY_NAME, i);
				continue;
			}
		}
#endif
		regulator_set_voltage(adsp->regs[i].reg, 0, INT_MAX);
		regulator_set_load(adsp->regs[i].reg, 0);
		regulator_disable(adsp->regs[i].reg);
	}
}

static int enable_regulators(struct qcom_adsp *adsp)
{
	int i, rc = 0;

	for (i = 0; i < adsp->reg_cnt; i++) {
		regulator_set_voltage(adsp->regs[i].reg, adsp->regs[i].uV, INT_MAX);
		regulator_set_load(adsp->regs[i].reg, adsp->regs[i].uA);
		rc = regulator_enable(adsp->regs[i].reg);
		if (rc) {
			dev_err(adsp->dev, "Regulator enable failed(rc:%d)\n",
				rc);
			goto err_enable;
		}
	}
	return rc;

err_enable:
	disable_regulators(adsp);
	return rc;
}

static int do_bus_scaling(struct qcom_adsp *adsp, bool enable)
{
	int rc = 0;
	u32 avg_bw = enable ? PIL_TZ_AVG_BW : 0;
	u32 peak_bw = enable ? PIL_TZ_PEAK_BW : 0;

	if (IS_ERR(adsp->bus_client))
		dev_err(adsp->dev, "Bus scaling not setup for %s\n",
			adsp->rproc->name);
	else
		rc = icc_set_bw(adsp->bus_client, avg_bw, peak_bw);

	if (rc)
		dev_err(adsp->dev, "bandwidth request failed(rc:%d)\n", rc);

	return rc;
}

static int adsp_start(struct rproc *rproc)
{
	struct qcom_adsp *adsp = (struct qcom_adsp *)rproc->priv;
	int i, ret;
	const struct firmware *fw = NULL;

	trace_rproc_qcom_event(dev_name(adsp->dev), "adsp_start", "enter");

	qcom_q6v5_prepare(&adsp->q6v5);

	ret = do_bus_scaling(adsp, true);
	if (ret < 0)
		goto disable_irqs;

	ret = adsp_pds_enable(adsp, adsp->active_pds, adsp->active_pd_count);
	if (ret < 0)
		goto unscale_bus;

	ret = adsp_pds_enable(adsp, adsp->proxy_pds, adsp->proxy_pd_count);
	if (ret < 0)
		goto disable_active_pds;

	if (adsp->qmp) {
		ret = adsp_toggle_load_state(adsp->qmp, adsp->qmp_name, true);
		if (ret)
			goto disable_proxy_pds;
	}

	ret = clk_prepare_enable(adsp->xo);
	if (ret)
		goto disable_load_state;

	ret = clk_prepare_enable(adsp->aggre2_clk);
	if (ret)
		goto disable_xo_clk;

	ret = enable_regulators(adsp);
	if (ret)
		goto disable_aggre2_clk;

	scm_pas_enable_bw();
	trace_rproc_qcom_event(dev_name(adsp->dev), "dtb_auth_reset", "enter");
	if (adsp->dtb_pas_id || adsp->dtb_fw_name) {
		ret = qcom_scm_pas_auth_and_reset(adsp->dtb_pas_id);
		if (ret)
			panic("Panicking, auth and reset failed for remoteproc %s dtb\n",
				 rproc->name);
	}

	trace_rproc_qcom_event(dev_name(adsp->dev), "Q6_firmware_loading", "enter");
	ret = request_firmware(&fw, rproc->firmware, adsp->dev);
	if (ret)
		goto free_metadata_dtb;

	ret = qcom_mdt_load_no_free(adsp->dev, fw, rproc->firmware, adsp->pas_id,
				    adsp->mem_region, adsp->mem_phys, adsp->mem_size,
				    &adsp->mem_reloc, adsp->dma_phys_below_32b, adsp->mdata);
	if (ret)
		goto free_firmware;

	qcom_pil_info_store(adsp->info_name, adsp->mem_phys, adsp->mem_size);

	adsp_add_coredump_segments(adsp, fw);
	trace_rproc_qcom_event(dev_name(adsp->dev), "Q6_auth_reset", "enter");

	ret = qcom_scm_pas_auth_and_reset(adsp->pas_id);
#ifdef HDM_SUPPORT
	if (ret) {
		// Intentionally block cp load.
		if (hdm_cp_support)
			goto free_metadata;
		else
			panic("Panicking, auth and reset failed for remoteproc %s\n", rproc->name);
	}
#else
	if (ret)
		panic("Panicking, auth and reset failed for remoteproc %s\n", rproc->name);
#endif
	trace_rproc_qcom_event(dev_name(adsp->dev), "Q6_auth_reset", "exit");

	/* if needed, signal Q6 to continute booting */
	if (adsp->q6v5.rmb_base) {
		for (i = 0; i < RMB_POLL_MAX_TIMES || timeout_disabled; i++) {
			if (readl_relaxed(adsp->q6v5.rmb_base + RMB_BOOT_WAIT_REG)) {
				writel_relaxed(1, adsp->q6v5.rmb_base + RMB_BOOT_CONT_REG);
				break;
			}
			msleep(20);
		}

		if (!readl_relaxed(adsp->q6v5.rmb_base + RMB_BOOT_WAIT_REG)) {
			dev_err(adsp->dev, "Didn't get rmb signal from  %s\n", rproc->name);
			goto free_metadata;
		}
	}

	if (!timeout_disabled) {
		ret = qcom_q6v5_wait_for_start(&adsp->q6v5, msecs_to_jiffies(5000));
		if (rproc->recovery_disabled && ret)
			panic("Panicking, remoteproc %s failed to bootup.\n", adsp->rproc->name);
		else if (ret == -ETIMEDOUT)
			dev_err(adsp->dev, "start timed out\n");
	}

free_metadata:
	qcom_mdt_free_metadata(adsp->dev, adsp->pas_id, adsp->mdata,
					adsp->dma_phys_below_32b, ret);
free_firmware:
	if (fw)
		release_firmware(fw);

free_metadata_dtb:
	if (adsp->dtb_pas_id || adsp->dtb_fw_name) {
		qcom_mdt_free_metadata(adsp->dev, adsp->dtb_pas_id,
					&adsp->dtb_mdata, adsp->dma_phys_below_32b, ret);
		release_firmware(adsp->dtb_firmware);
	}

	scm_pas_disable_bw();
	if (!ret)
		goto exit;

	disable_regulators(adsp);
disable_aggre2_clk:
	clk_disable_unprepare(adsp->aggre2_clk);
disable_xo_clk:
	clk_disable_unprepare(adsp->xo);
disable_load_state:
	if (adsp->qmp)
		adsp_toggle_load_state(adsp->qmp, adsp->qmp_name, false);
disable_proxy_pds:
	adsp_pds_disable(adsp, adsp->proxy_pds, adsp->proxy_pd_count);
disable_active_pds:
	adsp_pds_disable(adsp, adsp->active_pds, adsp->active_pd_count);
unscale_bus:
	do_bus_scaling(adsp, false);
disable_irqs:
	qcom_q6v5_unprepare(&adsp->q6v5);
exit:
	trace_rproc_qcom_event(dev_name(adsp->dev), "adsp_start", "exit");
	return ret;
}

static void qcom_pas_handover(struct qcom_q6v5 *q6v5)
{
	struct qcom_adsp *adsp = container_of(q6v5, struct qcom_adsp, q6v5);

#if IS_ENABLED(CONFIG_SEC_SENSORS_SSC)
	if (strcmp(adsp->rproc->name, "adsp"))
		disable_regulators(adsp);
#else
	disable_regulators(adsp);
#endif
	clk_disable_unprepare(adsp->aggre2_clk);
	clk_disable_unprepare(adsp->xo);
	adsp_pds_disable(adsp, adsp->proxy_pds, adsp->proxy_pd_count);
	do_bus_scaling(adsp, false);
}

static int adsp_stop(struct rproc *rproc)
{
	struct qcom_adsp *adsp = (struct qcom_adsp *)rproc->priv;
	int handover;
	int ret;

	trace_rproc_qcom_event(dev_name(adsp->dev), "adsp_stop", "enter");

	ret = qcom_q6v5_request_stop(&adsp->q6v5, adsp->sysmon);
	if (ret == -ETIMEDOUT)
		dev_err(adsp->dev, "timed out on wait\n");

	scm_pas_enable_bw();
	if (adsp->retry_shutdown)
		ret = qcom_scm_pas_shutdown_retry(adsp->pas_id);
	else
		ret = qcom_scm_pas_shutdown(adsp->pas_id);
	if (ret)
		panic("Panicking, remoteproc %s failed to shutdown.\n", rproc->name);

	if (adsp->dtb_pas_id) {
		ret = qcom_scm_pas_shutdown(adsp->dtb_pas_id);
		if (ret)
			panic("Panicking, remoteproc %s dtb failed to shutdown.\n", rproc->name);
	}

	scm_pas_disable_bw();
	adsp_pds_disable(adsp, adsp->active_pds, adsp->active_pd_count);
	if (adsp->qmp)
		adsp_toggle_load_state(adsp->qmp, adsp->qmp_name, false);
	handover = qcom_q6v5_unprepare(&adsp->q6v5);
	if (handover)
		qcom_pas_handover(&adsp->q6v5);
#if IS_ENABLED(CONFIG_SEC_SENSORS_SSC)
	if (!strcmp(adsp->info_name, "adsp") && sensor_supply_reg_idx > 0)
		disable_regulators_sensor_vdd(adsp);
#endif

	trace_rproc_qcom_event(dev_name(adsp->dev), "adsp_stop", "exit");

	return ret;
}

static int adsp_attach(struct rproc *rproc)
{
	struct qcom_adsp *adsp = (struct qcom_adsp *)rproc->priv;
	const struct firmware *fw;
	int ret = 0;
	int i;

	/* try to register fw for dumps; continue if we fail */
	ret = request_firmware(&fw, rproc->firmware, &rproc->dev);
	if (ret < 0) {
		dev_err(adsp->dev, "Failed to request DSP firmware\n");
		dev_err(adsp->dev, "Dumps will not be available\n");
		goto begin_attach;
	}

	adsp_add_coredump_segments(adsp, fw);
	release_firmware(fw);

begin_attach:
	qcom_q6v5_prepare(&adsp->q6v5);

	ret = do_bus_scaling(adsp, true);
	if (ret < 0)
		goto disable_irqs;

	ret = adsp_pds_enable(adsp, adsp->active_pds, adsp->active_pd_count);
	if (ret < 0)
		goto unscale_bus;

	if (!adsp->q6v5.rmb_base) {
		dev_err(adsp->dev, "Remote proc cannot be late attached\n");
		goto disable_active_pds;
	}

	ret = adsp_pds_enable(adsp, adsp->proxy_pds, adsp->proxy_pd_count);
	if (ret < 0)
		goto disable_active_pds;

	ret = adsp_toggle_load_state(adsp->qmp, adsp->qmp_name, true);
	if (ret)
		goto disable_proxy_pds;

	ret = clk_prepare_enable(adsp->xo);
	if (ret)
		goto disable_load_state;

	ret = clk_prepare_enable(adsp->aggre2_clk);
	if (ret)
		goto disable_xo_clk;

	ret = enable_regulators(adsp);
	if (ret)
		goto disable_aggre2_clk;

	/* Signal the Q6 to continue booting */
	for (i = 0; i < RMB_POLL_MAX_TIMES || timeout_disabled; i++) {
		if (readl_relaxed(adsp->q6v5.rmb_base + RMB_BOOT_WAIT_REG)) {
			writel_relaxed(1, adsp->q6v5.rmb_base + RMB_BOOT_CONT_REG);
			break;
		}
		msleep(20);
	}

	if (!readl_relaxed(adsp->q6v5.rmb_base + RMB_BOOT_WAIT_REG)) {
		dev_err(adsp->dev, "Didn't get rmb signal from %s\n", rproc->name);
		goto disable_regs;
	}

	if (!timeout_disabled) {
		ret = qcom_q6v5_wait_for_start(&adsp->q6v5, msecs_to_jiffies(5000));
		if (rproc->recovery_disabled && ret) {
			panic("Panicking, remoteproc %s failed to bootup.\n", adsp->rproc->name);
		} else if (ret == -ETIMEDOUT) {
			dev_err(adsp->dev, "start timed out\n");
			goto disable_regs;
		}
	}

	return ret;

disable_regs:
	disable_regulators(adsp);
disable_aggre2_clk:
	clk_disable_unprepare(adsp->aggre2_clk);
disable_xo_clk:
	clk_disable_unprepare(adsp->xo);
disable_load_state:
	adsp_toggle_load_state(adsp->qmp, adsp->qmp_name, false);
disable_proxy_pds:
	adsp_pds_disable(adsp, adsp->proxy_pds, adsp->proxy_pd_count);
disable_active_pds:
	adsp_pds_disable(adsp, adsp->active_pds, adsp->active_pd_count);
unscale_bus:
	do_bus_scaling(adsp, false);
disable_irqs:
	qcom_q6v5_unprepare(&adsp->q6v5);

	/* attempt to normally boot rproc if we can't late attach */
	return adsp_start(rproc);
}

static void *adsp_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
{
	struct qcom_adsp *adsp = (struct qcom_adsp *)rproc->priv;
	int offset;

	offset = da - adsp->mem_reloc;
	if (offset < 0 || offset + len > adsp->mem_size)
		return NULL;

	if (is_iomem)
		*is_iomem = true;

	return adsp->mem_region + offset;
}

static unsigned long adsp_panic(struct rproc *rproc)
{
	struct qcom_adsp *adsp = (struct qcom_adsp *)rproc->priv;

	return qcom_q6v5_panic(&adsp->q6v5);
}

static const struct rproc_ops adsp_ops = {
	.attach = adsp_attach,
	.start = adsp_start,
	.stop = adsp_stop,
	.da_to_va = adsp_da_to_va,
	.load = adsp_load,
	.panic = adsp_panic,
};

static const struct rproc_ops adsp_minidump_ops = {
	.attach = adsp_attach,
	.start = adsp_start,
	.stop = adsp_stop,
	.da_to_va = adsp_da_to_va,
	.load = adsp_load,
	.panic = adsp_panic,
	.coredump = adsp_minidump,
};

static int adsp_init_clock(struct qcom_adsp *adsp)
{
	int ret;

	adsp->xo = devm_clk_get(adsp->dev, "xo");
	if (IS_ERR(adsp->xo)) {
		ret = PTR_ERR(adsp->xo);
		if (ret != -EPROBE_DEFER)
			dev_err(adsp->dev, "failed to get xo clock");
		return ret;
	}

	if (adsp->has_aggre2_clk) {
		adsp->aggre2_clk = devm_clk_get(adsp->dev, "aggre2");
		if (IS_ERR(adsp->aggre2_clk)) {
			ret = PTR_ERR(adsp->aggre2_clk);
			if (ret != -EPROBE_DEFER)
				dev_err(adsp->dev,
					"failed to get aggre2 clock");
			return ret;
		}
	}

	return 0;
}

static int adsp_init_regulator(struct qcom_adsp *adsp)
{
	int len;
	int i, rc;
	char uv_ua[50];
	u32 uv_ua_vals[2];
	const char *reg_name;

	adsp->reg_cnt = of_property_count_strings(adsp->dev->of_node,
						  "reg-names");
	if (adsp->reg_cnt <= 0) {
		dev_err(adsp->dev, "No regulators added!\n");
		return 0;
	}

	adsp->regs = devm_kzalloc(adsp->dev,
				  sizeof(struct reg_info) * adsp->reg_cnt,
				  GFP_KERNEL);
	if (!adsp->regs)
		return -ENOMEM;

	for (i = 0; i < adsp->reg_cnt; i++) {
		of_property_read_string_index(adsp->dev->of_node, "reg-names",
					      i, &reg_name);

		adsp->regs[i].reg = devm_regulator_get(adsp->dev, reg_name);
		if (IS_ERR(adsp->regs[i].reg)) {
#if IS_ENABLED(CONFIG_SEC_SENSORS_SSC)
			if (!strcmp(reg_name, SENSOR_SUPPLY_NAME) ||
				!strcmp(reg_name, SUBSENSOR_SUPPLY_NAME) ||
				!strcmp(reg_name, PROX_VDD_NAME)) {
				dev_info(adsp->dev, "%s ignore %s %d\n",
					__func__, reg_name, adsp->reg_cnt--);
				return 0;
			}
#endif
			dev_err(adsp->dev, "failed to get %s reg\n", reg_name);
			return PTR_ERR(adsp->regs[i].reg);
		}

		/* Read current(uA) and voltage(uV) value */
		snprintf(uv_ua, sizeof(uv_ua), "%s-uV-uA", reg_name);
		if (!of_find_property(adsp->dev->of_node, uv_ua, &len))
			continue;

		rc = of_property_read_u32_array(adsp->dev->of_node, uv_ua,
						uv_ua_vals,
						ARRAY_SIZE(uv_ua_vals));
		if (rc) {
			dev_err(adsp->dev, "Failed to read uVuA value(rc:%d)\n",
				rc);
			return rc;
		}

		if (uv_ua_vals[0] > 0)
			adsp->regs[i].uV = uv_ua_vals[0];
		if (uv_ua_vals[1] > 0)
			adsp->regs[i].uA = uv_ua_vals[1];
#if IS_ENABLED(CONFIG_SEC_SENSORS_SSC)
		if (!strcmp(reg_name, SENSOR_SUPPLY_NAME)) {
			dev_info(adsp->dev, "found %s, idx: %d\n", reg_name, i);
			sensor_supply_reg_idx = i;
		} else if (!strcmp(reg_name, SUBSENSOR_SUPPLY_NAME)) {
			dev_info(adsp->dev, "found %s, idx: %d\n", reg_name, i);
			subsensor_supply_reg_idx = i;
		} else if (!strcmp(reg_name, PROX_VDD_NAME)) {
			dev_info(adsp->dev, "found %s, idx: %d\n", reg_name, i);
			prox_vdd_reg_idx = i;
		}
#endif
	}
	return 0;
}

static void adsp_init_bus_scaling(struct qcom_adsp *adsp)
{
	if (scm_perf_client)
		goto get_rproc_client;

	scm_perf_client = of_icc_get(adsp->dev, "crypto_ddr");
	if (IS_ERR(scm_perf_client))
		dev_warn(adsp->dev, "Crypto scaling not setup\n");

get_rproc_client:
	adsp->bus_client = of_icc_get(adsp->dev, "rproc_ddr");
	if (IS_ERR(adsp->bus_client))
		dev_warn(adsp->dev, "%s: No bus client\n", __func__);

}

static int adsp_pds_attach(struct device *dev, struct device **devs,
			   char **pd_names)
{
	size_t num_pds = 0;
	int ret;
	int i;

	if (!pd_names)
		return 0;

	/* Handle single power domain */
	if (dev->pm_domain) {
		devs[0] = dev;
		pm_runtime_enable(dev);
		return 1;
	}

	while (pd_names[num_pds])
		num_pds++;

	for (i = 0; i < num_pds; i++) {
		devs[i] = dev_pm_domain_attach_by_name(dev, pd_names[i]);
		if (IS_ERR_OR_NULL(devs[i])) {
			ret = PTR_ERR(devs[i]) ? : -ENODATA;
			goto unroll_attach;
		}
	}

	return num_pds;

unroll_attach:
	for (i--; i >= 0; i--)
		dev_pm_domain_detach(devs[i], false);

	return ret;
};

static void adsp_pds_detach(struct qcom_adsp *adsp, struct device **pds,
			    size_t pd_count)
{
	struct device *dev = adsp->dev;
	int i;

	/* Handle single power domain */
	if (dev->pm_domain && pd_count) {
		pm_runtime_disable(dev);
		return;
	}

	for (i = 0; i < pd_count; i++)
		dev_pm_domain_detach(pds[i], false);
}

static int adsp_alloc_memory_region(struct qcom_adsp *adsp)
{
	struct device_node *node;
	struct resource r;
	int ret;

	node = of_parse_phandle(adsp->dev->of_node, "memory-region", 0);
	if (!node) {
		dev_err(adsp->dev, "no memory-region specified\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(node, 0, &r);
	if (ret)
		return ret;

	adsp->mem_phys = adsp->mem_reloc = r.start;
	adsp->mem_size = resource_size(&r);
	adsp->mem_region = devm_ioremap_wc(adsp->dev, adsp->mem_phys, adsp->mem_size);
	if (!adsp->mem_region) {
		dev_err(adsp->dev, "unable to map memory region: %pa+%zx\n",
			&r.start, adsp->mem_size);
		return -EBUSY;
	}

	if (!adsp->dtb_pas_id)
		return 0;

	node = of_parse_phandle(adsp->dev->of_node, "memory-region", 1);
	if (!node) {
		dev_err(adsp->dev, "no dtb memory-region specified\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(node, 0, &r);
	if (ret)
		return ret;

	adsp->dtb_mem_phys = adsp->dtb_mem_reloc = r.start;
	adsp->dtb_mem_size = resource_size(&r);
	adsp->dtb_mem_region = devm_ioremap_wc(adsp->dev, adsp->dtb_mem_phys, adsp->dtb_mem_size);
	if (!adsp->dtb_mem_region) {
		dev_err(adsp->dev, "unable to map memory region: %pa+%zx\n",
			&r.start, adsp->dtb_mem_size);
		return -EBUSY;
	}

	return 0;
}

static int adsp_setup_32b_dma_allocs(struct qcom_adsp *adsp)
{
	int ret;

	if (!adsp->dma_phys_below_32b)
		return 0;

	ret = of_reserved_mem_device_init_by_idx(adsp->dev, adsp->dev->of_node, 2);
	if (ret) {
		dev_err(adsp->dev,
			"Unable to get the CMA area for performing dma_alloc_* calls\n");
		goto out;
	}

	ret = dma_set_mask_and_coherent(adsp->dev, DMA_BIT_MASK(32));
	if (ret)
		dev_err(adsp->dev, "Unable to set the coherent mask to 32-bits!\n");

out:
	return ret;
}

static int setup_mpss_dsm_mem(struct platform_device *pdev)
{
	struct device_node *node;
	struct resource res;
	int hlosvm[1] = {VMID_HLOS};
	int mssvm[1] = {VMID_MSS_MSA};
	int vmperm[1] = {PERM_READ | PERM_WRITE};
	phys_addr_t mem_phys;
	u64 mem_size;
	int ret;

	node = of_parse_phandle(pdev->dev.of_node, "mpss_dsm_mem_reg", 0);
	if (!node) {
		dev_err(&pdev->dev, "mpss dsm mem region is missing\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		dev_err(&pdev->dev, "address to resource failed for mpss dsm mem\n");
		return ret;
	}

	mem_phys = res.start;
	mem_size = resource_size(&res);
	ret = hyp_assign_phys(mem_phys, mem_size, hlosvm, 1, mssvm, vmperm, 1);
	if (ret) {
		dev_err(&pdev->dev, "hyp assign for mpss dsm mem failed\n");
		return ret;
	}

	mpss_dsm_mem_setup = true;
	return 0;
}

static int adsp_probe(struct platform_device *pdev)
{
	const struct adsp_data *desc;
	struct qcom_adsp *adsp;
	struct rproc *rproc;
	const char *fw_name;
	const struct rproc_ops *ops = &adsp_ops;
	char md_dev_name[32];
	int ret;
	bool signal_aop;

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	if (!qcom_scm_is_available())
		return -EPROBE_DEFER;

	fw_name = desc->firmware_name;
	ret = of_property_read_string(pdev->dev.of_node, "firmware-name",
				      &fw_name);
	if (ret < 0 && ret != -EINVAL)
		return ret;

	if (desc->needs_dsm_mem_setup && !mpss_dsm_mem_setup &&
			!strcmp(fw_name, "modem.mdt")) {
		ret = setup_mpss_dsm_mem(pdev);
		if (ret) {
			dev_err(&pdev->dev, "failed to setup mpss dsm mem\n");
			return -EINVAL;
		}
	}

	if (desc->minidump_id)
		ops = &adsp_minidump_ops;

	rproc = rproc_alloc(&pdev->dev, pdev->name, ops, fw_name, sizeof(*adsp));

	if (!rproc) {
		dev_err(&pdev->dev, "unable to allocate remoteproc\n");
		return -ENOMEM;
	}

	rproc->recovery_disabled = true;
	rproc->auto_boot = desc->auto_boot;
	if (desc->uses_elf64)
		rproc_coredump_set_elf_info(rproc, ELFCLASS64, EM_NONE);
	else
		rproc_coredump_set_elf_info(rproc, ELFCLASS32, EM_NONE);

	adsp = (struct qcom_adsp *)rproc->priv;
	adsp->dev = &pdev->dev;
	adsp->rproc = rproc;
	adsp->minidump_id = desc->minidump_id;
	adsp->pas_id = desc->pas_id;
	adsp->dtb_pas_id = desc->dtb_pas_id;
	adsp->dtb_fw_name = desc->dtb_firmware_name;
	adsp->has_aggre2_clk = desc->has_aggre2_clk;
	adsp->info_name = desc->sysmon_name;
	adsp->qmp_name = desc->qmp_name;
	adsp->dma_phys_below_32b = desc->dma_phys_below_32b;

	if (desc->free_after_auth_reset) {
		adsp->mdata = devm_kzalloc(adsp->dev, sizeof(struct qcom_mdt_metadata), GFP_KERNEL);
		adsp->retry_shutdown = true;
	}
	platform_set_drvdata(pdev, adsp);

	ret = device_init_wakeup(adsp->dev, true);
	if (ret)
		goto free_rproc;

	ret = adsp_alloc_memory_region(adsp);
	if (ret)
		goto deinit_wakeup_source;

	ret = adsp_setup_32b_dma_allocs(adsp);
	if (ret)
		goto deinit_wakeup_source;

	ret = adsp_init_clock(adsp);
	if (ret)
		goto deinit_wakeup_source;

	ret = adsp_init_regulator(adsp);
	if (ret)
		goto deinit_wakeup_source;

	adsp_init_bus_scaling(adsp);

	ret = adsp_pds_attach(&pdev->dev, adsp->active_pds,
			      desc->active_pd_names);
	if (ret < 0)
		goto deinit_wakeup_source;
	adsp->active_pd_count = ret;

	ret = adsp_pds_attach(&pdev->dev, adsp->proxy_pds,
			      desc->proxy_pd_names);
	if (ret < 0)
		goto detach_active_pds;
	adsp->proxy_pd_count = ret;

	signal_aop = of_property_read_bool(pdev->dev.of_node,
			"qcom,signal-aop");

	if (signal_aop) {
		adsp->qmp = qmp_get(adsp->dev);
		if (IS_ERR_OR_NULL(adsp->qmp))
			goto detach_proxy_pds;
	}

	ret = qcom_q6v5_init(&adsp->q6v5, pdev, rproc, desc->crash_reason_smem,
			     qcom_pas_handover);

	if (ret)
		goto detach_proxy_pds;

	qcom_q6v5_register_ssr_subdev(&adsp->q6v5, &adsp->ssr_subdev.subdev);

	if (adsp->q6v5.rmb_base &&
			readl_relaxed(adsp->q6v5.rmb_base + RMB_Q6_BOOT_STATUS_REG))
		rproc->state = RPROC_DETACHED;

	timeout_disabled = qcom_pil_timeouts_disabled();
	qcom_add_glink_subdev(rproc, &adsp->glink_subdev, desc->ssr_name);
	qcom_add_smd_subdev(rproc, &adsp->smd_subdev);
	adsp->sysmon = qcom_add_sysmon_subdev(rproc,
					      desc->sysmon_name,
					      desc->ssctl_id);
	if (IS_ERR(adsp->sysmon)) {
		ret = PTR_ERR(adsp->sysmon);
		goto detach_proxy_pds;
	}

	qcom_add_ssr_subdev(rproc, &adsp->ssr_subdev, desc->ssr_name);
	ret = device_create_file(adsp->dev, &dev_attr_txn_id);
	if (ret)
		goto remove_subdevs;

	snprintf(md_dev_name, ARRAY_SIZE(md_dev_name), "%s-md", pdev->dev.of_node->name);
	adsp->minidump_dev = qcom_create_ramdump_device(md_dev_name, NULL);
	if (!adsp->minidump_dev)
		dev_err(&pdev->dev, "Unable to create %s minidump device.\n", md_dev_name);

	adsp->adsp_wq = alloc_workqueue("ssr_wq",
		WQ_UNBOUND | WQ_HIGHPRI | WQ_CPU_INTENSIVE, 0);
	BUG_ON(!adsp->adsp_wq);
	INIT_WORK(&adsp->ssr_handler, adsp_ssr_handler_work);
	ret = device_create_file(adsp->dev, &dev_attr_ssr);
	if (ret)
		goto destroy_minidump_dev;

	ret = rproc_add(rproc);
	if (ret)
		goto remove_attr_ssr;

	return 0;
remove_attr_ssr:
	device_remove_file(adsp->dev, &dev_attr_ssr);

destroy_minidump_dev:
	if (adsp->minidump_dev)
		qcom_destroy_ramdump_device(adsp->minidump_dev);

	device_remove_file(adsp->dev, &dev_attr_txn_id);
remove_subdevs:
	qcom_remove_sysmon_subdev(adsp->sysmon);
detach_proxy_pds:
	adsp_pds_detach(adsp, adsp->proxy_pds, adsp->proxy_pd_count);
detach_active_pds:
	adsp_pds_detach(adsp, adsp->active_pds, adsp->active_pd_count);
deinit_wakeup_source:
	device_init_wakeup(adsp->dev, false);
free_rproc:
	rproc_free(rproc);

	return ret;
}

static int adsp_remove(struct platform_device *pdev)
{
	struct qcom_adsp *adsp = platform_get_drvdata(pdev);

	flush_workqueue(adsp->adsp_wq);
	destroy_workqueue(adsp->adsp_wq);

	rproc_del(adsp->rproc);
	if (adsp->minidump_dev)
		qcom_destroy_ramdump_device(adsp->minidump_dev);
	device_remove_file(adsp->dev, &dev_attr_ssr);
	device_remove_file(adsp->dev, &dev_attr_txn_id);
	qcom_remove_glink_subdev(adsp->rproc, &adsp->glink_subdev);
	qcom_remove_sysmon_subdev(adsp->sysmon);
	qcom_remove_smd_subdev(adsp->rproc, &adsp->smd_subdev);
	qcom_remove_ssr_subdev(adsp->rproc, &adsp->ssr_subdev);
	device_init_wakeup(adsp->dev, false);
	rproc_free(adsp->rproc);

	return 0;
}

static const struct adsp_data adsp_resource_init = {
		.crash_reason_smem = 423,
		.firmware_name = "adsp.mdt",
		.pas_id = 1,
		.has_aggre2_clk = false,
		.auto_boot = true,
		.ssr_name = "lpass",
		.sysmon_name = "adsp",
		.ssctl_id = 0x14,
};

static const struct adsp_data sm8150_adsp_resource = {
		.crash_reason_smem = 423,
		.firmware_name = "adsp.mdt",
		.pas_id = 1,
		.minidump_id = 5,
		.uses_elf64 = true,
		.has_aggre2_clk = false,
		.auto_boot = true,
		.ssr_name = "lpass",
		.sysmon_name = "adsp",
		.qmp_name = "adsp",
		.ssctl_id = 0x14,
};

static const struct adsp_data sm8150_mpss_resource = {
	.crash_reason_smem = 421,
	.firmware_name = "modem.mdt",
	.pas_id = 4,
	.minidump_id = 3,
	.has_aggre2_clk = false,
	.auto_boot = true,
	.ssr_name = "mpss",
	.qmp_name = "modem",
	.sysmon_name = "modem",
	.ssctl_id = 0x12,
};

static const struct adsp_data sm8250_adsp_resource = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.mdt",
	.pas_id = 1,
	.has_aggre2_clk = false,
	.auto_boot = true,
	.active_pd_names = (char*[]){
		"load_state",
		NULL
	},
	.proxy_pd_names = (char*[]){
		"lcx",
		"lmx",
		NULL
	},
	.ssr_name = "lpass",
	.sysmon_name = "adsp",
	.ssctl_id = 0x14,
};

static const struct adsp_data sm8350_adsp_resource = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.mdt",
	.pas_id = 1,
	.has_aggre2_clk = false,
	.auto_boot = true,
	.active_pd_names = (char*[]){
		"load_state",
		NULL
	},
	.proxy_pd_names = (char*[]){
		"lcx",
		"lmx",
		NULL
	},
	.ssr_name = "lpass",
	.sysmon_name = "adsp",
	.ssctl_id = 0x14,
};

static const struct adsp_data waipio_adsp_resource = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.mdt",
	.pas_id = 1,
	.minidump_id = 5,
	.uses_elf64 = true,
	.has_aggre2_clk = false,
	.auto_boot = false,
	.ssr_name = "lpass",
	.sysmon_name = "adsp",
	.qmp_name = "adsp",
	.ssctl_id = 0x14,
};

static const struct adsp_data kalama_adsp_resource = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.mdt",
	.dtb_firmware_name = "adsp_dtb.mdt",
	.pas_id = 1,
	.dtb_pas_id = 0x24,
	.minidump_id = 5,
	.uses_elf64 = true,
	.has_aggre2_clk = false,
	.auto_boot = false,
	.ssr_name = "lpass",
	.sysmon_name = "adsp",
	.qmp_name = "adsp",
	.ssctl_id = 0x14,
};

static const struct adsp_data khaje_adsp_resource = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.mdt",
	.pas_id = 1,
	.minidump_id = 5,
	.uses_elf64 = false,
	.ssr_name = "lpass",
	.sysmon_name = "adsp",
	.ssctl_id = 0x14,
};

static const struct adsp_data msm8998_adsp_resource = {
		.crash_reason_smem = 423,
		.firmware_name = "adsp.mdt",
		.pas_id = 1,
		.has_aggre2_clk = false,
		.auto_boot = true,
		.proxy_pd_names = (char*[]){
			"cx",
			NULL
		},
		.ssr_name = "lpass",
		.sysmon_name = "adsp",
		.ssctl_id = 0x14,
};

static const struct adsp_data cdsp_resource_init = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.pas_id = 18,
	.has_aggre2_clk = false,
	.auto_boot = true,
	.ssr_name = "cdsp",
	.sysmon_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct adsp_data sm8150_cdsp_resource = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.pas_id = 18,
	.has_aggre2_clk = false,
	.auto_boot = true,
	.ssr_name = "cdsp",
	.sysmon_name = "cdsp",
	.qmp_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct adsp_data sm8250_cdsp_resource = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.pas_id = 18,
	.has_aggre2_clk = false,
	.auto_boot = true,
	.active_pd_names = (char*[]){
		"load_state",
		NULL
	},
	.proxy_pd_names = (char*[]){
		"cx",
		NULL
	},
	.ssr_name = "cdsp",
	.sysmon_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct adsp_data sm8350_cdsp_resource = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.pas_id = 18,
	.has_aggre2_clk = false,
	.auto_boot = true,
	.active_pd_names = (char*[]){
		"load_state",
		NULL
	},
	.proxy_pd_names = (char*[]){
		"cx",
		"mxc",
		NULL
	},
	.ssr_name = "cdsp",
	.sysmon_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct adsp_data waipio_cdsp_resource = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.pas_id = 18,
	.minidump_id = 7,
	.uses_elf64 = true,
	.has_aggre2_clk = false,
	.auto_boot = false,
	.ssr_name = "cdsp",
	.sysmon_name = "cdsp",
	.qmp_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct adsp_data kalama_cdsp_resource = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.dtb_firmware_name = "cdsp_dtb.mdt",
	.pas_id = 18,
	.dtb_pas_id = 0x25,
	.minidump_id = 7,
	.uses_elf64 = true,
	.has_aggre2_clk = false,
	.auto_boot = false,
	.ssr_name = "cdsp",
	.sysmon_name = "cdsp",
	.qmp_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct adsp_data khaje_cdsp_resource = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.pas_id = 18,
	.minidump_id = 7,
	.uses_elf64 = false,
	.ssr_name = "cdsp",
	.sysmon_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct adsp_data mpss_resource_init = {
	.crash_reason_smem = 421,
	.firmware_name = "modem.mdt",
	.pas_id = 4,
	.minidump_id = 3,
	.has_aggre2_clk = false,
	.auto_boot = false,
	.active_pd_names = (char*[]){
		"load_state",
		NULL
	},
	.proxy_pd_names = (char*[]){
		"cx",
		"mss",
		NULL
	},
	.ssr_name = "mpss",
	.sysmon_name = "modem",
	.ssctl_id = 0x12,
};

static const struct adsp_data waipio_mpss_resource = {
	.crash_reason_smem = 421,
	.firmware_name = "modem.mdt",
	.pas_id = 4,
	.free_after_auth_reset = true,
	.minidump_id = 3,
	.uses_elf64 = true,
	.has_aggre2_clk = false,
	.auto_boot = false,
	.ssr_name = "mpss",
	.sysmon_name = "modem",
	.qmp_name = "modem",
	.ssctl_id = 0x12,
};

static const struct adsp_data kalama_mpss_resource = {
	.crash_reason_smem = 421,
	.firmware_name = "modem.mdt",
	.dtb_firmware_name = "modem_dtb.mdt",
	.pas_id = 4,
	.dtb_pas_id = 0x26,
	.free_after_auth_reset = true,
	.minidump_id = 3,
	.uses_elf64 = true,
	.has_aggre2_clk = false,
	.auto_boot = false,
	.needs_dsm_mem_setup = true,
	.ssr_name = "mpss",
	.sysmon_name = "modem",
	.qmp_name = "modem",
	.ssctl_id = 0x12,
	.dma_phys_below_32b = true,
};

static const struct adsp_data cinder_mpss_resource = {
	.crash_reason_smem = 421,
	.firmware_name = "modem.mdt",
	.dtb_firmware_name = "modem_dtb.mdt",
	.pas_id = 4,
	.dtb_pas_id = 0x26,
	.free_after_auth_reset = true,
	.minidump_id = 3,
	.uses_elf64 = true,
	.has_aggre2_clk = false,
	.auto_boot = false,
	.ssr_name = "mpss",
	.sysmon_name = "modem",
	.qmp_name = "modem",
	.ssctl_id = 0x12,
	.dma_phys_below_32b = true,
};

static const struct adsp_data khaje_mpss_resource = {
	.crash_reason_smem = 421,
	.firmware_name = "modem.mdt",
	.pas_id = 4,
	.free_after_auth_reset = true,
	.minidump_id = 3,
	.uses_elf64 = true,
	.ssr_name = "mpss",
	.sysmon_name = "modem",
	.ssctl_id = 0x12,
};

static const struct adsp_data slpi_resource_init = {
		.crash_reason_smem = 424,
		.firmware_name = "slpi.mdt",
		.pas_id = 12,
		.has_aggre2_clk = true,
		.auto_boot = true,
		.ssr_name = "dsps",
		.sysmon_name = "slpi",
		.ssctl_id = 0x16,
};

static const struct adsp_data sm8150_slpi_resource = {
		.crash_reason_smem = 424,
		.firmware_name = "slpi.mdt",
		.pas_id = 12,
		.has_aggre2_clk = false,
		.auto_boot = true,
		.active_pd_names = (char*[]){
			"load_state",
			NULL
		},
		.proxy_pd_names = (char*[]){
			"lcx",
			"lmx",
			NULL
		},
		.ssr_name = "dsps",
		.sysmon_name = "slpi",
		.ssctl_id = 0x16,
};

static const struct adsp_data sm8250_slpi_resource = {
	.crash_reason_smem = 424,
	.firmware_name = "slpi.mdt",
	.pas_id = 12,
	.has_aggre2_clk = false,
	.auto_boot = true,
	.active_pd_names = (char*[]){
		"load_state",
		NULL
	},
	.proxy_pd_names = (char*[]){
		"lcx",
		"lmx",
		NULL
	},
	.ssr_name = "dsps",
	.sysmon_name = "slpi",
	.ssctl_id = 0x16,
};

static const struct adsp_data sm8350_slpi_resource = {
	.crash_reason_smem = 424,
	.firmware_name = "slpi.mdt",
	.pas_id = 12,
	.has_aggre2_clk = false,
	.auto_boot = true,
	.active_pd_names = (char*[]){
		"load_state",
		NULL
	},
	.proxy_pd_names = (char*[]){
		"lcx",
		"lmx",
		NULL
	},
	.ssr_name = "dsps",
	.sysmon_name = "slpi",
	.ssctl_id = 0x16,
};

static const struct adsp_data waipio_slpi_resource = {
	.crash_reason_smem = 424,
	.firmware_name = "slpi.mdt",
	.pas_id = 12,
	.has_aggre2_clk = false,
	.auto_boot = false,
	.ssr_name = "dsps",
	.sysmon_name = "slpi",
	.qmp_name = "slpi",
	.ssctl_id = 0x16,
};

static const struct adsp_data msm8998_slpi_resource = {
		.crash_reason_smem = 424,
		.firmware_name = "slpi.mdt",
		.pas_id = 12,
		.has_aggre2_clk = true,
		.auto_boot = true,
		.proxy_pd_names = (char*[]){
			"ssc_cx",
			NULL
		},
		.ssr_name = "dsps",
		.sysmon_name = "slpi",
		.ssctl_id = 0x16,
};

static const struct adsp_data wcss_resource_init = {
	.crash_reason_smem = 421,
	.firmware_name = "wcnss.mdt",
	.pas_id = 6,
	.auto_boot = true,
	.ssr_name = "mpss",
	.sysmon_name = "wcnss",
	.ssctl_id = 0x12,
};

static const struct adsp_data sdx55_mpss_resource = {
	.crash_reason_smem = 421,
	.firmware_name = "modem.mdt",
	.pas_id = 4,
	.has_aggre2_clk = false,
	.auto_boot = true,
	.proxy_pd_names = (char*[]){
		"cx",
		"mss",
		NULL
	},
	.ssr_name = "mpss",
	.sysmon_name = "modem",
	.ssctl_id = 0x22,
};

static const struct adsp_data sc8180x_mpss_resource = {
	.crash_reason_smem = 421,
	.firmware_name = "modem.mdt",
	.pas_id = 4,
	.has_aggre2_clk = false,
	.auto_boot = false,
	.active_pd_names = (char*[]){
		"load_state",
		NULL
	},
	.proxy_pd_names = (char*[]){
		"cx",
		NULL
	},
	.ssr_name = "mpss",
	.sysmon_name = "modem",
	.ssctl_id = 0x12,
};

static const struct adsp_data sdmshrike_adsp_resource = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.mdt",
	.pas_id = 1,
	.has_aggre2_clk = false,
	.auto_boot = true,
	.ssr_name = "lpass",
	.sysmon_name = "adsp",
	.qmp_name = "adsp",
	.ssctl_id = 0x14,
};

static const struct adsp_data sdmshrike_cdsp_resource = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.pas_id = 18,
	.has_aggre2_clk = false,
	.auto_boot = true,
	.ssr_name = "lpass",
	.sysmon_name = "cdsp",
	.qmp_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct adsp_data scuba_auto_mpss_resource = {
	.crash_reason_smem = 421,
        .firmware_name = "modem.mdt",
        .pas_id = 4,
        .free_after_auth_reset = true,
        .minidump_id = 3,
        .uses_elf64 = true,
        .has_aggre2_clk = false,
        .auto_boot = false,
        .ssr_name = "mpss",
        .sysmon_name = "modem",
        .qmp_name = "modem",
        .ssctl_id = 0x12,
};

static const struct adsp_data monaco_adsp_resource = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.mdt",
	.pas_id = 1,
	.minidump_id = 5,
	.uses_elf64 = false,
	.ssr_name = "lpass",
	.sysmon_name = "adsp",
	.ssctl_id = 0x14,
};

static const struct adsp_data monaco_modem_resource = {
	.crash_reason_smem = 421,
	.firmware_name = "modem.mdt",
	.pas_id = 4,
	.free_after_auth_reset = true,
	.minidump_id = 3,
	.uses_elf64 = true,
	.ssr_name = "mpss",
	.sysmon_name = "modem",
	.ssctl_id = 0x12,
};

static const struct adsp_data scuba_auto_lpass_resource = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.mdt",
	.pas_id = 1,
	.free_after_auth_reset = true,
	.minidump_id = 5,
	.uses_elf64 = true,
	.has_aggre2_clk = false,
	.auto_boot = false,
	.ssr_name = "lpass",
	.sysmon_name = "adsp",
	.qmp_name = "adsp",
	.ssctl_id = 0x14,
};

static const struct adsp_data lemans_adsp_resource = {
	.crash_reason_smem = 2,
	.firmware_name = "adsp.mdt",
	.pas_id = 1,
	.uses_elf64 = true,
	.has_aggre2_clk = false,
	.auto_boot = false,
	.ssr_name = "lpass",
	.sysmon_name = "adsp",
	.qmp_name = "adsp",
	.ssctl_id = 0x14,
};

static const struct adsp_data lemans_cdsp_resource = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp0.mdt",
	.pas_id = 18,
	.uses_elf64 = true,
	.has_aggre2_clk = false,
	.auto_boot = false,
	.ssr_name = "cdsp",
	.sysmon_name = "cdsp",
	.qmp_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct adsp_data lemans_cdsp1_resource = {
	.crash_reason_smem = 633,
	.firmware_name = "cdsp1.mdt",
	.pas_id = 30,
	.uses_elf64 = true,
	.has_aggre2_clk = false,
	.auto_boot = false,
	.ssr_name = "cdsp1",
	.sysmon_name = "cdsp1",
	.qmp_name = "cdsp1",
	.ssctl_id = 0x20,
};

static const struct adsp_data lemans_gpdsp0_resource = {
	.crash_reason_smem = 640,
	.firmware_name = "gpdsp0.mdt",
	.pas_id = 39,
	.uses_elf64 = true,
	.has_aggre2_clk = false,
	.auto_boot = false,
	.ssr_name = "gpdsp0",
	.sysmon_name = "gpdsp0",
	.qmp_name = "gpdsp0",
	.ssctl_id = 0x21,
};

static const struct adsp_data lemans_gpdsp1_resource = {
	.crash_reason_smem = 641,
	.firmware_name = "gpdsp1.mdt",
	.pas_id = 40,
	.uses_elf64 = true,
	.has_aggre2_clk = false,
	.auto_boot = false,
	.ssr_name = "gpdsp1",
	.sysmon_name = "gpdsp1",
	.qmp_name = "gpdsp1",
	.ssctl_id = 0x22,
};

static const struct adsp_data kona_adsp_resource = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.mdt",
	.pas_id = 1,
	.has_aggre2_clk = false,
	.auto_boot = true,
	.ssr_name = "lpass",
	.sysmon_name = "adsp",
	.qmp_name = "adsp",
	.ssctl_id = 0x14,
};

static const struct adsp_data kona_cdsp_resource = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.pas_id = 18,
	.has_aggre2_clk = false,
	.auto_boot = true,
	.ssr_name = "cdsp",
	.sysmon_name = "cdsp",
	.qmp_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct adsp_data kona_slpi_resource = {
	.crash_reason_smem = 424,
	.firmware_name = "slpi.mdt",
	.pas_id = 12,
	.has_aggre2_clk = false,
	.auto_boot = false,
	.ssr_name = "dsps",
	.sysmon_name = "slpi",
	.qmp_name = "slpi",
	.ssctl_id = 0x16,
};

static const struct of_device_id adsp_of_match[] = {
	{ .compatible = "qcom,msm8974-adsp-pil", .data = &adsp_resource_init},
	{ .compatible = "qcom,msm8996-adsp-pil", .data = &adsp_resource_init},
	{ .compatible = "qcom,msm8996-slpi-pil", .data = &slpi_resource_init},
	{ .compatible = "qcom,msm8998-adsp-pas", .data = &msm8998_adsp_resource},
	{ .compatible = "qcom,msm8998-slpi-pas", .data = &msm8998_slpi_resource},
	{ .compatible = "qcom,qcs404-adsp-pas", .data = &adsp_resource_init },
	{ .compatible = "qcom,qcs404-cdsp-pas", .data = &cdsp_resource_init },
	{ .compatible = "qcom,qcs404-wcss-pas", .data = &wcss_resource_init },
	{ .compatible = "qcom,sc7180-mpss-pas", .data = &mpss_resource_init},
	{ .compatible = "qcom,sc8180x-adsp-pas", .data = &sm8150_adsp_resource},
	{ .compatible = "qcom,sc8180x-cdsp-pas", .data = &sm8150_cdsp_resource},
	{ .compatible = "qcom,sc8180x-mpss-pas", .data = &sc8180x_mpss_resource},
	{ .compatible = "qcom,sdm660-adsp-pas", .data = &adsp_resource_init},
	{ .compatible = "qcom,sdm845-adsp-pas", .data = &adsp_resource_init},
	{ .compatible = "qcom,sdm845-cdsp-pas", .data = &cdsp_resource_init},
	{ .compatible = "qcom,sdx55-mpss-pas", .data = &sdx55_mpss_resource},
	{ .compatible = "qcom,sm8150-adsp-pas", .data = &sm8150_adsp_resource},
	{ .compatible = "qcom,sm8150-cdsp-pas", .data = &sm8150_cdsp_resource},
	{ .compatible = "qcom,sm8150-mpss-pas", .data = &sm8150_mpss_resource},
	{ .compatible = "qcom,sm8150-slpi-pas", .data = &sm8150_slpi_resource},
	{ .compatible = "qcom,sm8250-adsp-pas", .data = &sm8250_adsp_resource},
	{ .compatible = "qcom,sm8250-cdsp-pas", .data = &sm8250_cdsp_resource},
	{ .compatible = "qcom,sm8250-slpi-pas", .data = &sm8250_slpi_resource},
	{ .compatible = "qcom,sm8350-adsp-pas", .data = &sm8350_adsp_resource},
	{ .compatible = "qcom,sm8350-cdsp-pas", .data = &sm8350_cdsp_resource},
	{ .compatible = "qcom,sm8350-slpi-pas", .data = &sm8350_slpi_resource},
	{ .compatible = "qcom,sm8350-mpss-pas", .data = &mpss_resource_init},
	{ .compatible = "qcom,waipio-adsp-pas", .data = &waipio_adsp_resource},
	{ .compatible = "qcom,waipio-cdsp-pas", .data = &waipio_cdsp_resource},
	{ .compatible = "qcom,waipio-slpi-pas", .data = &waipio_slpi_resource},
	{ .compatible = "qcom,waipio-modem-pas", .data = &waipio_mpss_resource},
	{ .compatible = "qcom,kalama-adsp-pas", .data = &kalama_adsp_resource},
	{ .compatible = "qcom,kalama-cdsp-pas", .data = &kalama_cdsp_resource},
	{ .compatible = "qcom,kalama-modem-pas", .data = &kalama_mpss_resource},
	{ .compatible = "qcom,cinder-modem-pas", .data = &cinder_mpss_resource},
	{ .compatible = "qcom,khaje-adsp-pas", .data = &khaje_adsp_resource},
	{ .compatible = "qcom,khaje-cdsp-pas", .data = &khaje_cdsp_resource},
	{ .compatible = "qcom,khaje-modem-pas", .data = &khaje_mpss_resource},
	{ .compatible = "qcom,sdmshrike-adsp-pas", .data = &sdmshrike_adsp_resource},
	{ .compatible = "qcom,sdmshrike-cdsp-pas", .data = &sdmshrike_cdsp_resource},
	{ .compatible = "qcom,scuba_auto-modem-pas", .data = &scuba_auto_mpss_resource},
	{ .compatible = "qcom,scuba_auto-lpass-pas", .data = &scuba_auto_lpass_resource},
	{ .compatible = "qcom,monaco-adsp-pas", .data = &monaco_adsp_resource},
	{ .compatible = "qcom,monaco-modem-pas", .data = &monaco_modem_resource},
	{ .compatible = "qcom,lemans-adsp-pas", .data = &lemans_adsp_resource},
	{ .compatible = "qcom,lemans-cdsp-pas", .data = &lemans_cdsp_resource},
	{ .compatible = "qcom,lemans-cdsp1-pas", .data = &lemans_cdsp1_resource},
	{ .compatible = "qcom,lemans-gpdsp0-pas", .data = &lemans_gpdsp0_resource},
	{ .compatible = "qcom,lemans-gpdsp1-pas", .data = &lemans_gpdsp1_resource},
	{ .compatible = "qcom,kona-adsp-pas", .data = &kona_adsp_resource},
	{ .compatible = "qcom,kona-cdsp-pas", .data = &kona_cdsp_resource},
	{ .compatible = "qcom,kona-slpi-pas", .data = &kona_slpi_resource},
	{ },
};
MODULE_DEVICE_TABLE(of, adsp_of_match);

static struct platform_driver adsp_driver = {
	.probe = adsp_probe,
	.remove = adsp_remove,
	.driver = {
		.name = "qcom_q6v5_pas",
		.of_match_table = adsp_of_match,
	},
};

module_platform_driver(adsp_driver);
MODULE_DESCRIPTION("Qualcomm Hexagon v5 Peripheral Authentication Service driver");
MODULE_LICENSE("GPL v2");
