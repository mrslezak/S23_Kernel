/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_NPU_CC_SM8250_H
#define _DT_BINDINGS_CLK_QCOM_NPU_CC_SM8250_H

/* NPU_CC clocks */
#define NPU_CC_PLL0						0
#define NPU_CC_PLL1						1
#define NPU_Q6SS_PLL						2
#define NPU_CC_ATB_CLK						3
#define NPU_CC_BTO_CORE_CLK					4
#define NPU_CC_BWMON_CLK					5
#define NPU_CC_CAL_HM0_CDC_CLK					6
#define NPU_CC_CAL_HM0_CLK					7
#define NPU_CC_CAL_HM0_CLK_SRC					8
#define NPU_CC_CAL_HM0_DPM_IP_CLK				9
#define NPU_CC_CAL_HM0_PERF_CNT_CLK				10
#define NPU_CC_CAL_HM1_CDC_CLK					11
#define NPU_CC_CAL_HM1_CLK					12
#define NPU_CC_CAL_HM1_CLK_SRC					13
#define NPU_CC_CAL_HM1_DPM_IP_CLK				14
#define NPU_CC_CAL_HM1_PERF_CNT_CLK				15
#define NPU_CC_CORE_CLK						16
#define NPU_CC_CORE_CLK_SRC					17
#define NPU_CC_DL_DPM_CLK					18
#define NPU_CC_DL_LLM_CLK					19
#define NPU_CC_DPM_CLK						20
#define NPU_CC_DPM_TEMP_CLK					21
#define NPU_CC_DPM_XO_CLK					22
#define NPU_CC_DSP_AHBM_CLK					23
#define NPU_CC_DSP_AHBS_CLK					24
#define NPU_CC_DSP_AXI_CLK					25
#define NPU_CC_DSP_BWMON_AHB_CLK				26
#define NPU_CC_DSP_BWMON_CLK					27
#define NPU_CC_ISENSE_CLK					28
#define NPU_CC_LLM_CLK						29
#define NPU_CC_LLM_CURR_CLK					30
#define NPU_CC_LLM_TEMP_CLK					31
#define NPU_CC_LLM_XO_CLK					32
#define NPU_CC_LMH_CLK_SRC					33
#define NPU_CC_NOC_AHB_CLK					34
#define NPU_CC_NOC_AXI_CLK					35
#define NPU_CC_NOC_DMA_CLK					36
#define NPU_CC_RSC_XO_CLK					37
#define NPU_CC_S2P_CLK						38
#define NPU_CC_XO_CLK						39
#define NPU_CC_XO_CLK_SRC					40
#define NPU_DSP_CORE_CLK_SRC					41

/* NPU_CC resets */
#define NPU_CC_CAL_HM0_BCR					0
#define NPU_CC_CAL_HM1_BCR					1
#define NPU_CC_CORE_BCR						2
#define NPU_CC_DSP_BCR						3

#endif
