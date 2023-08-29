// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <drm/drm_edid.h>
#include <linux/string.h>
#if defined(CONFIG_SECDP_BIGDATA)
#include <linux/secdp_bigdata.h>
#endif

#include "dp_debug.h"
#include "secdp.h"
#include "secdp_sysfs.h"
#include "sde_edid_parser.h"
#include "secdp_unit_test.h"

enum secdp_unit_test_cmd {
	SECDP_UTCMD_EDID_PARSE = 0,
};

struct secdp_sysfs_private {
	struct device *dev;
	struct secdp_misc *sec;
	struct secdp_sysfs dp_sysfs;
	enum secdp_unit_test_cmd test_cmd;
};

static struct secdp_sysfs_private *g_secdp_sysfs;

static inline char *secdp_utcmd_to_str(u32 cmd_type)
{
	switch (cmd_type) {
	case SECDP_UTCMD_EDID_PARSE:
		return SECDP_ENUM_STR(SECDP_UTCMD_EDID_PARSE);
	default:
		return "unknown";
	}
}

/** check if buf has range('-') format
 * @buf		buf to be checked
 * @size	buf size
 * @retval	0 if args are ok, -1 if '-' included
 */
static int secdp_check_store_args(const char *buf, size_t size)
{
	int ret;

	if (strnchr(buf, size, '-')) {
		DP_ERR("range is forbidden!\n");
		ret = -1;
		goto exit;
	}

	ret = 0;
exit:
	return ret;
}

#if IS_ENABLED(CONFIG_SEC_FACTORY)
static ssize_t dp_sbu_sw_sel_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	int val[10] = {0,};
	int sbu_sw_sel, sbu_sw_oe;

	if (secdp_check_store_args(buf, size)) {
		DP_ERR("args error!\n");
		goto exit;
	}

	get_options(buf, ARRAY_SIZE(val), val);

	sbu_sw_sel = val[1];
	sbu_sw_oe = val[2];
	DP_INFO("sbu_sw_sel(%d), sbu_sw_oe(%d)\n", sbu_sw_sel, sbu_sw_oe);

	if (sbu_sw_oe == 0/*on*/)
		secdp_config_gpios_factory(sbu_sw_sel, true);
	else if (sbu_sw_oe == 1/*off*/)
		secdp_config_gpios_factory(sbu_sw_sel, false);
	else
		DP_ERR("unknown sbu_sw_oe value: %d\n", sbu_sw_oe);

exit:
	return size;
}

static CLASS_ATTR_WO(dp_sbu_sw_sel);
#endif

#define SECDP_DEX_ADAPTER_SKIP	"SkipAdapterCheck"

static ssize_t dex_show(struct class *class,
				struct class_attribute *attr, char *buf)
{
	struct secdp_sysfs_private *sysfs = g_secdp_sysfs;
	struct secdp_dex *dex = &sysfs->sec->dex;
	int rc = 0;

	if (!secdp_get_cable_status() || !secdp_get_hpd_status() ||
			secdp_get_poor_connection_status() ||
			!secdp_get_link_train_status()) {
		DP_INFO("cable is out\n");
		dex->prev = dex->curr = dex->status = DEX_DISABLED;
	}

	DP_INFO("prev:%d curr:%d status:%d %s:%d\n",
			dex->prev, dex->curr, dex->status,
			SECDP_DEX_ADAPTER_SKIP, secdp_dex_adapter_skip_show());
	rc = scnprintf(buf, PAGE_SIZE, "%d\n", dex->status);

	if (dex->status == DEX_MODE_CHANGING)
		dex->status = dex->curr;

	return rc;
}

/*
 * assume that 1 HMD device has name(14),vid(4),pid(4) each, then
 * max 32 HMD devices(name,vid,pid) need 806 bytes including TAG, NUM, comba
 */
#define MAX_DEX_STORE_LEN	1024

static ssize_t dex_store(struct class *class,
		struct class_attribute *attr, const char *buf, size_t size)
{
	char str[MAX_DEX_STORE_LEN] = {0,}, *p, *tok;
	int len, val[4] = {0,};
	int setting_ui;	/* setting has Dex mode? if yes, 1. otherwise 0 */
	int run;	/* dex is running now?   if yes, 1. otherwise 0 */

	struct secdp_sysfs_private *sysfs = g_secdp_sysfs;
	struct secdp_misc *sec = sysfs->sec;
	struct secdp_dex *dex = &sec->dex;
	struct secdp_pdic_noti *pdic_noti = &sec->pdic_noti;

	if (secdp_get_lpm_mode()) {
		DP_INFO("LPM mode. skip\n");
		goto exit;
	}

	if (size >= MAX_DEX_STORE_LEN) {
		DP_ERR("too long args! %lu\n", size);
		goto exit;
	}

	if (secdp_check_store_args(buf, size)) {
		DP_ERR("args error!\n");
		goto exit;
	}

	mutex_lock(&sec->hmd.lock);
	memcpy(str, buf, size);
	p   = str;
	tok = strsep(&p, ",");
	len = strlen(tok);
	//DP_DEBUG("tok:%s len:%d\n", tok, len);

	if (len && !strncmp(DEX_TAG_HMD, tok, len)) {
		int num_hmd = 0, sz = 0, ret;

		tok = strsep(&p, ",");
		if (!tok) {
			DP_ERR("wrong input!\n");
			mutex_unlock(&sec->hmd.lock);
			goto exit;
		}
		sz  = strlen(tok);
		ret = kstrtouint(tok, 10, &num_hmd);
		DP_DEBUG("[%s] num:%d sz:%d ret:%d\n", DEX_TAG_HMD,
			num_hmd, sz, ret);
		if (!ret) {
			ret = secdp_store_hmd_dev(str + (len + sz + 2),
					size - (len + sz + 2), num_hmd);
		}
		if (ret)
			size = ret;	/* error! */

		mutex_unlock(&sec->hmd.lock);
		goto exit;
	}
	mutex_unlock(&sec->hmd.lock);

	if (len && !strncmp(SECDP_DEX_ADAPTER_SKIP, tok, len)) {
		int param = 0, sz = 0, ret;

		tok = strsep(&p, ",");
		if (!tok) {
			DP_ERR("wrong input!\n");
			goto exit;
		}
		sz  = strlen(tok);
		ret = kstrtouint(tok, 2, &param);
		if (ret) {
			DP_ERR("error:%d\n", ret);
			goto exit;
		}

		DP_DEBUG("[%s] param:%d sz:%d ret:%d\n", SECDP_DEX_ADAPTER_SKIP,
			param, sz, ret);

		secdp_dex_adapter_skip_store((!param) ? false : true);
		goto exit;
	}

	get_options(buf, ARRAY_SIZE(val), val);
	setting_ui = (val[1] & 0xf0) >> 4;
	run = (val[1] & 0x0f);

	DP_INFO("0x%02x setting_ui:%d run:%d cable:%d\n", val[1],
		setting_ui, run, sec->cable_connected);

	dex->setting_ui = setting_ui;
	dex->status = dex->curr = run;

	mutex_lock(&sec->notifier_lock);
	if (!pdic_noti->registered) {
		int rc;

		/* cancel immediately */
		rc = cancel_delayed_work(&pdic_noti->reg_work);
		DP_DEBUG("notifier get registered by dex, cancel:%d\n", rc);
		destroy_delayed_work_on_stack(&pdic_noti->reg_work);

		/* register */
		rc = secdp_pdic_noti_register_ex(sec, false);
		if (rc)
			DP_ERR("noti register fail, rc:%d\n", rc);

		mutex_unlock(&sec->notifier_lock);
		goto exit;
	}
	mutex_unlock(&sec->notifier_lock);

	if (!secdp_get_cable_status() || !secdp_get_hpd_status() ||
			secdp_get_poor_connection_status() ||
			!secdp_get_link_train_status()) {
		DP_INFO("cable is out\n");
		dex->prev = dex->curr = dex->status = DEX_DISABLED;
		goto exit;
	}

	if (sec->hpd.noti_deferred) {
		secdp_send_deferred_hpd_noti();
		dex->prev = dex->setting_ui;
		goto exit;
	}

	if (dex->curr == dex->prev) {
		DP_INFO("dex is already %s\n",
			(dex->curr == DEX_ENABLED) ? "enabled" :
			((dex->curr == DEX_DISABLED) ? "disabled" : "changing"));
		goto exit;
	}

	if (dex->curr != dex->setting_ui) {
		DP_INFO("curr and setting_ui are different: %d %d\n",
			dex->curr, dex->setting_ui);
		goto exit;
	}

#if defined(CONFIG_SECDP_BIGDATA)
	if (run)
		secdp_bigdata_save_item(BD_DP_MODE, "DEX");
	else
		secdp_bigdata_save_item(BD_DP_MODE, "MIRROR");
#endif

	if (sec->dex.res == DEX_RES_NOT_SUPPORT) {
		DP_DEBUG("this dongle does not support dex\n");
		goto exit;
	}

	if (!secdp_check_reconnect()) {
		DP_INFO("not need reconnect\n");
		goto exit;
	}

	secdp_reconnect();
	dex->prev = run;
exit:
	return size;
}

static CLASS_ATTR_RW(dex);

static ssize_t dex_ver_show(struct class *class,
				struct class_attribute *attr, char *buf)
{
	struct secdp_sysfs_private *sysfs = g_secdp_sysfs;
	struct secdp_adapter *adapter = &sysfs->sec->adapter;
	int rc;

	DP_INFO("branch revision: HW(0x%X), SW(0x%X, 0x%X)\n",
		adapter->fw_ver[0], adapter->fw_ver[1], adapter->fw_ver[2]);

	rc = scnprintf(buf, PAGE_SIZE, "%02X%02X\n",
		adapter->fw_ver[1], adapter->fw_ver[2]);

	return rc;
}

static CLASS_ATTR_RO(dex_ver);

/* note: needs test once wifi is fixed */
static ssize_t monitor_info_show(struct class *class,
				struct class_attribute *attr, char *buf)
{
	struct dp_panel *info = NULL;
	struct sde_edid_ctrl *edid_ctrl = NULL;
	struct edid *edid = NULL;
	short prod_id = 0;
	int rc = 0;

	info = secdp_get_panel_info();
	if (!info) {
		DP_ERR("unable to find panel info\n");
		goto exit;
	}

	edid_ctrl = info->edid_ctrl;
	if (!edid_ctrl) {
		DP_ERR("unable to find edid_ctrl\n");
		goto exit;
	}

	edid = edid_ctrl->edid;
	if (!edid) {
		DP_ERR("unable to find edid\n");
		goto exit;
	}

	prod_id |= (edid->prod_code[0] << 8) | (edid->prod_code[1]);
	DP_DEBUG("prod_code[0]:%02x, prod_code[1]:%02x, prod_id:%04x\n",
		edid->prod_code[0], edid->prod_code[1], prod_id);

	rc = snprintf(buf, SZ_32, "%s,0x%x,0x%x\n",
			edid_ctrl->vendor_id, prod_id, edid->serial); /* byte order? */
exit:
	return rc;
}

static CLASS_ATTR_RO(monitor_info);

#if defined(CONFIG_SECDP_BIGDATA)
static ssize_t dp_error_info_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	return _secdp_bigdata_show(class, attr, buf);
}

static ssize_t dp_error_info_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	return _secdp_bigdata_store(dev, attr, buf, size);
}

static CLASS_ATTR_RW(dp_error_info);
#endif

#ifdef SECDP_SELF_TEST
static struct secdp_sef_test_item g_self_test[] = {
	{DP_ENUM_STR(ST_CLEAR_CMD), .arg_cnt = 0, .arg_str = "clear all configurations"},
	{DP_ENUM_STR(ST_LANE_CNT), .arg_cnt = 1, .arg_str = "lane_count: 1 = 1 lane, 2 = 2 lane, 4 = 4 lane, 555 = disable"},
	{DP_ENUM_STR(ST_LINK_RATE), .arg_cnt = 1, .arg_str = "link_rate: 1 = 1.62G , 2 = 2.7G, 3 = 5.4G, 555 = disable"},
	{DP_ENUM_STR(ST_CONNECTION_TEST), .arg_cnt = 1, .arg_str = "reconnection time(sec) : range = 5 ~ 50, 555 = disable"},
	{DP_ENUM_STR(ST_HDCP_TEST), .arg_cnt = 1, .arg_str = "hdcp on/off time(sec): range = 5 ~ 50, 555 = disable"},
	{DP_ENUM_STR(ST_EDID), .arg_cnt = 0, .arg_str = "need to write edid to \"sys/class/dp_sec/dp_edid\" sysfs node, 555 = disable"},
	{DP_ENUM_STR(ST_PREEM_TUN), .arg_cnt = 16, .arg_str = "pre-emphasis calibration value, 555 = disable"},
	{DP_ENUM_STR(ST_VOLTAGE_TUN), .arg_cnt = 16, .arg_str = "voltage-level calibration value, 555 = disable"},
};

int secdp_self_test_status(int cmd)
{
	if (cmd >= ST_MAX)
		return -EINVAL;

	if (g_self_test[cmd].enabled) {
		DP_INFO("%s: %s\n", g_self_test[cmd].cmd_str,
			g_self_test[cmd].enabled ? "true" : "false");
	}

	return g_self_test[cmd].enabled ? g_self_test[cmd].arg_cnt : -1;
}

int *secdp_self_test_get_arg(int cmd)
{
	return g_self_test[cmd].arg;
}

void secdp_self_register_clear_func(int cmd, void (*func)(void))
{
	if (cmd >= ST_MAX)
		return;

	g_self_test[cmd].clear = func;
	DP_INFO("%s: clear func was registered.\n", g_self_test[cmd].cmd_str);
}

u8 *secdp_self_test_get_edid(void)
{
	struct secdp_sysfs_private *sysfs = g_secdp_sysfs;

	return	sysfs->sec->self_test_edid;
}

static void secdp_self_test_reconnect_work(struct work_struct *work)
{
	struct secdp_sysfs_private *sysfs = g_secdp_sysfs;
	int delay = g_self_test[ST_CONNECTION_TEST].arg[0];
	static unsigned long test_cnt;

	if (!secdp_get_cable_status() || !secdp_get_hpd_status()) {
		DP_INFO("cable is out\n");
		test_cnt = 0;
		return;
	}

	if (sysfs->sec->self_test_reconnect_cb)
		sysfs->sec->self_test_reconnect_cb();

	test_cnt++;
	DP_INFO("test_cnt: %lu\n", test_cnt);

	schedule_delayed_work(&sysfs->sec->self_test_reconnect_work,
		msecs_to_jiffies(delay * 1000));
}

void secdp_self_test_start_reconnect(void (*func)(void))
{
	struct secdp_sysfs_private *sysfs = g_secdp_sysfs;
	int delay = g_self_test[ST_CONNECTION_TEST].arg[0];

	if (delay > 50 || delay < 5)
		delay = g_self_test[ST_CONNECTION_TEST].arg[0] = 10;

	DP_INFO("start reconnect test: delay %d sec\n", delay);

	sysfs->sec->self_test_reconnect_cb = func;
	schedule_delayed_work(&sysfs->sec->self_test_reconnect_work,
		msecs_to_jiffies(delay * 1000));
}

static void secdp_self_test_hdcp_test_work(struct work_struct *work)
{
	struct secdp_sysfs_private *sysfs = g_secdp_sysfs;
	int delay = g_self_test[ST_HDCP_TEST].arg[0];
	static unsigned long test_cnt;

	if (!secdp_get_cable_status() || !secdp_get_hpd_status()) {
		DP_INFO("cable is out\n");
		test_cnt = 0;
		return;
	}

	if (sysfs->sec->self_test_hdcp_off_cb)
		sysfs->sec->self_test_hdcp_off_cb();

	msleep(3000);

	if (sysfs->sec->self_test_hdcp_on_cb)
		sysfs->sec->self_test_hdcp_on_cb();

	test_cnt++;
	DP_INFO("test_cnt: %lu\n", test_cnt);

	schedule_delayed_work(&sysfs->sec->self_test_hdcp_test_work,
		msecs_to_jiffies(delay * 1000));

}

void secdp_self_test_start_hdcp_test(void (*func_on)(void),
		void (*func_off)(void))
{
	struct secdp_sysfs_private *sysfs = g_secdp_sysfs;
	int delay = g_self_test[ST_HDCP_TEST].arg[0];

	if (delay == 0) {
		DP_INFO("hdcp test is aborted\n");
		return;
	}

	if (delay > 50 || delay < 5)
		delay = g_self_test[ST_HDCP_TEST].arg[0] = 10;

	DP_INFO("start hdcp test: delay %d sec\n", delay);

	sysfs->sec->self_test_hdcp_on_cb = func_on;
	sysfs->sec->self_test_hdcp_off_cb = func_off;

	schedule_delayed_work(&sysfs->sec->self_test_hdcp_test_work,
		msecs_to_jiffies(delay * 1000));
}

static ssize_t dp_self_test_show(struct class *class,
				struct class_attribute *attr, char *buf)
{
	int i, j, rc = 0;

	for (i = 0; i < ST_MAX; i++) {
		rc += scnprintf(buf + rc, PAGE_SIZE - rc,
				"%d. %s: %s\n   ==>", i,
				g_self_test[i].cmd_str, g_self_test[i].arg_str);

		if (g_self_test[i].enabled) {
			rc += scnprintf(buf + rc, PAGE_SIZE - rc,
					"current value: enabled - arg: ");

			for (j = 0; j < g_self_test[i].arg_cnt; j++) {
				rc += scnprintf(buf + rc, PAGE_SIZE - rc,
						"0x%x ", g_self_test[i].arg[j]);
			}

			rc += scnprintf(buf + rc, PAGE_SIZE - rc, "\n\n");
		} else {
			rc += scnprintf(buf + rc, PAGE_SIZE - rc,
				"current value: disabled\n\n");
		}
	}

	return rc;
}

static void dp_self_test_clear_func(int cmd)
{
	int arg_cnt = (g_self_test[cmd].arg_cnt < ST_ARG_CNT) ? g_self_test[cmd].arg_cnt : ST_ARG_CNT;

	g_self_test[cmd].enabled = false;
	memset(g_self_test[cmd].arg, 0,	sizeof(int) * arg_cnt);

	if (g_self_test[cmd].clear)
		g_self_test[cmd].clear();
}

static ssize_t dp_self_test_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	int val[ST_ARG_CNT + 2] = {0, };
	int arg, arg_cnt, cmd, i;

	if (secdp_check_store_args(buf, size)) {
		DP_ERR("args error!\n");
		goto end;
	}

	get_options(buf, ARRAY_SIZE(val), val);
	cmd = val[1];
	arg = val[2];

	if (cmd < 0 || cmd >= ST_MAX) {
		DP_INFO("invalid cmd\n");
		goto end;
	}

	if (cmd == ST_CLEAR_CMD) {
		for (i = 1; i < ST_MAX; i++)
			dp_self_test_clear_func(i);

		DP_INFO("cmd: ST_CLEAR_CMD\n");
		goto end;
	}

	g_self_test[cmd].enabled = (arg == ST_TEST_EXIT) ? false : true;
	if (g_self_test[cmd].enabled) {
		if ((val[0] - 1) != g_self_test[cmd].arg_cnt) {
			DP_INFO("invalid param.\n");
			goto end;
		}

		arg_cnt = (g_self_test[cmd].arg_cnt < ST_ARG_CNT) ? g_self_test[cmd].arg_cnt : ST_ARG_CNT;
		memcpy(g_self_test[cmd].arg, val + 2, sizeof(int) * arg_cnt);
	} else {
		dp_self_test_clear_func(cmd);
	}

	DP_INFO("cmd: %d. %s, enabled:%s\n", cmd,
		(cmd < ST_MAX) ? g_self_test[cmd].cmd_str : "null",
		(cmd < ST_MAX) ? (g_self_test[cmd].enabled ? "true" : "false") : "null");
end:
	return size;
}

static CLASS_ATTR_RW(dp_self_test);

static ssize_t dp_edid_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	struct secdp_sysfs_private *sysfs = g_secdp_sysfs;
	int i, flag = 0;
	int rc = 0;

	rc += scnprintf(buf + rc, PAGE_SIZE - rc,
			"EDID test is %s\n", g_self_test[ST_EDID].enabled ? "enabled" : "disabled");

	for (i = 0; i < ST_EDID_SIZE; i++) {
		rc += scnprintf(buf + rc, PAGE_SIZE - rc,
				"%02x ", sysfs->sec->self_test_edid[i]);
		if (!((i+1)%8)) {
			rc += scnprintf(buf + rc, PAGE_SIZE - rc,
					"%s", flag ? "\n" : "  ");
			flag = !flag;
			if (!((i+1) % 128))
				rc += scnprintf(buf + rc, PAGE_SIZE - rc, "\n");
		}
	}

	return rc;
}

static ssize_t dp_edid_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	struct secdp_sysfs_private *sysfs = g_secdp_sysfs;
	char *temp, *buf_t;
	const int char_to_nib = 2;
	size_t i, j = 0, edid_buf_index = 0;
	size_t len = 0, edid_size = 0;

	len = min_t(size_t, size, SZ_1K);
	if ((len - 1/*0xa*/) % EDID_LENGTH) {
		DP_ERR("invalid edid len: %d\n", len - 1);
		dp_self_test_clear_func(ST_EDID);
		goto error;
	}

	temp = kzalloc(len, GFP_KERNEL);
	if (!temp) {
		DP_ERR("buffer alloc error\n");
		dp_self_test_clear_func(ST_EDID);
		goto error;
	}

	/* remove space if any */
	for (i = 0; i < size; i++) {
		if (buf[i] != ' ')
			temp[j++] = buf[i];
	}

	memset(sysfs->sec->self_test_edid, 0, sizeof(ST_EDID_SIZE));
	edid_size = len / char_to_nib;
	buf_t = temp;

	for (i = 0; i < edid_size; i++) {
		char t[3];
		int d;

		memcpy(t, buf_t, sizeof(char) * char_to_nib);
		t[char_to_nib] = '\0';

		if (kstrtoint(t, 16, &d)) {
			DP_ERR("kstrtoint error\n");
			goto end;
		}

		sysfs->sec->self_test_edid[edid_buf_index++] = d;
		buf_t += char_to_nib;
	}

	g_self_test[ST_EDID].enabled = true;
end:
	kfree(temp);
error:
	return size;
}

static CLASS_ATTR_RW(dp_edid);

void secdp_replace_edid(u8 *edid)
{
	struct secdp_sysfs_private *sysfs = g_secdp_sysfs;

	if (!edid)
		goto end;

	memcpy(edid, sysfs->sec->self_test_edid, ST_EDID_SIZE);
end:
	return;
}
#else/*!SECDP_SELF_TEST*/
void secdp_replace_edid(u8 *edid)
{
	DP_ERR("cannot be here!\n");
	return;
}
#endif

#if defined(CONFIG_SECDP_DBG)
/* regarding to set force DP timing, use QC way instead
 * this node is for debug purpose and available at eng build only
 */
static ssize_t dp_forced_resolution_show(struct class *class,
				struct class_attribute *attr, char *buf)
{
	char tmp[SZ_1K] = {0,};
	int  rc = 0;

	secdp_show_hmd_dev(tmp);
	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "\n< HMD >\n%s\n", tmp);

	memset(tmp, 0, ARRAY_SIZE(tmp));
	secdp_show_phy_param(tmp);
	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "\n< DP-PHY >\n%s\n", tmp);

	memset(tmp, 0, ARRAY_SIZE(tmp));
	secdp_show_link_param(tmp);
	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "\n< link params >\n%s\n", tmp);

#if IS_ENABLED(CONFIG_COMBO_REDRIVER_PS5169)
	memset(tmp, 0, ARRAY_SIZE(tmp));
	secdp_show_ps5169_param(tmp);
	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "\n< PS5169 EQ0/EQ1 >\n%s\n", tmp);
#endif

	return rc;
}

/* regarding to set force DP timing, use QC way instead
 * this node is for debug purpose and available at eng build only
 */
static ssize_t dp_forced_resolution_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	int val[10] = {0, };

	if (secdp_check_store_args(buf, size)) {
		DP_ERR("args error!\n");
		goto exit;
	}

	get_options(buf, ARRAY_SIZE(val), val);

#if 0/*blocked for future test*/
{
	char str[MAX_DEX_STORE_LEN] = {0,}, *p, *tok;
	int len;

	if (size >= MAX_DEX_STORE_LEN) {
		DP_ERR("too long args! %d\n", size);
		goto exit;
	}

	memcpy(str, buf, size);
	p   = str;
	tok = strsep(&p, ",");
	if (!p)
		goto exit;

	len = strlen(tok);
	if (!len)
		goto exit;

	DP_DEBUG("tok:%s, len:%d\n", tok, len);
}
#endif

exit:
	return size;
}

static CLASS_ATTR_RW(dp_forced_resolution);

static ssize_t dp_unit_test_show(struct class *class,
				struct class_attribute *attr, char *buf)
{
	struct secdp_sysfs_private *sysfs = g_secdp_sysfs;
	int rc, cmd = sysfs->test_cmd;
	bool res = false;

	DP_INFO("test_cmd: %s\n", secdp_utcmd_to_str(cmd));

	switch (cmd) {
	case SECDP_UTCMD_EDID_PARSE:
		res = secdp_unit_test_edid_parse();
		break;
	default:
		DP_INFO("invalid test_cmd: %d\n", cmd);
		break;
	}

	rc = scnprintf(buf, 3, "%d\n", res ? 1 : 0);
	return rc;
}

static ssize_t dp_unit_test_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	struct secdp_sysfs_private *sysfs = g_secdp_sysfs;
	int val[10] = {0, };

	if (secdp_check_store_args(buf, size)) {
		DP_ERR("args error!\n");
		goto exit;
	}

	get_options(buf, ARRAY_SIZE(val), val);
	sysfs->test_cmd = val[1];

	DP_INFO("test_cmd: %d...%s\n", sysfs->test_cmd,
		secdp_utcmd_to_str(sysfs->test_cmd));

exit:
	return size;
}

static CLASS_ATTR_RW(dp_unit_test);

static ssize_t dp_aux_cfg_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	char tmp[SZ_1K] = {0,};
	int  rc = 0;

	secdp_aux_cfg_show(tmp);
	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "\n< AUX cfg >\n%s\n", tmp);

	return rc;
}

static ssize_t dp_aux_cfg_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	char tmp[SZ_1K] = {0,};

	memcpy(tmp, buf, min(ARRAY_SIZE(tmp) - 1, size));
	secdp_aux_cfg_store(tmp);

	return size;
}

static CLASS_ATTR_RW(dp_aux_cfg);

static ssize_t dp_preshoot_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	char tmp[SZ_64] = {0,};
	int  rc = 0;

	secdp_catalog_preshoot_show(tmp);
	rc = snprintf(buf + rc, PAGE_SIZE - rc, "%s\n", tmp);

	return rc;
}

static ssize_t dp_preshoot_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	char tmp[SZ_64] = {0,};
	int len = min(sizeof(tmp), size);

	if (!len || len >= SZ_64) {
		DP_ERR("wrong length! %d\n", len);
		goto end;
	}

	memcpy(tmp, buf, len);
	tmp[SZ_64 - 1] = '\0';
	secdp_catalog_preshoot_store(tmp);
end:
	return size;
}

static CLASS_ATTR_RW(dp_preshoot);

static ssize_t dp_vx_lvl_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	char tmp[SZ_1K] = {0,};
	int  rc = 0;

	secdp_parse_vxpx_show(DP_HW_LEGACY, DP_PARAM_VX, tmp);
	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "%s\n", tmp);

	return rc;
}

static ssize_t dp_vx_lvl_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	char tmp[SZ_64] = {0,};
	int len = min(sizeof(tmp), size);

	if (!len || len >= SZ_64) {
		DP_ERR("wrong length! %d\n", len);
		goto end;
	}

	memcpy(tmp, buf, len);
	tmp[SZ_64 - 1] = '\0';
	secdp_parse_vxpx_store(DP_HW_LEGACY, DP_PARAM_VX, tmp);
end:
	return size;
}

static CLASS_ATTR_RW(dp_vx_lvl);

static ssize_t dp_px_lvl_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	char tmp[SZ_1K] = {0,};
	int  rc = 0;

	secdp_parse_vxpx_show(DP_HW_LEGACY, DP_PARAM_PX, tmp);
	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "%s\n", tmp);

	return rc;
}

static ssize_t dp_px_lvl_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	char tmp[SZ_64] = {0,};
	int len = min(sizeof(tmp), size);

	if (!len || len >= SZ_64) {
		DP_ERR("wrong length! %d\n", len);
		goto end;
	}

	memcpy(tmp, buf, len);
	tmp[SZ_64 - 1] = '\0';
	secdp_parse_vxpx_store(DP_HW_LEGACY, DP_PARAM_PX, tmp);
end:
	return size;
}

static CLASS_ATTR_RW(dp_px_lvl);

static ssize_t dp_vx_lvl_hbr2_hbr3_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	char tmp[SZ_1K] = {0,};
	int  rc = 0;

	secdp_parse_vxpx_show(DP_HW_V123_HBR2_HBR3, DP_PARAM_VX, tmp);
	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "%s\n", tmp);

	return rc;
}

static ssize_t dp_vx_lvl_hbr2_hbr3_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	char tmp[SZ_64] = {0,};
	int len = min(sizeof(tmp), size);

	if (!len || len >= SZ_64) {
		DP_ERR("wrong length! %d\n", len);
		goto end;
	}

	memcpy(tmp, buf, len);
	tmp[SZ_64 - 1] = '\0';
	secdp_parse_vxpx_store(DP_HW_V123_HBR2_HBR3, DP_PARAM_VX, tmp);
end:
	return size;

}

static CLASS_ATTR_RW(dp_vx_lvl_hbr2_hbr3);

static ssize_t dp_px_lvl_hbr2_hbr3_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	char tmp[SZ_1K] = {0,};
	int  rc = 0;

	secdp_parse_vxpx_show(DP_HW_V123_HBR2_HBR3, DP_PARAM_PX, tmp);
	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "%s\n", tmp);

	return rc;
}

static ssize_t dp_px_lvl_hbr2_hbr3_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	char tmp[SZ_64] = {0,};
	int len = min(sizeof(tmp), size);

	if (!len || len >= SZ_64) {
		DP_ERR("wrong length! %d\n", len);
		goto end;
	}

	memcpy(tmp, buf, len);
	tmp[SZ_64 - 1] = '\0';
	secdp_parse_vxpx_store(DP_HW_V123_HBR2_HBR3, DP_PARAM_PX, tmp);
end:
	return size;
}

static CLASS_ATTR_RW(dp_px_lvl_hbr2_hbr3);

static ssize_t dp_vx_lvl_hbr_rbr_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	char tmp[SZ_1K] = {0,};
	int  rc = 0;

	secdp_parse_vxpx_show(DP_HW_V123_HBR_RBR, DP_PARAM_VX, tmp);
	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "%s\n", tmp);

	return rc;
}

static ssize_t dp_vx_lvl_hbr_rbr_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	char tmp[SZ_64] = {0,};
	int len = min(sizeof(tmp), size);

	if (!len || len >= SZ_64) {
		DP_ERR("wrong length! %d\n", len);
		goto end;
	}

	memcpy(tmp, buf, len);
	tmp[SZ_64 - 1] = '\0';
	secdp_parse_vxpx_store(DP_HW_V123_HBR_RBR, DP_PARAM_VX, tmp);
end:
	return size;
}

static CLASS_ATTR_RW(dp_vx_lvl_hbr_rbr);

static ssize_t dp_px_lvl_hbr_rbr_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	char tmp[SZ_1K] = {0,};
	int  rc = 0;

	secdp_parse_vxpx_show(DP_HW_V123_HBR_RBR, DP_PARAM_PX, tmp);
	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "%s\n", tmp);

	return rc;
}

static ssize_t dp_px_lvl_hbr_rbr_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	char tmp[SZ_64] = {0,};
	int len = min(sizeof(tmp), size);

	if (!len || len >= SZ_64) {
		DP_ERR("wrong length! %d\n", len);
		goto end;
	}

	memcpy(tmp, buf, len);
	tmp[SZ_64 - 1] = '\0';
	secdp_parse_vxpx_store(DP_HW_V123_HBR_RBR, DP_PARAM_PX, tmp);
end:
	return size;
}

static CLASS_ATTR_RW(dp_px_lvl_hbr_rbr);

static ssize_t dp_pref_skip_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	int skip, rc;

	DP_ENTER("\n");

	skip = secdp_debug_prefer_skip_show();
	rc = snprintf(buf, SZ_8, "%d\n", skip);

	return rc;
}

static ssize_t dp_pref_skip_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	int i, val[30] = {0, };

	if (secdp_check_store_args(buf, size)) {
		DP_ERR("args error!\n");
		goto exit;
	}

	DP_DEBUG("size:%d\n", (int)size);

	get_options(buf, ARRAY_SIZE(val), val);
	for (i = 0; i < 16; i = i + 4) {
		DP_DEBUG("%02x,%02x,%02x,%02x\n",
			val[i+1], val[i+2], val[i+3], val[i+4]);
	}

	secdp_debug_prefer_skip_store(val[1]);
exit:
	return size;
}

static CLASS_ATTR_RW(dp_pref_skip);

static ssize_t dp_pref_ratio_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	int ratio, rc;

	DP_ENTER("\n");

	ratio = secdp_debug_prefer_ratio_show();
	rc = snprintf(buf, SZ_8, "%d\n", ratio);

	return rc;
}

static ssize_t dp_pref_ratio_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	int i, val[30] = {0, };

	if (secdp_check_store_args(buf, size)) {
		DP_ERR("args error!\n");
		goto exit;
	}

	DP_DEBUG("size:%d\n", (int)size);

	get_options(buf, ARRAY_SIZE(val), val);
	for (i = 0; i < 16; i = i + 4) {
		DP_DEBUG("%02x,%02x,%02x,%02x\n",
			val[i+1], val[i+2], val[i+3], val[i+4]);
	}

	secdp_debug_prefer_ratio_store(val[1]);
exit:
	return size;
}

static CLASS_ATTR_RW(dp_pref_ratio);

#if IS_ENABLED(CONFIG_COMBO_REDRIVER_PS5169)
static ssize_t dp_ps5169_rbr_eq0_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	char tmp[SZ_1K] = {0,};
	int  rc = 0;

	secdp_parse_ps5169_show(DP_PS5169_EQ0, DP_PS5169_RATE_RBR, tmp);
	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "%s\n", tmp);

	return rc;
}

static ssize_t dp_ps5169_rbr_eq0_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	char tmp[SZ_64] = {0,};
	int len = min(sizeof(tmp), size);

	if (!len || len >= SZ_64) {
		DP_ERR("wrong length! %d\n", len);
		goto end;
	}

	memcpy(tmp, buf, len);
	tmp[SZ_64 - 1] = '\0';
	secdp_parse_ps5169_store(DP_PS5169_EQ0, DP_PS5169_RATE_RBR, tmp);
end:
	return size;
}
static CLASS_ATTR_RW(dp_ps5169_rbr_eq0);

static ssize_t dp_ps5169_rbr_eq1_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	char tmp[SZ_1K] = {0,};
	int  rc = 0;

	secdp_parse_ps5169_show(DP_PS5169_EQ1, DP_PS5169_RATE_RBR, tmp);
	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "%s\n", tmp);

	return rc;
}

static ssize_t dp_ps5169_rbr_eq1_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	char tmp[SZ_64] = {0,};
	int len = min(sizeof(tmp), size);

	if (!len || len >= SZ_64) {
		DP_ERR("wrong length! %d\n", len);
		goto end;
	}

	memcpy(tmp, buf, len);
	tmp[SZ_64 - 1] = '\0';
	secdp_parse_ps5169_store(DP_PS5169_EQ1, DP_PS5169_RATE_RBR, tmp);
end:
	return size;
}
static CLASS_ATTR_RW(dp_ps5169_rbr_eq1);

static ssize_t dp_ps5169_hbr_eq0_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	char tmp[SZ_1K] = {0,};
	int  rc = 0;

	secdp_parse_ps5169_show(DP_PS5169_EQ0, DP_PS5169_RATE_HBR, tmp);
	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "%s\n", tmp);

	return rc;
}

static ssize_t dp_ps5169_hbr_eq0_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	char tmp[SZ_64] = {0,};
	int len = min(sizeof(tmp), size);

	if (!len || len >= SZ_64) {
		DP_ERR("wrong length! %d\n", len);
		goto end;
	}

	memcpy(tmp, buf, len);
	tmp[SZ_64 - 1] = '\0';
	secdp_parse_ps5169_store(DP_PS5169_EQ0, DP_PS5169_RATE_HBR, tmp);
end:
	return size;
}
static CLASS_ATTR_RW(dp_ps5169_hbr_eq0);

static ssize_t dp_ps5169_hbr_eq1_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	char tmp[SZ_1K] = {0,};
	int  rc = 0;

	secdp_parse_ps5169_show(DP_PS5169_EQ1, DP_PS5169_RATE_HBR, tmp);
	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "%s\n", tmp);

	return rc;
}

static ssize_t dp_ps5169_hbr_eq1_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	char tmp[SZ_64] = {0,};
	int len = min(sizeof(tmp), size);

	if (!len || len >= SZ_64) {
		DP_ERR("wrong length! %d\n", len);
		goto end;
	}

	memcpy(tmp, buf, len);
	tmp[SZ_64 - 1] = '\0';
	secdp_parse_ps5169_store(DP_PS5169_EQ1, DP_PS5169_RATE_HBR, tmp);
end:
	return size;
}
static CLASS_ATTR_RW(dp_ps5169_hbr_eq1);

static ssize_t dp_ps5169_hbr2_eq0_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	char tmp[SZ_1K] = {0,};
	int  rc = 0;

	secdp_parse_ps5169_show(DP_PS5169_EQ0, DP_PS5169_RATE_HBR2, tmp);
	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "%s\n", tmp);

	return rc;
}

static ssize_t dp_ps5169_hbr2_eq0_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	char tmp[SZ_64] = {0,};
	int len = min(sizeof(tmp), size);

	if (!len || len >= SZ_64) {
		DP_ERR("wrong length! %d\n", len);
		goto end;
	}

	memcpy(tmp, buf, len);
	tmp[SZ_64 - 1] = '\0';
	secdp_parse_ps5169_store(DP_PS5169_EQ0, DP_PS5169_RATE_HBR2, tmp);
end:
	return size;
}
static CLASS_ATTR_RW(dp_ps5169_hbr2_eq0);

static ssize_t dp_ps5169_hbr2_eq1_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	char tmp[SZ_1K] = {0,};
	int  rc = 0;

	secdp_parse_ps5169_show(DP_PS5169_EQ1, DP_PS5169_RATE_HBR2, tmp);
	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "%s\n", tmp);

	return rc;
}

static ssize_t dp_ps5169_hbr2_eq1_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	char tmp[SZ_64] = {0,};
	int len = min(sizeof(tmp), size);

	if (!len || len >= SZ_64) {
		DP_ERR("wrong length! %d\n", len);
		goto end;
	}

	memcpy(tmp, buf, len);
	tmp[SZ_64 - 1] = '\0';
	secdp_parse_ps5169_store(DP_PS5169_EQ1, DP_PS5169_RATE_HBR2, tmp);
end:
	return size;
}
static CLASS_ATTR_RW(dp_ps5169_hbr2_eq1);

static ssize_t dp_ps5169_hbr3_eq0_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	char tmp[SZ_1K] = {0,};
	int  rc = 0;

	secdp_parse_ps5169_show(DP_PS5169_EQ0, DP_PS5169_RATE_HBR3, tmp);
	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "%s\n", tmp);

	return rc;
}

static ssize_t dp_ps5169_hbr3_eq0_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	char tmp[SZ_64] = {0,};
	int len = min(sizeof(tmp), size);

	if (!len || len >= SZ_64) {
		DP_ERR("wrong length! %d\n", len);
		goto end;
	}

	memcpy(tmp, buf, len);
	tmp[SZ_64 - 1] = '\0';
	secdp_parse_ps5169_store(DP_PS5169_EQ0, DP_PS5169_RATE_HBR3, tmp);
end:
	return size;
}
static CLASS_ATTR_RW(dp_ps5169_hbr3_eq0);

static ssize_t dp_ps5169_hbr3_eq1_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	char tmp[SZ_1K] = {0,};
	int  rc = 0;

	secdp_parse_ps5169_show(DP_PS5169_EQ1, DP_PS5169_RATE_HBR3, tmp);
	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "%s\n", tmp);

	return rc;
}

static ssize_t dp_ps5169_hbr3_eq1_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	char tmp[SZ_64] = {0,};
	int len = min(sizeof(tmp), size);

	if (!len || len >= SZ_64) {
		DP_ERR("wrong length! %d\n", len);
		goto end;
	}

	memcpy(tmp, buf, len);
	tmp[SZ_64 - 1] = '\0';
	secdp_parse_ps5169_store(DP_PS5169_EQ1, DP_PS5169_RATE_HBR3, tmp);
end:
	return size;
}
static CLASS_ATTR_RW(dp_ps5169_hbr3_eq1);
#endif/*CONFIG_COMBO_REDRIVER_PS5169*/
#endif/*CONFIG_SECDP_DBG*/

enum {
	DEX = 0,
	DEX_VER,
	MONITOR_INFO,
#if defined(CONFIG_SECDP_BIGDATA)
	DP_ERROR_INFO,
#endif
#if IS_ENABLED(CONFIG_SEC_FACTORY)
	DP_SBU_SW_SEL,
#endif
#ifdef SECDP_SELF_TEST
	DP_SELF_TEST,
	DP_EDID,
#endif
#if defined(CONFIG_SECDP_DBG)
	DP_FORCED_RES,
	DP_UNIT_TEST,
	DP_AUX_CFG,
	DP_PRESHOOT,
	DP_VX_LVL,
	DP_PX_LVL,
	DP_VX_LVL_HBR2_HBR3,
	DP_PX_LVL_HBR2_HBR3,
	DP_VX_LVL_HBR_RBR,
	DP_PX_LVL_HBR_RBR,
	DP_PREF_SKIP,
	DP_PREF_RATIO,
#if IS_ENABLED(CONFIG_COMBO_REDRIVER_PS5169)
	DP_PS5169_RBR_EQ0,
	DP_PS5169_RBR_EQ1,
	DP_PS5169_HBR_EQ0,
	DP_PS5169_HBR_EQ1,
	DP_PS5169_HBR2_EQ0,
	DP_PS5169_HBR2_EQ1,
	DP_PS5169_HBR3_EQ0,
	DP_PS5169_HBR3_EQ1,
#endif/*CONFIG_COMBO_REDRIVER_PS5169*/
#endif
};

static struct attribute *secdp_class_attrs[] = {
	[DEX]		= &class_attr_dex.attr,
	[DEX_VER]	= &class_attr_dex_ver.attr,
	[MONITOR_INFO]	= &class_attr_monitor_info.attr,
#if defined(CONFIG_SECDP_BIGDATA)
	[DP_ERROR_INFO] = &class_attr_dp_error_info.attr,
#endif
#if IS_ENABLED(CONFIG_SEC_FACTORY)
	[DP_SBU_SW_SEL]	= &class_attr_dp_sbu_sw_sel.attr,
#endif
#ifdef SECDP_SELF_TEST
	[DP_SELF_TEST]	= &class_attr_dp_self_test.attr,
	[DP_EDID]	= &class_attr_dp_edid.attr,
#endif
#if defined(CONFIG_SECDP_DBG)
	[DP_FORCED_RES]	= &class_attr_dp_forced_resolution.attr,
	[DP_UNIT_TEST]	= &class_attr_dp_unit_test.attr,
	[DP_AUX_CFG]	= &class_attr_dp_aux_cfg.attr,
	[DP_PRESHOOT]	= &class_attr_dp_preshoot.attr,
	[DP_VX_LVL]	= &class_attr_dp_vx_lvl.attr,
	[DP_PX_LVL]	= &class_attr_dp_px_lvl.attr,
	[DP_VX_LVL_HBR2_HBR3]	= &class_attr_dp_vx_lvl_hbr2_hbr3.attr,
	[DP_PX_LVL_HBR2_HBR3]	= &class_attr_dp_px_lvl_hbr2_hbr3.attr,
	[DP_VX_LVL_HBR_RBR]	= &class_attr_dp_vx_lvl_hbr_rbr.attr,
	[DP_PX_LVL_HBR_RBR]	= &class_attr_dp_px_lvl_hbr_rbr.attr,
	[DP_PREF_SKIP]	= &class_attr_dp_pref_skip.attr,
	[DP_PREF_RATIO]	= &class_attr_dp_pref_ratio.attr,
#if IS_ENABLED(CONFIG_COMBO_REDRIVER_PS5169)
	[DP_PS5169_RBR_EQ0]  = &class_attr_dp_ps5169_rbr_eq0.attr,
	[DP_PS5169_RBR_EQ1]  = &class_attr_dp_ps5169_rbr_eq1.attr,
	[DP_PS5169_HBR_EQ0]  = &class_attr_dp_ps5169_hbr_eq0.attr,
	[DP_PS5169_HBR_EQ1]  = &class_attr_dp_ps5169_hbr_eq1.attr,
	[DP_PS5169_HBR2_EQ0] = &class_attr_dp_ps5169_hbr2_eq0.attr,
	[DP_PS5169_HBR2_EQ1] = &class_attr_dp_ps5169_hbr2_eq1.attr,
	[DP_PS5169_HBR3_EQ0] = &class_attr_dp_ps5169_hbr3_eq0.attr,
	[DP_PS5169_HBR3_EQ1] = &class_attr_dp_ps5169_hbr3_eq1.attr,
#endif/*CONFIG_COMBO_REDRIVER_PS5169*/
#endif
	NULL,
};
ATTRIBUTE_GROUPS(secdp_class);

struct secdp_sysfs *secdp_sysfs_init(void)
{
	struct class *dp_class;
	static struct secdp_sysfs *sysfs;
	int rc = -1;

	DP_ENTER("\n");

	if (sysfs) {
		DP_ERR("already called!\n");
		return sysfs;
	}

	dp_class = kzalloc(sizeof(*dp_class), GFP_KERNEL);
	if (!dp_class) {
		DP_ERR("fail to alloc sysfs->dp_class\n");
		goto err_exit;
	}

	dp_class->name = "dp_sec";
	dp_class->owner = THIS_MODULE;
	dp_class->class_groups = secdp_class_groups;

	rc = class_register(dp_class);
	if (rc < 0) {
		DP_ERR("couldn't register secdp sysfs class, rc: %d\n", rc);
		goto free_exit;
	}

	sysfs = kzalloc(sizeof(*sysfs), GFP_KERNEL);
	if (!sysfs) {
		DP_ERR("fail to alloc sysfs\n");
		goto free_exit;
	}

	sysfs->dp_class = dp_class;

#if defined(CONFIG_SECDP_BIGDATA)
	secdp_bigdata_init(dp_class);
#endif

	DP_DEBUG("success, rc:%d\n", rc);
	return sysfs;

free_exit:
	kfree(dp_class);
err_exit:
	return NULL;
}

void secdp_sysfs_deinit(struct secdp_sysfs *sysfs)
{
	DP_ENTER("\n");

	if (sysfs) {
		if (sysfs->dp_class) {
			class_unregister(sysfs->dp_class);
			kfree(sysfs->dp_class);
			sysfs->dp_class = NULL;
			DP_DEBUG("freeing dp_class done\n");
		}

		kfree(sysfs);
	}
}

struct secdp_sysfs *secdp_sysfs_get(struct device *dev, struct secdp_misc *sec)
{
	struct secdp_sysfs_private *sysfs;
	struct secdp_sysfs *dp_sysfs;
	int rc = 0;

	if (!dev || !sec) {
		DP_ERR("invalid input\n");
		rc = -EINVAL;
		goto error;
	}

	sysfs = devm_kzalloc(dev, sizeof(*sysfs), GFP_KERNEL);
	if (!sysfs) {
		rc = -EINVAL;
		goto error;
	}

	sysfs->dev   = dev;
	sysfs->sec   = sec;
	dp_sysfs = &sysfs->dp_sysfs;

	g_secdp_sysfs = sysfs;

#ifdef SECDP_SELF_TEST
	INIT_DELAYED_WORK(&sec->self_test_reconnect_work,
		secdp_self_test_reconnect_work);
	INIT_DELAYED_WORK(&sec->self_test_hdcp_test_work,
		secdp_self_test_hdcp_test_work);
#endif
	return dp_sysfs;

error:
	return ERR_PTR(rc);
}

void secdp_sysfs_put(struct secdp_sysfs *dp_sysfs)
{
	struct secdp_sysfs_private *sysfs;

	if (!dp_sysfs)
		return;

	sysfs = container_of(dp_sysfs, struct secdp_sysfs_private, dp_sysfs);
	devm_kfree(sysfs->dev, sysfs);

	g_secdp_sysfs = NULL;
}
