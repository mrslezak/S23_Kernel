/*
 * calibration.h  --  Calibration defines for Cirrus Logic CS35L41 codec
 *
 * Copyright 2017 Cirrus Logic
 *
 * Author:	David Rhodes	<david.rhodes@cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/notifier.h>
#include <sound/soc.h>



#define CS35L41_NG_ENABLE_MASK	0x00010000

#define CSPL_STATE_RUNNING	0x00000000
#define CSPL_STATE_ERROR	0x00000001

#define CS35L41_VBST_CTL_11		0xAA
#define CIRRUS_CAL_CLASSH_DELAY_50MS	0x32
#define CIRRUS_CAL_CLASSD_DELAY_50MS	0x32

#define CIRRUS_CAL_VIMON_STATUS_INVALID	1
#define CIRRUS_CAL_VIMON_STATUS_SUCCESS	2

#define CSPL_STATUS_OUT_OF_RANGE	0x00000003
#define CSPL_STATUS_INCOMPLETE		0x00000002

#define CIRRUS_CAL_AMP_CONSTANT	        5.85714
#define CIRRUS_CAL_AMP_CONSTANT_NUM	292857
#define CIRRUS_CAL_AMP_CONSTANT_DENOM	50000
#define CIRRUS_CAL_RDC_RADIX		13

#define CIRRUS_CAL_RDC_DEFAULT		8580

#define CIRRUS_CAL_VFS_MV		12300
#define CIRRUS_CAL_IFS_MA		2100

#define CIRRUS_CAL_V_VAL_UB_MV		2000
#define CIRRUS_CAL_V_VAL_LB_MV		50

#define CIRRUS_CAL_VIMON_CAL_VSC_UB	0x00010624
#define CIRRUS_CAL_VIMON_CAL_VSC_LB	0x00FEF9DC
#define CIRRUS_CAL_VIMON_CAL_ISC_UB	0x00004189
#define CIRRUS_CAL_VIMON_CAL_ISC_LB	0x00FFBE77

#define CS35L41_CAL_RTLOG_ID_V_PEAK	947
#define CS35L41_CAL_RTLOG_ID_I_PEAK	948
#define CIRRUS_CAL_RTLOG_ID_V_PEAK	1064
#define CIRRUS_CAL_RTLOG_ID_I_PEAK	1065
#define CIRRUS_CAL_RTLOG_ID_TEMP	111
#define CIRRUS_CAL_RTLOG_RADIX_TEMP	14

#define CS35L41_MPU_UNLOCK_CODE_0		0x5555
#define CS35L41_MPU_UNLOCK_CODE_1		0xaaaa

#ifdef CONFIG_SND_SOC_CIRRUS_REINIT_SYSFS
#define CIRRUS_CAL_NUM_ATTRS_BASE	5
#else
#define CIRRUS_CAL_NUM_ATTRS_BASE	4
#endif

#define CIRRUS_CAL_NUM_ATTRS_AMP	7

extern struct cirrus_cal_ops cirrus_cspl_cal_ops;
extern struct cirrus_cal_ops cirrus_cs35l43_cal_ops;

/* needs to match ops container struct in cirrus-amp.c */
enum cirrus_cal_ops_idx {
	CIRRUS_CAL_OPS_INVALID,
	CIRRUS_CAL_CSPL_CAL_OPS_IDX,
	CIRRUS_CAL_CS35L43_CAL_OPS_IDX,
};

int cirrus_cal_read_temp(const char *mfd_suffix);
int cirrus_cal_apply(const char *mfd_suffix);
int cirrus_cal_set_surface_temp(const char *suffix, int temperature);
int cirrus_cal_init(void);
void cirrus_cal_exit(void);

