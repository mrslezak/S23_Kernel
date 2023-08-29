// SPDX-License-Identifier: GPL-2.0
/*
 * COPYRIGHT(C) 2022 Samsung Electronics Co., Ltd. All Right Reserved.
 */

#include <trace/hooks/traps.h>

#include <linux/samsung/debug/qcom/sec_qc_mem_debug.h>

#include "sec_qc_debug.h"

/* NOTE: BACKPORT from android12-5.10/msm-kernel, qcom_cpu_vendor_hooks.c */
static void sec_trace_android_rvh_do_undefinstr(void *unused, struct pt_regs *regs)
{
	if (user_mode(regs))
		return;

	sec_qc_memdbg_dump_instr("PC", regs->pc);
	sec_qc_memdbg_dump_instr("LR", ptrauth_strip_insn_pac(regs->regs[30]));
	sec_qc_memdbg_show_regs_min(regs);
}

int sec_qc_vendor_hooks_init(struct builder *bd)
{
	struct device *dev = bd->dev;
	int err;

	err = register_trace_android_rvh_do_undefinstr(
			sec_trace_android_rvh_do_undefinstr, NULL);
	if (err)
		dev_warn(dev, "Failed to android_rvh_do_undefinstr hook\n");

	return 0;
}
