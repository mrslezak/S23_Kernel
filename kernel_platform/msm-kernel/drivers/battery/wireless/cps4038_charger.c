/*
 *  cps4038_charger.c
 *  Samsung CPS4038 IC Charger Driver
 *
 *  Copyright (C) 2022 Samsung Electronics
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "cps4038_charger.h"
#include <linux/errno.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/pm.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/kernel.h>
#include <asm/uaccess.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/ctype.h>
#include <linux/firmware.h>
#if IS_ENABLED(CONFIG_SPU_VERIFY)
#include <linux/spu-verify.h>
#endif

#define ENABLE 1
#define DISABLE 0
#define CMD_CNT 3
#define ISR_CNT 10
#define MAX_I2C_ERROR_COUNT		30
#define MAX_MTP_PGM_CNT 3

#define MAX_BUF 4095
#define SENDSZ 16

#if defined(CONFIG_WIRELESS_IC_PARAM)
static unsigned int __read_mostly wireless_ic;
module_param(wireless_ic, uint, 0444);
#endif

static u8 ADT_buffer_rdata[MAX_BUF] = {0, };
static int adt_readSize;
static bool is_shutdn;

typedef struct _sgf_data {
	unsigned int	size;
	unsigned int	type;
	char			*data;
} sgf_data;

static const u16 mfc_cps_vout_val16[] = {
	0x006F, /* MFC_VOUT_4_5V */
	0x0088, /* MFC_VOUT_5V */
	0x00A1, /* MFC_VOUT_5_5V */
	0x00BA, /* MFC_VOUT_6V */
	0x00EC, /* MFC_VOUT_7V */
	0x011E, /* MFC_VOUT_8V */
	0x0150, /* MFC_VOUT_9V */
	0x0182, /* MFC_VOUT_10V */
	0x01B4, /* MFC_VOUT_11V */
	0x01E6, /* MFC_VOUT_12V */
	0x01ff, /* MFC_VOUT_12_5V */
	0x0079, /* MFC_VOUT_OTG, 4.7V */
};

static struct device_attribute mfc_attrs[] = {
	CPS4038_ATTR(mfc_addr),
	CPS4038_ATTR(mfc_size),
	CPS4038_ATTR(mfc_data),
	CPS4038_ATTR(mfc_packet),
};

static enum power_supply_property mfc_charger_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};
static const u8 CPS4038_BL[0x800] = {
	// CPS4038_BL_01_07_V0.2_CRC57AE
	0x78, 0x1D, 0x00, 0x20, 0xF9, 0x04, 0x00, 0x20,
	0x55, 0x01, 0x00, 0x20, 0x7D, 0x01, 0x00, 0x20,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x59, 0x01, 0x00, 0x20,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x5B, 0x01, 0x00, 0x20, 0x5D, 0x01, 0x00, 0x20,
	0x5F, 0x01, 0x00, 0x20, 0x5F, 0x01, 0x00, 0x20,
	0x5F, 0x01, 0x00, 0x20, 0x5F, 0x01, 0x00, 0x20,
	0x5F, 0x01, 0x00, 0x20, 0x5F, 0x01, 0x00, 0x20,
	0x5F, 0x01, 0x00, 0x20, 0x5F, 0x01, 0x00, 0x20,
	0x5F, 0x01, 0x00, 0x20, 0x5F, 0x01, 0x00, 0x20,
	0x5F, 0x01, 0x00, 0x20, 0x5F, 0x01, 0x00, 0x20,
	0x5F, 0x01, 0x00, 0x20, 0x5F, 0x01, 0x00, 0x20,
	0x5F, 0x01, 0x00, 0x20, 0x5F, 0x01, 0x00, 0x20,
	0x5F, 0x01, 0x00, 0x20, 0x5F, 0x01, 0x00, 0x20,
	0x5F, 0x01, 0x00, 0x20, 0x5F, 0x01, 0x00, 0x20,
	0x5F, 0x01, 0x00, 0x20, 0x5F, 0x01, 0x00, 0x20,
	0x5F, 0x01, 0x00, 0x20, 0x5F, 0x01, 0x00, 0x20,
	0x5F, 0x01, 0x00, 0x20, 0x5F, 0x01, 0x00, 0x20,
	0x5F, 0x01, 0x00, 0x20, 0x5F, 0x01, 0x00, 0x20,
	0x5F, 0x01, 0x00, 0x20, 0x5F, 0x01, 0x00, 0x20,
	0x5F, 0x01, 0x00, 0x20, 0x5F, 0x01, 0x00, 0x20,
	0x00, 0x01, 0x01, 0x07, 0x00, 0x02, 0x00, 0x00,
	0x28, 0x05, 0x00, 0x00, 0x00, 0xF0, 0x02, 0xF8,
	0x00, 0xF0, 0x30, 0xF8, 0x0C, 0xA0, 0x30, 0xC8,
	0x08, 0x38, 0x24, 0x18, 0x2D, 0x18, 0xA2, 0x46,
	0x67, 0x1E, 0xAB, 0x46, 0x54, 0x46, 0x5D, 0x46,
	0xAC, 0x42, 0x01, 0xD1, 0x00, 0xF0, 0x22, 0xF8,
	0x7E, 0x46, 0x0F, 0x3E, 0x0F, 0xCC, 0xB6, 0x46,
	0x01, 0x26, 0x33, 0x42, 0x00, 0xD0, 0xFB, 0x1A,
	0xA2, 0x46, 0xAB, 0x46, 0x33, 0x43, 0x18, 0x47,
	0x0C, 0x04, 0x00, 0x00, 0x1C, 0x04, 0x00, 0x00,
	0x00, 0x23, 0x00, 0x24, 0x00, 0x25, 0x00, 0x26,
	0x10, 0x3A, 0x01, 0xD3, 0x78, 0xC1, 0xFB, 0xD8,
	0x52, 0x07, 0x00, 0xD3, 0x30, 0xC1, 0x00, 0xD5,
	0x0B, 0x60, 0x70, 0x47, 0x1F, 0xB5, 0x1F, 0xBD,
	0x10, 0xB5, 0x10, 0xBD, 0x00, 0xF0, 0xAD, 0xF9,
	0x11, 0x46, 0xFF, 0xF7, 0xF7, 0xFF, 0x00, 0xF0,
	0x5D, 0xF9, 0x00, 0xF0, 0xC5, 0xF9, 0x03, 0xB4,
	0xFF, 0xF7, 0xF2, 0xFF, 0x03, 0xBC, 0x00, 0xF0,
	0xCB, 0xF9, 0x00, 0x00, 0xFE, 0xE7, 0xFE, 0xE7,
	0xFE, 0xE7, 0xFE, 0xE7, 0xFE, 0xE7, 0xFE, 0xE7,
	0x02, 0x48, 0x03, 0x49, 0x03, 0x4A, 0x04, 0x4B,
	0x70, 0x47, 0x00, 0x00, 0x78, 0x18, 0x00, 0x20,
	0x78, 0x1D, 0x00, 0x20, 0x78, 0x19, 0x00, 0x20,
	0x78, 0x19, 0x00, 0x20, 0xFE, 0xE7, 0xA9, 0x48,
	0x01, 0x68, 0x00, 0x29, 0x08, 0xD1, 0xA8, 0x4A,
	0x0B, 0x21, 0xD1, 0x61, 0xA6, 0x4A, 0xA7, 0x49,
	0xC0, 0x32, 0x91, 0x63, 0x01, 0x21, 0x01, 0x60,
	0x70, 0x47, 0xA2, 0x48, 0x01, 0x68, 0x01, 0x29,
	0x09, 0xD1, 0xA1, 0x49, 0x03, 0x22, 0xCA, 0x61,
	0x0B, 0x22, 0xCA, 0x61, 0x9E, 0x4A, 0x00, 0x21,
	0xC0, 0x32, 0x91, 0x63, 0x01, 0x60, 0x70, 0x47,
	0x30, 0xB5, 0x5F, 0x22, 0x00, 0x23, 0x12, 0x02,
	0x90, 0x42, 0x04, 0xDB, 0x9A, 0x4A, 0x90, 0x42,
	0x01, 0xD0, 0x00, 0x20, 0x30, 0xBD, 0x96, 0x4A,
	0x90, 0x60, 0xD1, 0x60, 0x03, 0x21, 0xD1, 0x61,
	0x07, 0x20, 0xD0, 0x61, 0xD0, 0x61, 0xD0, 0x61,
	0xD0, 0x61, 0xD1, 0x61, 0x93, 0x48, 0x01, 0x24,
	0x15, 0x6A, 0x00, 0x2D, 0x05, 0xD0, 0x40, 0x1E,
	0xFA, 0xD2, 0xD4, 0x61, 0xD1, 0x61, 0x01, 0x23,
	0xF6, 0xE7, 0x08, 0x20, 0x40, 0x1E, 0xFD, 0xD2,
	0x18, 0x46, 0x30, 0xBD, 0x70, 0xB5, 0x04, 0x46,
	0x00, 0x20, 0x8B, 0x4D, 0x02, 0x46, 0x0E, 0xE0,
	0xA3, 0x5C, 0x1B, 0x02, 0x58, 0x40, 0x00, 0x23,
	0x06, 0x04, 0x02, 0xD5, 0x40, 0x00, 0x68, 0x40,
	0x00, 0xE0, 0x40, 0x00, 0x5B, 0x1C, 0x80, 0xB2,
	0x08, 0x2B, 0xF5, 0xDB, 0x52, 0x1C, 0x8A, 0x42,
	0xEE, 0xDB, 0x70, 0xBD, 0x00, 0x25, 0xFF, 0xF7,
	0xA2, 0xFF, 0x69, 0x1E, 0x7C, 0x48, 0xFF, 0xF7,
	0xBB, 0xFF, 0xFF, 0xF7, 0xAA, 0xFF, 0x7D, 0x4F,
	0x7D, 0x49, 0x08, 0x68, 0x00, 0x28, 0xFB, 0xD0,
	0x00, 0x22, 0x0A, 0x60, 0x14, 0x46, 0x01, 0x22,
	0x39, 0x68, 0x52, 0x02, 0x91, 0x42, 0x01, 0xDD,
	0x40, 0x20, 0x7B, 0xE0, 0x66, 0x22, 0x77, 0x49,
	0x90, 0x28, 0x0A, 0x60, 0x24, 0xD0, 0x06, 0xDC,
	0x10, 0x28, 0x41, 0xD0, 0x20, 0x28, 0x5B, 0xD0,
	0x80, 0x28, 0x73, 0xD1, 0x34, 0xE0, 0xB0, 0x28,
	0x02, 0xD0, 0xC0, 0x28, 0x6E, 0xD1, 0x66, 0xE0,
	0xFF, 0xF7, 0x87, 0xFF, 0x6E, 0x48, 0x84, 0x68,
	0x80, 0x14, 0x84, 0x42, 0x02, 0xDD, 0x6D, 0x48,
	0x04, 0x60, 0x45, 0xE0, 0x01, 0x20, 0x21, 0x1F,
	0x40, 0x07, 0xFF, 0xF7, 0xAF, 0xFF, 0x69, 0x4A,
	0x69, 0x49, 0x10, 0x60, 0x61, 0x18, 0xC9, 0x6F,
	0x51, 0x60, 0x88, 0x42, 0x38, 0xD1, 0x50, 0xE0,
	0xFF, 0xF7, 0x6F, 0xFF, 0xC0, 0x20, 0x84, 0x68,
	0x00, 0x2C, 0x31, 0xDB, 0xC0, 0x01, 0x84, 0x42,
	0x2E, 0xDC, 0xC8, 0x2C, 0x2C, 0xDB, 0x21, 0x1F,
	0x00, 0x20, 0xFF, 0xF7, 0x97, 0xFF, 0x01, 0x46,
	0x5C, 0x48, 0x80, 0x3C, 0x01, 0x60, 0xE2, 0x6F,
	0x42, 0x60, 0x91, 0x42, 0x20, 0xD1, 0x38, 0xE0,
	0xFF, 0xF7, 0x49, 0xFF, 0x59, 0x49, 0x50, 0x48,
	0xFF, 0xF7, 0x62, 0xFF, 0x04, 0x46, 0x11, 0xE0,
	0xFF, 0xF7, 0x41, 0xFF, 0x00, 0x26, 0x08, 0xE0,
	0x52, 0x48, 0xB1, 0x00, 0x41, 0x58, 0x28, 0x46,
	0xFF, 0xF7, 0x56, 0xFF, 0x04, 0x43, 0x2D, 0x1D,
	0x76, 0x1C, 0x38, 0x68, 0x86, 0x42, 0xF3, 0xDB,
	0x4C, 0x48, 0x1A, 0xE0, 0xFF, 0xF7, 0x3D, 0xFF,
	0x60, 0x1C, 0x1F, 0xD0, 0x00, 0x2C, 0x18, 0xD0,
	0xAA, 0x21, 0x46, 0x48, 0x01, 0x60, 0x8B, 0xE7,
	0xFF, 0xF7, 0x25, 0xFF, 0x00, 0x26, 0x08, 0xE0,
	0x47, 0x48, 0xB1, 0x00, 0x41, 0x58, 0x28, 0x46,
	0xFF, 0xF7, 0x3A, 0xFF, 0x04, 0x43, 0x2D, 0x1D,
	0x76, 0x1C, 0x38, 0x68, 0x86, 0x42, 0xF3, 0xDB,
	0x41, 0x48, 0x05, 0x60, 0xE2, 0xE7, 0x3D, 0x48,
	0x05, 0x68, 0x55, 0x20, 0x39, 0x49, 0x08, 0x60,
	0x72, 0xE7, 0xFF, 0xE7, 0x40, 0x21, 0xE0, 0xE7,
	0x3D, 0x48, 0x3C, 0x49, 0x01, 0x60, 0x3D, 0x4A,
	0x00, 0x21, 0x91, 0x60, 0x33, 0x22, 0x3C, 0x4B,
	0x92, 0x03, 0x5A, 0x60, 0x01, 0x60, 0x70, 0x47,
	0x01, 0x20, 0x3A, 0x49, 0x80, 0x04, 0x48, 0x63,
	0x38, 0x48, 0x39, 0x49, 0x40, 0x30, 0x81, 0x62,
	0x01, 0x21, 0xC9, 0x05, 0xC1, 0x62, 0x40, 0x21,
	0x41, 0x63, 0xFF, 0x21, 0x35, 0x4A, 0xBD, 0x31,
	0x11, 0x60, 0x35, 0x49, 0x41, 0x62, 0x30, 0x49,
	0x00, 0x14, 0x48, 0x60, 0x70, 0x47, 0x70, 0xB5,
	0x2D, 0x48, 0x00, 0x24, 0x80, 0x30, 0x04, 0x62,
	0x31, 0x4B, 0x30, 0x4D, 0xDD, 0x61, 0x2B, 0x49,
	0x07, 0x20, 0x40, 0x31, 0x48, 0x61, 0xFF, 0xF7,
	0xDB, 0xFF, 0xDC, 0x61, 0x18, 0x48, 0x38, 0x38,
	0xC0, 0x6B, 0x2C, 0x49, 0x88, 0x42, 0x0F, 0xD1,
	0xDD, 0x61, 0xC1, 0x21, 0x23, 0x4A, 0x00, 0x20,
	0xC9, 0x01, 0x80, 0x3A, 0x45, 0x18, 0x2D, 0x68,
	0x86, 0x18, 0x35, 0x60, 0x00, 0x1D, 0x80, 0x28,
	0xF8, 0xDB, 0xDC, 0x61, 0x01, 0x20, 0x70, 0xBD,
	0x00, 0x20, 0x70, 0xBD, 0xFF, 0x21, 0x09, 0x48,
	0x01, 0x31, 0x41, 0x60, 0x0D, 0x4A, 0x49, 0x00,
	0x11, 0x60, 0x0D, 0x4A, 0x00, 0x21, 0x11, 0x60,
	0x0C, 0x4B, 0x55, 0x22, 0x1A, 0x60, 0x01, 0x60,
	0xFF, 0xF7, 0xC9, 0xFF, 0xFF, 0xF7, 0xA4, 0xFF,
	0xFF, 0xF7, 0x08, 0xFF, 0x0C, 0x18, 0x00, 0x20,
	0x00, 0x20, 0x01, 0x40, 0x78, 0x56, 0x00, 0x00,
	0xF8, 0x60, 0x00, 0x00, 0x50, 0xC3, 0x00, 0x00,
	0x21, 0x10, 0x00, 0x00, 0x08, 0x18, 0x00, 0x20,
	0x00, 0x18, 0x00, 0x20, 0x04, 0x18, 0x00, 0x20,
	0xC0, 0x00, 0x00, 0x20, 0x00, 0x08, 0x00, 0x20,
	0x80, 0xFF, 0xFF, 0x1F, 0x85, 0xE1, 0x24, 0x57,
	0x00, 0x10, 0x00, 0x20, 0x51, 0xE5, 0xCC, 0x1A,
	0x00, 0x8C, 0x00, 0x40, 0x00, 0x80, 0x00, 0x40,
	0x00, 0x00, 0x04, 0x40, 0x80, 0xF0, 0x00, 0x40,
	0x2C, 0x00, 0x00, 0x04, 0x00, 0x40, 0x00, 0x40,
	0x00, 0x02, 0xA1, 0x02, 0x50, 0x12, 0x00, 0x00,
	0x40, 0xE7, 0x00, 0x40, 0x4E, 0x87, 0x55, 0x74,
	0x70, 0x47, 0x70, 0x47, 0x70, 0x47, 0x70, 0x47,
	0x70, 0x47, 0x75, 0x46, 0x00, 0xF0, 0x24, 0xF8,
	0xAE, 0x46, 0x05, 0x00, 0x69, 0x46, 0x53, 0x46,
	0xC0, 0x08, 0xC0, 0x00, 0x85, 0x46, 0x18, 0xB0,
	0x20, 0xB5, 0xFF, 0xF7, 0x59, 0xFE, 0x60, 0xBC,
	0x00, 0x27, 0x49, 0x08, 0xB6, 0x46, 0x00, 0x26,
	0xC0, 0xC5, 0xC0, 0xC5, 0xC0, 0xC5, 0xC0, 0xC5,
	0xC0, 0xC5, 0xC0, 0xC5, 0xC0, 0xC5, 0xC0, 0xC5,
	0x40, 0x3D, 0x49, 0x00, 0x8D, 0x46, 0x70, 0x47,
	0x10, 0xB5, 0x04, 0x46, 0xC0, 0x46, 0xC0, 0x46,
	0x20, 0x46, 0xFF, 0xF7, 0x34, 0xFE, 0x10, 0xBD,
	0x00, 0x48, 0x70, 0x47, 0x14, 0x18, 0x00, 0x20,
	0x01, 0x49, 0x18, 0x20, 0xAB, 0xBE, 0xFE, 0xE7,
	0x26, 0x00, 0x02, 0x00, 0x70, 0x47, 0x00, 0x00,
	0x03, 0x49, 0x00, 0x20, 0x48, 0x60, 0x08, 0x60,
	0x02, 0x48, 0x80, 0x47, 0x02, 0x48, 0x00, 0x47,
	0x14, 0xE1, 0x00, 0x40, 0x8B, 0x04, 0x00, 0x20,
	0xCD, 0x00, 0x00, 0x20, 0x24, 0x05, 0x00, 0x20,
	0x00, 0x08, 0x00, 0x20, 0x78, 0x15, 0x00, 0x00,
	0x10, 0x01, 0x00, 0x20, 0x16, 0x2B, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static irqreturn_t mfc_wpc_det_irq_thread(int irq, void *irq_data);
static irqreturn_t mfc_wpc_irq_thread(int irq, void *irq_data);
static int mfc_reg_multi_write_verify(struct i2c_client *client, u16 reg, const u8 *val, int size);
static void mfc_set_tx_fod_thresh1(struct i2c_client *client, u32 fod_thresh1);
static void mfc_set_tx_fod_ta_thresh(struct i2c_client *client, u32 fod_thresh);
static int mfc_cps_wls_write_word(struct i2c_client *client, u32 reg, u32 val);
static int mfc_cps_wls_read_word(struct i2c_client *client, u32 reg, u8 *val);

#if defined(CONFIG_WIRELESS_IC_PARAM)
static unsigned int mfc_get_wrlic(void) { return wireless_ic; }
#endif

static void mfc_check_i2c_error(struct mfc_charger_data *charger, bool is_error)
{
	charger->i2c_error_count =
		(charger->det_state && gpio_get_value(charger->pdata->wpc_det) && is_error) ?
		(charger->i2c_error_count + 1) : 0;

	if (charger->i2c_error_count > MAX_I2C_ERROR_COUNT) {
		charger->i2c_error_count = 0;
		queue_delayed_work(charger->wqueue, &charger->wpc_i2c_error_work, 0);
	}
}

static bool is_unknown_pad(struct mfc_charger_data *charger)
{
	pr_info("%s: tx_id_cnt(%d) tx_id(%d)\n", __func__, charger->tx_id_cnt, charger->tx_id);

	if (charger->tx_id_cnt >= TX_ID_CHECK_CNT && charger->tx_id == TX_ID_UNKNOWN)
		return true;
	return false;
}

static int mfc_reg_read(struct i2c_client *client, u16 reg, u8 *val)
{
	struct mfc_charger_data *charger = i2c_get_clientdata(client);
	int ret;
	struct i2c_msg msg[2];
	u8 wbuf[2];
	u8 rbuf[2];

	if (charger->reg_access_lock) {
		pr_err("%s: can not access to reg during fw update\n", __func__);
		return -1;
	}

	msg[0].addr = client->addr;
	msg[0].flags = client->flags & I2C_M_TEN;
	msg[0].len = 2;
	msg[0].buf = wbuf;

	wbuf[0] = (reg & 0xFF00) >> 8;
	wbuf[1] = (reg & 0xFF);

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = rbuf;

	mutex_lock(&charger->io_lock);
	ret = i2c_transfer(client->adapter, msg, 2);
	mfc_check_i2c_error(charger, (ret < 0));
	mutex_unlock(&charger->io_lock);
	if (ret < 0) {
		pr_err("%s: i2c read error, reg: 0x%x, ret: %d (called by %ps)\n",
			__func__, reg, ret, __builtin_return_address(0));
		return -1;
	}

	*val = rbuf[0];

	return ret;
}

static int mfc_reg_multi_read(struct i2c_client *client, u16 reg, u8 *val, int size)
{
	struct mfc_charger_data *charger = i2c_get_clientdata(client);
	int ret;
	struct i2c_msg msg[2];
	u8 wbuf[2];

	if (charger->reg_access_lock) {
		pr_err("%s: can not access to reg during fw update\n", __func__);
		return -1;
	}

	msg[0].addr = client->addr;
	msg[0].flags = client->flags & I2C_M_TEN;
	msg[0].len = 2;
	msg[0].buf = wbuf;

	wbuf[0] = (reg & 0xFF00) >> 8;
	wbuf[1] = (reg & 0xFF);

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = size;
	msg[1].buf = val;

	mutex_lock(&charger->io_lock);
	ret = i2c_transfer(client->adapter, msg, 2);
	mfc_check_i2c_error(charger, (ret < 0));
	mutex_unlock(&charger->io_lock);
	if (ret < 0) {
		pr_err("%s: i2c transfer fail", __func__);
		return -1;
	}

	return ret;
}

static int mfc_reg_write(struct i2c_client *client, u16 reg, u8 val)
{
	struct mfc_charger_data *charger = i2c_get_clientdata(client);
	int ret;
	unsigned char data[3] = { reg >> 8, reg & 0xff, val };

	if (charger->reg_access_lock) {
		pr_err("%s: can not access to reg during fw update\n", __func__);
		return -1;
	}

	mutex_lock(&charger->io_lock);
	ret = i2c_master_send(client, data, 3);
	mfc_check_i2c_error(charger, (ret < 3));
	mutex_unlock(&charger->io_lock);
	if (ret < 3) {
		pr_err("%s: i2c write error, reg: 0x%x, ret: %d (called by %ps)\n",
				__func__, reg, ret, __builtin_return_address(0));
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

static int mfc_reg_update(struct i2c_client *client, u16 reg, u8 val, u8 mask)
{
	//val = 0b 0000 0001
	//ms = 0b 1111 1110
	struct mfc_charger_data *charger = i2c_get_clientdata(client);
	unsigned char data[3] = {reg >> 8, reg & 0xff, val};
	u8 data2;
	int ret;

	if (charger->reg_access_lock) {
		pr_err("%s: can not access to reg during fw update\n", __func__);
		return -1;
	}

	ret = mfc_reg_read(client, reg, &data2);
	if (ret >= 0) {
		u8 old_val = data2 & 0xff;
		u8 new_val = (val & mask) | (old_val & (~mask));

		data[2] = new_val;
		mutex_lock(&charger->io_lock);
		ret = i2c_master_send(client, data, 3);
		mfc_check_i2c_error(charger, (ret < 3));
		mutex_unlock(&charger->io_lock);
		if (ret < 3) {
			pr_err("%s: i2c write error, reg: 0x%x, ret: %d\n",
				__func__, reg, ret);
			return ret < 0 ? ret : -EIO;
		}
	}
	mfc_reg_read(client, reg, &data2);

	return ret;
}

static int mfc_get_firmware_version(struct mfc_charger_data *charger, int firm_mode)
{
	int ver_major = -1, ver_minor = -1;
	int ret;
	u8 fw_major[2] = {0, };
	u8 fw_minor[2] = {0, };

	pr_info("%s: called by (%ps)\n", __func__, __builtin_return_address(0));
	switch (firm_mode) {
	case MFC_RX_FIRMWARE:
		ret = mfc_reg_read(charger->client, MFC_FW_MAJOR_REV_L_REG, &fw_major[0]);
		if (ret < 0)
			break;

		ret = mfc_reg_read(charger->client, MFC_FW_MAJOR_REV_H_REG, &fw_major[1]);
		if (ret < 0)
			break;

		ver_major = fw_major[0] | (fw_major[1] << 8);
		pr_info("%s: rx major firmware version 0x%x\n", __func__, ver_major);

		ret = mfc_reg_read(charger->client, MFC_FW_MINOR_REV_L_REG, &fw_minor[0]);
		if (ret < 0)
			break;

		ret = mfc_reg_read(charger->client, MFC_FW_MINOR_REV_H_REG, &fw_minor[1]);
		if (ret < 0)
			break;

		ver_minor = fw_minor[0] | (fw_minor[1] << 8);
		pr_info("%s: rx minor firmware version 0x%x\n", __func__, ver_minor);
		break;
	default:
		pr_err("%s: Wrong firmware mode\n", __func__);
		ver_major = -1;
		ver_minor = -1;
		break;
	}

#if defined(CONFIG_WIRELESS_IC_PARAM)
	if (ver_minor > 0 && charger->wireless_fw_ver_param != ver_minor) {
		pr_info("%s: fw_ver (param:0x%04X, version:0x%04X)\n",
			__func__, charger->wireless_fw_ver_param, ver_minor);
		charger->wireless_fw_ver_param = ver_minor;
		charger->wireless_param_info &= 0xFF0000FF;
		charger->wireless_param_info |= (charger->wireless_fw_ver_param & 0xFFFF) << 8;
		pr_info("%s: wireless_param_info (0x%08X)\n", __func__, charger->wireless_param_info);
	}
#endif

	return ver_minor;
}

static bool mfc_check_chip_id(struct mfc_charger_data *charger)
{
	u8 data[4] = {0, };

	charger->reg_access_lock = true;

	mfc_cps_wls_write_word(charger->client, 0xFFFFFF00, 0x0000000E); /*enable 32bit i2c*/
	mfc_cps_wls_write_word(charger->client, 0x4000E75C, 0x00001250); /*write password*/
	mfc_cps_wls_read_word(charger->client, 0x60D4, data);
	mfc_cps_wls_write_word(charger->client, 0xFFFFFF00, 0x00000000); /* i2c 32bit mode disable */

	charger->reg_access_lock = false;

	pr_info("%s: CHIP_L(0x%x), CHIP_H(0x%x)\n", __func__, data[2], data[3]);

	if ((data[2] == 0x38) && (data[3] == 0x40))
		return true;

	return false;
}

static int mfc_get_chip_id(struct mfc_charger_data *charger)
{
	u8 chip_id = 0, chip_id_h = 0;
	int ret = 0;

	ret = mfc_reg_read(charger->client, MFC_CHIP_ID_L_REG, &chip_id);
	ret = mfc_reg_read(charger->client, MFC_CHIP_ID_H_REG, &chip_id_h);
	if (ret >= 0) {
		pr_info("%s: CHIP_L(0x%x), CHIP_H(0x%x)\n", __func__, chip_id, chip_id_h);
		charger->chip_id = MFC_CHIP_CPS;
	} else
		return -1;

#if defined(CONFIG_WIRELESS_IC_PARAM)
	if (chip_id > 0 && charger->wireless_chip_id_param != chip_id) {
		pr_info("%s: chip_id (param:0x%02X, chip_id:0x%02X)\n",
			__func__, charger->wireless_chip_id_param, chip_id);
		charger->wireless_chip_id_param = chip_id;
		charger->wireless_param_info &= 0x00FFFFFF;
		charger->wireless_param_info |= (charger->wireless_chip_id_param & 0xFF) << 24;
		pr_info("%s: wireless_param_info (0x%08X)\n", __func__, charger->wireless_param_info);
	}
#endif

	return charger->chip_id;
}

static int mfc_get_ic_revision(struct mfc_charger_data *charger, int read_mode)
{
	u8 temp;
	int ret = -1;

	pr_info("%s: called by (%ps)\n", __func__, __builtin_return_address(0));

	switch (read_mode) {
	case MFC_IC_REVISION:
		ret = mfc_reg_read(charger->client, MFC_CHIP_REVISION_REG, &temp);
		if (ret >= 0) {
			temp &= MFC_CHIP_REVISION_MASK;
			pr_info("%s: ic revision = %d\n", __func__, temp);
			ret = temp;
		}
		break;
	case MFC_IC_FONT:
		ret = mfc_reg_read(charger->client, MFC_CHIP_REVISION_REG, &temp);
		if (ret >= 0) {
			temp &= MFC_CHIP_FONT_MASK;
			pr_info("%s: ic font = %d\n", __func__, temp);
			ret = temp;
		}
		break;
	default:
		break;
	}
	return ret;
}

static int mfc_get_adc(struct mfc_charger_data *charger, int adc_type)
{
	int ret = 0;
	u8 data[2] = {0,};

	switch (adc_type) {
	case MFC_ADC_VOUT:
		ret = mfc_reg_read(charger->client, MFC_ADC_VOUT_L_REG, &data[0]);
		if (ret < 0)
			break;
		ret = mfc_reg_read(charger->client, MFC_ADC_VOUT_H_REG, &data[1]);
		if (ret < 0)
			break;

		ret = (data[0] | (data[1] << 8));
		break;
	case MFC_ADC_VRECT:
		ret = mfc_reg_read(charger->client, MFC_ADC_VRECT_L_REG, &data[0]);
		if (ret < 0)
			break;
		ret = mfc_reg_read(charger->client, MFC_ADC_VRECT_H_REG, &data[1]);
		if (ret < 0)
			break;

		ret = (data[0] | (data[1] << 8));
		break;
	case MFC_ADC_RX_IOUT:
		ret = mfc_reg_read(charger->client, MFC_ADC_IOUT_L_REG, &data[0]);
		if (ret < 0)
			break;
		ret = mfc_reg_read(charger->client, MFC_ADC_IOUT_H_REG, &data[1]);
		if (ret < 0)
			break;

		ret = (data[0] | (data[1] << 8));
		break;
	case MFC_ADC_DIE_TEMP:
		ret = mfc_reg_read(charger->client, MFC_ADC_DIE_TEMP_L_REG, &data[0]);
		if (ret < 0)
			break;
		ret = mfc_reg_read(charger->client, MFC_ADC_DIE_TEMP_H_REG, &data[1]);
		if (ret < 0)
			break;

		/* only 4 MSB[3:0] field is used, Celsius */
		data[1] &= 0x0f;
		ret = (data[0] | (data[1] << 8));
		break;
	case MFC_ADC_OP_FRQ: /* kHz */
		ret = mfc_reg_read(charger->client, MFC_TRX_OP_FREQ_L_REG, &data[0]);
		if (ret < 0)
			break;
		ret = mfc_reg_read(charger->client, MFC_TRX_OP_FREQ_H_REG, &data[1]);
		if (ret < 0)
			break;

		ret = (data[0] | (data[1] << 8)) / 10;
		break;
	case MFC_ADC_TX_MAX_OP_FRQ:
		ret = mfc_reg_read(charger->client, MFC_TX_MAX_OP_FREQ_L_REG, &data[0]);
		if (ret < 0)
			break;
		ret = mfc_reg_read(charger->client, MFC_TX_MAX_OP_FREQ_H_REG, &data[1]);
		if (ret < 0)
			break;

		ret = (data[0] | (data[1] << 8)) / 10;
		break;
	case MFC_ADC_TX_MIN_OP_FRQ:
		ret = mfc_reg_read(charger->client, MFC_TX_MIN_OP_FREQ_L_REG, &data[0]);
		if (ret < 0)
			break;
		ret = mfc_reg_read(charger->client, MFC_TX_MIN_OP_FREQ_H_REG, &data[1]);
		if (ret < 0)
			break;

		ret = (data[0] | (data[1] << 8)) / 10;
		break;
	case MFC_ADC_PING_FRQ:
		ret = mfc_reg_read(charger->client, MFC_TX_PING_FREQ_L_REG, &data[0]);
		if (ret < 0)
			break;
		ret = mfc_reg_read(charger->client, MFC_TX_PING_FREQ_H_REG, &data[1]);
		if (ret < 0)
			break;

		ret = (data[0] | (data[1] << 8)) / 10;
		break;
	case MFC_ADC_TX_VOUT:
		ret = mfc_reg_read(charger->client, MFC_ADC_VOUT_L_REG, &data[0]);
		if (ret < 0)
			break;
		ret = mfc_reg_read(charger->client, MFC_ADC_VOUT_H_REG, &data[1]);
		if (ret < 0)
			break;

		ret = (data[0] | (data[1] << 8));
		break;
	case MFC_ADC_TX_IOUT:
		if (charger->wc_tx_enable) {
			ret = mfc_reg_read(charger->client, MFC_ADC_IRECT_L_REG, &data[0]);
			if (ret < 0)
				break;
			ret = mfc_reg_read(charger->client, MFC_ADC_IRECT_H_REG, &data[1]);
			if (ret < 0)
				break;
		} else {
			ret = mfc_reg_read(charger->client, MFC_ADC_IOUT_L_REG, &data[0]);
			if (ret < 0)
				break;
			ret = mfc_reg_read(charger->client, MFC_ADC_IOUT_H_REG, &data[1]);
			if (ret < 0)
				break;
		}

		ret = (data[0] | (data[1] << 8));
		break;
	default:
		break;
	}

	return ret;
}

static void mfc_cma_cmb_onoff(struct mfc_charger_data *charger, bool cma_on, bool cmb_on)
{
	u8 data;
	int ret;

	if (!charger->det_state) {
		pr_info("%s: wireless is disconnected\n", __func__);
		return;
	}

	if (is_unknown_pad(charger)) {
		if (charger->pdata->unknown_cmb_ctrl) {
			cma_on = true;
			cmb_on = true;
		} else {
			cma_on = true;
			cmb_on = false;
		}
	}

	data = ((cma_on) ? 0xC0 : 0x00) | ((cmb_on) ? 0x30 : 0x00);
	pr_info("%s: val(0x%x)\n", __func__, data);
	mfc_reg_write(charger->client, MFC_CMFET_CTRL_REG, data);
	ret = mfc_reg_read(charger->client, MFC_CMFET_CTRL_REG, &data);
	if (ret < 0)
		pr_err("%s: fail to read MFC_CMFET_CTRL_REG (%d)\n", __func__, ret);
	else
		pr_info("%s: read_val (0x%x)\n", __func__, data);
}

static void mfc_set_wpc_en(struct mfc_charger_data *charger, char flag, char on)
{
	union power_supply_propval value = {0, };
	int enable = 0, temp = charger->wpc_en_flag;
	int old_val = 0, new_val = 0;

	psy_do_property("battery", get,
		POWER_SUPPLY_PROP_STATUS, value);

	mutex_lock(&charger->wpc_en_lock);

	if (on) {
		if (value.intval == POWER_SUPPLY_STATUS_DISCHARGING)
			charger->wpc_en_flag |= WPC_EN_CHARGING;
		charger->wpc_en_flag |= flag;
	} else {
		charger->wpc_en_flag &= ~flag;
	}

	if (charger->wpc_en_flag & WPC_EN_FW)
		enable = 1;
	else if (!(charger->wpc_en_flag & WPC_EN_SYSFS) || !(charger->wpc_en_flag & WPC_EN_CCIC))
		enable = 0;
	else if (!(charger->wpc_en_flag & (WPC_EN_SLATE | WPC_EN_MST)))
		enable = 0;
	else if (!(charger->wpc_en_flag & (WPC_EN_CHARGING | WPC_EN_MST | WPC_EN_TX)))
		enable = 0;
	else
		enable = 1;

	if (charger->pdata->wpc_en >= 0) {
		old_val = gpio_get_value(charger->pdata->wpc_en);
		if (enable)
			gpio_direction_output(charger->pdata->wpc_en, 0);
		else
			gpio_direction_output(charger->pdata->wpc_en, 1);
		new_val = gpio_get_value(charger->pdata->wpc_en);

		pr_info("@DIS_MFC %s: before(0x%x), after(0x%x), en(%d), val(%d->%d)\n",
			__func__, temp, charger->wpc_en_flag, enable, old_val, new_val);

		mutex_unlock(&charger->wpc_en_lock);
		if (old_val != new_val) {
			value.intval = enable;
			psy_do_property("battery", set,
				POWER_SUPPLY_EXT_PROP_WPC_EN, value);
		}
	} else {
		pr_info("@DIS_MFC %s: there`s no wpc_en\n", __func__);
		mutex_unlock(&charger->wpc_en_lock);
	}
}

static void mfc_set_coil_sw_en(struct mfc_charger_data *charger, int enable)
{
#if defined(CONFIG_SEC_FACTORY)
	pr_info("@Tx_Mode %s: do not set with factory bin\n", __func__);
	return;
#endif

	if (charger->pdata->coil_sw_en < 0) {
		pr_info("@Tx_Mode %s: no coil_sw_en\n", __func__);
		return;
	}

	if (gpio_get_value(charger->pdata->coil_sw_en) == enable) {
		pr_info("@Tx_Mode %s: Skip to set same config, val(%d)\n",
			__func__, gpio_get_value(charger->pdata->coil_sw_en));
		return;
	}

	gpio_direction_output(charger->pdata->coil_sw_en, enable);
	pr_info("@Tx_Mode %s: en(%d), val(%d)\n", __func__,
		enable, gpio_get_value(charger->pdata->coil_sw_en));
}

static void mfc_set_vout(struct mfc_charger_data *charger, int vout)
{
	u8 data[2] = {0,};

	if (charger->wc_ldo_status == MFC_LDO_OFF) {
		pr_info("%s: vout updated to 5V becauseof wc ldo\n", __func__);
		vout = MFC_VOUT_5V;
	}

	data[0] = mfc_cps_vout_val16[vout] & 0xff;
	data[1] = (mfc_cps_vout_val16[vout] & 0xff00) >> 8;
	mfc_reg_write(charger->client, MFC_VOUT_SET_H_REG, data[1]);
	mfc_reg_write(charger->client, MFC_VOUT_SET_L_REG, data[0]);
	msleep(100);

	pr_info("%s: set vout(%s, 0x%04X) read = %d mV\n", __func__,
		sb_rx_vout_str(vout), (data[0] | (data[1] << 8)), mfc_get_adc(charger, MFC_ADC_VOUT));
	charger->pdata->vout_status = vout;
}

static int mfc_get_vout(struct mfc_charger_data *charger)
{
	u8 data[2] = {0,};
	int ret;

	ret = mfc_reg_read(charger->client, MFC_VOUT_SET_L_REG, &data[0]);
	ret = mfc_reg_read(charger->client, MFC_VOUT_SET_H_REG, &data[1]);

	if (ret < 0) {
		pr_err("%s: fail to read vout. (%d)\n", __func__, ret);
	} else {
		ret = (data[0] | (data[1] << 8));
		pr_info("%s: vout(0x%04x)\n", __func__, ret);
	}

	return ret;
}

static void mfc_uno_on(struct mfc_charger_data *charger, bool on)
{
	union power_supply_propval value = {0, };

	if (charger->wc_tx_enable && on) { /* UNO ON */
		value.intval = SEC_BAT_CHG_MODE_UNO_ONLY;
		psy_do_property("otg", set,
			POWER_SUPPLY_EXT_PROP_CHARGE_UNO_CONTROL, value);
		pr_info("%s: UNO ON\n", __func__);
	} else if (on) { /* UNO ON */
		value.intval = 1;
		psy_do_property("otg", set,
			POWER_SUPPLY_EXT_PROP_CHARGE_UNO_CONTROL, value);
		pr_info("%s: UNO ON\n", __func__);
	} else { /* UNO OFF */
		value.intval = 0;
		psy_do_property("otg", set,
			POWER_SUPPLY_EXT_PROP_CHARGE_UNO_CONTROL, value);
		pr_info("%s: UNO OFF\n", __func__);
	}
}

static void mfc_rpp_set(struct mfc_charger_data *charger, u8 val)
{
	u8 data;
	int ret;

	pr_info("%s: Scale Factor %d\n", __func__, val);
	mfc_reg_write(charger->client, MFC_RPP_SCALE_COEF_REG, val);
	usleep_range(5000, 6000);
	ret = mfc_reg_read(charger->client, MFC_RPP_SCALE_COEF_REG, &data);
	if (ret < 0)
		pr_err("%s: fail to read RPP scaling coefficient. (%d)\n", __func__, ret);
	else
		pr_info("%s: RPP scaling coefficient(0x%x)\n", __func__, data);
}

static struct mfc_fod_data *mfc_get_fod_data(struct mfc_charger_data *charger)
{
	int i = 0;

	if (charger->pdata->fod_data_count <= 0)
		return NULL;

	for (i = 0; i < charger->pdata->fod_data_count; i++)
		if (charger->pdata->fod_list[i].pad_id == charger->tx_id)
			return &charger->pdata->fod_list[i];

	return (charger->pdata->fod_list[DEFAULT_PAD_ID].pad_id != 0) ?
		NULL : &charger->pdata->fod_list[DEFAULT_PAD_ID];
}

static void mfc_fod_set(struct mfc_charger_data *charger, int fod_state)
{
	struct mfc_fod_data *pfod_data = NULL;
	int i;

	fod_state = (charger->is_full_status) ? FOD_STATE_FULL : fod_state;
	pr_info("%s: pad_id = 0x%X, is_full_status = %d, fod_state = %d\n",
		__func__, charger->tx_id, charger->is_full_status, fod_state);

	if (charger->pdata->cable_type == SEC_BATTERY_CABLE_NONE)
		return;

	pfod_data = mfc_get_fod_data(charger);
	if (pfod_data == NULL || pfod_data->data[fod_state] == NULL) {
		pr_info("%s: skip to set fod because fod data is null\n", __func__);
		return;
	}

	if (fod_state == FOD_STATE_FULL) {
		for (i = 0; i < MFC_NUM_FOD_REG; i++)
			mfc_reg_write(charger->client, MFC_WPC_FOD_0A_REG + i,
				pfod_data->data[fod_state][i]);

		for (i = 0; i < MFC_NUM_FOD_REG; i++)
			mfc_reg_write(charger->client, MFC_WPC_FWC_FOD_0A_REG + i,
				pfod_data->data[fod_state][i]);
	} else {
		for (i = 0; i < MFC_NUM_FOD_REG; i++)
			mfc_reg_write(charger->client, MFC_WPC_FOD_0A_REG + i,
				pfod_data->data[FOD_STATE_CC][i]);

		for (i = 0; i < MFC_NUM_FOD_REG; i++)
			mfc_reg_write(charger->client, MFC_WPC_FWC_FOD_0A_REG + i,
				pfod_data->data[FOD_STATE_HV][i]);
	}
}

static void mfc_set_tx_conflict_current(struct mfc_charger_data *charger, int tx_cf_current)
{
	u8 data = (u8)(tx_cf_current / 100);

	mfc_reg_write(charger->client, MFC_TX_CONFLICT_CURRENT_REG, data);
	pr_info("%s: current = %d, data = 0x%x\n", __func__, tx_cf_current, data);
}

static void mfc_set_cmd_l_reg(struct mfc_charger_data *charger, u8 val, u8 mask)
{
	u8 temp = 0;
	int ret = 0, i = 0;

	do {
		pr_info("%s\n", __func__);
		ret = mfc_reg_update(charger->client, MFC_AP2MFC_CMD_L_REG, val, mask); // command
		if (ret >= 0) {
			usleep_range(10000, 11000);
			ret = mfc_reg_read(charger->client, MFC_AP2MFC_CMD_L_REG, &temp); // check out set bit exists
			if (temp != 0)
				pr_info("%s: CMD is not clear yet, cnt = %d\n", __func__, i);
			if (ret < 0 || i > 3)
				break;
		}
		i++;
	} while ((temp != 0) && (i < 3));
}

static void mfc_send_eop(struct mfc_charger_data *charger, int health_mode)
{
	int i = 0;
	int ret = 0;

	pr_info("%s: health_mode(0x%x), cable_type(%d)\n",
		__func__, health_mode, charger->pdata->cable_type);

	switch (health_mode) {
	case POWER_SUPPLY_HEALTH_OVERHEAT:
	case POWER_SUPPLY_EXT_HEALTH_OVERHEATLIMIT:
	case POWER_SUPPLY_HEALTH_COLD:
		pr_info("%s: ept-ot\n", __func__);
		if (charger->pdata->cable_type == SEC_BATTERY_CABLE_PMA_WIRELESS) {
			for (i = 0; i < CMD_CNT; i++) {
				ret = mfc_reg_write(charger->client, MFC_EPT_REG, MFC_WPC_EPT_END_OF_CHG);
				if (ret < 0)
					break;

				mfc_set_cmd_l_reg(charger, MFC_CMD_SEND_EOP_MASK, MFC_CMD_SEND_EOP_MASK);
				msleep(250);
			}
		} else {
			for (i = 0; i < CMD_CNT; i++) {
				ret = mfc_reg_write(charger->client, MFC_EPT_REG, MFC_WPC_EPT_OVER_TEMP);
				if (ret < 0)
					break;

				mfc_set_cmd_l_reg(charger, MFC_CMD_SEND_EOP_MASK, MFC_CMD_SEND_EOP_MASK);
				msleep(250);
			}
		}
		break;
	default:
		break;
	}
}

static void mfc_packet_assist(struct mfc_charger_data *charger)
{
	u8 dummy;

	mfc_reg_read(charger->client, MFC_WPC_RX_DATA_COM_REG, &dummy);
	mfc_reg_read(charger->client, MFC_WPC_RX_DATA_VALUE0_REG, &dummy);
	mfc_reg_read(charger->client, MFC_AP2MFC_CMD_L_REG, &dummy);
}


static void mfc_send_packet(struct mfc_charger_data *charger, u8 header, u8 rx_data_com, u8 *data_val, int data_size)
{
	int i;

	mfc_reg_write(charger->client, MFC_WPC_PCKT_HEADER_REG, header);
	mfc_reg_write(charger->client, MFC_WPC_RX_DATA_COM_REG, rx_data_com);

	for (i = 0; i < data_size; i++)
		mfc_reg_write(charger->client, MFC_WPC_RX_DATA_VALUE0_REG + i, data_val[i]);
	mfc_set_cmd_l_reg(charger, MFC_CMD_SEND_TRX_DATA_MASK, MFC_CMD_SEND_TRX_DATA_MASK);
}

static int mfc_send_cs100(struct mfc_charger_data *charger)
{
	int i = 0;
	int ret = 0;

	for (i = 0; i < CMD_CNT; i++) {
		ret = mfc_reg_write(charger->client, MFC_BATTERY_CHG_STATUS_REG, 100);
		if (ret >= 0) {
			mfc_set_cmd_l_reg(charger, MFC_CMD_SEND_CHG_STS_MASK, MFC_CMD_SEND_CHG_STS_MASK);
			msleep(250);
			ret = 1;
		} else {
			ret = -1;
			break;
		}
	}
	return ret;
}

static void mfc_send_ept_unknown(struct mfc_charger_data *charger)
{
	pr_info("%s: EPT-Unknown\n", __func__);
	mfc_reg_write(charger->client, MFC_EPT_REG, MFC_WPC_EPT_UNKNOWN);
	mfc_set_cmd_l_reg(charger, MFC_CMD_SEND_EOP_MASK, MFC_CMD_SEND_EOP_MASK);
}

static void mfc_send_command(struct mfc_charger_data *charger, int cmd_mode)
{
	u8 data_val[4];
	u8 cmd = 0;
	u8 i;

	switch (cmd_mode) {
	case MFC_AFC_CONF_5V:
		pr_info("%s: WPC/PMA set 5V\n", __func__);
		cmd = WPC_COM_AFC_SET;
		data_val[0] = 0x05; /* Value for WPC AFC_SET 5V */
		mfc_send_packet(charger, MFC_HEADER_AFC_CONF, cmd, data_val, 1);  // 0x28 0x06 0x05
		msleep(120);

		charger->vout_mode = WIRELESS_VOUT_5V;
		cancel_delayed_work(&charger->wpc_vout_mode_work);
		__pm_stay_awake(charger->wpc_vout_mode_ws);
		queue_delayed_work(charger->wqueue, &charger->wpc_vout_mode_work, 0);

		pr_info("%s: vout read = %d\n", __func__, mfc_get_adc(charger, MFC_ADC_VOUT));
		mfc_packet_assist(charger);
		break;
	case MFC_AFC_CONF_10V:
		pr_info("%s: WPC set 10V\n", __func__);
		/* trigger 10V vout work after 8sec */
		__pm_stay_awake(charger->wpc_afc_vout_ws);
#if defined(CONFIG_SEC_FACTORY)
		queue_delayed_work(charger->wqueue, &charger->wpc_afc_vout_work, msecs_to_jiffies(3000));
#else
		queue_delayed_work(charger->wqueue, &charger->wpc_afc_vout_work, msecs_to_jiffies(8000));
#endif
		break;
	case MFC_AFC_CONF_5V_TX:
		for (i = 0; i < CMD_CNT; i++) {
			cmd = WPC_COM_AFC_SET;
			data_val[0] = RX_DATA_VAL2_5V; /* Value for WPC AFC_SET 5V */
			pr_info("%s: set 5V to TX, cnt = %d\n", __func__, i);
			mfc_send_packet(charger, MFC_HEADER_AFC_CONF, cmd, data_val, 1);  // 28 06 05
			mfc_packet_assist(charger);
			msleep(100);
		}
		mfc_rpp_set(charger, 128);
		break;
	case MFC_AFC_CONF_10V_TX:
		for (i = 0; i < CMD_CNT; i++) {
			cmd = WPC_COM_AFC_SET;
			data_val[0] = RX_DATA_VAL2_10V; /* Value for WPC AFC_SET 10V */
			pr_info("%s: set 10V to TX, cnt = %d\n", __func__, i);
			mfc_send_packet(charger, MFC_HEADER_AFC_CONF, cmd, data_val, 1);  // 28 06 2c
			mfc_packet_assist(charger);
			msleep(100);
		}
		mfc_rpp_set(charger, 64);
		break;
	case MFC_AFC_CONF_12V_TX:
		for (i = 0; i < CMD_CNT; i++) {
			cmd = WPC_COM_AFC_SET;
			data_val[0] = RX_DATA_VAL2_12V; /* Value for WPC AFC_SET 12V */
			pr_info("%s: set 12V to TX, cnt = %d\n", __func__, i);
			mfc_send_packet(charger, MFC_HEADER_AFC_CONF, cmd, data_val, 1);
			mfc_packet_assist(charger);
			msleep(100);
		}
		mfc_rpp_set(charger, 64);
		break;
	case MFC_AFC_CONF_12_5V_TX:
		for (i = 0; i < CMD_CNT; i++) {
			cmd = WPC_COM_AFC_SET;
			data_val[0] = RX_DATA_VAL2_12_5V; /* Value for WPC AFC_SET 12V */
			pr_info("%s: set 12.5V to TX, cnt = %d\n", __func__, i);
			mfc_send_packet(charger, MFC_HEADER_AFC_CONF, cmd, data_val, 1);
			mfc_packet_assist(charger);
			msleep(100);
		}
		mfc_rpp_set(charger, 64);

		if (delayed_work_pending(&charger->wpc_check_rx_power_work)) {
			__pm_relax(charger->wpc_check_rx_power_ws);
			cancel_delayed_work(&charger->wpc_check_rx_power_work);
		}
		charger->check_rx_power = true;
		__pm_stay_awake(charger->wpc_check_rx_power_ws);
		queue_delayed_work(charger->wqueue, &charger->wpc_check_rx_power_work, msecs_to_jiffies(3000));

		break;
	case MFC_AFC_CONF_20V_TX:
		for (i = 0; i < CMD_CNT; i++) {
			cmd = WPC_COM_AFC_SET;
			data_val[0] = RX_DATA_VAL2_20V; /* Value for WPC AFC_SET 20V */
			pr_info("%s: set 20V to TX, cnt = %d\n", __func__, i);
			mfc_send_packet(charger, MFC_HEADER_AFC_CONF, cmd, data_val, 1);
			mfc_packet_assist(charger);
			msleep(100);
		}
		mfc_rpp_set(charger, 32);
		break;
	case MFC_LED_CONTROL_ON:
		pr_info("%s: led on\n", __func__);
		cmd = WPC_COM_LED_CONTROL;
		data_val[0] = 0x00; /* Value for WPC LED ON */
		mfc_send_packet(charger, MFC_HEADER_AFC_CONF, cmd, data_val, 1);
		msleep(100);
		break;
	case MFC_LED_CONTROL_OFF:
		pr_info("%s: led off\n", __func__);
		cmd = WPC_COM_LED_CONTROL;
		data_val[0] = 0xff; /* Value for WPC LED OFF */
		mfc_send_packet(charger, MFC_HEADER_AFC_CONF, cmd, data_val, 1);
		msleep(100);
		break;
	case MFC_LED_CONTROL_DIMMING:
		pr_info("%s: led dimming\n", __func__);
		cmd = WPC_COM_LED_CONTROL;
		data_val[0] = 0x55; /* Value for WPC LED DIMMING */
		mfc_send_packet(charger, MFC_HEADER_AFC_CONF, cmd, data_val, 1);
		msleep(100);
		break;
	case MFC_FAN_CONTROL_ON:
		pr_info("%s: fan on\n", __func__);
		cmd = WPC_COM_COOLING_CTRL;
		data_val[0] = 0x00; /* Value for WPC FAN ON */
		mfc_send_packet(charger, MFC_HEADER_AFC_CONF, cmd, data_val, 1);
		msleep(100);
		break;
	case MFC_FAN_CONTROL_OFF:
		pr_info("%s: fan off\n", __func__);
		cmd = WPC_COM_COOLING_CTRL;
		data_val[0] = 0xff; /* Value for WPC FAN OFF */
		mfc_send_packet(charger, MFC_HEADER_AFC_CONF, cmd, data_val, 1);
		msleep(100);
		break;
	case MFC_REQUEST_AFC_TX:
		pr_info("%s: request afc tx, cable(0x%x)\n", __func__, charger->pdata->cable_type);
		cmd = WPC_COM_REQ_AFC_TX;
		data_val[0] = 0x00; /* Value for WPC Request AFC_TX */
		mfc_send_packet(charger, MFC_HEADER_AFC_CONF, cmd, data_val, 1);
		break;
	case MFC_REQUEST_TX_ID:
		pr_info("%s: request TX ID\n", __func__);
		cmd = WPC_COM_TX_ID;
		data_val[0] = 0x00; /* Value for WPC TX ID */
		mfc_send_packet(charger, MFC_HEADER_AFC_CONF, cmd, data_val, 1);
		break;
	case MFC_DISABLE_TX:
		pr_info("%s: Disable TX\n", __func__);
		cmd = WPC_COM_DISABLE_TX;
		data_val[0] = 0x55; /* Value for Disable TX */
		mfc_send_packet(charger, MFC_HEADER_AFC_CONF, cmd, data_val, 1);
		break;
	case MFC_PHM_ON:
		pr_info("%s: Enter PHM\n", __func__);
		cmd = WPC_COM_ENTER_PHM;
		data_val[0] = 0x01; /* Enter PHM */
		mfc_send_packet(charger, MFC_HEADER_AFC_CONF, cmd, data_val, 1);
		break;
	case MFC_SET_OP_FREQ:
		pr_info("%s: set tx op freq\n", __func__);
		cmd = WPC_COM_OP_FREQ_SET;
		data_val[0] = 0x69; /* Value for OP FREQ 120.5kHz */
		mfc_send_packet(charger, MFC_HEADER_AFC_CONF, cmd, data_val, 1);
		break;
	case MFC_TX_UNO_OFF:
		pr_info("%s: UNO off TX\n", __func__);
		cmd = WPC_COM_DISABLE_TX;
		data_val[0] = 0xFF; /* Value for Uno off */
		mfc_send_packet(charger, MFC_HEADER_AFC_CONF, cmd, data_val, 1);
		break;
	case MFC_REQ_TX_PWR_BUDG:
		pr_info("%s: request PWR BUDG\n", __func__);
		cmd = WPC_COM_REQ_PWR_BUDG;
		data_val[0] = 0x00;
		mfc_send_packet(charger, MFC_HEADER_AFC_CONF, cmd, data_val, 1);
		break;
	default:
		break;
	}
}

static void mfc_send_fsk(struct mfc_charger_data *charger, u8 tx_data_com, u8 data_val)
{
	int i;

	for (i = 0; i < 3; i++) {
		mfc_reg_write(charger->client, MFC_WPC_TX_DATA_COM_REG, tx_data_com);
		mfc_reg_write(charger->client, MFC_WPC_TX_DATA_VALUE0_REG, data_val);
		mfc_set_cmd_l_reg(charger, MFC_CMD_SEND_TRX_DATA_MASK, MFC_CMD_SEND_TRX_DATA_MASK);
		msleep(250);
	}
}

static void mfc_led_control(struct mfc_charger_data *charger, int state)
{
	int i = 0;

	for (i = 0; i < CMD_CNT; i++)
		mfc_send_command(charger, state);
}

static void mfc_fan_control(struct mfc_charger_data *charger, bool on)
{
	int i = 0;

	if (on) {
		for (i = 0; i < CMD_CNT - 1; i++)
			mfc_send_command(charger, MFC_FAN_CONTROL_ON);
	} else {
		for (i = 0; i < CMD_CNT - 1; i++)
			mfc_send_command(charger, MFC_FAN_CONTROL_OFF);
	}
}

static void mfc_set_vrect_adjust(struct mfc_charger_data *charger, int set)
{
	if (charger->vout_mode == charger->pdata->wpc_vout_ctrl_full)
		return;

	switch (set) {
	case MFC_HEADROOM_0:
			mfc_reg_write(charger->client, MFC_VRECT_ADJ_REG, 0x0);
		break;
	case MFC_HEADROOM_1:
			mfc_reg_write(charger->client, MFC_VRECT_ADJ_REG, 0x1c); //LSB 10mV
		break;
	case MFC_HEADROOM_2:
			mfc_reg_write(charger->client, MFC_VRECT_ADJ_REG, 0x32);
		break;
	case MFC_HEADROOM_3:
			mfc_reg_write(charger->client, MFC_VRECT_ADJ_REG, 0x41);
		break;
	case MFC_HEADROOM_4:
			mfc_reg_write(charger->client, MFC_VRECT_ADJ_REG, 0x03);
		break;
	case MFC_HEADROOM_5:
			mfc_reg_write(charger->client, MFC_VRECT_ADJ_REG, 0x08);
		break;
	case MFC_HEADROOM_6:
			mfc_reg_write(charger->client, MFC_VRECT_ADJ_REG, 0x0a);
		break;
	case MFC_HEADROOM_7:
			mfc_reg_write(charger->client, MFC_VRECT_ADJ_REG, 0xc4);
		break;
	default:
		pr_info("%s no headroom mode\n", __func__);
		break;
	}
}

static void mfc_set_vout_ctrl_full(struct mfc_charger_data *charger)
{
	if (!charger->pdata->wpc_vout_ctrl_full ||  !volt_ctrl_pad(charger->tx_id))
		return;

	if (charger->pdata->wpc_headroom_ctrl_full)
		mfc_set_vrect_adjust(charger, MFC_HEADROOM_7);
	charger->vout_mode = charger->pdata->wpc_vout_ctrl_full;
	cancel_delayed_work(&charger->wpc_vout_mode_work);
	__pm_stay_awake(charger->wpc_vout_mode_ws);
	queue_delayed_work(charger->wqueue,
		&charger->wpc_vout_mode_work, msecs_to_jiffies(250));
	pr_info("%s: 2nd wireless charging done! vout set %s & headroom offset %dmV!\n",
		__func__, sb_vout_ctr_mode_str(charger->vout_mode),
		charger->pdata->wpc_headroom_ctrl_full ? -600 : 0);
}

static void mfc_set_cep_timeout(struct mfc_charger_data *charger, u32 timeout)
{
	if (timeout) {
		pr_info("%s: timeout = %dmsec\n", __func__, timeout);
		timeout = timeout/100;
		mfc_reg_write(charger->client, MFC_CEP_TIME_OUT_REG, timeout);
	}
}

/* these pads can control fans for sleep/auto mode */
static bool is_sleep_mode_active(int pad_id)
{
	/* standard fw, hv pad, no multiport pad, non hv pad and dream pad */
	if (fan_ctrl_pad(pad_id)) {
		pr_info("%s: this pad can control fan for auto mode\n", __func__);
		return 1;
	}

	pr_info("%s: this pad cannot control fan\n", __func__);
	return 0;
}

static void mfc_mis_align(struct mfc_charger_data *charger)
{
	if (charger->wc_checking_align) {
		pr_info("%s: skip Reset M0\n", __func__);
		return;
	}

	pr_info("%s: Reset M0\n", __func__);
	/* reset MCU of MFC IC */
	mfc_set_cmd_l_reg(charger, MFC_CMD_MCU_RESET_MASK, MFC_CMD_MCU_RESET_MASK);
}

static bool mfc_tx_function_check(struct mfc_charger_data *charger)
{
	u8 reg_f2, reg_f4;

	mfc_reg_read(charger->client, MFC_TX_RXID1_READ_REG, &reg_f2);
	mfc_reg_read(charger->client, MFC_TX_RXID3_READ_REG, &reg_f4);

	pr_info("@Tx_Mode %s: 0x%x 0x%x\n", __func__, reg_f2, reg_f4);

	if ((reg_f2 == SS_ID) && (reg_f4 == SS_CODE))
		return true;
	else
		return false;
}

static void mfc_set_tx_ioffset(struct mfc_charger_data *charger, int ioffset)
{
	u8 ioffset_data_l = 0, ioffset_data_h = 0;

	ioffset_data_l = 0xFF & ioffset;
	ioffset_data_h = 0xFF & (ioffset >> 8);

	mfc_reg_write(charger->client, MFC_TX_IUNO_OFFSET_L_REG, ioffset_data_l);
	mfc_reg_write(charger->client, MFC_TX_IUNO_OFFSET_H_REG, ioffset_data_h);

	pr_info("@Tx_Mode %s: Tx Iout set %d(0x%x 0x%x)\n", __func__, ioffset, ioffset_data_h, ioffset_data_l);
}

static void mfc_set_tx_iout(struct mfc_charger_data *charger, unsigned int iout)
{
	u8 iout_data_l = 0, iout_data_h = 0;

	iout_data_l = 0xFF & iout;
	iout_data_h = 0xFF & (iout >> 8);

	mfc_reg_write(charger->client, MFC_TX_IUNO_LIMIT_L_REG, iout_data_l);
	mfc_reg_write(charger->client, MFC_TX_IUNO_LIMIT_H_REG, iout_data_h);

	pr_info("@Tx_Mode %s: Tx Iout set %d(0x%x 0x%x)\n", __func__, iout, iout_data_h, iout_data_l);

	if (iout == 1100)
		mfc_set_tx_ioffset(charger, 100);
	else
		mfc_set_tx_ioffset(charger, 150);
}

static void mfc_test_read(struct mfc_charger_data *charger)
{
	u8 reg_data;

	if (!charger->wc_tx_enable)
		return;

	if (mfc_reg_read(charger->client, MFC_STARTUP_EPT_COUNTER, &reg_data) <= 0)
		return;

	pr_info("%s: [AOV] ept-counter = %d\n", __func__, reg_data);
}

static void mfc_set_tx_vout(struct mfc_charger_data *charger, unsigned int vout)
{
	unsigned int vout_val;
	u8 vout_data_l = 0, vout_data_h = 0;

	if (vout < WC_TX_VOUT_MIN) {
		pr_err("%s: vout(%d) is lower than min\n", __func__, vout);
		vout = WC_TX_VOUT_MIN;
	} else if (vout > WC_TX_VOUT_MAX) {
		pr_err("%s: vout(%d) is higher than max\n", __func__, vout);
		vout = WC_TX_VOUT_MAX;
	}

	mfc_test_read(charger);

	if (vout <= 2280)
		vout_val = 0;
	else
		vout_val = (vout - 2280) / 20;
	vout_data_l = 0xFF & vout_val;
	vout_data_h = 0xFF & (vout_val >> 8);

	mfc_reg_write(charger->client, MFC_VOUT_SET_L_REG, vout_data_l);
	mfc_reg_write(charger->client, MFC_VOUT_SET_H_REG, vout_data_h);

	pr_info("@Tx_Mode %s: Tx Vout set %d(0x%x 0x%x)\n", __func__, vout, vout_data_h, vout_data_l);
}

static void mfc_print_buffer(struct mfc_charger_data *charger, u8 *buffer, int size)
{
	int start_idx = 0;

	do {
		char temp_buf[1024] = {0, };
		int size_temp = 0, str_len = 1024;
		int old_idx = start_idx;

		size_temp = ((start_idx + 0x7F) > size) ? size : (start_idx + 0x7F);
		for (; start_idx < size_temp; start_idx++) {
			snprintf(temp_buf + strlen(temp_buf), str_len, "0x%02x ", buffer[start_idx]);
			str_len = 1024 - strlen(temp_buf);
		}
		pr_info("%s: (%04d ~ %04d) %s\n", __func__, old_idx, start_idx - 1, temp_buf);
	} while (start_idx < size);
}

static int mfc_auth_adt_read(struct mfc_charger_data *charger, u8 *readData)
{
	int buffAddr = MFC_ADT_BUFFER_ADT_PARAM_REG;
	union power_supply_propval value = {0, };
	int i = 0, size = 0;
	int ret = 0;
	u8 size_temp[2] = {0, 0};

	ret = mfc_reg_multi_read(charger->client, MFC_ADT_BUFFER_ADT_TYPE_REG, size_temp, 2);
	if (ret < 0) {
		pr_err("%s:  failed to read adt type (ret = %d)\n", __func__, ret);
		return ret;
	}

	adt_readSize = ((size_temp[0] & 0x07) << 8) | (size_temp[1]);
	pr_info("%s %s: (size_temp = 0x%x, readSize = %d bytes)\n",
		WC_AUTH_MSG, __func__, (size_temp[0] | size_temp[1]), adt_readSize);
	if (adt_readSize <= 0) {
		pr_err("%s %s: failed to read adt data size\n", WC_AUTH_MSG, __func__);
		return -EINVAL;
	}
	size = adt_readSize;
	if ((buffAddr + adt_readSize) - 1 > MFC_ADT_BUFFER_ADT_PARAM_MAX_REG) {
		pr_err("%s %s: failed to read adt stream, too large data\n", WC_AUTH_MSG, __func__);
		return -EINVAL;
	}

	if (size <= 2) {
		pr_err("%s %s: data from mcu has invalid size\n", WC_AUTH_MSG, __func__);
		//charger->adt_transfer_status = WIRELESS_AUTH_FAIL;
		/* notify auth service to send TX PAD a request key */
		value.intval = WIRELESS_AUTH_FAIL;
		psy_do_property("wireless", set,
			POWER_SUPPLY_EXT_PROP_WIRELESS_AUTH_ADT_STATUS, value);
		return -EINVAL;
	}

	while (size > SENDSZ) {
		ret = mfc_reg_multi_read(charger->client, buffAddr, readData + i, SENDSZ);
		if (ret < 0) {
			pr_err("%s %s: failed to read adt stream (%d, buff %d)\n",
				WC_AUTH_MSG, __func__, ret, buffAddr);
			return ret;
		}
		i += SENDSZ;
		buffAddr += SENDSZ;
		size = size - SENDSZ;
		pr_info("%s %s: 0x%x %d %d\n", WC_AUTH_MSG, __func__, i, buffAddr, size);
	}

	if (size > 0) {
		ret = mfc_reg_multi_read(charger->client, buffAddr, readData + i, size);
		if (ret < 0) {
			pr_err("%s %s: failed to read adt stream (%d, buff %d)\n",
				WC_AUTH_MSG, __func__, ret, buffAddr);
			return ret;
		}
	}
	mfc_print_buffer(charger, readData, adt_readSize);

	value.intval = WIRELESS_AUTH_RECEIVED;
	psy_do_property("wireless", set, POWER_SUPPLY_EXT_PROP_WIRELESS_AUTH_ADT_STATUS, value);

	charger->adt_transfer_status = WIRELESS_AUTH_RECEIVED;

	pr_info("%s %s: succeeded to read ADT\n", WC_AUTH_MSG, __func__);

	return 0;
}

static int mfc_auth_adt_write(struct mfc_charger_data *charger, u8 *srcData, int srcSize)
{
	int buffAddr = MFC_ADT_BUFFER_ADT_PARAM_REG;
	u8 wdata = 0;
	u8 sBuf[144] = {0,};
	int ret;
	u8 i;

	pr_info("%s %s: start to write ADT\n", WC_AUTH_MSG, __func__);

	if ((buffAddr + srcSize) - 1 > MFC_ADT_BUFFER_ADT_PARAM_MAX_REG) {
		pr_err("%s %s: failed to write adt stream, too large data.\n", WC_AUTH_MSG, __func__);
		return -EINVAL;
	}

	wdata = 0x08; /* Authentication purpose */
	wdata = wdata | ((srcSize >> 8) & 0x07);
	mfc_reg_write(charger->client, MFC_ADT_BUFFER_ADT_TYPE_REG, wdata);

	wdata = srcSize; /* need to check */
	mfc_reg_write(charger->client, MFC_ADT_BUFFER_ADT_MSG_SIZE_REG, wdata);

	buffAddr = MFC_ADT_BUFFER_ADT_PARAM_REG;
	for (i = 0; i < srcSize; i += 128) { //write each 128byte
		int restSize = srcSize - i;

		if (restSize < 128) {
			memcpy(sBuf, srcData + i, restSize);
			ret = mfc_reg_multi_write_verify(charger->client, buffAddr, sBuf, restSize);
			if (ret < 0) {
				pr_err("%s %s: failed to write adt stream (%d, buff %d)\n",
					WC_AUTH_MSG, __func__, ret, buffAddr);
				return ret;
			}
			break;
		}

		memcpy(sBuf, srcData + i, 128);
		ret = mfc_reg_multi_write_verify(charger->client, buffAddr, sBuf, 128);
		if (ret < 0) {
			pr_err("%s %s: failed to write adt stream (%d, buff %d)\n",
				WC_AUTH_MSG, __func__, ret, buffAddr);
			return ret;
		}
		buffAddr += 128;

		if (buffAddr > MFC_ADT_BUFFER_ADT_PARAM_MAX_REG - 128)
			break;
	}
	pr_info("%s %s: succeeded to write ADT\n", WC_AUTH_MSG, __func__);
	return 0;
}

static void mfc_auth_adt_send(struct mfc_charger_data *charger, u8 *srcData, int srcSize)
{
	u8 temp;
	int ret = 0;

	charger->adt_transfer_status = WIRELESS_AUTH_SENT;
	ret = mfc_auth_adt_write(charger, srcData, srcSize); /* write buff fw datas to send fw datas to tx */

	mfc_reg_read(charger->client, MFC_AP2MFC_CMD_H_REG, &temp); // check out set bit exists
	pr_info("%s %s: MFC_CMD_H_REG(0x%x)\n", WC_AUTH_MSG, __func__, temp);
	temp |= 0x02;
	pr_info("%s %s: MFC_CMD_H_REG(0x%x)\n", WC_AUTH_MSG, __func__, temp);
	mfc_reg_write(charger->client, MFC_AP2MFC_CMD_H_REG, temp);

	mfc_reg_read(charger->client, MFC_AP2MFC_CMD_H_REG, &temp); // check out set bit exists
	pr_info("%s %s: MFC_CMD_H_REG(0x%x)\n", WC_AUTH_MSG, __func__, temp);

	mfc_reg_update(charger->client, MFC_INT_A_ENABLE_H_REG,
					MFC_STAT_H_ADT_SENT_MASK, MFC_STAT_H_ADT_SENT_MASK);
}

#if !defined(CONFIG_SEC_FACTORY)
#define AUTH_READY 0
#define AUTH_COMPLETE 1
#define AUTH_OPFREQ 140
static void mfc_auth_set_configs(struct mfc_charger_data *charger, int opt)
{
	u8 data = 0;

	if (opt == AUTH_READY) {
		int op_freq = mfc_get_adc(charger, MFC_ADC_OP_FRQ);

		pr_info("%s: op_freq(%d)\n", __func__, op_freq);
		if ((charger->tx_id == TX_ID_AUTH_PAD) && (op_freq >= AUTH_OPFREQ)) {
			mfc_cma_cmb_onoff(charger, true, true);
			mfc_reg_write(charger->client, MFC_RECT_MODE_AP_CTRL, 0x01);
			mfc_reg_update(charger->client, MFC_RECTMODE_REG, 0x40, 0xC0);
			mfc_reg_read(charger->client, MFC_RECTMODE_REG, &data);
		}
	} else if (opt == AUTH_COMPLETE) {
		if (charger->tx_id == TX_ID_AUTH_PAD) {
			mfc_reg_write(charger->client, MFC_RECT_MODE_AP_CTRL, 0x00);
			mfc_reg_read(charger->client, MFC_RECTMODE_REG, &data);
		}
	} else {
		pr_info("%s: undefined cmd(%d)\n", __func__, opt);
	}
}
#endif

/* uno on/off control function */
static void mfc_set_tx_power(struct mfc_charger_data *charger, bool on)
{
	union power_supply_propval value = {0, };

	charger->wc_tx_enable = on;

	if (on) {
		pr_info("@Tx_Mode %s: Turn On TX Power\n", __func__);
		mfc_set_coil_sw_en(charger, 1);
		mfc_uno_on(charger, true);
		msleep(200);

		charger->pdata->otp_firmware_ver = mfc_get_firmware_version(charger, MFC_RX_FIRMWARE);
		charger->wc_rx_fod = false;
	} else {
		pr_info("@Tx_Mode %s: Turn Off TX Power, and reset UNO config\n", __func__);

		mfc_uno_on(charger, false);
		mfc_set_coil_sw_en(charger, 0);

		value.intval = WC_TX_VOUT_5000MV;
		psy_do_property("otg", set,
			POWER_SUPPLY_EXT_PROP_WIRELESS_TX_VOUT, value);

		charger->wc_rx_connected = false;
		charger->wc_rx_type = NO_DEV;
		charger->duty_min = 20;
		charger->tx_device_phm = 0;

		alarm_cancel(&charger->phm_alarm);
		if (delayed_work_pending(&charger->wpc_rx_type_det_work)) {
			cancel_delayed_work(&charger->wpc_rx_type_det_work);
			__pm_relax(charger->wpc_rx_det_ws);
		}
		if (delayed_work_pending(&charger->wpc_rx_connection_work))
			cancel_delayed_work(&charger->wpc_rx_connection_work);
		if (delayed_work_pending(&charger->wpc_tx_isr_work)) {
			cancel_delayed_work(&charger->wpc_tx_isr_work);
			__pm_relax(charger->wpc_tx_ws);
		}
		if (delayed_work_pending(&charger->wpc_tx_phm_work)) {
			cancel_delayed_work(&charger->wpc_tx_phm_work);
			__pm_relax(charger->wpc_tx_phm_ws);
		}
		if (delayed_work_pending(&charger->mode_change_work)) {
			cancel_delayed_work(&charger->mode_change_work);
			__pm_relax(charger->mode_change_ws);
		}

		charger->tx_status = SEC_TX_OFF;
	}
}

/* determine rx connection status with tx sharing mode */
static void mfc_wpc_rx_connection_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, wpc_rx_connection_work.work);
	union power_supply_propval value = {0, };

	if (!charger->wc_tx_enable) {
		pr_info("@Tx_Mode %s : wc_tx_enable(%d)\n", __func__, charger->wc_tx_enable);
		charger->wc_rx_connected = false;
		return;
	}

	if (charger->wc_rx_connected) {
		pr_info("@Tx_Mode %s: Rx(%s) connected\n",
			__func__, mfc_tx_function_check(charger) ? "Samsung" : "Other");

		cancel_delayed_work(&charger->wpc_rx_type_det_work);
		__pm_stay_awake(charger->wpc_rx_det_ws);
		queue_delayed_work(charger->wqueue, &charger->wpc_rx_type_det_work, msecs_to_jiffies(1000));
	} else {
		pr_info("@Tx_Mode %s: Rx disconnected\n", __func__);

		mfc_set_coil_sw_en(charger, 1);
		charger->wc_rx_fod = false;
		charger->wc_rx_type = NO_DEV;
		charger->tx_device_phm = 0;

		if (delayed_work_pending(&charger->wpc_rx_type_det_work)) {
			__pm_relax(charger->wpc_rx_det_ws);
			cancel_delayed_work(&charger->wpc_rx_type_det_work);
		}
	}

	/* set rx connection condition */
	value.intval = charger->wc_rx_connected;
	psy_do_property("wireless", set, POWER_SUPPLY_EXT_PROP_WIRELESS_RX_CONNECTED, value);
}

/*
 * determine rx connection status with tx sharing mode,
 * this TX device has MFC_INTA_H_TRX_DATA_RECEIVED_MASK irq and RX device is connected HV wired cable
 */
static void mfc_tx_handle_rx_packet(struct mfc_charger_data *charger)
{
	u8 cmd_data = 0, val_data = 0, val2_data = 0;
	union power_supply_propval value = {0, };

	mfc_reg_read(charger->client, MFC_WPC_TRX_DATA2_COM_REG, &cmd_data);
	mfc_reg_read(charger->client, MFC_WPC_TRX_DATA2_VALUE0_REG, &val_data);
	mfc_reg_read(charger->client, MFC_WPC_TRX_DATA2_VALUE1_REG, &val2_data);
	charger->pdata->trx_data_cmd = cmd_data;
	charger->pdata->trx_data_val = val_data;

	pr_info("@Tx_Mode %s: CMD : 0x%x, DATA : 0x%x, DATA2 : 0x%x\n", __func__, cmd_data, val_data, val2_data);

	/* When RX device has got a AFC TA, this TX device should turn off TX power sharing(uno) */
	if (cmd_data == MFC_HEADER_AFC_CONF) { /* 0x28 */
		switch (val_data) {
		case WPC_COM_DISABLE_TX: /* 0x19 Turn off UNO of TX */
			if (val2_data == RX_DATA_VAL2_MISALIGN) {
				psy_do_property("wireless", get, POWER_SUPPLY_EXT_PROP_WIRELESS_TX_ERR, value);
				if (value.intval & BATT_TX_EVENT_WIRELESS_TX_RETRY) {
					pr_info("@Tx_Mode %s: ignore misalign packet during TX retry\n", __func__);
					return;
				}
				pr_info("@Tx_Mode %s: RX send TX off packet(Misalign or darkzone)\n", __func__);
				value.intval = BATT_TX_EVENT_WIRELESS_TX_MISALIGN;
				psy_do_property("wireless", set, POWER_SUPPLY_EXT_PROP_WIRELESS_TX_ERR, value);
			} else if (val2_data == RX_DATA_VAL2_TA_CONNECT_DURING_WC) {
				pr_info("@Tx_Mode %s: TX dev should turn off TX(uno), RX dev has AFC TA\n", __func__);
				value.intval = BATT_TX_EVENT_WIRELESS_RX_CHG_SWITCH;
				psy_do_property("wireless", set, POWER_SUPPLY_EXT_PROP_WIRELESS_TX_ERR, value);
			}
			break;
		case WPC_COM_ENTER_PHM: /* 0x18 Received PHM, GEAR sent PHM packet to TX */
			if (val2_data == RX_DATA_VAL2_ENABLE)
				pr_info("@Tx_Mode %s: Received PHM\n", __func__);
			break;
		case WPC_COM_RX_ID: /* 0x0E Received RX ID */
			if (val2_data >= RX_DATA_VAL2_RXID_ACC_BUDS && val2_data <= RX_DATA_VAL2_RXID_ACC_BUDS_MAX) {
				charger->wc_rx_type = SS_BUDS;
				pr_info("@Tx_Mode %s: Buds connected\n", __func__);
				mfc_set_tx_fod_thresh1(charger->client, charger->pdata->buds_fod_thresh1);
				mfc_set_tx_fod_ta_thresh(charger->client, charger->pdata->buds_fod_ta_thresh);
				value.intval = charger->wc_rx_type;
				psy_do_property("wireless", set,
						POWER_SUPPLY_EXT_PROP_WIRELESS_RX_TYPE, value);
			}
			break;
		default:
			pr_info("@Tx_Mode %s: unsupport : 0x%x", __func__, val_data);
			break;
		}
	} else if (cmd_data == MFC_HEADER_END_CHARGE_STATUS) {
		if (val_data == MFC_ECS_CS100) {	/* CS 100 */
			pr_info("@Tx_Mode %s: CS100 Received, TX power off\n", __func__);
			value.intval = BATT_TX_EVENT_WIRELESS_RX_CS100;
			psy_do_property("wireless", set, POWER_SUPPLY_EXT_PROP_WIRELESS_TX_ERR, value);
		}
	} else if (cmd_data == MFC_HEADER_END_POWER_TRANSFER) {
		if (val_data == MFC_EPT_CODE_OVER_TEMPERATURE) {
			pr_info("@Tx_Mode %s: EPT-OT Received, TX power off\n", __func__);
			value.intval = BATT_TX_EVENT_WIRELESS_RX_UNSAFE_TEMP;
			psy_do_property("wireless", set, POWER_SUPPLY_EXT_PROP_WIRELESS_TX_ERR, value);
		} else if (val_data == MFC_EPT_CODE_BATTERY_FAILURE) {
			if (charger->wc_rx_type != SS_GEAR) {
				pr_info("@Tx_Mode %s: EPT06 Received, TX power off and retry\n", __func__);
				value.intval = BATT_TX_EVENT_WIRELESS_TX_RETRY;
				psy_do_property("wireless", set, POWER_SUPPLY_EXT_PROP_WIRELESS_TX_ERR, value);
			}
		}
	} else if (cmd_data == MFC_HEADER_PROPRIETARY_1_BYTE) {
		if (val_data == WPC_COM_WDT_ERR) {
			pr_info("@Tx_Mode %s: WDT Error Received, TX power off\n", __func__);
			value.intval = BATT_TX_EVENT_WIRELESS_TX_ETC;
			psy_do_property("wireless", set, POWER_SUPPLY_EXT_PROP_WIRELESS_TX_ERR, value);
		}
	}
}

static int datacmp(const char *cs, const char *ct, int count)
{
	unsigned char c1, c2;

	while (count) {
		c1 = *cs++;
		c2 = *ct++;
		if (c1 != c2) {
			pr_err("%s: cnt %d\n", __func__, count);
			return c1 < c2 ? -1 : 1;
		}
		count--;
	}
	return 0;
}

static int mfc_reg_multi_write_verify(struct i2c_client *client, u16 reg, const u8 *val, int size)
{
	int ret = 0;
	int cnt = 0;
	int retry_cnt = 0;
	unsigned char data[SENDSZ + 2];
	u8 rdata[SENDSZ + 2];
	struct mfc_charger_data *charger = i2c_get_clientdata(client);

	if (charger->reg_access_lock) {
		pr_err("%s: can not access to reg during fw update\n", __func__);
		return -1;
	}

	while (size > SENDSZ) {
		data[0] = (reg + cnt) >> 8;
		data[1] = (reg + cnt) & 0xFF;
		memcpy(data + 2, val + cnt, SENDSZ);
//		dev_dbg(&client->dev, "%s: addr: 0x%x, cnt: 0x%x\n", __func__, reg + cnt, cnt);
		ret = i2c_master_send(client, data, SENDSZ + 2);
		if (ret < SENDSZ + 2) {
			pr_err("%s: i2c write error, reg: 0x%x\n", __func__, reg);
			return ret < 0 ? ret : -EIO;
		}
		if (mfc_reg_multi_read(client, reg + cnt, rdata, SENDSZ) < 0) {
			pr_err("%s: read failed(%d)\n", __func__, reg + cnt);
			return -1;
		}
		if (datacmp(val + cnt, rdata, SENDSZ)) {
			pr_err("%s: data is not matched. retry(%d)\n", __func__, retry_cnt);
			retry_cnt++;
			if (retry_cnt > 4) {
				pr_err("%s: data is not matched. write failed\n", __func__);
				retry_cnt = 0;
				return -1;
			}
			continue;
		}
//		pr_debug("%s: data is matched!\n", __func__);
		cnt += SENDSZ;
		size -= SENDSZ;
		retry_cnt = 0;
	}
	while (size > 0) {
		data[0] = (reg + cnt) >> 8;
		data[1] = (reg + cnt) & 0xFF;
		memcpy(data + 2, val + cnt, size);
//		dev_dbg(&client->dev, "%s: addr: 0x%x, cnt: 0x%x, size: 0x%x\n", __func__, reg + cnt, cnt, size);
		ret = i2c_master_send(client, data, size + 2);
		if (ret < size + 2) {
			pr_err("%s: i2c write error, reg: 0x%x\n", __func__, reg);
			return ret < 0 ? ret : -EIO;
		}
		if (mfc_reg_multi_read(client, reg + cnt, rdata, size) < 0) {
			pr_err("%s: read failed(%d)\n", __func__, reg + cnt);
			return -1;
		}
		if (datacmp(val + cnt, rdata, size)) {
			pr_err("%s: data is not matched. retry(%d)\n", __func__, retry_cnt);
			retry_cnt++;
			if (retry_cnt > 4) {
				pr_err("%s: data is not matched. write failed\n", __func__);
				retry_cnt = 0;
				return -1;
			}
			continue;
		}
		size = 0;
		pr_err("%s: data is matched!\n", __func__);
	}
	return ret;
}

#if 0
static int mfc_reg_multi_write(struct i2c_client *client, u16 reg, const u8 *val, int size)
{
	struct mfc_charger_data *charger = i2c_get_clientdata(client);
	int ret = 0;
	unsigned char data[SENDSZ + 2];
	int cnt = 0;

	pr_err("%s: size: 0x%x\n", __func__, size);
	while (size > SENDSZ) {
		data[0] = (reg + cnt) >> 8;
		data[1] = (reg + cnt) & 0xff;
		memcpy(data + 2, val + cnt, SENDSZ);
		mutex_lock(&charger->io_lock);
		ret = i2c_master_send(client, data, SENDSZ + 2);
		mutex_unlock(&charger->io_lock);
		if (ret < SENDSZ + 2) {
			pr_err("%s: i2c write error, reg: 0x%x\n", __func__, reg);
			return ret < 0 ? ret : -EIO;
		}
		cnt = cnt + SENDSZ;
		size = size - SENDSZ;
	}
	if (size > 0) {
		data[0] = (reg + cnt) >> 8;
		data[1] = (reg + cnt) & 0xff;
		memcpy(data + 2, val + cnt, size);
		mutex_lock(&charger->io_lock);
		ret = i2c_master_send(client, data, size + 2);
		mutex_unlock(&charger->io_lock);
		if (ret < size + 2) {
			dev_err(&client->dev, "%s: i2c write error, reg: 0x%x\n", __func__, reg);
			return ret < 0 ? ret : -EIO;
		}
	}

	return ret;
}
#endif

static int mfc_cps_wls_write_word(struct i2c_client *client, u32 reg, u32 val)
{
	struct mfc_charger_data *charger = i2c_get_clientdata(client);
	int ret = 0;
	unsigned char data[8];

	data[0] = reg >> 24;
	data[1] = (reg >> 16) & 0xff;
	data[2] = (reg >> 8) & 0xff;
	data[3] = reg & 0xff;
	data[4] = val & 0xff;
	data[5] = (val >> 8) & 0xff;
	data[6] = (val >> 16) & 0xff;
	data[7] = (val >> 24) & 0xff;
	mutex_lock(&charger->io_lock);
	ret = i2c_master_send(client, data, 8);
	mutex_unlock(&charger->io_lock);
	if (ret < 8) {
		dev_err(&client->dev, "%s: i2c write error, reg: 0x%x\n", __func__, reg);
		return ret < 0 ? ret : -EIO;
	}

	return ret;
}

static int mfc_cps_wls_read_word(struct i2c_client *client, u32 reg, u8 *val)
{
	struct mfc_charger_data *charger = i2c_get_clientdata(client);
	int ret;
	struct i2c_msg msg[2];
	u8 wbuf[4];

	msg[0].addr = client->addr;
	msg[0].flags = client->flags & I2C_M_TEN;
	msg[0].len = 4;
	msg[0].buf = wbuf;

	wbuf[0] = reg >> 24;
	wbuf[1] = (reg >> 16) & 0xff;
	wbuf[2] = (reg >> 8) & 0xff;
	wbuf[3] = reg & 0xff;
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 4;
	msg[1].buf = val;

	mutex_lock(&charger->io_lock);
	ret = i2c_transfer(client->adapter, msg, 2);
	mfc_check_i2c_error(charger, (ret < 0));
	mutex_unlock(&charger->io_lock);
	if (ret < 0) {
		pr_err("%s: i2c transfer fail", __func__);
		return -1;
	}

	return ret;
}
static void mfc_cps_cmd_send(struct i2c_client *client, u32 cmd)
{
	//struct mfc_charger_data *charger = i2c_get_clientdata(client);
	//u8 data[4];
	//u32 res;

	pr_info("%s: ---> send_cmd %02x\n", __func__, cmd);
	mfc_cps_wls_write_word(client, ADDR_CMD, cmd);
}

static int mfc_cps_wls_cmd_wait(struct i2c_client *client)
{
	int res;
	//struct mfc_charger_data *charger = i2c_get_clientdata(client);
	int cnt = 0;
	u8 data[4];

	while (1) {
		usleep_range(1000, 1100);
		if (mfc_cps_wls_read_word(client, ADDR_FLAG, data) < 0) {
			pr_err("%s: read failed(%d)\n", __func__, ADDR_FLAG);
			return -1;
		}
		res = data[0];
		switch (res) {
		case RUNNING:
		case PASS:
			break;
		case FAIL:
			pr_info("%s: ---> FAIL : 0x%x\n", __func__, res);
			return res;
		case ILLEGAL:
			pr_info("%s: ---> ILLEGAL : 0x%x\n", __func__, res);
			return res;
		default:
			pr_info("%s: ---> ERROR-CODE : 0x%x\n", __func__, res);
			return res;
		}
		if (res == PASS)
			break;
	    /*3s over time*/
		if ((cnt++) == 3000) {
			pr_info("%s: ---> CMD-OVERTIME\n", __func__);
			break;
		}
	}
	return res;
}

static int mfc_cps_wls_program_sram(struct i2c_client *client, int reg, const u8 *wdate, int size)
{
	struct mfc_charger_data *charger = i2c_get_clientdata(client);
	int ret = 0;
	u8 data[SENDSZ + 4];
	int cnt = 0;

	pr_info("%s: size: 0x%x\n", __func__, size);
	while (size > SENDSZ) {
		data[0] = (reg+cnt) >> 24;
		data[1] = ((reg+cnt) >> 16) & 0xff;
		data[2] = ((reg+cnt) >> 8) & 0xff;
		data[3] = (reg+cnt) & 0xff;
		memcpy(data + 4, wdate + cnt, SENDSZ);
		mutex_lock(&charger->io_lock);
		ret = i2c_master_send(client, data, SENDSZ + 4);
		mutex_unlock(&charger->io_lock);
		if (ret < SENDSZ + 4) {
			pr_err("%s: i2c write error, reg: 0x%x\n", __func__, (reg + cnt));
			return ret < 0 ? ret : -EIO;
		}
		cnt = cnt + SENDSZ;
		size = size - SENDSZ;
	}
	if (size > 0) {
		data[0] = (reg+cnt) >> 24;
		data[1] = ((reg+cnt) >> 16) & 0xff;
		data[2] = ((reg+cnt) >> 8) & 0xff;
		data[3] = (reg+cnt) & 0xff;
		memcpy(data + 4, wdate + cnt, size);
		mutex_lock(&charger->io_lock);
		ret = i2c_master_send(client, data, size + 4);
		mutex_unlock(&charger->io_lock);
		if (ret < size + 4) {
			pr_err("%s: i2c write error, reg: 0x%x\n", __func__, (reg + cnt));
			return ret < 0 ? ret : -EIO;
		}
	}

	return ret;
}

static int PgmOTPwRAM_CPS(struct mfc_charger_data *charger, unsigned short OtpAddr,
					  const u8 *srcData, int srcOffs, int size)
{
	int addr;
	int i, j;
	int ret, retry = 3;
	u8 fw_ver[4] = {0, };
	u8 fw_ver_bin[4] = {0, };
    //int flag = 0;
	int cfg_buf_size;
	int buf0_flag;
	int buf1_flag;
	int result;
	//int write_count = 0;

	mfc_reg_read(charger->client, MFC_FW_MAJOR_REV_L_REG, &fw_ver[0]);
	mfc_reg_read(charger->client, MFC_FW_MAJOR_REV_H_REG, &fw_ver[1]);
	pr_info("%s BEFORE rx major firmware version 0x%x\n",
		__func__, fw_ver[0] | (fw_ver[1] << 8));

	mfc_reg_read(charger->client, MFC_FW_MINOR_REV_L_REG, &fw_ver[2]);
	mfc_reg_read(charger->client, MFC_FW_MINOR_REV_H_REG, &fw_ver[3]);
	pr_info("%s BEFORE rx minor firmware version 0x%x\n",
		__func__, fw_ver[2] | (fw_ver[3] << 8));

	/* get major and minor firmware version from firmware file */
	memcpy(fw_ver_bin, &srcData[MFC_FW_VER_BIN_CPS], 4);

	pr_info("%s NEW rx major firmware version 0x%x\n",
		__func__, fw_ver_bin[0] | (fw_ver_bin[1] << 8));
	pr_info("%s NEW rx minor firmware version 0x%x\n",
		__func__, fw_ver_bin[2] | (fw_ver_bin[3] << 8));

	/*
	 * block to access regisgter map during fw update
	 * register-map is not available since SRAM is usued as buffer or bootloader
	 */
	charger->reg_access_lock = true;

	/***************************************************************************************
     *                                  Step1, load to sram                                *
     ***************************************************************************************/
	mfc_cps_wls_write_word(charger->client, 0xFFFFFF00, 0x0000000E); /*enable 32bit i2c*/
	mfc_cps_wls_write_word(charger->client, 0x4000E75C, 0x00001250); /*write password*/
	mfc_cps_wls_write_word(charger->client, 0x40040010, 0x00000006); /*reset and halt mcu*/
	mfc_cps_wls_write_word(charger->client, 0x4000E01C, 0x000003E8); /*config SDA timeout to 1s*/
	pr_info("%s: START LOAD SRAM HEX\n", __func__);
	mfc_cps_wls_program_sram(charger->client, 0x20000000, CPS4038_BL, 0x0800);

	mfc_cps_wls_write_word(charger->client, 0x400400A0, 0x000000FF); /*enable remap function*/
	mfc_cps_wls_write_word(charger->client, 0x40040010, 0x00008003); /*triming load function is disabled and run mcu*/

	usleep_range(10000, 11000);

	mfc_cps_wls_write_word(charger->client, 0xFFFFFF00, 0x0000000E); /*enable 32bit i2c*/
	mfc_cps_wls_write_word(charger->client, 0x4000E01C, 0x000003E8); /*config SDA timeout to 1s*/

	usleep_range(10000, 11000);

	/***************************************************************************************
     *                          Step2, bootloader crc check                                *
	***************************************************************************************/
	mfc_cps_cmd_send(charger->client, CACL_CRC_TEST);
	result = mfc_cps_wls_cmd_wait(charger->client);

	if (result != PASS) {
		pr_err("%s: ---> BOOTLOADER CRC FAIL\n", __func__);
		goto fw_update_fail;
	}

	pr_info("%s: ---> LOAD BOOTLOADER SUCCESSFUL\n", __func__);

	/***************************************************************************************
     *                          Step3, load firmware to MTP                                *
	***************************************************************************************/
	pr_info("%s: START LOAD APP HEX\n", __func__);
	buf0_flag = 0;
	buf1_flag = 0;
	cfg_buf_size = 512;
	addr = 0;
	mfc_cps_wls_write_word(charger->client, ADDR_BUF_SIZE, cfg_buf_size);

	result = mfc_cps_wls_cmd_wait(charger->client);
	if (result != PASS) {
		pr_err("%s: ---> ERASE FAIL\n", __func__);
		goto fw_update_fail;
	}

	for (i = 0; i < (24*1024)/4/cfg_buf_size; i++) {
		if (buf0_flag == 0) {
			mfc_cps_wls_program_sram(charger->client, ADDR_BUFFER0, srcData+addr, cfg_buf_size*4);
			addr  = addr + cfg_buf_size*4;

			if (buf1_flag == 1) {
				result = mfc_cps_wls_cmd_wait(charger->client);
				if (result != PASS) {
					pr_err("%s: ---> WRITE BUFFER1 DATA TO MTP FAIL\n", __func__);
					goto fw_update_fail;
				}
				buf1_flag = 0;
			}
			mfc_cps_cmd_send(charger->client, PGM_BUFFER0);
			buf0_flag = 1;
			continue;
		}

		if (buf1_flag == 0) {
			mfc_cps_wls_program_sram(charger->client, ADDR_BUFFER1, srcData+addr, cfg_buf_size*4);
			addr  = addr + cfg_buf_size*4;

			if (buf0_flag == 1) {
				result = mfc_cps_wls_cmd_wait(charger->client);
				if (result != PASS) {
					pr_err("%s: ---> WRITE BUFFER0 DATA TO MTP FAIL\n", __func__);
					goto fw_update_fail;
				}
				buf0_flag = 0;
			}
			mfc_cps_cmd_send(charger->client, PGM_BUFFER1);
			buf1_flag = 1;
			continue;
		}
	}

	if (buf0_flag == 1) {
		result = mfc_cps_wls_cmd_wait(charger->client);
		if (result != PASS) {
			pr_err("%s: ---> WRITE BUFFER0 DATA TO MTP FAIL\n", __func__);
			goto fw_update_fail;
		}
		buf0_flag = 0;
	}

	if (buf1_flag == 1) {
		result = mfc_cps_wls_cmd_wait(charger->client);
		if (result != PASS) {
			pr_err("%s: ---> WRITE BUFFER1 DATA TO MTP FAIL\n", __func__);
			return MFC_FWUP_ERR_FAIL;
		}
		buf1_flag = 0;
	}
	pr_info("%s: ---> LOAD APP HEX SUCCESSFUL\n", __func__);
	/***************************************************************************************
     *                          Step4, check app CRC                                       *
	***************************************************************************************/
	mfc_cps_cmd_send(charger->client, CACL_CRC_APP);
	result = mfc_cps_wls_cmd_wait(charger->client);

	if (result != PASS) {
		pr_err("%s: ---> APP CRC FAIL\n", __func__);
		goto fw_update_fail;
	}
	pr_info("%s: ---> CHERK APP CRC SUCCESSFUL\n", __func__);
	/***************************************************************************************
     *                          Step5, write mcu start flag                                *
	***************************************************************************************/
	mfc_cps_cmd_send(charger->client, PGM_WR_FLAG);
	result = mfc_cps_wls_cmd_wait(charger->client);
	if (result != PASS) {
		pr_err("%s:---> WRITE MCU START FLAG FAIL\n", __func__);
		goto fw_update_fail;
	}

	pr_info("%s: ---> WRITE MCU START FLAG SUCCESSFUL\n", __func__);
	mfc_cps_wls_write_word(charger->client, 0x40040010, 0x00000008); /* reset all system */

	msleep(100);

	mfc_cps_wls_write_word(charger->client, 0xFFFFFF00, 0x00000000); /* i2c 32bit mode disable */

	charger->reg_access_lock = false;

	for (i = 0; i < retry; i++) {
		usleep_range(10000, 11000);
		ret = MFC_FWUP_ERR_SUCCEEDED;

		mfc_reg_read(charger->client, MFC_FW_MAJOR_REV_L_REG, &fw_ver[0]);
		mfc_reg_read(charger->client, MFC_FW_MAJOR_REV_H_REG, &fw_ver[1]);
		pr_info("%s NOW rx major firmware version 0x%x\n",
			__func__, fw_ver[0] | (fw_ver[1] << 8));

		mfc_reg_read(charger->client, MFC_FW_MINOR_REV_L_REG, &fw_ver[2]);
		mfc_reg_read(charger->client, MFC_FW_MINOR_REV_H_REG, &fw_ver[3]);
		pr_info("%s NOW rx minor firmware version 0x%x\n",
			__func__, fw_ver[2] | (fw_ver[3] << 8));

		for (j = 0; j < 4; j++)
			if (fw_ver[j] != fw_ver_bin[j])
				ret = MFC_FWUP_ERR_COMMON_FAIL;

		if (ret != MFC_FWUP_ERR_COMMON_FAIL)
			break;
		else {
			mfc_uno_on(charger, false);
			msleep(200);
			mfc_uno_on(charger, true);
			msleep(200);
		}
	}

	return ret;

fw_update_fail:
	charger->reg_access_lock = false;
	return MFC_FWUP_ERR_FAIL;
}

static void mfc_reset_rx_power(struct mfc_charger_data *charger, u8 rx_power)
{
#if defined(CONFIG_SEC_FACTORY)
	if (delayed_work_pending(&charger->wpc_rx_power_work))
		cancel_delayed_work(&charger->wpc_rx_power_work);
#endif

	if (charger->adt_transfer_status == WIRELESS_AUTH_PASS) {
		union power_supply_propval value = {0, };

		switch (rx_power) {
		case TX_RX_POWER_7_5W:
			value.intval = SEC_WIRELESS_RX_POWER_7_5W;
			break;
		case TX_RX_POWER_12W:
			value.intval = SEC_WIRELESS_RX_POWER_12W;
			break;
		case TX_RX_POWER_15W:
			value.intval = SEC_WIRELESS_RX_POWER_15W;
			break;
		case TX_RX_POWER_17_5W:
			value.intval = SEC_WIRELESS_RX_POWER_17_5W;
			break;
		case TX_RX_POWER_20W:
			value.intval = SEC_WIRELESS_RX_POWER_20W;
			break;
		default:
			value.intval = SEC_WIRELESS_RX_POWER_12W;
			pr_info("%s %s: undefine rx power reset\n", WC_AUTH_MSG, __func__);
			break;
		}
		psy_do_property("wireless", set,
			POWER_SUPPLY_EXT_PROP_WIRELESS_RX_POWER, value);
	} else {
		pr_info("%s %s: undefine rx power scenario, It is auth failed case how dose it get rx power?\n",
				WC_AUTH_MSG, __func__);
	}
}

static void mfc_wpc_rx_power_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, wpc_rx_power_work.work);
	union power_supply_propval value;

	pr_info("%s: rx power = %d, This W/A is only for Factory\n", __func__, charger->max_power_by_txid);

	value.intval = charger->max_power_by_txid;
	psy_do_property("wireless", set, POWER_SUPPLY_EXT_PROP_WIRELESS_RX_POWER, value);
}

static void mfc_set_pad_hv(struct mfc_charger_data *charger, bool set_hv)
{
	if (!is_hv_wireless_type(charger->pdata->cable_type) && !charger->is_abnormal_pad)
		return;

	if (set_hv && charger->is_full_status)
		return;

	if (set_hv) {
		if (charger->pdata->cable_type == SEC_BATTERY_CABLE_HV_WIRELESS_20)
			mfc_send_command(charger, charger->vrect_by_txid);
		else
			mfc_send_command(charger, MFC_AFC_CONF_10V_TX);

	} else {
		mfc_send_command(charger, MFC_AFC_CONF_5V_TX);
	}
	charger->is_afc_tx = set_hv;
}

static void mfc_recover_vout_by_pad(struct mfc_charger_data *charger)
{
	if (!is_hv_wireless_type(charger->pdata->cable_type))
		return;

	if (charger->is_full_status)
		return;

	if (charger->vout_mode != WIRELESS_VOUT_5V &&
		charger->vout_mode != WIRELESS_VOUT_5V_STEP &&
		charger->vout_mode != WIRELESS_VOUT_5_5V_STEP &&
		charger->vout_mode != WIRELESS_VOUT_OTG) {
		// need to fix here
		if (charger->pdata->cable_type == SEC_BATTERY_CABLE_HV_WIRELESS_20)
			charger->vout_mode = charger->vout_by_txid;
		else
			charger->vout_mode = WIRELESS_VOUT_10V;

		cancel_delayed_work(&charger->wpc_vout_mode_work);
		__pm_stay_awake(charger->wpc_vout_mode_ws);
		queue_delayed_work(charger->wqueue, &charger->wpc_vout_mode_work, 0);

		if ((charger->pdata->cable_type == SEC_BATTERY_CABLE_HV_WIRELESS_20) &&
			(charger->tx_id == TX_ID_FG_PAD)) {
			pr_info("%s: set power = %d\n", __func__, charger->current_rx_power);
			mfc_reset_rx_power(charger, charger->current_rx_power);
		}
	}
}

static void mfc_wpc_afc_vout_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, wpc_afc_vout_work.work);
	union power_supply_propval value = {0, };

	pr_info("%s: start, current cable(%d)\n", __func__, charger->pdata->cable_type);

	/* change cable type */
	if (charger->pdata->cable_type == SEC_BATTERY_CABLE_PREPARE_WIRELESS_HV) {
		if (charger->is_abnormal_pad) {
			pr_info("%s: skip voltgate set to pad, abnormal 3rd party pad\n", __func__);
			charger->pdata->cable_type = SEC_BATTERY_CABLE_WIRELESS;
		} else {
			charger->pdata->cable_type = SEC_BATTERY_CABLE_HV_WIRELESS;
		}
	}

	value.intval = charger->pdata->cable_type;
	psy_do_property("wireless", set,
		POWER_SUPPLY_PROP_ONLINE, value);

	if (charger->is_abnormal_pad) {
		charger->pad_vout = PAD_VOUT_5V;
		goto skip_set_afc_vout;
	}
#if defined(CONFIG_BATTERY_SWELLING)
	/* check swelling mode */
	psy_do_property("battery", get,
		POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT, value);
	pr_info("%s: check swelling mode(%d)\n", __func__, value.intval);

	if (value.intval) {
		pr_info("%s: skip voltgate set to pad, it is swelling mode\n", __func__);
		goto skip_set_afc_vout;
	}
#endif
	pr_info("%s: check OTG(%d), full(%d) tx_id(0x%x)\n",
		__func__, charger->is_otg_on, charger->is_full_status, charger->tx_id);

	if (charger->is_otg_on) {
		pr_info("%s: skip voltgate set to pad, otg is conntected\n", __func__);
		goto skip_set_afc_vout;
	}

	if (charger->is_full_status && volt_ctrl_pad(charger->tx_id)) {
		pr_info("%s: skip voltgate set to pad, full status with dream pad\n", __func__);
		goto skip_set_afc_vout;
	}
	// need to fix here to get vout setting
	mfc_set_pad_hv(charger, true);
	mfc_recover_vout_by_pad(charger);
	pr_info("%s: is_afc_tx = %d vout read = %d\n", __func__,
			charger->is_afc_tx, mfc_get_adc(charger, MFC_ADC_VOUT));
skip_set_afc_vout:
	__pm_relax(charger->wpc_afc_vout_ws);
}

static void mfc_wpc_fw_update_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, wpc_fw_update_work.work);
	union power_supply_propval value = {0, };
	int ret = 0;
	int i = 0;
	bool is_changed = false;
	char fwtime[8] = {32, 32, 32, 32, 32, 32, 32, 32};
	char fwdate[8] = {32, 32, 32, 32, 32, 32, 32, 32};
	u8 data = 32; /* ascii space */
	const char *fw_path = MFC_FLASH_FW_HEX_CPS_PATH;
	int fw_size;
	u8 pgmCnt = 0;
	bool repairEn = false;

	pr_info("%s: firmware update mode is = %d\n", __func__, charger->fw_cmd);

	if (gpio_get_value(charger->pdata->wpc_en)) {
		pr_info("%s: wpc_en disabled\n", __func__);
		goto end_of_fw_work;
	}
	mfc_set_wpc_en(charger, WPC_EN_FW, true); /* keep the wpc_en low during fw update */

	switch (charger->fw_cmd) {
	case SEC_WIRELESS_FW_UPDATE_SPU_MODE:
	case SEC_WIRELESS_FW_UPDATE_SPU_VERIFY_MODE:
	case SEC_WIRELESS_FW_UPDATE_SDCARD_MODE:
	case SEC_WIRELESS_FW_UPDATE_AUTO_MODE:
	case SEC_WIRELESS_FW_UPDATE_BUILTIN_MODE:
		mfc_uno_on(charger, true);
		msleep(200);
		if (!mfc_check_chip_id(charger)) {
			pr_info("%s: current IC's chip_id is not matching to driver's chip_id(0x%x)\n",
				__func__, MFC_CHIP_CPS);
			break;
		}
		if (charger->fw_cmd == SEC_WIRELESS_FW_UPDATE_AUTO_MODE) {
			charger->pdata->otp_firmware_ver = mfc_get_firmware_version(charger, MFC_RX_FIRMWARE);
#if defined(CONFIG_WIRELESS_IC_PARAM)
			if (charger->wireless_fw_mode_param == SEC_WIRELESS_FW_UPDATE_SPU_MODE &&
				charger->pdata->otp_firmware_ver > charger->pdata->fw_ver) {
				pr_info("%s: current version (0x%x) is higher than BIN_VERSION by SPU(0x%x)\n",
					__func__, charger->pdata->otp_firmware_ver, charger->pdata->fw_ver);
				break;
			}
#endif
			if (charger->pdata->otp_firmware_ver == charger->pdata->fw_ver) {
				pr_info("%s: current version (0x%x) is same to BIN_VERSION (0x%x)\n",
					__func__, charger->pdata->otp_firmware_ver, charger->pdata->fw_ver);
				break;
			}
		}
		charger->pdata->otp_firmware_result = MFC_FWUP_ERR_RUNNING;
		is_changed = true;

		dev_err(&charger->client->dev, "%s, request_firmware\n", __func__);

		if (charger->fw_cmd == SEC_WIRELESS_FW_UPDATE_SPU_MODE ||
			charger->fw_cmd == SEC_WIRELESS_FW_UPDATE_SPU_VERIFY_MODE)
			fw_path = MFC_FW_SPU_BIN_PATH;
		else if (charger->fw_cmd == SEC_WIRELESS_FW_UPDATE_SDCARD_MODE)
			fw_path = MFC_FW_SDCARD_BIN_PATH;
		else
			fw_path = MFC_FLASH_FW_HEX_CPS_PATH;

		ret = request_firmware(&charger->firm_data_bin, fw_path, &charger->client->dev);
		if (ret < 0) {
			dev_err(&charger->client->dev, "%s: failed to request firmware %s (%d)\n",
				__func__, fw_path, ret);
			charger->pdata->otp_firmware_result = MFC_FWUP_ERR_REQUEST_FW_BIN;
			goto fw_err;
		}
		fw_size = (int)charger->firm_data_bin->size;

#if IS_ENABLED(CONFIG_SPU_VERIFY)
		if (charger->fw_cmd == SEC_WIRELESS_FW_UPDATE_SPU_MODE ||
			charger->fw_cmd == SEC_WIRELESS_FW_UPDATE_SPU_VERIFY_MODE) {
			if (spu_firmware_signature_verify("MFC", charger->firm_data_bin->data, charger->firm_data_bin->size) ==
				(fw_size - SPU_METADATA_SIZE(MFC))) {
				pr_err("%s: spu_firmware_signature_verify success\n", __func__);
				fw_size -= SPU_METADATA_SIZE(MFC);
				if (charger->fw_cmd == SEC_WIRELESS_FW_UPDATE_SPU_VERIFY_MODE) {
					charger->pdata->otp_firmware_result = MFC_FW_RESULT_PASS;
					goto fw_err;
				}
			} else {
				pr_err("%s: spu_firmware_signature_verify failed\n", __func__);
				goto fw_err;
			}
		}
#endif

		disable_irq(charger->pdata->irq_wpc_int);
		disable_irq(charger->pdata->irq_wpc_det);
		if (charger->pdata->irq_wpc_pdrc)
			disable_irq(charger->pdata->irq_wpc_pdrc);
		if (charger->pdata->irq_wpc_pdet_b)
			disable_irq(charger->pdata->irq_wpc_pdet_b);
		__pm_stay_awake(charger->wpc_update_ws);
		pr_info("%s data size = %ld\n", __func__, (long)fw_size);

		do {
			if (repairEn == true) {
				if (charger->pdata->wpc_en >= 0)
					gpio_direction_output(charger->pdata->wpc_en, 1);
				mfc_uno_on(charger, false);
				msleep(1000);
				mfc_uno_on(charger, true);
				if (charger->pdata->wpc_en >= 0)
					gpio_direction_output(charger->pdata->wpc_en, 0);
				msleep(300);
			}
			ret = PgmOTPwRAM_CPS(charger, 0, charger->firm_data_bin->data, 0, fw_size);
			if (ret != MFC_FWUP_ERR_SUCCEEDED)
				repairEn = true;
			else
				repairEn = false;

			pgmCnt++;

			pr_info("%s %s: repairEn(%d), pgmCnt(%d), ret(%d)\n",
				MFC_FW_MSG, __func__, repairEn, pgmCnt, ret);
		} while ((ret != MFC_FWUP_ERR_SUCCEEDED) && (pgmCnt < MAX_MTP_PGM_CNT));

		release_firmware(charger->firm_data_bin);

		for (i = 0; i < 8; i++) {
			if (mfc_reg_read(charger->client, MFC_FW_DATA_CODE_0+i, &data) > 0)
				fwdate[i] = (char)data;
		}
		for (i = 0; i < 8; i++) {
			if (mfc_reg_read(charger->client, MFC_FW_TIMER_CODE_0+i, &data) > 0)
				fwtime[i] = (char)data;
		}
		pr_info("%s: %d%d%d%d%d%d%d%d, %d%d%d%d%d%d%d%d\n", __func__,
			fwdate[0], fwdate[1], fwdate[2], fwdate[3], fwdate[4], fwdate[5], fwdate[6], fwdate[7],
			fwtime[0], fwtime[1], fwtime[2], fwtime[3], fwtime[4], fwtime[5], fwtime[6], fwtime[7]);

		charger->pdata->otp_firmware_ver = mfc_get_firmware_version(charger, MFC_RX_FIRMWARE);
		charger->pdata->wc_ic_rev = mfc_get_ic_revision(charger, MFC_IC_REVISION);

		for (i = 0; i < 8; i++) {
			if (mfc_reg_read(charger->client, MFC_FW_DATA_CODE_0+i, &data) > 0)
				fwdate[i] = (char)data;
		}
		for (i = 0; i < 8; i++) {
			if (mfc_reg_read(charger->client, MFC_FW_TIMER_CODE_0+i, &data) > 0)
				fwtime[i] = (char)data;
		}
		pr_info("%s: %d%d%d%d%d%d%d%d, %d%d%d%d%d%d%d%d\n", __func__,
			fwdate[0], fwdate[1], fwdate[2], fwdate[3], fwdate[4], fwdate[5], fwdate[6], fwdate[7],
			fwtime[0], fwtime[1], fwtime[2], fwtime[3], fwtime[4], fwtime[5], fwtime[6], fwtime[7]);

		enable_irq(charger->pdata->irq_wpc_int);
		enable_irq(charger->pdata->irq_wpc_det);
		if (charger->pdata->irq_wpc_pdrc)
			enable_irq(charger->pdata->irq_wpc_pdrc);
		if (charger->pdata->irq_wpc_pdet_b)
			enable_irq(charger->pdata->irq_wpc_pdet_b);
		__pm_relax(charger->wpc_update_ws);
		break;
	default:
		break;
	}

	msleep(200);
	mfc_uno_on(charger, false);
	pr_info("%s---------------------------------------------------------------\n", __func__);

	if (is_changed) {
		if (ret == MFC_FWUP_ERR_SUCCEEDED) {
			charger->pdata->otp_firmware_result = MFC_FW_RESULT_PASS;
#if defined(CONFIG_WIRELESS_IC_PARAM)
			charger->wireless_fw_mode_param = charger->fw_cmd & 0xF;
			pr_info("%s: succeed. fw_mode(0x%01X)\n",
				__func__, charger->wireless_fw_mode_param);
			charger->wireless_param_info &= 0xFFFFFF0F;
			charger->wireless_param_info |= (charger->wireless_fw_mode_param & 0xF) << 4;
			pr_info("%s: wireless_param_info (0x%08X)\n", __func__, charger->wireless_param_info);
#endif
		} else {
			charger->pdata->otp_firmware_result = ret;
		}
	}
	value.intval = false;
	psy_do_property("battery", set, POWER_SUPPLY_EXT_PROP_MFC_FW_UPDATE, value);

	mfc_set_wpc_en(charger, WPC_EN_FW, false);

	return;
fw_err:
	mfc_uno_on(charger, false);
	mfc_set_wpc_en(charger, WPC_EN_FW, false);
end_of_fw_work:
	value.intval = false;
	psy_do_property("battery", set, POWER_SUPPLY_EXT_PROP_MFC_FW_UPDATE, value);
}

static void mfc_set_tx_data(struct mfc_charger_data *charger, int idx)
{
	if (idx < 0)
		idx = 0;
	else if (idx >= charger->pdata->len_wc20_list)
		idx = charger->pdata->len_wc20_list - 1;

	charger->vout_by_txid = charger->pdata->wireless20_vout_list[idx];
	charger->vrect_by_txid = charger->pdata->wireless20_vrect_list[idx];
	charger->max_power_by_txid = charger->pdata->wireless20_max_power_list[idx];
}

static int mfc_set_sgf_data(struct mfc_charger_data *charger, sgf_data *pdata)
{
	pr_info("%s: size = %d, type = %d\n", __func__, pdata->size, pdata->type);

	switch (pdata->type) {
	case WPC_TX_COM_RX_POWER:
	{
		union power_supply_propval value = {0, };
		int tx_power = *(int *)pdata->data;

		switch (tx_power) {
		case TX_RX_POWER_7_5W:
			pr_info("%s : TX Power is 7.5W\n", __func__);
			charger->current_rx_power = TX_RX_POWER_7_5W;
			mfc_set_tx_data(charger, 0);
			break;
		case TX_RX_POWER_12W:
			pr_info("%s : TX Power is 12W\n", __func__);
			charger->current_rx_power = TX_RX_POWER_12W;
			mfc_set_tx_data(charger, 1);
			break;
		case TX_RX_POWER_15W:
			pr_info("%s : TX Power is 15W\n", __func__);
			charger->current_rx_power = TX_RX_POWER_15W;
			mfc_set_tx_data(charger, 2);
			break;
		case TX_RX_POWER_17_5W:
			pr_info("%s : TX Power is 17.5W\n", __func__);
			charger->current_rx_power = TX_RX_POWER_17_5W;
			mfc_set_tx_data(charger, 3);
			break;
		case TX_RX_POWER_20W:
			pr_info("%s : TX Power is 20W\n", __func__);
			charger->current_rx_power = TX_RX_POWER_20W;
			mfc_set_tx_data(charger, 4);
			break;
		default:
			pr_info("%s : Undefined TX Power(%d)\n", __func__, tx_power);
			return -EINVAL;
		}
		charger->adt_transfer_status = WIRELESS_AUTH_PASS;
		charger->pdata->cable_type = value.intval = SEC_BATTERY_CABLE_HV_WIRELESS_20;
		pr_info("%s: change cable type to WPC HV 2.0\n", __func__);

		__pm_stay_awake(charger->wpc_afc_vout_ws);
		queue_delayed_work(charger->wqueue, &charger->wpc_afc_vout_work, msecs_to_jiffies(0));
	}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

#if defined(CONFIG_MST_V2)
static void mfc_send_mst_cmd(int cmd, struct mfc_charger_data *charger, u8 irq_src_l, u8 irq_src_h)
{
	switch (cmd) {
	case MST_MODE_ON:
		/* clear interrupt */
		mfc_reg_write(charger->client, MFC_INT_A_CLEAR_L_REG, irq_src_l);	// clear int
		mfc_reg_write(charger->client, MFC_INT_A_CLEAR_H_REG, irq_src_h);	// clear int
		mfc_set_cmd_l_reg(charger, 0x20, MFC_CMD_CLEAR_INT_MASK);	// command

#if defined(CONFIG_MST_PCR)
		pr_info("%s : MST ISET_PCR : %d\n", __func__, charger->pdata->mst_iset_pcr);
		mfc_reg_write(charger->client, MFC_ISET_PCR, charger->pdata->mst_iset_pcr);
		/* set PCR mode2 */
		mfc_reg_write(charger->client, MFC_MST_MODE_SEL_REG, MFC_TX_MODE_MST_PCR_MODE2);
#else
		mfc_reg_write(charger->client, MFC_MST_MODE_SEL_REG, MFC_TX_MODE_MST_MODE2);	/* set MST mode2 */
#endif
		pr_info("%s 2AC Missing ! : MST on REV : %d\n", __func__, charger->pdata->wc_ic_rev);

		/* clear interrupt */
		mfc_reg_write(charger->client, MFC_INT_A_CLEAR_L_REG, irq_src_l);	// clear int
		mfc_reg_write(charger->client, MFC_INT_A_CLEAR_H_REG, irq_src_h);	// clear int
		mfc_set_cmd_l_reg(charger, 0x20, MFC_CMD_CLEAR_INT_MASK);	// command

		usleep_range(10000, 11000);
		break;
	case MST_MODE_OFF:
		pr_info("%s: set MST mode off\n", __func__);
		break;
	default:
		break;
	}
}

static int mfc_get_mst_mode(struct mfc_charger_data *charger)
{
	u8 mst_mode, reg_data;
	int ret;

	ret = mfc_reg_read(charger->client, MFC_MST_MODE_SEL_REG, &mst_mode);
	if (ret < 0) {
		pr_info("%s mst mode(0x2) i2c write failed, ret = %d\n",
			__func__, ret);
		return ret;
	}

	ret = mfc_reg_read(charger->client, MFC_SYS_OP_MODE_REG, &reg_data);
	if (ret < 0) {
		pr_info("%s mst mode change irq(0x4) read failed, ret = %d\n",
			__func__, ret);
		return ret;
	}

	pr_info("%s mst mode check: mst_mode = %d, reg_data = %d\n",
		__func__, mst_mode, reg_data);

	reg_data &= 0x0C;	/* use only [3:2]bit of sys_op_mode register for MST */

	ret = 0;
	if (reg_data == 0x4)
		ret = mst_mode;

	return ret;
}
#endif

#define ALIGN_WORK_CHK_CNT	5
#define ALIGN_WORK_DELAY	500
#define ALIGN_CHK_PERIOD	1000
#define ALIGN_WORK_CHK_PERIOD	100
#define MISALIGN_TX_OFF_TIME	10

static int mfc_get_target_vout(struct mfc_charger_data *charger)
{
	if (charger->vout_strength == 0)
		return (charger->pdata->mis_align_target_vout + charger->pdata->mis_align_offset);
	else
		return charger->pdata->mis_align_target_vout; // falling uvlo
}

static int mfc_unsafe_vout_check(struct mfc_charger_data *charger)
{
	int vout;
	int target_vout;

	if (charger->pdata->wpc_vout_ctrl_full && charger->is_full_status)
		return 0;

	vout = mfc_get_adc(charger, MFC_ADC_VOUT);
	target_vout = mfc_get_target_vout(charger);

	pr_info("%s: vout(%d) target_vout(%d)\n", __func__, vout, target_vout);
	if (vout < target_vout)
		return 1;
	return 0;
}

static bool mfc_check_wire_status(void)
{
	union power_supply_propval value = {0, };
	int wire_type = SEC_BATTERY_CABLE_NONE;

	psy_do_property("battery", get, POWER_SUPPLY_EXT_PROP_CHARGE_COUNTER_SHADOW, value);
		wire_type = value.intval;

	if (is_wired_type(wire_type) || (wire_type == SEC_BATTERY_CABLE_OTG)) {
		pr_info("%s: return misalign check, cable_type(%d)\n",
				__func__, wire_type);
		return true;
	}

	return false;
}

static void mfc_wpc_align_check_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, align_check_work.work);
	struct timespec64 current_ts = {0, };
	union power_supply_propval value = {0, };
	long checking_time = 0;
	int vout = 0, vout_avr = 0, i = 0;
	static int vout_sum, align_work_cnt;

	if (!charger->det_state)
		goto end_align_work;

	if (charger->pdata->wpc_vout_ctrl_full && charger->is_full_status)
		goto end_align_work;

	if (charger->wc_align_check_start.tv_sec == 0) {
		charger->wc_align_check_start = ktime_to_timespec64(ktime_get_boottime());
		align_work_cnt = 0;
		vout_sum = 0;
	}
	current_ts = ktime_to_timespec64(ktime_get_boottime());
	checking_time = current_ts.tv_sec - charger->wc_align_check_start.tv_sec;

	vout = mfc_get_adc(charger, MFC_ADC_VOUT);
	vout_sum += vout;
	align_work_cnt++;
	vout_avr = vout_sum / align_work_cnt;

	pr_info("%s: vout(%d), vout_avr(%d), work_cnt(%d), checking_time(%ld)\n",
		__func__, vout, vout_avr, align_work_cnt, checking_time);

	if (align_work_cnt < ALIGN_WORK_CHK_CNT) {
		queue_delayed_work(charger->wqueue,
			&charger->align_check_work, msecs_to_jiffies(ALIGN_WORK_CHK_PERIOD));

		return;
	}

	if (vout_avr >= mfc_get_target_vout(charger)) {
		value.intval = charger->vout_strength = 100;
		psy_do_property("battery",
			set, POWER_SUPPLY_EXT_PROP_WPC_FREQ_STRENGTH, value);
		psy_do_property("wireless", set, POWER_SUPPLY_PROP_CURRENT_MAX, value);
		pr_info("%s: Finish to check (Align)\n", __func__);
		goto end_align_work;
	} else if (checking_time >= MISALIGN_TX_OFF_TIME) {
		pr_info("%s: %s to check (Timer cnt :%d)\n",
			__func__, charger->mis_align_tx_try_cnt ==  MISALIGN_TX_TRY_CNT ? "Finish" : "Retry",
			charger->mis_align_tx_try_cnt);

		for (i = 0; i < 30; i++) {
			pr_info("%s: Send a packet to TX device to stop power sharing\n",
				__func__);
			mfc_send_command(charger, MFC_TX_UNO_OFF);
			if (mfc_get_adc(charger, MFC_ADC_VRECT) <= 0)
				break;
		}
		charger->mis_align_tx_try_cnt++;
		mfc_set_wpc_en(charger, WPC_EN_CHARGING, false);
		goto end_algin_work_by_retry;
	} else if (checking_time >= MISALIGN_TX_OFF_TIME * MISALIGN_TX_TRY_CNT) {
		pr_info("%s: Finish to check (Timer expired %d secs)\n",
			__func__, MISALIGN_TX_OFF_TIME * MISALIGN_TX_TRY_CNT);
		goto end_align_work;
	} else {
		if (mfc_check_wire_status())
			goto end_align_work;

		pr_info("%s: Continue to check until %d secs (Misalign)\n",
			__func__, MISALIGN_TX_OFF_TIME * MISALIGN_TX_TRY_CNT);
		value.intval = charger->vout_strength = 0;
		psy_do_property("battery",
			set, POWER_SUPPLY_EXT_PROP_WPC_FREQ_STRENGTH, value);

		align_work_cnt = 0;
		vout_sum = 0;
		queue_delayed_work(charger->wqueue,
			&charger->align_check_work, msecs_to_jiffies(ALIGN_CHK_PERIOD));
	}

	return;

end_align_work:
	mfc_set_wpc_en(charger, WPC_EN_CHARGING, true);
	charger->mis_align_tx_try_cnt = 1;
	charger->wc_checking_align = false;
	charger->wc_align_check_start.tv_sec = 0;
end_algin_work_by_retry:
	__pm_relax(charger->align_check_ws);
}

static void mfc_wpc_align_check(struct mfc_charger_data *charger, unsigned int work_delay)
{
	if (!charger->pdata->mis_align_guide)
		return;

	if (mfc_check_wire_status())
		return;

	if (charger->wc_checking_align) {
		pr_info("%s: return, wc_checking_align(%d)\n", __func__, charger->wc_checking_align);
		return;
	}

	if (!charger->pdata->is_charging) {
		pr_info("%s: return, is_charging(%d)\n",
			__func__, charger->pdata->is_charging);
		return;
	}

	if (charger->vout_strength >= 100) {
		if (!mfc_unsafe_vout_check(charger)) {
			pr_info("%s: return, safe vout\n", __func__);
			return;
		}
	}

	pr_info("%s: start\n", __func__);
	__pm_stay_awake(charger->align_check_ws);
	charger->wc_checking_align = true;
	queue_delayed_work(charger->wqueue, &charger->align_check_work, msecs_to_jiffies(work_delay));
}

static void mfc_wpc_mode_change_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, mode_change_work.work);
	union power_supply_propval value = {0, };
	u8 op_mode = 0;

	pr_info("%s: start\n", __func__);
	if (!charger->wc_tx_enable)
		goto end_work;

	if (mfc_reg_read(charger->client, MFC_SYS_OP_MODE_REG, &op_mode) <= 0)
		goto end_work;

	op_mode = op_mode & 0xF;
	pr_info("%s: op_mode = 0x%x\n", __func__, op_mode);

	if (op_mode == MFC_TX_MODE_TX_PWR_HOLD) {
		if (charger->wc_rx_type == SS_GEAR) {
			/* start 3min alarm timer */
			pr_info("@Tx_Mode %s: Received PHM and start PHM disable alarm by 3min\n", __func__);
			alarm_start(&charger->phm_alarm,
					ktime_add(ktime_get_boottime(), ktime_set(180, 0)));
		} else {
			pr_info("%s: TX entered PHM but no PHM disable 3min timer\n", __func__);
		}

		charger->tx_device_phm = 1;
		value.intval = 1;
		psy_do_property("wireless", set,
			POWER_SUPPLY_EXT_PROP_GEAR_PHM_EVENT, value);
	} else {
		mfc_test_read(charger);

		if (charger->tx_device_phm) {
			pr_info("@Tx_Mode %s: Ended PHM\n", __func__);

			charger->tx_device_phm = 0;
			value.intval = 0;
			psy_do_property("wireless", set,
				POWER_SUPPLY_EXT_PROP_GEAR_PHM_EVENT, value);
		}
		if (charger->phm_alarm.state & ALARMTIMER_STATE_ENQUEUED) {
			pr_info("@Tx_Mode %s: escape PHM mode, cancel PHM alarm\n", __func__);
			cancel_delayed_work(&charger->wpc_tx_phm_work);
			__pm_relax(charger->wpc_tx_phm_ws);
			alarm_cancel(&charger->phm_alarm);
		}
	}

end_work:
	__pm_relax(charger->mode_change_ws);
}

static int cps4038_chg_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct mfc_charger_data *charger = power_supply_get_drvdata(psy);
	enum power_supply_ext_property ext_psp = (enum power_supply_ext_property) psp;
//	union power_supply_propval value;

	switch ((int)psp) {
	case POWER_SUPPLY_PROP_STATUS:
		pr_info("%s: charger->pdata->cs100_status %d\n", __func__, charger->pdata->cs100_status);
		val->intval = charger->pdata->cs100_status;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
	case POWER_SUPPLY_PROP_HEALTH:
		return -ENODATA;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		if (charger->pdata->is_charging)
			val->intval = mfc_get_vout(charger);
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		return -ENODATA;
	case POWER_SUPPLY_PROP_ONLINE:
		pr_info("%s: cable_type =%d\n", __func__, charger->pdata->cable_type);
		val->intval = charger->pdata->cable_type;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		pr_info("%s: POWER_SUPPLY_PROP_MANUFACTURER, intval(0x%x), called by(%ps)\n",
				__func__, val->intval, __builtin_return_address(0));
		if (val->intval == SEC_WIRELESS_OTP_FIRM_RESULT) {
			pr_info("%s: otp firmware result = %d\n", __func__, charger->pdata->otp_firmware_result);
			val->intval = charger->pdata->otp_firmware_result;
		} else if (val->intval == SEC_WIRELESS_IC_REVISION) {
			pr_info("%s: check ic revision\n", __func__);
			val->intval = mfc_get_ic_revision(charger, MFC_IC_REVISION);
		} else if (val->intval == SEC_WIRELESS_IC_CHIP_ID) {
			pr_info("%s: check ic chip_id(0x%02X)\n", __func__, charger->chip_id);
			val->intval = charger->chip_id;
		} else if (val->intval == SEC_WIRELESS_OTP_FIRM_VER_BIN) {
			/* update latest kernl f/w version */
			val->intval = charger->pdata->fw_ver;
		} else if (val->intval == SEC_WIRELESS_OTP_FIRM_VER) {
			val->intval = mfc_get_firmware_version(charger, MFC_RX_FIRMWARE);
			pr_info("%s: check f/w revision (0x%x)\n", __func__, val->intval);
			if (val->intval < 0 && charger->pdata->otp_firmware_ver > 0)
				val->intval = charger->pdata->otp_firmware_ver;
		} else if (val->intval == SEC_WIRELESS_OTP_FIRM_VERIFY) {
			pr_info("%s: CPS FIRM_VERIFY is not implemented\n", __func__);
			val->intval = 1;
		} else {
			val->intval = -ENODATA;
			pr_err("%s: wrong mode\n", __func__);
		}
		break;
	case POWER_SUPPLY_PROP_ENERGY_NOW: /* vout */
		if (charger->pdata->is_charging) {
			val->intval = mfc_get_adc(charger, MFC_ADC_VOUT);
			pr_info("%s: wc vout (%d)\n", __func__, val->intval);
		} else {
			val->intval = 0;
		}
		break;
	case POWER_SUPPLY_PROP_ENERGY_AVG: /* vrect */
		if (charger->pdata->is_charging)
			val->intval = mfc_get_adc(charger, MFC_ADC_VRECT);
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		val->intval = charger->vrect_by_txid;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = mfc_get_adc(charger, val->intval);
		break;
	case POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN:
		break;
	case POWER_SUPPLY_PROP_CHARGE_EMPTY:
		val->intval = charger->wc_ldo_status;
		break;
	case POWER_SUPPLY_EXT_PROP_MIN ... POWER_SUPPLY_EXT_PROP_MAX:
		switch (ext_psp) {
		case POWER_SUPPLY_EXT_PROP_WIRELESS_OP_FREQ:
			val->intval = mfc_get_adc(charger, MFC_ADC_OP_FRQ);
			pr_info("%s: Operating FQ %dkHz\n", __func__, val->intval);
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_OP_FREQ_STRENGTH:
			val->intval = charger->vout_strength;
			pr_info("%s: vout strength = (%d)\n",
				__func__, charger->vout_strength);
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_TRX_CMD:
			val->intval = charger->pdata->trx_data_cmd;
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_TRX_VAL:
			val->intval = charger->pdata->trx_data_val;
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_TX_ID:
			val->intval = charger->tx_id;
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_TX_ID_CNT:
			val->intval = charger->tx_id_cnt;
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_RX_CONNECTED:
			val->intval = charger->wc_rx_connected;
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_RX_TYPE:
			val->intval = charger->wc_rx_type;
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_TX_UNO_VIN:
			val->intval = mfc_get_adc(charger, MFC_ADC_TX_VOUT);
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_TX_UNO_IIN:
			val->intval = mfc_get_adc(charger, MFC_ADC_TX_IOUT);
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_RX_POWER:
			val->intval = charger->current_rx_power;
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_AUTH_ADT_STATUS:
			val->intval = charger->adt_transfer_status;
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_AUTH_ADT_DATA:
			{
				//int i = 0;
				//u8 *p_data;

				if (charger->adt_transfer_status == WIRELESS_AUTH_RECEIVED) {
					pr_info("%s %s: MFC_ADT_RECEIVED (%d)\n",
							WC_AUTH_MSG, __func__, charger->adt_transfer_status);
					val->strval = (u8 *)ADT_buffer_rdata;
					//p_data = ADT_buffer_rdata;
					//for (i = 0; i < adt_readSize; i++)
					//	pr_info("%s: auth read data = %x", __func__, p_data[i]);
					//pr_info("\n", __func__);
				} else {
					pr_info("%s: data hasn't been received yet\n", __func__);
					return -ENODATA;
				}
			}
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_AUTH_ADT_SIZE:
			val->intval = adt_readSize;
			pr_info("%s %s: MFC_ADT_RECEIVED (%d), DATA SIZE(%d)\n",
					WC_AUTH_MSG, __func__, charger->adt_transfer_status, val->intval);
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_RX_VOUT:
			val->intval = charger->vout_mode;
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_INITIAL_WC_CHECK:
			val->intval = charger->initial_wc_check;
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_CHECK_FW_VER:
#if defined(CONFIG_WIRELESS_IC_PARAM)
			pr_info("%s: fw_ver (param:0x%04X, build:0x%04X)\n",
				__func__, charger->wireless_fw_ver_param, charger->pdata->fw_ver);
			if (charger->wireless_fw_ver_param == charger->pdata->fw_ver)
				val->intval = 1;
			else
				val->intval = 0;
#else
			val->intval = 0;
#endif
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_MST_PWR_EN:
			if (gpio_is_valid(charger->pdata->mst_pwr_en)) {
				val->intval = gpio_get_value(charger->pdata->mst_pwr_en);
			} else {
				pr_info("%s: invalid gpio(mst_pwr_en)\n", __func__);
				val->intval = 0;
			}
			break;
		case POWER_SUPPLY_EXT_PROP_PAD_VOLT_CTRL:
			val->intval = charger->is_afc_tx;
			break;
		case POWER_SUPPLY_EXT_PROP_WPC_EN:
			val->intval = gpio_get_value(charger->pdata->wpc_en);
			break;
#if defined(CONFIG_MST_V2)
		case POWER_SUPPLY_EXT_PROP_MST_MODE:
			val->intval = mfc_get_mst_mode(charger);
			break;
		case POWER_SUPPLY_EXT_PROP_MST_DELAY:
			val->intval = DELAY_FOR_MST;
			break;
#endif
		case POWER_SUPPLY_EXT_PROP_MONITOR_WORK:
			if (gpio_get_value(charger->pdata->wpc_en))
				pr_info("@DIS_MFC %s: charger->wpc_en_flag(0x%x)\n", __func__, charger->wpc_en_flag);
			break;
		case POWER_SUPPLY_EXT_PROP_GEAR_PHM_EVENT:
			val->intval = charger->tx_device_phm;
			break;
		case POWER_SUPPLY_EXT_PROP_CHARGE_OTG_CONTROL:
		case POWER_SUPPLY_EXT_PROP_CHARGE_POWERED_OTG_CONTROL:
			return -ENODATA;
		case POWER_SUPPLY_EXT_PROP_INPUT_VOLTAGE_REGULATION:
			val->intval = charger->pdata->vout_status;
			break;
#if defined(CONFIG_WIRELESS_IC_PARAM)
		case POWER_SUPPLY_EXT_PROP_WIRELESS_PARAM_INFO:
			val->intval = charger->wireless_param_info;
			break;
#endif
		case POWER_SUPPLY_EXT_PROP_WIRELESS_SGF:
		{
			sgf_data data;

			data.size = *(int *)val->strval;
			data.type = *(int *)(val->strval + 4);
			data.data = (char *)(val->strval + 8);
			mfc_set_sgf_data(charger, &data);
		}
			break;
		case POWER_SUPPLY_EXT_PROP_WPC_FREQ_STRENGTH:
			pr_info("%s: vout_strength = %d\n",
				__func__, charger->vout_strength);
			val->intval = charger->vout_strength;
			break;
		case POWER_SUPPLY_EXT_PROP_BATT_DUMP:
		{
			char buf[1024] = {0, };

			if (val->intval == SB_WRL_TX_MODE) {
				sprintf(buf, "%d,%d,%d,%d,%d,%d,%s,%x,0x%x,",
					mfc_get_adc(charger, MFC_ADC_TX_VOUT),
					mfc_get_adc(charger, MFC_ADC_TX_IOUT),
					mfc_get_adc(charger, MFC_ADC_PING_FRQ),
					mfc_get_adc(charger, MFC_ADC_TX_MIN_OP_FRQ),
					mfc_get_adc(charger, MFC_ADC_TX_MAX_OP_FRQ),
					charger->tx_device_phm,
					sb_rx_type_str(charger->wc_rx_type),
					charger->pdata->otp_firmware_ver,
					charger->pdata->wc_ic_rev);
			} else if (val->intval == SB_WRL_RX_MODE) {
				sprintf(buf, "%d,%d,%d,%d,0x%x,%x,0x%x,",
					mfc_get_adc(charger, MFC_ADC_VOUT),
					mfc_get_adc(charger, MFC_ADC_VRECT),
					mfc_get_adc(charger, MFC_ADC_RX_IOUT),
					mfc_get_adc(charger, MFC_ADC_OP_FRQ),
					charger->tx_id,
					charger->pdata->otp_firmware_ver,
					charger->pdata->wc_ic_rev);
			}
			val->strval = buf;
		}
			break;
		case POWER_SUPPLY_EXT_PROP_TX_PWR_BUDG:
			switch (charger->tx_pwr_budg) {
			case MFC_TX_PWR_BUDG_2W:
			case MFC_TX_PWR_BUDG_5W:
				val->intval = RX_POWER_5W;
				break;
			case MFC_TX_PWR_BUDG_7_5W:
				val->intval = RX_POWER_7_5W;
				break;
			case MFC_TX_PWR_BUDG_12W:
				val->intval = RX_POWER_12W;
				break;
			case MFC_TX_PWR_BUDG_15W:
				val->intval = RX_POWER_15W;
				break;
			default:
				val->intval = RX_POWER_NONE;
			}
			break;
		default:
			return -ENODATA;
		}
		break;
	default:
		return -ENODATA;
	}
	return 0;
}

static void mfc_wpc_vout_mode_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, wpc_vout_mode_work.work);
	int vout_step = charger->pdata->vout_status;
	int vout = MFC_VOUT_10V;
	int wpc_vout_ctrl_lcd_on = 0;
	union power_supply_propval value = {0, };

	if (is_shutdn) {
		pr_err("%s: Escape by shtudown\n", __func__);
		return;
	}
	pr_info("%s: start - vout_mode(%s), vout_status(%s)\n",
		__func__, sb_vout_ctr_mode_str(charger->vout_mode), sb_rx_vout_str(charger->pdata->vout_status));
	switch (charger->vout_mode) {
	case WIRELESS_VOUT_5V:
		mfc_set_vout(charger, MFC_VOUT_5V);
		if (charger->tx_id == TX_ID_P1100_PAD)
			mfc_cma_cmb_onoff(charger, true, false);
		break;
	case WIRELESS_VOUT_9V:
		if (charger->tx_id == TX_ID_P1100_PAD)
			mfc_cma_cmb_onoff(charger, true, true);
		mfc_set_vout(charger, MFC_VOUT_9V);
		break;
	case WIRELESS_VOUT_10V:
		if (charger->tx_id == TX_ID_P1100_PAD)
			mfc_cma_cmb_onoff(charger, true, true);
		mfc_set_vout(charger, MFC_VOUT_10V);
		/* reset AICL */
		psy_do_property("wireless", set, POWER_SUPPLY_PROP_CURRENT_MAX, value);
		break;
	case WIRELESS_VOUT_11V:
		if (charger->tx_id == TX_ID_P1100_PAD)
			mfc_cma_cmb_onoff(charger, true, true);
		mfc_set_vout(charger, MFC_VOUT_11V);
		/* reset AICL */
		psy_do_property("wireless", set, POWER_SUPPLY_PROP_CURRENT_MAX, value);
		break;
	case WIRELESS_VOUT_12V:
		if (charger->tx_id == TX_ID_P1100_PAD)
			mfc_cma_cmb_onoff(charger, true, true);
		mfc_set_vout(charger, MFC_VOUT_12V);
		/* reset AICL */
		psy_do_property("wireless", set, POWER_SUPPLY_PROP_CURRENT_MAX, value);
		break;
	case WIRELESS_VOUT_12_5V:
		if (charger->tx_id == TX_ID_P1100_PAD)
			mfc_cma_cmb_onoff(charger, true, true);
		mfc_set_vout(charger, MFC_VOUT_12_5V);
		/* reset AICL */
		psy_do_property("wireless", set, POWER_SUPPLY_PROP_CURRENT_MAX, value);
		break;
	case WIRELESS_VOUT_5V_STEP:
		vout_step--;
		if (vout_step >= MFC_VOUT_5V) {
			mfc_set_vout(charger, vout_step);
			cancel_delayed_work(&charger->wpc_vout_mode_work);
			queue_delayed_work(charger->wqueue, &charger->wpc_vout_mode_work, msecs_to_jiffies(250));
			return;
		}
		break;
	case WIRELESS_VOUT_5_5V_STEP:
		psy_do_property("battery", get,	POWER_SUPPLY_EXT_PROP_LCD_FLICKER, value);
		wpc_vout_ctrl_lcd_on = value.intval;
		vout_step--;
		if (vout_step < MFC_VOUT_5_5V) {
			if (wpc_vout_ctrl_lcd_on && opfreq_ctrl_pad(charger->tx_id)) {
				pr_info("%s: tx id = 0x%x , set op freq\n", __func__, charger->tx_id);
				mfc_send_command(charger, MFC_SET_OP_FREQ);
				msleep(500);
			}
			if (charger->tx_id == TX_ID_P1100_PAD)
				mfc_cma_cmb_onoff(charger, true, false);
			break;
		}

		if (wpc_vout_ctrl_lcd_on) {
			psy_do_property("battery", get,	POWER_SUPPLY_EXT_PROP_PAD_VOLT_CTRL, value);
			if (value.intval && charger->is_afc_tx) {
				if (vout_step == charger->flicker_vout_threshold) {
					mfc_set_vout(charger, vout_step);
					cancel_delayed_work(&charger->wpc_vout_mode_work);
					queue_delayed_work(charger->wqueue,
						&charger->wpc_vout_mode_work,
						msecs_to_jiffies(charger->flicker_delay));
					return;
				} else if (vout_step < charger->flicker_vout_threshold) {
					pr_info("%s: set TX 5V because LCD ON\n", __func__);
					mfc_set_pad_hv(charger, false);
					charger->pad_ctrl_by_lcd = true;
				}
			}
		}
		mfc_set_vout(charger, vout_step);
		cancel_delayed_work(&charger->wpc_vout_mode_work);
		queue_delayed_work(charger->wqueue,
			&charger->wpc_vout_mode_work, msecs_to_jiffies(250));
		return;
	case WIRELESS_VOUT_4_5V_STEP:
		vout_step--;
		if (vout_step >= MFC_VOUT_4_5V) {
			mfc_set_vout(charger, vout_step);
			cancel_delayed_work(&charger->wpc_vout_mode_work);
			queue_delayed_work(charger->wqueue,
				&charger->wpc_vout_mode_work, msecs_to_jiffies(250));
			return;
		}
		break;
	case WIRELESS_VOUT_9V_STEP:
		vout = MFC_VOUT_9V;
	case WIRELESS_VOUT_10V_STEP:
		vout_step++;
		if (vout_step <= vout) {
			mfc_set_vout(charger, vout_step);
			cancel_delayed_work(&charger->wpc_vout_mode_work);
			queue_delayed_work(charger->wqueue,
				&charger->wpc_vout_mode_work, msecs_to_jiffies(250));
			return;
		}
		break;
	case WIRELESS_VOUT_CC_CV_VOUT:
		mfc_set_vout(charger, MFC_VOUT_5_5V);
		if (charger->tx_id == TX_ID_P1100_PAD)
			mfc_cma_cmb_onoff(charger, true, false);
		break;
	case WIRELESS_VOUT_OTG:
		mfc_set_vout(charger, MFC_VOUT_OTG);
		break;
	default:
		break;
	}
#if !defined(CONFIG_SEC_FACTORY)
	if (charger->pdata->vout_status <= MFC_VOUT_5_5V &&
		(charger->is_full_status || charger->sleep_mode || (charger->tx_id == TX_ID_BATT_PACK)))
		mfc_set_pad_hv(charger, false);
#endif
	pr_info("%s: finish - vout_mode(%s), vout_status(%s)\n",
		__func__, sb_vout_ctr_mode_str(charger->vout_mode), sb_rx_vout_str(charger->pdata->vout_status));
	__pm_relax(charger->wpc_vout_mode_ws);
}

static void mfc_wpc_i2c_error_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, wpc_i2c_error_work.work);

	if (charger->det_state &&
		gpio_get_value(charger->pdata->wpc_det)) {
		union power_supply_propval value;

		psy_do_property("battery", set,
			POWER_SUPPLY_EXT_PROP_WC_CONTROL, value);
	}
}

static void mfc_set_tx_fod_common(struct mfc_charger_data *charger)
{
	return;

	pr_info("@Tx_Mode %s\n", __func__);

	/* Gain */
	mfc_reg_write(charger->client, MFC_TX_FOD_GAIN_REG, 0x72);
	/* Offset */
	mfc_reg_write(charger->client, MFC_TX_FOD_OFFSET_L_REG, 0x8C);
	mfc_reg_write(charger->client, MFC_TX_FOD_OFFSET_H_REG, 0x0);
}

static void mfc_set_tx_fod_thresh1(struct i2c_client *client, u32 fod_thresh1)
{
	u8 data[2] = {0,};

	return;

	/* Thresh1 */
	data[0] = fod_thresh1 & 0xff;
	data[1] = (fod_thresh1 & 0xff00) >> 8;
	pr_info("%s: fod_thresh1(0x%x, 0x%x)\n", __func__, data[1], data[0]);
	mfc_reg_write(client, MFC_TX_FOD_THRESH1_L_REG, data[0]);
	mfc_reg_write(client, MFC_TX_FOD_THRESH1_H_REG, data[1]);
}

static void mfc_set_tx_fod_ta_thresh(struct i2c_client *client, u32 fod_thresh)
{
	u8 data[2] = {0,};

	/* TA Thresh */
	data[0] = fod_thresh & 0xff;
	data[1] = (fod_thresh & 0xff00) >> 8;
	pr_info("%s: fod_thresh1(0x%x, 0x%x)\n", __func__, data[1], data[0]);
	mfc_reg_write(client, MFC_TX_FOD_TA_THRESH_L_REG, data[0]);
	mfc_reg_write(client, MFC_TX_FOD_TA_THRESH_H_REG, data[1]);
}

static void mfc_wpc_rx_type_det_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, wpc_rx_type_det_work.work);
	u8 reg_data, prmc_id;
	union power_supply_propval value;

	if (!charger->wc_tx_enable) {
		__pm_relax(charger->wpc_rx_det_ws);
		return;
	}

	mfc_reg_read(charger->client, MFC_STARTUP_EPT_COUNTER, &reg_data);
	mfc_reg_read(charger->client, MFC_TX_RXID1_READ_REG, &prmc_id);

	pr_info("@Tx_Mode %s: prmc_id 0x%x\n", __func__, prmc_id);

	mfc_set_tx_fod_common(charger);
	if (prmc_id == 0x42 && reg_data >= 1) {
		pr_info("@Tx_Mode %s: Samsung Gear Connected\n", __func__);
		charger->wc_rx_type = SS_GEAR;
	} else if (prmc_id == 0x42) {
		pr_info("@Tx_Mode %s: Samsung Phone Connected\n", __func__);
		charger->wc_rx_type = SS_PHONE;
		mfc_set_coil_sw_en(charger, 0);
		mfc_set_tx_fod_thresh1(charger->client, charger->pdata->phone_fod_thresh1);
	} else {
		pr_info("@Tx_Mode %s: Unknown device connected\n", __func__);
		charger->wc_rx_type = OTHER_DEV;
	}
	value.intval = charger->wc_rx_type;
	psy_do_property("wireless", set, POWER_SUPPLY_EXT_PROP_WIRELESS_RX_TYPE, value);

	__pm_relax(charger->wpc_rx_det_ws);
}

static void mfc_cs100_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, wpc_cs100_work.work);

	pr_info("%s: full_status(%d) , tx_id(0x%x)\n",
		__func__, charger->is_full_status, charger->tx_id);
	/* set fake FOD values before send cs100, need to tune */
	if (is_wireless_type(charger->pdata->cable_type))
		mfc_fod_set(charger, FOD_STATE_FULL);

	charger->pdata->cs100_status = mfc_send_cs100(charger);

#if !defined(CONFIG_SEC_FACTORY)
	if (!charger->is_full_status) {
		charger->is_full_status = 1;
		/* 1st chg done ~ 2nd chg done : CMA+CMB (samsung pad only) */
		if (charger->tx_id)
			mfc_cma_cmb_onoff(charger, true, true);

		if (is_hv_wireless_type(charger->pdata->cable_type) &&
			volt_ctrl_pad(charger->tx_id)) {
			charger->vout_mode = WIRELESS_VOUT_5_5V_STEP;
			cancel_delayed_work(&charger->wpc_vout_mode_work);
			__pm_stay_awake(charger->wpc_vout_mode_ws);
			queue_delayed_work(charger->wqueue, &charger->wpc_vout_mode_work, msecs_to_jiffies(250));
		}
	}
#endif
	__pm_relax(charger->wpc_cs100_ws);
}

static void mfc_check_rx_power_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, wpc_check_rx_power_work.work);

	pr_info("%s %d\n", __func__, charger->check_rx_power);
	if (charger->check_rx_power) {
		pr_info("%s: set 7.5W\n", __func__);
		mfc_reset_rx_power(charger, TX_RX_POWER_7_5W);
		charger->current_rx_power = TX_RX_POWER_7_5W;
	}
	__pm_relax(charger->wpc_check_rx_power_ws);
}

static void mfc_tx_phm_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, wpc_tx_phm_work.work);

	pr_info("@Tx_Mode %s\n", __func__);
	mfc_set_cmd_l_reg(charger, MFC_CMD_TOGGLE_PHM_MASK, MFC_CMD_TOGGLE_PHM_MASK);

	if (charger->tx_device_phm) {
		union power_supply_propval value = {0, };

		charger->tx_device_phm = 0;
		value.intval = 0;
		psy_do_property("wireless", set,
			POWER_SUPPLY_EXT_PROP_GEAR_PHM_EVENT, value);
	}
	charger->skip_phm_work_in_sleep = false;
	__pm_relax(charger->wpc_tx_phm_ws);
}

static void mfc_wpc_init_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, wpc_init_work.work);

	union power_supply_propval value = {0, };

	pr_info("%s\n", __func__);
	if (charger->pdata->cable_type != SEC_BATTERY_CABLE_NONE) {
		value.intval = charger->pdata->cable_type;
		psy_do_property("wireless", set, POWER_SUPPLY_PROP_ONLINE, value);
		pr_info("%s: Reset M0\n", __func__);
		/* reset MCU of MFC IC */
		mfc_set_cmd_l_reg(charger, MFC_CMD_MCU_RESET_MASK, MFC_CMD_MCU_RESET_MASK);
	}
	if (charger->is_otg_on) {
		psy_do_property("wireless", set,
			POWER_SUPPLY_EXT_PROP_CHARGE_OTG_CONTROL, value);
	}
}

static void mfc_set_afc_vout_control(struct mfc_charger_data *charger, int vout_mode)
{
	if (!charger->is_full_status && !is_shutdn && !charger->pad_ctrl_by_lcd) {
		if (!charger->is_afc_tx) {
			pr_info("%s: need to set afc tx before vout control\n", __func__);
			mfc_set_pad_hv(charger, true);
			pr_info("%s: is_afc_tx = %d vout read = %d\n", __func__,
				charger->is_afc_tx, mfc_get_adc(charger, MFC_ADC_VOUT));
		}
		charger->vout_mode = vout_mode;
		cancel_delayed_work(&charger->wpc_vout_mode_work);
		__pm_stay_awake(charger->wpc_vout_mode_ws);
		queue_delayed_work(charger->wqueue,
			&charger->wpc_vout_mode_work, msecs_to_jiffies(250));
	} else {
		pr_info("%s: block to set high vout level(vs=%s) because full status(%d), shutdn(%d)\n",
			__func__, sb_rx_vout_str(charger->pdata->vout_status),
			charger->is_full_status, is_shutdn);
	}
}

static void mfc_recover_vout(struct mfc_charger_data *charger)
{
	int ct = charger->pdata->cable_type;

	pr_info("%s: cable_type =%d\n", __func__, ct);

	if (is_hv_wireless_type(ct) && ct != SEC_BATTERY_CABLE_HV_WIRELESS_20)
		mfc_set_vout(charger, MFC_VOUT_10V);
}

#if defined(CONFIG_UPDATE_BATTERY_DATA)
static int mfc_chg_parse_dt(struct device *dev, mfc_charger_platform_data_t *pdata);
#endif
static int cps4038_chg_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct mfc_charger_data *charger = power_supply_get_drvdata(psy);
	enum power_supply_ext_property ext_psp = (enum power_supply_ext_property) psp;
	int i = 0;
	u8 tmp = 0;

	switch ((int)psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (val->intval == POWER_SUPPLY_STATUS_FULL) {
			pr_info("%s: full status\n", __func__);
			__pm_stay_awake(charger->wpc_cs100_ws);
			queue_delayed_work(charger->wqueue, &charger->wpc_cs100_work, msecs_to_jiffies(0));
		} else if (val->intval == POWER_SUPPLY_STATUS_NOT_CHARGING) {
			mfc_mis_align(charger);
		} else if (val->intval == POWER_SUPPLY_STATUS_CHARGING ||
			val->intval == POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE) {
			if (val->intval == POWER_SUPPLY_STATUS_CHARGING) {
				charger->is_full_status = 0;
				mfc_set_pad_hv(charger, true);
				mfc_recover_vout_by_pad(charger);
			}
			pr_info("%s: CV status. tx_id(0x%x)\n", __func__, charger->tx_id);

			if (charger->tx_id) {
				if ((charger->tx_id == TX_ID_DAVINCI_PAD_V) ||
					(charger->tx_id == TX_ID_DAVINCI_PAD_H)) {
					mfc_cma_cmb_onoff(charger, true, false);
				} else {
					mfc_cma_cmb_onoff(charger, true, true);
				}
			}
		}
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		queue_delayed_work(charger->wqueue, &charger->wpc_init_work, 0);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		if (val->intval == POWER_SUPPLY_HEALTH_OVERHEAT ||
			val->intval == POWER_SUPPLY_EXT_HEALTH_OVERHEATLIMIT ||
			val->intval == POWER_SUPPLY_HEALTH_COLD)
			mfc_send_eop(charger, val->intval);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		pr_info("%s: ept-internal fault\n", __func__);
		mfc_reg_write(charger->client, MFC_EPT_REG, MFC_WPC_EPT_INT_FAULT);
		mfc_set_cmd_l_reg(charger, MFC_CMD_SEND_EOP_MASK, MFC_CMD_SEND_EOP_MASK);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		if (val->intval) {
			charger->is_mst_on = MST_MODE_2;
			pr_info("%s: set MST mode 2\n", __func__);
		} else {
#if defined(CONFIG_MST_V2)
			// it will send MST driver a message.
			mfc_send_mst_cmd(MST_MODE_OFF, charger, 0, 0);
#endif
			pr_info("%s: set MST mode off\n", __func__);
			charger->is_mst_on = MST_MODE_0;
		}
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		charger->pdata->otp_firmware_result = val->intval;
		pr_info("%s: otp_firmware result initialize (%d)\n", __func__,
			charger->pdata->otp_firmware_result);
		break;
	case POWER_SUPPLY_PROP_ENERGY_NOW:
		charger->pdata->capacity = val->intval;
		if (charger->wc_tx_enable) {
			pr_info("@Tx_Mode %s: FW Ver(%x) TX_VOUT(%dmV) TX_IOUT(%dmA), PHM(%d), %s connected\n", __func__,
				charger->pdata->otp_firmware_ver,
				mfc_get_adc(charger, MFC_ADC_TX_VOUT),
				mfc_get_adc(charger, MFC_ADC_TX_IOUT),
				charger->tx_device_phm,
				sb_rx_type_str(charger->wc_rx_type));
			pr_info("@Tx_Mode %s: PING_FRQ(%dKHz) OP_FRQ(%dKHz) TX_MIN_FRQ(%dKHz) TX_MAX_FRQ(%dKHz)\n",
				__func__,
				mfc_get_adc(charger, MFC_ADC_PING_FRQ),
				mfc_get_adc(charger, MFC_ADC_OP_FRQ),
				mfc_get_adc(charger, MFC_ADC_TX_MIN_OP_FRQ),
				mfc_get_adc(charger, MFC_ADC_TX_MAX_OP_FRQ));
		} else if (charger->pdata->cable_type != SEC_BATTERY_CABLE_NONE) {
			u8 fod[MFC_NUM_FOD_REG] = {0, };

			pr_info("%s: FW Ver(%x) RX_VOUT(%dmV) RX_VRECT(%dmV) RX_IOUT(%dmA)\n", __func__,
				charger->pdata->otp_firmware_ver,
				mfc_get_adc(charger, MFC_ADC_VOUT),
				mfc_get_adc(charger, MFC_ADC_VRECT),
				mfc_get_adc(charger, MFC_ADC_RX_IOUT));
			pr_info("%s: OP_FRQ(%dKHz) TX ID(0x%x) IC Rev(0x%x)\n", __func__,
				mfc_get_adc(charger, MFC_ADC_OP_FRQ),
				charger->tx_id,
				charger->pdata->wc_ic_rev);

			for (i = 0; i < MFC_NUM_FOD_REG; i++)
				if (mfc_reg_read(charger->client, MFC_WPC_FOD_0A_REG+i, &fod[i]) < 0)
					pr_info("%s: failed to read FOD(%d) data\n", __func__, i);
			pr_info("%s: FOD0(0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X)\n",
				__func__, fod[0], fod[1], fod[2], fod[3], fod[4],
				fod[5], fod[6], fod[7], fod[8], fod[9]);
			pr_info("%s: FOD1(0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X)\n",
				__func__, fod[10], fod[11], fod[12], fod[13], fod[14],
				fod[15], fod[16], fod[17], fod[18], fod[19]);
		}
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		charger->pdata->capacity = val->intval;
		break;
	case POWER_SUPPLY_PROP_CHARGE_EMPTY:
	{
		int vout = 0, vrect = 0;
		u8 is_vout_on = 0;
		bool error = false;

		if (mfc_reg_read(charger->client, MFC_STATUS_L_REG, &is_vout_on) < 0)
			error = true;
		is_vout_on = is_vout_on >> 7;
		vout = mfc_get_adc(charger, MFC_ADC_VOUT);
		vrect = mfc_get_adc(charger, MFC_ADC_VRECT);
		pr_info("%s: SET MFC LDO (%s), Current VOUT STAT (%d), RX_VOUT = %dmV, RX_VRECT = %dmV, error(%d)\n",
				__func__, (val->intval == MFC_LDO_ON ? "ON" : "OFF"), is_vout_on, vout, vrect, error);
		if ((val->intval == MFC_LDO_ON) && (!is_vout_on || error)) { /* LDO ON */
			pr_info("%s: MFC LDO ON toggle ------------ cable_work\n", __func__);
			for (i = 0; i < 2; i++) {
				mfc_reg_read(charger->client, MFC_STATUS_L_REG, &is_vout_on);
				is_vout_on = is_vout_on >> 7;
				if (!is_vout_on)
					mfc_set_cmd_l_reg(charger, MFC_CMD_TOGGLE_LDO_MASK, MFC_CMD_TOGGLE_LDO_MASK);
				else
					break;
				msleep(500);
				mfc_reg_read(charger->client, MFC_STATUS_L_REG, &is_vout_on);
				is_vout_on = is_vout_on >> 7;
				if (is_vout_on) {
					pr_info("%s: cnt = %d, LDO is ON -> MFC LDO STAT(%d)\n",
						__func__, i, is_vout_on);
					break;
				}
				msleep(1000);
				vout = mfc_get_adc(charger, MFC_ADC_VOUT);
				vrect = mfc_get_adc(charger, MFC_ADC_VRECT);
				pr_info("%s: cnt = %d, LDO Should ON -> MFC LDO STAT(%d), RX_VOUT = %dmV, RX_VRECT = %dmV\n",
						__func__, i, is_vout_on, vout, vrect);
			}
			charger->wc_ldo_status = MFC_LDO_ON;
			mfc_recover_vout(charger);
		} else if ((val->intval == MFC_LDO_OFF) && (is_vout_on || error)) { /* LDO OFF */
			mfc_set_vout(charger, MFC_VOUT_5V);
			msleep(300);
			pr_info("%s: MFC LDO OFF toggle ------------ cable_work\n", __func__);
			mfc_set_cmd_l_reg(charger, MFC_CMD_TOGGLE_LDO_MASK, MFC_CMD_TOGGLE_LDO_MASK);
			msleep(400);
			mfc_reg_read(charger->client, MFC_STATUS_L_REG, &is_vout_on);
			is_vout_on = is_vout_on >> 7;
			vout = mfc_get_adc(charger, MFC_ADC_VOUT);
			vrect = mfc_get_adc(charger, MFC_ADC_VRECT);
			pr_info("%s: LDO Should OFF -> MFC LDO STAT(%d), RX_VOUT = %dmV, RX_VRECT = %dmV\n",
					__func__, is_vout_on, vout, vrect);
			charger->wc_ldo_status = MFC_LDO_OFF;
		}
	}
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		charger->input_current = val->intval;
		pr_info("%s: input_current: %d\n", __func__, charger->input_current);
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		return -ENODATA;
	case POWER_SUPPLY_EXT_PROP_MIN ... POWER_SUPPLY_EXT_PROP_MAX:
		switch (ext_psp) {
		case POWER_SUPPLY_EXT_PROP_WC_CONTROL:
			if (val->intval == 0) {
				tmp = 0x01;
				mfc_send_packet(charger, MFC_HEADER_AFC_CONF,
					0x20, &tmp, 1);
				pr_info("%s: send command after wc control\n", __func__);
				msleep(150);
			}
			break;
		case POWER_SUPPLY_EXT_PROP_WC_EPT_UNKNOWN:
			if (val->intval == 1)
				mfc_send_ept_unknown(charger);
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_SWITCH:
			/*
			 * It is a RX device , send a packet to TX device to stop power sharing.
			 * TX device will have MFC_INTA_H_TRX_DATA_RECEIVED_MASK irq
			 */
			if (charger->pdata->cable_type == SEC_BATTERY_CABLE_WIRELESS_TX) {
				if (val->intval) {
					pr_info("%s: It is a RX device , send a packet to TX device to stop power sharing\n",
							__func__);
					mfc_send_command(charger, MFC_DISABLE_TX);
				}
			}
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_SEND_FSK:
			/* send fsk packet for rx device aicl reset */
			if (val->intval && (charger->wc_rx_type != SS_GEAR)) {
				pr_info("@Tx_mode %s: Send FSK packet for Rx device aicl reset\n", __func__);
				mfc_send_fsk(charger, WPC_TX_COM_WPS, WPS_AICL_RESET);
			}
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_TX_ENABLE:
			/* on/off tx power */
			mfc_set_tx_power(charger, val->intval);
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_RX_CONNECTED:
			charger->wc_rx_connected = val->intval;
			queue_delayed_work(charger->wqueue, &charger->wpc_rx_connection_work, 0);
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_AUTH_ADT_STATUS: /* it has only PASS and FAIL */
#if !defined(CONFIG_SEC_FACTORY)
			if ((charger->pdata->cable_type == SEC_BATTERY_CABLE_NONE) ||
				(charger->adt_transfer_status == WIRELESS_AUTH_WAIT)) {
				pr_info("%s %s: auth service sent wrong cmd(%d)\n",
					WC_AUTH_MSG, __func__, val->intval);
				break;
			}

			if (charger->adt_transfer_status != val->intval) {
				charger->adt_transfer_status = val->intval;

				if (charger->adt_transfer_status == WIRELESS_AUTH_PASS) {
					/* this cable type will call a proper RX power having done vout_work */
					charger->pdata->cable_type = SEC_BATTERY_CABLE_HV_WIRELESS_20;
					__pm_stay_awake(charger->wpc_afc_vout_ws);
					queue_delayed_work(charger->wqueue,
						&charger->wpc_afc_vout_work, msecs_to_jiffies(0));
					pr_info("%s %s: PASS! type = %d\n", WC_AUTH_MSG,
							__func__, charger->pdata->cable_type);
				} else if (charger->adt_transfer_status == WIRELESS_AUTH_FAIL) {
					if (charger->afc_tx_done) {
						charger->pdata->cable_type = SEC_BATTERY_CABLE_HV_WIRELESS;
						__pm_stay_awake(charger->wpc_afc_vout_ws);
						queue_delayed_work(charger->wqueue,
							&charger->wpc_afc_vout_work, msecs_to_jiffies(0));
					} else {
						union power_supply_propval value = {0, };

						charger->pdata->cable_type = SEC_BATTERY_CABLE_WIRELESS;
						value.intval = charger->pdata->cable_type;
						psy_do_property("wireless", set,
							POWER_SUPPLY_PROP_ONLINE, value);
					}
					pr_info("%s %s: FAIL\n", WC_AUTH_MSG, __func__);
				} else {
					pr_info("%s %s: undefined PASS/FAIL result\n", WC_AUTH_MSG, __func__);
				}
				mfc_auth_set_configs(charger, AUTH_COMPLETE);
			} else {
				pr_info("%s %s: skip a same PASS/FAIL result\n", WC_AUTH_MSG, __func__);
			}
#endif
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_AUTH_ADT_DATA: /* data from auth service will be sent */
			if (charger->pdata->cable_type != SEC_BATTERY_CABLE_NONE) {
				u8 *p_data;

				p_data = (u8 *)val->strval;

				for (i = 0; i < adt_readSize; i++)
					pr_info("%s %s: p_data[%d] = %x\n", WC_AUTH_MSG, __func__, i, p_data[i]);
				mfc_auth_adt_send(charger, p_data, adt_readSize);
			}
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_AUTH_ADT_SIZE:
			if (charger->pdata->cable_type != SEC_BATTERY_CABLE_NONE)
				adt_readSize = val->intval;
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_RX_TYPE:
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_TX_VOUT:
			mfc_set_tx_vout(charger, val->intval);
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_TX_IOUT:
			mfc_set_tx_iout(charger, val->intval);
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_TIMER_ON:
			pr_info("%s %s: TX receiver detecting timer enable(%d)\n", WC_AUTH_MSG, __func__, val->intval);
			if (charger->wc_tx_enable) {
				if (val->intval) {
					pr_info("%s %s: enable TX OFF timer (90sec)", WC_AUTH_MSG, __func__);
					mfc_reg_update(charger->client, MFC_INT_A_ENABLE_H_REG, (0x1 << 5), (0x1 << 5));
				} else {
					pr_info("%s %s: disable TX OFF timer (90sec)", WC_AUTH_MSG, __func__);
					mfc_reg_update(charger->client, MFC_INT_A_ENABLE_H_REG, 0x0, (0x1 << 5));
				}
			} else {
				pr_info("%s %s: Don't need to set TX 90sec timer, on TX OFF state\n",
					WC_AUTH_MSG, __func__);
			}
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_MIN_DUTY:
			charger->duty_min = val->intval;
			break;
		case POWER_SUPPLY_EXT_PROP_CALL_EVENT:
			if (val->intval & BATT_EXT_EVENT_CALL) {
				charger->device_event |= BATT_EXT_EVENT_CALL;

				/* call in is after wireless connection */
				if (charger->pdata->cable_type == SEC_BATTERY_CABLE_WIRELESS_PACK ||
					charger->pdata->cable_type == SEC_BATTERY_CABLE_WIRELESS_HV_PACK ||
					charger->pdata->cable_type == SEC_BATTERY_CABLE_WIRELESS_TX) {
					union power_supply_propval value2;

					pr_info("%s %s: enter PHM\n", WC_TX_MSG, __func__);
					/* notify "wireless" PHM status */
					value2.intval = 1;
					psy_do_property("wireless", set, POWER_SUPPLY_EXT_PROP_CALL_EVENT, value2);
					mfc_send_command(charger, MFC_PHM_ON);
					msleep(250);
					mfc_send_command(charger, MFC_PHM_ON);
				}
			} else if (val->intval == BATT_EXT_EVENT_NONE) {
				charger->device_event &= ~BATT_EXT_EVENT_CALL;
			}
			break;
		case POWER_SUPPLY_EXT_PROP_SLEEP_MODE:
			charger->sleep_mode = val->intval;
			break;
		case POWER_SUPPLY_EXT_PROP_PAD_VOLT_CTRL:
			if (delayed_work_pending(&charger->wpc_vout_mode_work)) {
				pr_info("%s: Already vout change. skip pad control\n", __func__);
				return 0;
			}

			if (!volt_ctrl_pad(charger->tx_id))
				break;

			if (val->intval && charger->is_afc_tx) {
				pr_info("%s: set TX 5V because LCD ON\n", __func__);
				mfc_set_pad_hv(charger, false);
				charger->pad_ctrl_by_lcd = true;
			} else if (!val->intval && !charger->is_afc_tx && charger->pad_ctrl_by_lcd) {
				mfc_set_pad_hv(charger, true);
				charger->pad_ctrl_by_lcd = false;
				pr_info("%s: need to set afc tx because LCD OFF\n", __func__);
			}
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_VOUT:
			for (i = 0; i < charger->pdata->len_wc20_list; i++)
				charger->pdata->wireless20_vout_list[i] = val->intval;

			pr_info("%s: vout(%d) len(%d)\n", __func__, val->intval, i - 1);
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_2ND_DONE:
			mfc_cma_cmb_onoff(charger, true, false);
			pr_info("%s: 2nd wireless charging done! CMA only\n", __func__);
			mfc_set_vout_ctrl_full(charger);
			break;
		case POWER_SUPPLY_EXT_PROP_WPC_EN:
			mfc_set_wpc_en(charger, val->strval[0], val->strval[1]);
			break;
		case POWER_SUPPLY_EXT_PROP_WPC_EN_MST:
			if (val->intval)
				mfc_set_wpc_en(charger, WPC_EN_MST, true);
			else
				mfc_set_wpc_en(charger, WPC_EN_MST, false);
			break;
		case POWER_SUPPLY_EXT_PROP_CHARGE_OTG_CONTROL:
			if (val->intval) {
				pr_info("%s: mfc otg on\n", __func__);
				charger->is_otg_on = true;
			} else {
				pr_info("%s: mfc otg off\n", __func__);
				charger->is_otg_on = false;
			}
			break;
		case POWER_SUPPLY_EXT_PROP_CHARGE_POWERED_OTG_CONTROL:
			{
				unsigned int work_state;

				mutex_lock(&charger->fw_lock);
				/* check delayed work state */
				work_state = work_busy(&charger->wpc_fw_update_work.work);
				pr_info("%s: check fw_work state(0x%x)\n", __func__, work_state);
				if (work_state & (WORK_BUSY_PENDING | WORK_BUSY_RUNNING)) {
					pr_info("%s: skip update_fw!!\n", __func__);
				} else {
					charger->fw_cmd = val->intval;
					queue_delayed_work(charger->wqueue, &charger->wpc_fw_update_work, 0);
					pr_info("%s: rx result = %d, tx result = %d\n", __func__,
						charger->pdata->otp_firmware_result,
						charger->pdata->tx_firmware_result);
				}
				mutex_unlock(&charger->fw_lock);
			}
			break;
		case POWER_SUPPLY_EXT_PROP_INPUT_VOLTAGE_REGULATION:
			if (val->intval == WIRELESS_VOUT_NORMAL_VOLTAGE) {
				pr_info("%s: Wireless Vout forced set to 5V. + PAD CMD\n", __func__);
				for (i = 0; i < CMD_CNT; i++) {
					mfc_send_command(charger, MFC_AFC_CONF_5V);
					msleep(250);
				}
			} else if (val->intval == WIRELESS_VOUT_HIGH_VOLTAGE) {
				pr_info("%s: Wireless Vout forced set to 10V. + PAD CMD\n", __func__);
				for (i = 0; i < CMD_CNT; i++) {
					mfc_send_command(charger, MFC_AFC_CONF_10V);
					msleep(250);
				}
			} else if (val->intval == WIRELESS_VOUT_CC_CV_VOUT) {
				if ((charger->tx_id != TX_ID_DAVINCI_PAD_V) &&
					(charger->tx_id != TX_ID_DAVINCI_PAD_H))
					mfc_cma_cmb_onoff(charger, true, true);
				charger->vout_mode = val->intval;
				cancel_delayed_work(&charger->wpc_vout_mode_work);
				__pm_stay_awake(charger->wpc_vout_mode_ws);
				queue_delayed_work(charger->wqueue, &charger->wpc_vout_mode_work, 0);
			} else if (val->intval == WIRELESS_VOUT_5V ||
					val->intval == WIRELESS_VOUT_5V_STEP ||
					val->intval == WIRELESS_VOUT_5_5V_STEP ||
					val->intval == WIRELESS_VOUT_OTG) {
				int def_delay = 0;

				charger->vout_mode = val->intval;
				if (is_hv_wireless_type(charger->pdata->cable_type) &&
					val->intval != WIRELESS_VOUT_OTG)
					def_delay = 250;
				cancel_delayed_work(&charger->wpc_vout_mode_work);
				__pm_stay_awake(charger->wpc_vout_mode_ws);
				queue_delayed_work(charger->wqueue,
					&charger->wpc_vout_mode_work, msecs_to_jiffies(def_delay));
			} else if (val->intval == WIRELESS_VOUT_9V ||
				val->intval == WIRELESS_VOUT_10V ||
				val->intval == WIRELESS_VOUT_11V ||
				val->intval == WIRELESS_VOUT_12V ||
				val->intval == WIRELESS_VOUT_12_5V ||
				val->intval == WIRELESS_VOUT_9V_STEP ||
				val->intval == WIRELESS_VOUT_10V_STEP) {
				pr_info("%s: vout (%d)\n", __func__, val->intval);
				mfc_set_afc_vout_control(charger, val->intval);
			} else if (val->intval == WIRELESS_VOUT_FORCE_9V) {
				mfc_set_vout(charger, MFC_VOUT_9V);
			} else {
				pr_info("%s: Unknown Command(%d)\n", __func__, val->intval);
			}
			break;
		case POWER_SUPPLY_EXT_PROP_WIRELESS_RX_CONTROL:
			if (val->intval == WIRELESS_PAD_FAN_OFF) {
				pr_info("%s: fan off\n", __func__);
				mfc_fan_control(charger, 0);
			} else if (val->intval == WIRELESS_PAD_FAN_ON) {
				pr_info("%s: fan on\n", __func__);
				mfc_fan_control(charger, 1);
			} else if (val->intval == WIRELESS_PAD_LED_OFF) {
				pr_info("%s: led off\n", __func__);
				mfc_led_control(charger, MFC_LED_CONTROL_OFF);
			} else if (val->intval == WIRELESS_PAD_LED_ON) {
				pr_info("%s: led on\n", __func__);
				mfc_led_control(charger, MFC_LED_CONTROL_ON);
			} else if (val->intval == WIRELESS_PAD_LED_DIMMING) {
				pr_info("%s: led dimming\n", __func__);
				mfc_led_control(charger, MFC_LED_CONTROL_DIMMING);
			} else if (val->intval == WIRELESS_VRECT_ADJ_ON) {
				pr_info("%s: vrect adjust to have big headroom(default value)\n", __func__);
				mfc_set_vrect_adjust(charger, MFC_HEADROOM_1);
			} else if (val->intval == WIRELESS_VRECT_ADJ_OFF) {
				pr_info("%s: vrect adjust to have small headroom\n", __func__);
				mfc_set_vrect_adjust(charger, MFC_HEADROOM_0);
			} else if (val->intval == WIRELESS_VRECT_ADJ_ROOM_0) {
				pr_info("%s: vrect adjust to have headroom 0(0mV)\n", __func__);
				mfc_set_vrect_adjust(charger, MFC_HEADROOM_0);
			} else if (val->intval == WIRELESS_VRECT_ADJ_ROOM_1) {
				pr_info("%s: vrect adjust to have headroom 1(277mV)\n", __func__);
				mfc_set_vrect_adjust(charger, MFC_HEADROOM_1);
			} else if (val->intval == WIRELESS_VRECT_ADJ_ROOM_2) {
				pr_info("%s: vrect adjust to have headroom 2(497mV)\n", __func__);
				mfc_set_vrect_adjust(charger, MFC_HEADROOM_2);
			} else if (val->intval == WIRELESS_VRECT_ADJ_ROOM_3) {
				pr_info("%s: vrect adjust to have headroom 3(650mV)\n", __func__);
				mfc_set_vrect_adjust(charger, MFC_HEADROOM_3);
			} else if (val->intval == WIRELESS_VRECT_ADJ_ROOM_4) {
				pr_info("%s: vrect adjust to have headroom 4(30mV)\n", __func__);
				mfc_set_vrect_adjust(charger, MFC_HEADROOM_4);
			} else if (val->intval == WIRELESS_VRECT_ADJ_ROOM_5) {
				pr_info("%s: vrect adjust to have headroom 5(82mV)\n", __func__);
				mfc_set_vrect_adjust(charger, MFC_HEADROOM_5);
			} else if (val->intval == WIRELESS_CLAMP_ENABLE) {
				pr_info("%s: enable clamp1, clamp2 for WPC modulation\n", __func__);
			} else if (val->intval == WIRELESS_SLEEP_MODE_ENABLE) {
				if (is_sleep_mode_active(charger->tx_id)) {
					pr_info("%s: sleep_mode enable\n", __func__);
					msleep(500);
					pr_info("%s: led dimming\n", __func__);
					mfc_led_control(charger, MFC_LED_CONTROL_DIMMING);
					msleep(500);
					pr_info("%s: fan off\n", __func__);
					mfc_fan_control(charger, 0);
				} else {
					pr_info("%s: sleep_mode inactive\n", __func__);
				}
			} else if (val->intval == WIRELESS_SLEEP_MODE_DISABLE) {
				if (is_sleep_mode_active(charger->tx_id)) {
					pr_info("%s: sleep_mode disable\n", __func__);
					msleep(500);
					pr_info("%s: led on\n", __func__);
					mfc_led_control(charger, MFC_LED_CONTROL_ON);
					msleep(500);
					pr_info("%s: fan on\n", __func__);
					mfc_fan_control(charger, 1);
				} else {
					pr_info("%s: sleep_mode inactive\n", __func__);
				}
			} else {
				pr_info("%s: Unknown Command(%d)\n", __func__, val->intval);
			}
			break;
#if defined(CONFIG_UPDATE_BATTERY_DATA)
		case POWER_SUPPLY_EXT_PROP_POWER_DESIGN:
			mfc_chg_parse_dt(charger->dev, charger->pdata);
			break;
#endif
		case POWER_SUPPLY_EXT_PROP_FILTER_CFG:
			charger->led_cover = val->intval;
			pr_info("%s: LED_COVER(%d)\n", __func__, charger->led_cover);
			break;
		case POWER_SUPPLY_EXT_PROP_TX_PWR_BUDG:
			break;
		default:
			return -ENODATA;
		}
		break;
	default:
		return -ENODATA;
	}

	return 0;
}

static u32 mfc_get_wireless20_vout_by_txid(struct mfc_charger_data *charger, u8 txid)
{
	u32 vout = 0, i = 0;

	if (txid < TX_ID_AUTH_PAD || txid > TX_ID_AUTH_PAD_END) {
		pr_info("%s: wrong tx id(0x%x) for wireless 2.0\n", __func__, txid);
		i = 0; /* defalut value is WIRELESS_VOUT_10V */
	} else if (txid >= TX_ID_AUTH_PAD + charger->pdata->len_wc20_list) {
		pr_info("%s: undefined tx id(0x%x) of the device\n", __func__, txid);
		i = charger->pdata->len_wc20_list - 1;
	} else
		i = txid - TX_ID_AUTH_PAD;

	vout = charger->pdata->wireless20_vout_list[i];
	pr_info("%s: vout = %d\n", __func__, vout);
	return vout;
}

static u32 mfc_get_wireless20_vrect_by_txid(struct mfc_charger_data *charger, u8 txid)
{
	u32 vrect = 0, i = 0;

	if (txid < TX_ID_AUTH_PAD || txid > TX_ID_AUTH_PAD_END) {
		pr_info("%s: wrong tx id(0x%x) for wireless 2.0\n", __func__, txid);
		i = 0; /* defalut value is MFC_AFC_CONF_10V_TX */
	} else if (txid >= TX_ID_AUTH_PAD + charger->pdata->len_wc20_list) {
		pr_info("%s: undefined tx id(0x%x) of the device\n", __func__, txid);
		i = charger->pdata->len_wc20_list - 1;
	} else {
		i = txid - TX_ID_AUTH_PAD;
	}

	vrect = charger->pdata->wireless20_vrect_list[i];
	pr_info("%s: vrect = %d\n", __func__, vrect);
	return vrect;
}

static u32 mfc_get_wireless20_max_power_by_txid(struct mfc_charger_data *charger, u8 txid)
{
	u32 max_power = 0, i = 0;

	if (txid < TX_ID_AUTH_PAD || txid > TX_ID_AUTH_PAD_END) {
		pr_info("%s: wrong tx id(0x%x) for wireless 2.0\n", __func__, txid);
		i = 0; /* defalut value is SEC_WIRELESS_RX_POWER_12W */
	} else if (txid >= TX_ID_AUTH_PAD + charger->pdata->len_wc20_list) {
		pr_info("%s: undefined tx id(0x%x) of the device\n", __func__, txid);
		i = charger->pdata->len_wc20_list - 1;
	} else {
		i = txid - TX_ID_AUTH_PAD;
	}

	max_power = charger->pdata->wireless20_max_power_list[i];
	pr_info("%s: max rx power = %d\n", __func__, max_power);
	return max_power;
}

static void mfc_recv_tx_pwr_budg(struct mfc_charger_data *charger, u8 data)
{
	switch (data) {
	case MFC_TX_PWR_BUDG_2W:
		pr_info("%s: TX POWER BUDGET 2W\n", __func__);
		charger->tx_pwr_budg = MFC_TX_PWR_BUDG_2W;
		break;
	case MFC_TX_PWR_BUDG_5W:
		pr_info("%s: TX POWER BUDGET 5W\n", __func__);
		charger->tx_pwr_budg = MFC_TX_PWR_BUDG_5W;
		break;
	case MFC_TX_PWR_BUDG_7_5W:
		pr_info("%s: TX POWER BUDGET 7.5W\n", __func__);
		charger->tx_pwr_budg = MFC_TX_PWR_BUDG_7_5W;
		break;
	case MFC_TX_PWR_BUDG_12W:
		pr_info("%s: TX POWER BUDGET 12W\n", __func__);
		charger->tx_pwr_budg = MFC_TX_PWR_BUDG_12W;
		break;
	case MFC_TX_PWR_BUDG_15W:
		pr_info("%s: TX POWER BUDGET 15W\n", __func__);
		charger->tx_pwr_budg = MFC_TX_PWR_BUDG_15W;
		break;
	default:
		pr_info("%s: NOT DEFINED TX POWER BUDGET(%d)\n", __func__, data);
		charger->tx_pwr_budg = MFC_TX_PWR_BUDG_NONE;
		break;
	}
}

static void mfc_wpc_det_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, wpc_det_work.work);
	union power_supply_propval value;
	u8 pad_mode;
	u8 vrect;
	int vrect_level, vout_level, capacity;

	pr_info("%s : start\n", __func__);

	/*
	 * We don't have to handle the wpc detect handling,
	 * when it's the MST mode.
	 */
	if (charger->is_mst_on == MST_MODE_2) {
		pr_info("%s: check det_state(%d - %d)\n", __func__,
			charger->det_state, gpio_get_value(charger->pdata->wpc_det));

		if (charger->det_state == 0) {
			pr_info("%s: skip wpc_det_work for MST operation\n", __func__);
			__pm_relax(charger->wpc_det_ws);
			return;
		}
	}

	if (charger->det_state) {
		int i = 0;

		for (i = 0; i < 3; i++) {
			if (mfc_get_chip_id(charger) >= 0)
				break;
		}
		if (i == 3) {
			__pm_relax(charger->wpc_det_ws);
			return;
		}
		charger->initial_wc_check = true;

		charger->pdata->otp_firmware_ver = mfc_get_firmware_version(charger, MFC_RX_FIRMWARE);
		charger->pdata->wc_ic_rev = mfc_get_ic_revision(charger, MFC_IC_REVISION);

		if (charger->is_otg_on)
			charger->pdata->vout_status = MFC_VOUT_5V;
		else
			charger->pdata->vout_status = MFC_VOUT_5_5V;

		mfc_set_vout(charger, charger->pdata->vout_status);

		charger->wc_tx_enable = false;

		/* enable Mode Change INT */
		mfc_reg_update(charger->client, MFC_INT_A_ENABLE_L_REG,
				MFC_STAT_L_OP_MODE_MASK, MFC_STAT_L_OP_MODE_MASK);
		mfc_reg_update(charger->client, MFC_INT_A_ENABLE_L_REG,
				MFC_STAT_L_OVER_TEMP_MASK, MFC_STAT_L_OVER_TEMP_MASK);

		if (!charger->pdata->default_clamp_volt) {
			/* SET OV 20V */
			mfc_reg_write(charger->client, MFC_RX_OV_CLAMP_REG, 0x4);
		}
		/* SET ILIM 1.6A */
		mfc_reg_write(charger->client, MFC_ILIM_SET_REG, 0x20);

		/* read vrect adjust */
		mfc_reg_read(charger->client, MFC_VRECT_ADJ_REG, &vrect);

		pr_info("%s: wireless charger activated, set V_INT as PN\n", __func__);

		if (charger->pdata->unknown_cmb_ctrl)
			mfc_cma_cmb_onoff(charger, true, true);
		else
			mfc_cma_cmb_onoff(charger, true, false);

		/* read pad mode */
		mfc_reg_read(charger->client, MFC_SYS_OP_MODE_REG, &pad_mode);
		pad_mode = pad_mode >> 5;
		pr_info("%s: Pad type (0x%x)\n", __func__, pad_mode);
		if ((pad_mode == PAD_MODE_PMA_SR1) ||
			(pad_mode == PAD_MODE_PMA_SR1E)) {
			charger->pdata->cable_type = SEC_BATTERY_CABLE_PMA_WIRELESS;
			value.intval = charger->pdata->cable_type;
			psy_do_property("wireless", set, POWER_SUPPLY_PROP_ONLINE, value);
		} else { /* WPC */
			charger->pdata->cable_type = SEC_BATTERY_CABLE_WIRELESS;
			value.intval = charger->pdata->cable_type;
			psy_do_property("wireless", set, POWER_SUPPLY_PROP_ONLINE, value);
		}

		/* set fod value */
		psy_do_property("battery", get, POWER_SUPPLY_PROP_CAPACITY, value);
		capacity = value.intval;
		mfc_fod_set(charger, FOD_STATE_CC);

		if (!charger->pdata->no_hv) {
			vrect_level = mfc_get_adc(charger, MFC_ADC_VRECT);
			vout_level = mfc_get_adc(charger, MFC_ADC_VOUT);
			pr_info("%s: read vrect(%dmV), vout(%dmV)\n", __func__, vrect_level, vout_level);

			/* reboot status with previous hv voltage setting */
			if (vrect_level >= 8500 && vout_level >= 8500) {
				/* re-set vout level */
				charger->pad_vout = PAD_VOUT_10V;
				mfc_set_vout(charger, MFC_VOUT_10V);

				/* change cable type */
				charger->pdata->cable_type = SEC_BATTERY_CABLE_HV_WIRELESS;
				value.intval = charger->pdata->cable_type;
				psy_do_property("wireless", set,
					POWER_SUPPLY_PROP_ONLINE, value);
			} else {
				if (!charger->is_full_status) {
					/* send request afc_tx , request afc is mandatory */
					msleep(charger->req_afc_delay);
					mfc_send_command(charger, MFC_REQUEST_AFC_TX);
					__pm_stay_awake(charger->wpc_tx_pwr_budg_ws);
					queue_delayed_work(charger->wqueue,
						&charger->wpc_tx_pwr_budg_work, msecs_to_jiffies(1200));
				}
			}
		}
		charger->pdata->is_charging = 1;

		__pm_stay_awake(charger->wpc_tx_id_ws);
		queue_delayed_work(charger->wqueue, &charger->wpc_tx_id_work, msecs_to_jiffies(2500));
	} else {
		/* Send last tx_id to battery to count tx_id */
		value.intval = charger->tx_id;
		psy_do_property("wireless", set, POWER_SUPPLY_PROP_AUTHENTIC, value);
		charger->pdata->cable_type = SEC_BATTERY_CABLE_NONE;
		charger->pdata->is_charging = 0;
		charger->pdata->vout_status = MFC_VOUT_5V;
		charger->pad_vout = PAD_VOUT_5V;
		charger->pdata->opfq_cnt = 0;
		charger->pdata->trx_data_cmd = 0;
		charger->pdata->trx_data_val = 0;
		charger->vout_mode = 0;
		charger->is_full_status = 0;
		charger->pdata->capacity = 101;
		charger->is_afc_tx = false;
		charger->pad_ctrl_by_lcd = false;
		charger->tx_id = TX_ID_UNKNOWN;
		charger->i2c_error_count = 0;
		charger->adt_transfer_status = WIRELESS_AUTH_WAIT;
		charger->current_rx_power = TX_RX_POWER_0W;
		charger->tx_id_cnt = 0;
		charger->wc_ldo_status = MFC_LDO_ON;
		charger->tx_id_done = false;
		charger->req_tx_id = false;
		charger->req_afc_delay = REQ_AFC_DLY;
		charger->afc_tx_done = false;
		charger->wc_align_check_start.tv_sec = 0;
		charger->vout_strength = 100;
		charger->is_abnormal_pad = false;
		charger->check_rx_power = false;
		charger->tx_pwr_budg = MFC_TX_PWR_BUDG_NONE;
		value.intval = charger->is_abnormal_pad;
		psy_do_property("wireless", set,
			POWER_SUPPLY_EXT_PROP_WIRELESS_ABNORMAL_PAD, value);

		value.intval = SEC_BATTERY_CABLE_NONE;
		psy_do_property("wireless", set, POWER_SUPPLY_PROP_ONLINE, value);

		/* tx retry case for mis-align */
		if (charger->wc_checking_align) {
			/* reset try cnt in real detach status */
			if (!gpio_get_value(charger->pdata->wpc_en))
				charger->mis_align_tx_try_cnt = 1;
			else {
				if (charger->mis_align_tx_try_cnt > MISALIGN_TX_TRY_CNT)
					charger->mis_align_tx_try_cnt = 1;
				msleep(1000);
				mfc_set_wpc_en(charger, WPC_EN_CHARGING, true);
			}
		} else
			charger->mis_align_tx_try_cnt = 1;

		charger->wc_checking_align = false;

		pr_info("%s: wpc deactivated, set V_INT as PD\n", __func__);

		if (delayed_work_pending(&charger->wpc_afc_vout_work)) {
			__pm_relax(charger->wpc_afc_vout_ws);
			cancel_delayed_work(&charger->wpc_afc_vout_work);
		}
		if (delayed_work_pending(&charger->wpc_vout_mode_work)) {
			__pm_relax(charger->wpc_vout_mode_ws);
			cancel_delayed_work(&charger->wpc_vout_mode_work);
		}
		if (delayed_work_pending(&charger->wpc_cs100_work)) {
			__pm_relax(charger->wpc_cs100_ws);
			cancel_delayed_work(&charger->wpc_cs100_work);
		}
		if (delayed_work_pending(&charger->wpc_check_rx_power_work)) {
			__pm_relax(charger->wpc_check_rx_power_ws);
			cancel_delayed_work(&charger->wpc_check_rx_power_work);
		}

		cancel_delayed_work(&charger->wpc_isr_work);
		cancel_delayed_work(&charger->wpc_tx_isr_work);
		cancel_delayed_work(&charger->wpc_tx_id_work);
		cancel_delayed_work(&charger->wpc_tx_pwr_budg_work);
		cancel_delayed_work(&charger->wpc_i2c_error_work);
		cancel_delayed_work(&charger->align_check_work);
		__pm_relax(charger->wpc_rx_ws);
		__pm_relax(charger->wpc_tx_ws);
		__pm_relax(charger->wpc_tx_id_ws);
		__pm_relax(charger->wpc_tx_pwr_budg_ws);
		__pm_relax(charger->align_check_ws);
	}
	__pm_relax(charger->wpc_det_ws);

	charger->initial_vrect = false;

	pr_info("%s : end\n", __func__);
}

static void mfc_wpc_pdrc_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, wpc_pdrc_work.work);
	union power_supply_propval value;

	if (charger->is_mst_on == MST_MODE_2 || charger->wc_tx_enable) {
		pr_info("%s: Noise made false Vrect IRQ !\n", __func__);
		if (charger->wc_tx_enable) {
			value.intval = BATT_TX_EVENT_WIRELESS_TX_ETC;
			psy_do_property("wireless", set, POWER_SUPPLY_EXT_PROP_WIRELESS_TX_ERR, value);
		}
		__pm_relax(charger->wpc_pdrc_ws);
		return;
	}

	pr_info("%s : cable_type: %d, status: %d\n", __func__,
		charger->pdata->cable_type, charger->pdrc_state);

	if (charger->pdata->cable_type == SEC_BATTERY_CABLE_NONE && (charger->pdrc_state == 0)) {
		int i = 0;

		for (i = 0; i < 3; i++) {
			if (mfc_get_chip_id(charger) >= 0)
				break;
		}
		if (i == 3) {
			__pm_relax(charger->wpc_pdrc_ws);
			return;
		}
		charger->pdata->cable_type = SEC_BATTERY_CABLE_WIRELESS_FAKE;
		value.intval = charger->pdata->cable_type;
		psy_do_property("wireless", set, POWER_SUPPLY_PROP_ONLINE, value);
	} else if ((charger->pdata->cable_type == SEC_BATTERY_CABLE_WIRELESS_FAKE) && (charger->pdrc_state == 1)) {
		charger->pdata->cable_type = SEC_BATTERY_CABLE_NONE;
		charger->is_full_status = 0;
		value.intval = SEC_BATTERY_CABLE_NONE;
		psy_do_property("wireless", set, POWER_SUPPLY_PROP_ONLINE, value);
	}

	__pm_relax(charger->wpc_pdrc_ws);
}

/* INT_A */
static void mfc_wpc_tx_isr_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, wpc_tx_isr_work.work);

	u8 status_l = 0, status_h = 0;
	int ret = 0;

	pr_info("@Tx_Mode %s\n", __func__);

	if (!charger->wc_tx_enable) {
		__pm_relax(charger->wpc_tx_ws);
		return;
	}

	ret = mfc_reg_read(charger->client, MFC_STATUS_L_REG, &status_l);
	ret = mfc_reg_read(charger->client, MFC_STATUS_H_REG, &status_h);

	pr_info("@Tx_Mode %s: DATA RECEIVED! status(0x%x)\n", __func__, status_h << 8 | status_l);

	mfc_tx_handle_rx_packet(charger);

	__pm_relax(charger->wpc_tx_ws);
}

/* INT_A */
static void mfc_wpc_isr_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, wpc_isr_work.work);
	u8 cmd_data, val_data;
	int wpc_vout_ctrl_lcd_on = 0;
	union power_supply_propval value = {0, };

	if (!charger->det_state) {
		pr_info("%s: charger->det_state is 0. exit wpc_isr_work.\n", __func__);
		__pm_relax(charger->wpc_rx_ws);
		return;
	}

	pr_info("%s: cable_type (%d)\n", __func__, charger->pdata->cable_type);

	mfc_reg_read(charger->client, MFC_WPC_TRX_DATA2_COM_REG, &cmd_data);
	mfc_reg_read(charger->client, MFC_WPC_TRX_DATA2_VALUE0_REG, &val_data);
	charger->pdata->trx_data_cmd = cmd_data;
	charger->pdata->trx_data_val = val_data;

	pr_info("%s: WPC Interrupt Occurred, CMD : 0x%x, DATA : 0x%x\n", __func__, cmd_data, val_data);

	/* None or RX mode */
	if (cmd_data == WPC_TX_COM_AFC_SET) {
		if (charger->req_tx_id) {
			pr_err("%s: requested tx id, but other packet received.\n", __func__);
			charger->req_tx_id = false;
			charger->is_abnormal_pad = true;
			value.intval = charger->is_abnormal_pad;
			psy_do_property("wireless", set,
				POWER_SUPPLY_EXT_PROP_WIRELESS_ABNORMAL_PAD, value);
			if (charger->afc_tx_done) {
				if (delayed_work_pending(&charger->wpc_afc_vout_work)) {
					__pm_relax(charger->wpc_afc_vout_ws);
					cancel_delayed_work(&charger->wpc_afc_vout_work);
				}
				mfc_set_pad_hv(charger, false);
				charger->pdata->cable_type = value.intval = SEC_BATTERY_CABLE_WIRELESS;
				psy_do_property("wireless", set,
					POWER_SUPPLY_PROP_ONLINE, value);
				charger->pad_vout = PAD_VOUT_5V;
				charger->afc_tx_done = false;
			}
			__pm_relax(charger->wpc_rx_ws);
			return;
		}

		switch (val_data) {
		case TX_AFC_SET_5V:
			pr_info("%s: data = 0x%x, might be 5V irq\n", __func__, val_data);
			charger->pad_vout = PAD_VOUT_5V;
			break;
		case TX_AFC_SET_10V:
			pr_info("%s: data = 0x%x, might be 10V irq\n", __func__, val_data);
			charger->afc_tx_done = true;
			if (!gpio_get_value(charger->pdata->wpc_det)) {
				pr_err("%s: Wireless charging is paused during set high voltage.\n", __func__);
				__pm_relax(charger->wpc_rx_ws);
				return;
			}
			if (!charger->pdata->no_hv) {
				if (is_hv_wireless_type(charger->pdata->cable_type) ||
						charger->pdata->cable_type == SEC_BATTERY_CABLE_PREPARE_WIRELESS_HV) {
					pr_err("%s: Is is already HV wireless cable. No need to set again\n", __func__);
					__pm_relax(charger->wpc_rx_ws);
					return;
				}
				if (charger->is_abnormal_pad) {
					pr_err("%s: Abnormal pad, Do not set\n", __func__);
					__pm_relax(charger->wpc_rx_ws);
					return;
				}

				/* send AFC_SET */
				mfc_send_command(charger, MFC_AFC_CONF_10V);
				msleep(500);

				/* change cable type */
				charger->pdata->cable_type = SEC_BATTERY_CABLE_PREPARE_WIRELESS_HV;
				value.intval = charger->pdata->cable_type;
				psy_do_property("wireless", set, POWER_SUPPLY_PROP_ONLINE, value);

				charger->pad_vout = PAD_VOUT_10V;
			}
			break;
		case TX_AFC_SET_12V:
			break;
		case TX_AFC_SET_18V:
		case TX_AFC_SET_19V:
		case TX_AFC_SET_20V:
		case TX_AFC_SET_24V:
			break;
		default:
			pr_info("%s: unsupport : 0x%x", __func__, val_data);
			break;
		}
	} else if (cmd_data == WPC_TX_COM_TX_ID) {
		if (charger->req_tx_id)
			charger->req_tx_id = false;

		if (!charger->tx_id_done) {
			int capacity = 0;

			charger->tx_id = val_data;
			psy_do_property("battery", get, POWER_SUPPLY_PROP_CAPACITY, value);
			capacity = value.intval;

			psy_do_property("battery", get, POWER_SUPPLY_EXT_PROP_LCD_FLICKER, value);
			wpc_vout_ctrl_lcd_on = value.intval;

			switch (val_data) {
			case TX_ID_UNKNOWN:
				break;
			case TX_ID_VEHICLE_PAD:
				if (charger->pad_vout == PAD_VOUT_10V) {
					if (charger->pdata->cable_type == SEC_BATTERY_CABLE_PREPARE_WIRELESS_HV) {
						charger->pdata->cable_type = SEC_BATTERY_CABLE_WIRELESS_HV_VEHICLE;
						value.intval = SEC_BATTERY_CABLE_PREPARE_WIRELESS_HV;
					} else {
						charger->pdata->cable_type = value.intval =
							SEC_BATTERY_CABLE_WIRELESS_HV_VEHICLE;
					}
				} else {
					charger->pdata->cable_type = value.intval = SEC_BATTERY_CABLE_WIRELESS_VEHICLE;
				}
				pr_info("%s: VEHICLE Wireless Charge PAD %s\n", __func__,
					charger->pad_vout == PAD_VOUT_10V ? "HV" : "");

				break;
			case TX_ID_STAND_TYPE_START:
				if (charger->pad_vout == PAD_VOUT_10V) {
					if (charger->pdata->cable_type == SEC_BATTERY_CABLE_PREPARE_WIRELESS_HV) {
						charger->pdata->cable_type = SEC_BATTERY_CABLE_WIRELESS_HV_STAND;
						value.intval = SEC_BATTERY_CABLE_PREPARE_WIRELESS_HV;
					} else {
						charger->pdata->cable_type = SEC_BATTERY_CABLE_WIRELESS_HV_STAND;
					}
				} else {
					charger->pdata->cable_type = value.intval =
						SEC_BATTERY_CABLE_WIRELESS_STAND;
				}
				pr_info("%s: Cable(%d), STAND Wireless Charge PAD %s\n", __func__,
					charger->pdata->cable_type, charger->pad_vout == PAD_VOUT_10V ? "HV" : "");
				break;
			case TX_ID_MULTI_PORT_START ... TX_ID_MULTI_PORT_END:
				value.intval = charger->pdata->cable_type;
				pr_info("%s: MULTI PORT PAD : 0x%x\n", __func__, val_data);
				break;
			case TX_ID_BATT_PACK ... TX_ID_BATT_PACK_END:
				if (charger->pad_vout == PAD_VOUT_10V) {
					if (charger->pdata->cable_type == SEC_BATTERY_CABLE_PREPARE_WIRELESS_HV) {
						charger->pdata->cable_type = SEC_BATTERY_CABLE_WIRELESS_HV_PACK;
						value.intval = SEC_BATTERY_CABLE_PREPARE_WIRELESS_HV;
						pr_info("%s: WIRELESS HV BATTERY PACK (PREP)\n", __func__);
					} else {
						charger->pdata->cable_type = value.intval =
							SEC_BATTERY_CABLE_WIRELESS_HV_PACK;
						pr_info("%s: WIRELESS HV BATTERY PACK\n", __func__);
					}
				} else {
					charger->pdata->cable_type = value.intval = SEC_BATTERY_CABLE_WIRELESS_PACK;
					pr_info("%s: WIRELESS BATTERY PACK\n", __func__);
				}
				if (charger->device_event & BATT_EXT_EVENT_CALL) {
					union power_supply_propval value2;

					pr_info("%s: enter PHM\n", __func__);
					/* notify "wireless" PHM status */
					value2.intval = 1;
					psy_do_property("wireless", set, POWER_SUPPLY_EXT_PROP_CALL_EVENT, value2);
					mfc_send_command(charger, MFC_PHM_ON);
					msleep(250);
					mfc_send_command(charger, MFC_PHM_ON);
				}
				break;
			case TX_ID_UNO_TX:
			case TX_ID_UNO_TX_B0 ... TX_ID_UNO_TX_MAX:
				charger->pdata->cable_type = value.intval = SEC_BATTERY_CABLE_WIRELESS_TX;
				pr_info("@Tx_Mode %s: TX by UNO\n", __func__);
				if (capacity < 100)
					mfc_wpc_align_check(charger, ALIGN_WORK_DELAY);

				if (charger->device_event & BATT_EXT_EVENT_CALL) {
					pr_info("%s: enter PHM\n", __func__);
					mfc_send_command(charger, MFC_PHM_ON);
					msleep(250);
					mfc_send_command(charger, MFC_PHM_ON);
				}
				break;
			case TX_ID_NON_AUTH_PAD ... TX_ID_NON_AUTH_PAD_END:
				value.intval = charger->pdata->cable_type;
				break;
			case TX_ID_AUTH_PAD ... TX_ID_AUTH_PAD_END:
				if (charger->pdata->no_hv) {
					pr_info("%s: WIRELESS HV is disabled\n", __func__);
					break;
				}
				charger->vout_by_txid = mfc_get_wireless20_vout_by_txid(charger, val_data);
				charger->vrect_by_txid = mfc_get_wireless20_vrect_by_txid(charger, val_data);
				charger->max_power_by_txid = mfc_get_wireless20_max_power_by_txid(charger, val_data);
#if !defined(CONFIG_SEC_FACTORY)
				/* power on charging */
				if (charger->adt_transfer_status == WIRELESS_AUTH_WAIT) {
					mfc_auth_set_configs(charger, AUTH_READY);
					if (delayed_work_pending(&charger->wpc_afc_vout_work)) {
						__pm_relax(charger->wpc_afc_vout_ws);
						cancel_delayed_work(&charger->wpc_afc_vout_work);
					}
					charger->adt_transfer_status = WIRELESS_AUTH_START;
					/* notify auth service to send TX PAD a request key */
					value.intval = WIRELESS_AUTH_START;
					psy_do_property("wireless", set,
						POWER_SUPPLY_EXT_PROP_WIRELESS_AUTH_ADT_STATUS, value);

					charger->pdata->cable_type = SEC_BATTERY_CABLE_PREPARE_WIRELESS_20;
					value.intval = charger->pdata->cable_type;
					pr_info("%s %s: AUTH PAD for WIRELESS 2.0 : 0x%x\n", WC_AUTH_MSG,
							__func__, val_data);
				} else {
					value.intval = charger->pdata->cable_type;
					pr_info("%s %s: undefined auth TX-ID scenario, auth service works strange...\n",
							WC_AUTH_MSG, __func__);
				}
#else /* FACTORY MODE dose not process auth, set 12W right after */
				if (charger->adt_transfer_status == WIRELESS_AUTH_WAIT) {
					if (delayed_work_pending(&charger->wpc_afc_vout_work)) {
						__pm_relax(charger->wpc_afc_vout_ws);
						cancel_delayed_work(&charger->wpc_afc_vout_work);
					}
					charger->adt_transfer_status = WIRELESS_AUTH_PASS;
					charger->pdata->cable_type = value.intval = SEC_BATTERY_CABLE_HV_WIRELESS_20;
					__pm_stay_awake(charger->wpc_afc_vout_ws);
					queue_delayed_work(charger->wqueue,
						&charger->wpc_afc_vout_work, msecs_to_jiffies(0));
				}
				pr_info("%s %s: AUTH PAD for WIRELESS 2.0 but FACTORY MODE\n", WC_AUTH_MSG, __func__);
#endif
				break;
			case TX_ID_DREAM_STAND:
				value.intval = charger->pdata->cable_type;
				pr_info("%s: Dream PAD STAND : 0x%x\n", __func__, val_data);
				break;
			case TX_ID_DREAM_DOWN:
				value.intval = charger->pdata->cable_type;
				pr_info("%s: Dream PAD DOWN : 0x%x\n", __func__, val_data);
				break;
			default:
				value.intval = charger->pdata->cable_type;
				pr_info("%s: UNDEFINED PAD : 0x%x\n", __func__, val_data);
				break;
			}

			if (wpc_vout_ctrl_lcd_on && opfreq_ctrl_pad(charger->tx_id)) {
				pr_info("%s: tx id = 0x%x , set op freq\n", __func__, val_data);
				mfc_send_command(charger, MFC_SET_OP_FREQ);
				msleep(500);
			}

			if (charger->is_full_status)
				mfc_set_vout_ctrl_full(charger);

			if (value.intval == 0) {
				pr_info("%s: cable type is null plz check out !!!\n", __func__);
				value.intval = charger->pdata->cable_type;
			}

			if (value.intval != SEC_BATTERY_CABLE_PREPARE_WIRELESS_HV &&
				value.intval != SEC_BATTERY_CABLE_HV_WIRELESS_20) {
				pr_info("%s: change cable type\n", __func__);
				psy_do_property("wireless", set, POWER_SUPPLY_PROP_ONLINE, value);
			}

			/* send max vout informaion of this pad such as WIRELESS_VOUT_10V, WIRELESS_VOUT_12_5V */
			value.intval = charger->vout_by_txid;
			psy_do_property("wireless", set, POWER_SUPPLY_EXT_PROP_WIRELESS_MAX_VOUT, value);

			/*
			 * send rx power informaion of this pad such as
			 * SEC_WIRELESS_RX_POWER_12W, SEC_WIRELESS_RX_POWER_17_5W
			 */
			value.intval = charger->max_power_by_txid;
			psy_do_property("wireless", set, POWER_SUPPLY_EXT_PROP_WIRELESS_RX_POWER, value);

#if defined(CONFIG_SEC_FACTORY)
			queue_delayed_work(charger->wqueue, &charger->wpc_rx_power_work, msecs_to_jiffies(3000));
#endif
			charger->tx_id_done = true;
			pr_info("%s: TX_ID : 0x%x\n", __func__, val_data);
		} else {
			pr_err("%s: TX ID isr is already done\n", __func__);
		}
	} else if (cmd_data == WPC_TX_COM_CHG_ERR) {
		switch (val_data) {
		case TX_CHG_ERR_OTP:
		case TX_CHG_ERR_OCP:
		case TX_CHG_ERR_DARKZONE:
			pr_info("%s: Received CHG error from the TX(0x%x)\n", __func__, val_data);
			break;
		/*case TX_CHG_ERR_FOD:*/
		case 0x20 ... 0x27:
			pr_info("%s: Received FOD state from the TX(0x%x)\n", __func__, val_data);
			value.intval = val_data;
			psy_do_property("wireless", set, POWER_SUPPLY_EXT_PROP_WIRELESS_ERR, value);
			break;
		default:
			pr_info("%s: Undefined Type(0x%x)\n", __func__, val_data);
			break;
		}
	} else if (cmd_data == WPC_TX_COM_RX_POWER) {
		charger->check_rx_power = false;
		if (delayed_work_pending(&charger->wpc_check_rx_power_work)) {
			__pm_relax(charger->wpc_check_rx_power_ws);
			cancel_delayed_work(&charger->wpc_check_rx_power_work);
		}
		if (charger->current_rx_power != val_data) {
			switch (val_data) {
			case TX_RX_POWER_3W:
				pr_info("%s %s: RX Power is 3W\n", WC_AUTH_MSG, __func__);
				charger->current_rx_power = TX_RX_POWER_3W;
				break;
			case TX_RX_POWER_5W:
				pr_info("%s %s: RX Power is 5W\n", WC_AUTH_MSG, __func__);
				charger->current_rx_power = TX_RX_POWER_5W;
				break;
			case TX_RX_POWER_6_5W:
				pr_info("%s %s: RX Power is 6.5W\n", WC_AUTH_MSG, __func__);
				charger->current_rx_power = TX_RX_POWER_6_5W;
				break;
			case TX_RX_POWER_7_5W:
				pr_info("%s %s: RX Power is 7.5W\n", WC_AUTH_MSG, __func__);
				mfc_reset_rx_power(charger, TX_RX_POWER_7_5W);
				charger->current_rx_power = TX_RX_POWER_7_5W;
				break;
			case TX_RX_POWER_10W:
				pr_info("%s %s: RX Power is 10W\n", WC_AUTH_MSG, __func__);
				charger->current_rx_power = TX_RX_POWER_10W;
				break;
			case TX_RX_POWER_12W:
				pr_info("%s %s: RX Power is 12W\n", WC_AUTH_MSG, __func__);
				mfc_reset_rx_power(charger, TX_RX_POWER_12W);
				charger->current_rx_power = TX_RX_POWER_12W;
				break;
			case TX_RX_POWER_15W:
				pr_info("%s %s: RX Power is 15W\n", WC_AUTH_MSG, __func__);
				mfc_reset_rx_power(charger, TX_RX_POWER_15W);
				charger->current_rx_power = TX_RX_POWER_15W;
				break;
			case TX_RX_POWER_17_5W:
				pr_info("%s %s: RX Power is 17.5W\n", WC_AUTH_MSG, __func__);
				mfc_reset_rx_power(charger, TX_RX_POWER_17_5W);
				charger->current_rx_power = TX_RX_POWER_17_5W;
				break;
			case TX_RX_POWER_20W:
				pr_info("%s %s: RX Power is 20W\n", WC_AUTH_MSG, __func__);
				mfc_reset_rx_power(charger, TX_RX_POWER_20W);
				charger->current_rx_power = TX_RX_POWER_20W;
				break;
			default:
				pr_info("%s %s: Undefined RX Power(0x%x)\n", WC_AUTH_MSG, __func__, val_data);
				break;
			}
		} else {
			pr_info("%s: skip same RX Power\n", __func__);
		}
	} else if (cmd_data == WPC_TX_COM_WPS) {
		switch (val_data) {
		case WPS_AICL_RESET:
			value.intval = 1;
			pr_info("@Tx_mode %s: Rx devic AICL Reset\n", __func__);
			psy_do_property("wireless", set, POWER_SUPPLY_PROP_CURRENT_MAX, value);
			break;
		default:
			pr_info("%s %s: Undefined RX Power(0x%x)\n", WC_AUTH_MSG, __func__, val_data);
			break;
		}
	} else if (cmd_data == WPC_TX_COM_TX_PWR_BUDG) {
		pr_info("%s: WPC_TX_COM_TX_PWR_BUDG (0x%x)\n", __func__, val_data);
		mfc_recv_tx_pwr_budg(charger, val_data);
	}
	__pm_relax(charger->wpc_rx_ws);
}

static void mfc_set_unknown_pad_config(struct mfc_charger_data *charger)
{
	pr_info("%s: TX ID not Received, cable_type(%d)\n",
			__func__, charger->pdata->cable_type);

	if (charger->is_full_status)
		mfc_set_vout_ctrl_full(charger);
	if (!charger->pdata->default_clamp_volt) {
		if (is_hv_wireless_type(charger->pdata->cable_type)) {
			/* SET OV 13V */
			mfc_reg_write(charger->client, MFC_RX_OV_CLAMP_REG, 0x1);
		} else {
			/* SET OV 11V */
			mfc_reg_write(charger->client, MFC_RX_OV_CLAMP_REG, 0x0);
		}
	}
}

static void mfc_wpc_tx_pwr_budg_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, wpc_tx_pwr_budg_work.work);

	mfc_send_command(charger, MFC_REQ_TX_PWR_BUDG);
	__pm_relax(charger->wpc_tx_pwr_budg_ws);
}

static void mfc_wpc_tx_id_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, wpc_tx_id_work.work);
	union power_supply_propval value;

	pr_info("%s\n", __func__);

	if (!charger->tx_id && !charger->is_abnormal_pad) {
		if (charger->tx_id_cnt < 10) {
			mfc_send_command(charger, MFC_REQUEST_TX_ID);
			charger->req_tx_id = true;
			charger->tx_id_cnt++;

			pr_info("%s: request TX ID (%d)\n", __func__, charger->tx_id_cnt);
			queue_delayed_work(charger->wqueue, &charger->wpc_tx_id_work, msecs_to_jiffies(1500));
			return;
		}

		mfc_set_unknown_pad_config(charger);
		charger->req_tx_id = false;
	} else {
		if (charger->is_abnormal_pad) {
			mfc_set_unknown_pad_config(charger);
		} else {
			pr_info("%s: TX ID (0x%x)\n", __func__, charger->tx_id);
			if ((charger->tx_id == TX_ID_P1100_PAD) ||
				(charger->tx_id == TX_ID_DREAM_STAND) ||
				(charger->tx_id == TX_ID_DREAM_DOWN) ||
				(charger->tx_id == TX_ID_AUTH_PAD)) {
				mfc_cma_cmb_onoff(charger, true, true);
			} else if (charger->tx_id == TX_ID_JIG_PAD) {
				value.intval = 1;
				pr_info("%s: it is jig pad\n", __func__);
				psy_do_property("wireless", set, POWER_SUPPLY_EXT_PROP_WIRELESS_JIG_PAD, value);
			}
			else
				mfc_cma_cmb_onoff(charger, true, false);
		}
	}

	__pm_relax(charger->wpc_tx_id_ws);
}

static irqreturn_t mfc_wpc_det_irq_thread(int irq, void *irq_data)
{
	struct mfc_charger_data *charger = irq_data;
	u8 det_state;

	pr_info("%s(%d)\n", __func__, irq);

	det_state = gpio_get_value(charger->pdata->wpc_det);

	if (charger->det_state != det_state) {
		pr_info("%s: det(%d to %d)\n", __func__, charger->det_state, det_state);
		charger->det_state = det_state;
		queue_delayed_work(charger->wqueue, &charger->wpc_det_work, 0);
	} else
		__pm_relax(charger->wpc_det_ws);

	return IRQ_HANDLED;
}

static irqreturn_t mfc_wpc_pdrc_irq_thread(int irq, void *irq_data)
{
	struct mfc_charger_data *charger = irq_data;
	u8 pdrc_state;

	pr_info("%s(%d)\n", __func__, irq);

	pdrc_state = gpio_get_value(charger->pdata->wpc_pdrc);

	if (charger->pdrc_state != pdrc_state) {
		pr_info("%s: pdrc(%d to %d)\n", __func__, charger->pdrc_state, pdrc_state);
		charger->pdrc_state = pdrc_state;
		if (charger->pdata->cable_type == SEC_BATTERY_CABLE_NONE)
			queue_delayed_work(charger->wqueue, &charger->wpc_pdrc_work, msecs_to_jiffies(0));
		else if (charger->pdata->cable_type == SEC_BATTERY_CABLE_WIRELESS_FAKE)
			queue_delayed_work(charger->wqueue, &charger->wpc_pdrc_work, msecs_to_jiffies(900));
		else
			__pm_relax(charger->wpc_pdrc_ws);
	} else
		__pm_relax(charger->wpc_pdrc_ws);

	return IRQ_HANDLED;
}

static irqreturn_t mfc_wpc_pdet_b_irq_thread(int irq, void *irq_data)
{
	struct mfc_charger_data *charger = irq_data;

	pr_info("%s : %d\n", __func__, charger->pdata->cable_type);

	return IRQ_HANDLED;
}

static void mfc_set_iec_params(struct mfc_charger_data *charger, mfc_charger_platform_data_t *pdata)
{
	pr_info("%s: iec_q_thresh_1 = %d\n", __func__, pdata->iec_q_thresh_1);
	mfc_reg_write(charger->client, MFC_IEC_Q_THRESH1_REG, pdata->iec_q_thresh_1);

	pr_info("%s: iec_q_thresh_2 = %d\n", __func__, pdata->iec_q_thresh_2);
	mfc_reg_write(charger->client, MFC_IEC_Q_THRESH2_REG, pdata->iec_q_thresh_2);

	pr_info("%s: iec_fres_thresh_1 = %d\n", __func__, pdata->iec_fres_thresh_1);
	mfc_reg_write(charger->client, MFC_IEC_FRES_THRESH1_REG, pdata->iec_fres_thresh_1);

	pr_info("%s: iec_fres_thresh_2 = %d\n", __func__, pdata->iec_fres_thresh_2);
	mfc_reg_write(charger->client, MFC_IEC_FRES_THRESH2_REG, pdata->iec_fres_thresh_2);

	pr_info("%s: iec_power_limit_thresh = %d\n", __func__, pdata->iec_power_limit_thresh);
	mfc_reg_write(charger->client, MFC_IEC_POWER_LIMIT_THRESH_L_REG, pdata->iec_power_limit_thresh);
	mfc_reg_write(charger->client, MFC_IEC_POWER_LIMIT_THRESH_H_REG, (pdata->iec_power_limit_thresh >> 8));

	pr_info("%s: iec_ploss_thresh_1 = %d\n", __func__, pdata->iec_ploss_thresh_1);
	mfc_reg_write(charger->client, MFC_IEC_PLOSS_THRESH1_L_REG, pdata->iec_ploss_thresh_1);
	mfc_reg_write(charger->client, MFC_IEC_PLOSS_THRESH1_H_REG, (pdata->iec_ploss_thresh_1 >> 8));

	pr_info("%s: iec_ploss_thresh_2 = %d\n", __func__, pdata->iec_ploss_thresh_2);
	mfc_reg_write(charger->client, MFC_IEC_PLOSS_THRESH2_L_REG, pdata->iec_ploss_thresh_2);
	mfc_reg_write(charger->client, MFC_IEC_PLOSS_THRESH2_H_REG, (pdata->iec_ploss_thresh_2 >> 8));

	pr_info("%s: iec_ploss_freq_thresh_1 = %d\n", __func__, pdata->iec_ploss_freq_thresh_1);
	mfc_reg_write(charger->client, MFC_IEC_PLOSS_FREQ_THRESH1_REG, pdata->iec_ploss_freq_thresh_1);

	pr_info("%s: iec_ploss_freq_thresh_2 = %d\n", __func__, pdata->iec_ploss_freq_thresh_2);
	mfc_reg_write(charger->client, MFC_IEC_PLOSS_FREQ_THRESH2_REG, pdata->iec_ploss_freq_thresh_2);

	pr_info("%s: iec_ta_power_limit_thresh = %d\n", __func__, pdata->iec_ta_power_limit_thresh);
	mfc_reg_write(charger->client, MFC_IEC_TA_POWER_LIMIT_THRESH_L_REG, pdata->iec_ta_power_limit_thresh);
	mfc_reg_write(charger->client, MFC_IEC_TA_POWER_LIMIT_THRESH_H_REG, (pdata->iec_ta_power_limit_thresh >> 8));

	pr_info("%s: iec_ta_ploss_thresh_1 = %d\n", __func__, pdata->iec_ta_ploss_thresh_1);
	mfc_reg_write(charger->client, MFC_IEC_TA_PLOSS_THRESH1_L_REG, pdata->iec_ta_ploss_thresh_1);
	mfc_reg_write(charger->client, MFC_IEC_TA_PLOSS_THRESH1_H_REG, (pdata->iec_ta_ploss_thresh_1 >> 8));

	pr_info("%s: iec_ta_ploss_thresh_2 = %d\n", __func__, pdata->iec_ta_ploss_thresh_2);
	mfc_reg_write(charger->client, MFC_IEC_TA_PLOSS_THRESH2_L_REG, pdata->iec_ta_ploss_thresh_2);
	mfc_reg_write(charger->client, MFC_IEC_TA_PLOSS_THRESH2_H_REG, (pdata->iec_ta_ploss_thresh_2 >> 8));

	pr_info("%s: iec_ta_ploss_freq_thresh_1 = %d\n", __func__, pdata->iec_ta_ploss_freq_thresh_1);
	mfc_reg_write(charger->client, MFC_IEC_TA_PLOSS_FREQ_THRESH1_REG, pdata->iec_ta_ploss_freq_thresh_1);

	pr_info("%s: iec_ta_ploss_freq_thresh_2 = %d\n", __func__, pdata->iec_ta_ploss_freq_thresh_2);
	mfc_reg_write(charger->client, MFC_IEC_TA_PLOSS_FREQ_THRESH2_REG, pdata->iec_ta_ploss_freq_thresh_2);

	pr_info("%s: iec_ploss_fod_enable = %d\n", __func__, pdata->iec_ploss_fod_enable);
	mfc_reg_write(charger->client, MFC_IEC_PLOSS_FOD_ENABLE_REG, pdata->iec_ploss_fod_enable);

	pr_info("%s: iec_qfod_enable = %d\n", __func__, pdata->iec_qfod_enable);
	mfc_reg_write(charger->client, MFC_IEC_QFOD_ENABLE_REG, pdata->iec_qfod_enable);

	pr_info("%s: iec_qfod_enable = %d\n", __func__, pdata->iec_pwm_duty_thresh);
	mfc_reg_write(charger->client, MFC_IEC_PWM_DUTY_THRESH, pdata->iec_pwm_duty_thresh);

	pr_info("%s: iec_qfod_enable = %d\n", __func__, pdata->iec_ta_pwm_duty_thresh);
	mfc_reg_write(charger->client, MFC_IEC_TA_PWM_DUTY_THRESH, pdata->iec_ta_pwm_duty_thresh);
}

/* mfc_mst_routine : MST dedicated codes */
static void mfc_mst_routine(struct mfc_charger_data *charger, u8 irq_src_l, u8 irq_src_h)
{
	u8 data = 0;

	pr_info("%s\n", __func__);

	if (charger->is_mst_on == MST_MODE_2) {
		charger->wc_tx_enable = false;
#if defined(CONFIG_MST_V2)
		// it will send MST driver a message.
		mfc_send_mst_cmd(MST_MODE_ON, charger, irq_src_l, irq_src_h);
#endif
	} else if (charger->wc_tx_enable) {
		mfc_reg_read(charger->client, MFC_STATUS_H_REG, &data);
		data &= 0x4; /* AC MISSING DETECT */
		msleep(100);
		pr_info("@Tx_Mode %s: 0x21 Register AC Missing(%d)\n", __func__, data);
		if (data) {
			/* initialize control reg */
			mfc_set_tx_conflict_current(charger, charger->pdata->tx_conflict_curr);
			mfc_set_iec_params(charger, charger->pdata);
			mfc_reg_write(charger->client, MFC_TX_IUNO_HYS_REG, 0x50);
			mfc_reg_write(charger->client, MFC_DEMOD1_REG, 0x00);
			mfc_reg_write(charger->client, MFC_DEMOD2_REG, 0x00);
			mfc_reg_write(charger->client, MFC_MST_MODE_SEL_REG, 0x03); /* set TX-ON mode */
			mfc_set_cep_timeout(charger, charger->pdata->cep_timeout); // this is only for CAN
			pr_info("@Tx_Mode %s: TX-ON Mode : %d\n", __func__, charger->pdata->wc_ic_rev);
		} // ac missing is 0, ie, TX detected
	}
}

static irqreturn_t mfc_wpc_irq_handler(int irq, void *irq_data)
{
	struct mfc_charger_data *charger = irq_data;

	if (irq == charger->pdata->irq_wpc_int)
		__pm_stay_awake(charger->wpc_ws);
	else if (irq == charger->pdata->irq_wpc_det)
		__pm_stay_awake(charger->wpc_det_ws);
	else if (irq == charger->pdata->irq_wpc_pdrc)
		__pm_stay_awake(charger->wpc_pdrc_ws);

#if IS_ENABLED(CONFIG_ENABLE_WIRELESS_IRQ_IN_SLEEP)
	return charger->is_suspend ? IRQ_HANDLED : IRQ_WAKE_THREAD;
#else
	return IRQ_WAKE_THREAD;
#endif
}

static irqreturn_t mfc_wpc_irq_thread(int irq, void *irq_data)
{
	struct mfc_charger_data *charger = irq_data;
	u8 int_state;
	int ret = 0;
	u8 irq_src_l = 0, irq_src_h = 0;
	u8 status_l = 0, status_h = 0;
	bool end_irq = false;
	union power_supply_propval value;

	pr_info("%s: start(%d)!\n", __func__, irq);

	int_state = gpio_get_value(charger->pdata->wpc_int);
	pr_info("%s: int_state = %d\n", __func__, int_state);

	if (int_state == 1) {
		pr_info("%s: Falling edge, End up ISR\n", __func__);
		__pm_relax(charger->wpc_ws);
		return IRQ_HANDLED;
	}

	ret = mfc_reg_read(charger->client, MFC_INT_A_L_REG, &irq_src_l);
	if (ret < 0) {
		pr_err("%s: Failed to read INT_A_L_REG: %d\n", __func__, ret);
		__pm_relax(charger->wpc_ws);
		return IRQ_HANDLED;
	}
	ret = mfc_reg_read(charger->client, MFC_INT_A_H_REG, &irq_src_h);
	if (ret < 0) {
		pr_err("%s: Failed to read INT_A_H_REG: %d\n", __func__, ret);
		__pm_relax(charger->wpc_ws);
		return IRQ_HANDLED;
	}

	ret = mfc_reg_read(charger->client, MFC_STATUS_L_REG, &status_l);
	if (ret < 0) {
		pr_err("%s: Failed to read STATUS_L_REG: %d\n", __func__, ret);
		__pm_relax(charger->wpc_ws);
		return IRQ_HANDLED;
	}
	ret = mfc_reg_read(charger->client, MFC_STATUS_H_REG, &status_h);
	if (ret < 0) {
		pr_err("%s: Failed to read STATUS_H_REG: %d\n", __func__, ret);
		__pm_relax(charger->wpc_ws);
		return IRQ_HANDLED;
	}

	pr_info("%s: interrupt source(0x%x), status(0x%x)\n",
		__func__, irq_src_h << 8 | irq_src_l, status_h << 8 | status_l);

	if (irq_src_h & MFC_INTA_H_AC_MISSING_DET_MASK) {
		pr_info("%s: 1AC Missing ! : MFC is on\n", __func__);
		mfc_mst_routine(charger, irq_src_l, irq_src_h);
	}

	if (irq_src_l & MFC_INTA_L_OP_MODE_MASK) {
		pr_info("%s: MODE CHANGE IRQ !\n", __func__);
		if (charger->wc_tx_enable) {
			__pm_stay_awake(charger->mode_change_ws);
			queue_delayed_work(charger->wqueue, &charger->mode_change_work, 0);
		}
	}

	if ((irq_src_l & MFC_INTA_L_OVER_VOL_MASK) || (irq_src_l & MFC_INTA_L_OVER_CURR_MASK))
		pr_info("%s: ABNORMAL STAT IRQ !\n", __func__);

	if (irq_src_l & MFC_INTA_L_OVER_TEMP_MASK) {
		pr_info("%s: OVER TEMP IRQ !\n", __func__);
		if (charger->wc_tx_enable) {
			value.intval = BATT_TX_EVENT_WIRELESS_RX_UNSAFE_TEMP;
			end_irq = true;
			goto INT_END;
		}
	}

	if (irq_src_l & MFC_INTA_L_STAT_VRECT_MASK) {
		pr_info("%s: Vrect IRQ !\n", __func__);
		if (charger->is_mst_on == MST_MODE_2 || charger->wc_tx_enable) {
			pr_info("%s: Noise made false Vrect IRQ!\n", __func__);
			if (charger->wc_tx_enable) {
				value.intval = BATT_TX_EVENT_WIRELESS_TX_ETC;
				end_irq = true;
				goto INT_END;
			}
		}
	}

	if (irq_src_l & MFC_INTA_L_TXCONFLICT_MASK) {
		pr_info("@Tx_Mode %s: TXCONFLICT IRQ !\n", __func__);
		if (charger->wc_tx_enable && (status_l & MFC_STAT_L_TXCONFLICT_MASK)) {
			value.intval = BATT_TX_EVENT_WIRELESS_TX_ETC;
			end_irq = true;
			goto INT_END;
		}
	}

	if (irq_src_h & MFC_INTA_H_TRX_DATA_RECEIVED_MASK) {
		pr_info("%s: TX RECEIVED IRQ !\n", __func__);
		if (charger->wc_tx_enable && !delayed_work_pending(&charger->wpc_tx_isr_work)) {
			__pm_stay_awake(charger->wpc_tx_ws);
			queue_delayed_work(charger->wqueue, &charger->wpc_tx_isr_work, 0);
		} else if (charger->pdata->cable_type == SEC_BATTERY_CABLE_WIRELESS_STAND ||
			charger->pdata->cable_type == SEC_BATTERY_CABLE_WIRELESS_HV_STAND) {
			pr_info("%s: Don't run ISR_WORK for NO ACK !\n", __func__);
		} else if (!delayed_work_pending(&charger->wpc_isr_work)) {
			__pm_stay_awake(charger->wpc_rx_ws);
			queue_delayed_work(charger->wqueue, &charger->wpc_isr_work, 0);
		}
	}

	if (irq_src_h & MFC_INTA_H_TX_CON_DISCON_MASK) {
		if (charger->wc_tx_enable) {
			pr_info("@Tx_Mode %s: TX CONNECT IRQ !\n", __func__);
			charger->tx_status = SEC_TX_POWER_TRANSFER;
			if (status_h & MFC_STAT_H_TX_CON_DISCON_MASK) {
				/* determine rx connection status with tx sharing mode */
				if (!charger->wc_rx_connected) {
					charger->wc_rx_connected = true;
					queue_delayed_work(charger->wqueue, &charger->wpc_rx_connection_work, 0);
				}
			} else {
				/* determine rx connection status with tx sharing mode */
				if (!charger->wc_rx_connected) {
					pr_info("@Tx_Mode %s: Ignore IRQ!! already Rx disconnected!\n", __func__);
				} else {
					charger->wc_rx_connected = false;
					queue_delayed_work(charger->wqueue, &charger->wpc_rx_connection_work, 0);
				}
			}
		} else {
			if (status_l & MFC_INTA_L_STAT_VRECT_MASK)
				pr_info("%s: VRECT SIGNAL !\n", __func__);
		}
	}

	/* when rx is not detacted in 3 mins */
	if (irq_src_h & MFC_INTA_H_TX_MODE_RX_NOT_DET_MASK) {
		if (!(irq_src_h & MFC_INTA_H_TX_CON_DISCON_MASK)) {
			pr_info("@Tx_Mode %s: Receiver NOT DETECTED IRQ !\n", __func__);
			if (charger->wc_tx_enable) {
				value.intval = BATT_TX_EVENT_WIRELESS_TX_ETC;
				end_irq = true;
				goto INT_END;
			}
		}
	}

	if (irq_src_h & MFC_STAT_H_ADT_RECEIVED_MASK) {
		pr_info("%s %s: ADT Received IRQ !\n", WC_AUTH_MSG, __func__);
		mfc_auth_adt_read(charger, ADT_buffer_rdata);
	}

	if (irq_src_h & MFC_STAT_H_ADT_SENT_MASK) {
		pr_info("%s %s: ADT Sent successful IRQ !\n", WC_AUTH_MSG, __func__);
		mfc_reg_update(charger->client, MFC_INT_A_ENABLE_H_REG, 0, MFC_STAT_H_ADT_SENT_MASK);
	}

	if (irq_src_h & MFC_INTA_H_TX_FOD_MASK) {
		pr_info("%s: TX FOD IRQ !\n", __func__);
		// this code will move to wpc_tx_isr_work
		if (charger->wc_tx_enable) {
			if (status_h & MFC_STAT_H_TX_FOD_MASK) {
				charger->wc_rx_fod = true;
				value.intval = BATT_TX_EVENT_WIRELESS_TX_FOD;
				end_irq = true;
				goto INT_END;
			}
		}
	}

	if (irq_src_h & MFC_INTA_H_TX_OCP_MASK)
		pr_info("%s: TX Over Current IRQ !\n", __func__);

INT_END:
	ret = mfc_reg_write(charger->client, MFC_INT_A_CLEAR_L_REG, irq_src_l); // clear int
	ret = mfc_reg_write(charger->client, MFC_INT_A_CLEAR_H_REG, irq_src_h); // clear int
	mfc_set_cmd_l_reg(charger, MFC_CMD_CLEAR_INT_MASK, MFC_CMD_CLEAR_INT_MASK); // command
	ret = mfc_reg_read(charger->client, MFC_STATUS_L_REG, &status_l);
	ret = mfc_reg_read(charger->client, MFC_STATUS_H_REG, &status_h);
	pr_info("%s: status after clear irq, status(0x%x)\n", __func__, status_h << 8 | status_l);

	/* tx off should work having done i2c */
	if (end_irq)
		psy_do_property("wireless", set, POWER_SUPPLY_EXT_PROP_WIRELESS_TX_ERR, value);

	pr_info("%s: end!\n", __func__);

	__pm_relax(charger->wpc_ws);

	return IRQ_HANDLED;
}

static void mfc_chg_parse_fod_data(struct device_node *np,
		mfc_charger_platform_data_t *pdata)
{
	const char *fod_state_string[FOD_STATE_MAX] = {"cc", "cv", "full", "hv"};
	struct device_node *child = NULL;
	int ret = 0, idx = 0, i = 0;

	np = of_find_node_by_name(np, "fod_list");
	if (!np) {
		pr_err("%s: fod list is NULL!\n", __func__);
		return;
	}

	ret = of_property_read_u32(np, "count", &pdata->fod_data_count);
	if (ret < 0) {
		pr_err("%s: fod data size is NULL!\n", __func__);
		return;
	}

	pdata->fod_list = kcalloc(pdata->fod_data_count, sizeof(struct mfc_fod_data), GFP_KERNEL);
	if (pdata->fod_list == NULL) {
		pdata->fod_data_count = 0;
		return;
	}

	for_each_child_of_node(np, child) {
		struct mfc_fod_data *pfod_data = NULL;

		if (idx >= pdata->fod_data_count) {
			pr_err("%s: skip set fod data because of overflow\n", __func__);
			break;
		}
		pfod_data = &pdata->fod_list[idx++];

		if (sscanf(child->name, "pad_0x%X", &pfod_data->pad_id) < 0) {
			pr_err("%s: failed to get pad id of %s\n", __func__, child->name);
			continue;
		}

		ret = of_property_read_u32(child, "flag", &pfod_data->flag);
		if (ret < 0) {
			pr_err("%s: failed to get flag of %s\n", __func__, child->name);
			continue;
		}

		for (i = 0; i < FOD_STATE_MAX; i++) {
			const u32 *p;
			u32 *pfod = NULL;
			int len = 0;

			switch ((pfod_data->flag >> (i * 4)) & 0xF) {
			case FOD_FLAG_ADD:
				p = of_get_property(child, fod_state_string[i], &len);
				if (!p) {
					pr_err("%s: %s - %s is null!!\n", __func__, child->name, fod_state_string[i]);
					break;
				}

				pfod = kcalloc(MFC_NUM_FOD_REG, sizeof(u32), GFP_KERNEL);
				if (pfod == NULL)
					break;

				len = (len / sizeof(u32));
				ret = of_property_read_u32_array(child, fod_state_string[i], pfod, len);
				if (ret < 0) {
					pr_err("%s: failed to get fod data(%s - %s)\n",
							__func__, child->name, fod_state_string[i]);
					break;
				}
				pfod_data->data[i] = pfod;
				break;
			case FOD_FLAG_USE_CC:
				if (pfod_data->data[FOD_STATE_CC] == NULL) {
					pr_err("%s: failed to get cc fod data(%s - %s)\n",
							__func__, child->name, fod_state_string[i]);
					break;
				}
				pfod_data->data[i] = pfod_data->data[FOD_STATE_CC];
				break;
			case FOD_FLAG_USE_DEFAULT:
				if ((pdata->fod_list[DEFAULT_PAD_ID].pad_id != 0) ||
					(pdata->fod_list[DEFAULT_PAD_ID].data[i] == NULL)) {
					pr_err("%s: failed to get default fod data(%s - %s)\n",
							__func__, child->name, fod_state_string[i]);
					break;
				}
				pfod_data->data[i] = pdata->fod_list[DEFAULT_PAD_ID].data[i];
				break;
			case FOD_FLAG_NONE:
			default:
				pr_info("%s: %s - %s is not set.", __func__, child->name, fod_state_string[i]);
				break;
			}
		}
	}

	for (idx = 0; idx < pdata->fod_data_count; idx++) {
		struct mfc_fod_data *pfod_data = &pdata->fod_list[idx];

		pr_info("%s: PAD_ID = 0x%X, FLAG=0x%X\n", __func__, pfod_data->pad_id, pfod_data->flag);
		for (i = 0; i < FOD_STATE_MAX; i++) {
			if (pfod_data->data[i] != NULL) {
				char temp_buf[1024] = {0, };
				int j = 0, size = 1024;

				for (j = 0; j < MFC_NUM_FOD_REG; j++) {
					snprintf(temp_buf + strlen(temp_buf), size, "%d ", pfod_data->data[i][j]);
					size = sizeof(temp_buf) - strlen(temp_buf);
				}
				pr_info("%s: %s - %s\n", __func__, fod_state_string[i], temp_buf);
			} else {
				pr_info("%s: %s is null\n", __func__, fod_state_string[i]);
			}
		}
	}
}

static void mfc_chg_parse_iec_data(struct device_node *np, mfc_charger_platform_data_t *pdata)
{
	int ret = 0;

	ret = of_property_read_u32(np, "battery,wireless20_iec_qfod_enable", &pdata->iec_qfod_enable);
	if (ret < 0) {
		pr_info("%s: fail to read iec_qfod_enable\n", __func__);
		pdata->iec_qfod_enable = 0x00;
	} else {
		pr_info("%s: wireless20_iec_qfod_enable = %d\n", __func__, pdata->iec_qfod_enable);
	}

	ret = of_property_read_u32(np, "battery,wireless20_iec_q_thresh_1", &pdata->iec_q_thresh_1);
	if (ret < 0) {
		pr_info("%s: fail to read iec_q_thresh_1\n", __func__);
		pdata->iec_q_thresh_1 = 0x46;
	} else {
		pr_info("%s: wireless20_iec_q_thresh_1 = %d\n", __func__, pdata->iec_q_thresh_1);
	}

	ret = of_property_read_u32(np, "battery,wireless20_iec_q_thresh_2", &pdata->iec_q_thresh_2);
	if (ret < 0) {
		pr_info("%s: fail to read iec_q_thresh_2\n", __func__);
		pdata->iec_q_thresh_2 = 0x3c;
	} else {
		pr_info("%s: wireless20_iec_q_thresh_2 = %d\n", __func__, pdata->iec_q_thresh_2);
	}

	ret = of_property_read_u32(np, "battery,wireless20_iec_fres_thresh_1", &pdata->iec_fres_thresh_1);
	if (ret < 0) {
		pr_info("%s: fail to read iec_fres_thresh_1\n", __func__);
		pdata->iec_fres_thresh_1 = 0x47;
	} else {
		pr_info("%s: wireless20_iec_fres_thresh_1 = %d\n", __func__, pdata->iec_fres_thresh_1);
	}

	ret = of_property_read_u32(np, "battery,wireless20_iec_fres_thresh_2", &pdata->iec_fres_thresh_2);
	if (ret < 0) {
		pr_info("%s: fail to read iec_fres_thresh_2\n", __func__);
		pdata->iec_fres_thresh_2 = 0x55;
	} else {
		pr_info("%s: wireless20_iec_fres_thresh_2 = %d\n", __func__, pdata->iec_fres_thresh_2);
	}

	ret = of_property_read_u32(np, "battery,wireless20_iec_power_limit_thresh", &pdata->iec_power_limit_thresh);
	if (ret < 0) {
		pr_info("%s: fail to read iec_power_limit_thresh\n", __func__);
		pdata->iec_power_limit_thresh = 0x190;
	} else {
		pr_info("%s: wireless20_iec_power_limit_thresh = %d\n", __func__, pdata->iec_power_limit_thresh);
	}

	ret = of_property_read_u32(np, "battery,wireless20_iec_ploss_thresh_1", &pdata->iec_ploss_thresh_1);
	if (ret < 0) {
		pr_info("%s: fail to read iec_ploss_thresh_1\n", __func__);
		pdata->iec_ploss_thresh_1 = 0x384;
	} else {
		pr_info("%s: wireless20_iec_ploss_thresh_1 = %d\n", __func__, pdata->iec_ploss_thresh_1);
	}

	ret = of_property_read_u32(np, "battery,wireless20_iec_ploss_thresh_2", &pdata->iec_ploss_thresh_2);
	if (ret < 0) {
		pr_info("%s: fail to read iec_ploss_thresh_2\n", __func__);
		pdata->iec_ploss_thresh_2 = 0x384;
	} else {
		pr_info("%s: wireless20_iec_ploss_thresh_2 = %d\n", __func__, pdata->iec_ploss_thresh_2);
	}

	ret = of_property_read_u32(np, "battery,wireless20_iec_ploss_freq_thresh_1", &pdata->iec_ploss_freq_thresh_1);
	if (ret < 0) {
		pr_info("%s: fail to read iec_ploss_freq_thresh_1\n", __func__);
		pdata->iec_ploss_freq_thresh_1 = 0xff;
	} else {
		pr_info("%s: wireless20_iec_ploss_freq_thresh_1 = %d\n", __func__, pdata->iec_ploss_freq_thresh_1);
	}

	ret = of_property_read_u32(np, "battery,wireless20_iec_ploss_freq_thresh_2", &pdata->iec_ploss_freq_thresh_2);
	if (ret < 0) {
		pr_info("%s: fail to read iec_ploss_freq_thresh_2\n", __func__);
		pdata->iec_ploss_freq_thresh_2 = 0xff;
	} else {
		pr_info("%s: wireless20_iec_ploss_freq_thresh_2 = %d\n", __func__, pdata->iec_ploss_freq_thresh_2);
	}

	ret = of_property_read_u32(np, "battery,wireless20_iec_ta_power_limit_thresh", &pdata->iec_ta_power_limit_thresh);
	if (ret < 0) {
		pr_info("%s: fail to read iec_ta_power_limit_thresh\n", __func__);
		pdata->iec_ta_power_limit_thresh = 0x258;
	} else {
		pr_info("%s: wireless20_iec_ta_power_limit_thresh = %d\n", __func__, pdata->iec_ta_power_limit_thresh);
	}

	ret = of_property_read_u32(np, "battery,wireless20_iec_ta_ploss_thresh_1", &pdata->iec_ta_ploss_thresh_1);
	if (ret < 0) {
		pr_info("%s: fail to read iec_ta_ploss_thresh_1\n", __func__);
		pdata->iec_ta_ploss_thresh_1 = 0x4b0;
	} else {
		pr_info("%s: wireless20_iec_ta_ploss_thresh_1 = %d\n", __func__, pdata->iec_ta_ploss_thresh_1);
	}

	ret = of_property_read_u32(np, "battery,wireless20_iec_ta_ploss_thresh_2", &pdata->iec_ta_ploss_thresh_2);
	if (ret < 0) {
		pr_info("%s: fail to read iec_ta_ploss_thresh_2\n", __func__);
		pdata->iec_ta_ploss_thresh_2 = 0x4b0;
	} else {
		pr_info("%s: wireless20_iec_ta_ploss_thresh_2 = %d\n", __func__, pdata->iec_ta_ploss_thresh_2);
	}

	ret = of_property_read_u32(np, "battery,wireless20_iec_ta_ploss_freq_thresh_1", &pdata->iec_ta_ploss_freq_thresh_1);
	if (ret < 0) {
		pr_info("%s: fail to read iec_ta_ploss_freq_thresh_1\n", __func__);
		pdata->iec_ta_ploss_freq_thresh_1 = 0xff;
	} else {
		pr_info("%s: wireless20_iec_ta_ploss_freq_thresh_1 = %d\n", __func__, pdata->iec_ta_ploss_freq_thresh_1);
	}

	ret = of_property_read_u32(np, "battery,wireless20_iec_ta_ploss_freq_thresh_2", &pdata->iec_ta_ploss_freq_thresh_2);
	if (ret < 0) {
		pr_info("%s: fail to read iec_ta_ploss_freq_thresh_2\n", __func__);
		pdata->iec_ta_ploss_freq_thresh_2 = 0xff;
	} else {
		pr_info("%s: wireless20_iec_ta_ploss_freq_thresh_2 = %d\n", __func__, pdata->iec_ta_ploss_freq_thresh_2);
	}

	ret = of_property_read_u32(np, "battery,wireless20_iec_ploss_fod_enable", &pdata->iec_ploss_fod_enable);
	if (ret < 0) {
		pr_info("%s: fail to read iec_ploss_fod_enable\n", __func__);
		pdata->iec_ploss_fod_enable = 0x0;
	} else {
		pr_info("%s: wireless20_iec_ploss_fod_enable = %d\n", __func__, pdata->iec_ploss_fod_enable);
	}

	ret = of_property_read_u32(np, "battery,wireless20_iec_pwm_duty_thresh", &pdata->iec_pwm_duty_thresh);
	if (ret < 0) {
		pr_info("%s: fail to read iec_pwm_duty_thresh\n", __func__);
		pdata->iec_ploss_fod_enable = 0x0;
	} else {
		pr_info("%s: wireless20_iec_pwm_duty_thresh = %d\n", __func__, pdata->iec_pwm_duty_thresh);
	}

	ret = of_property_read_u32(np, "battery,wireless20_iec_ta_pwm_duty_thresh", &pdata->iec_ta_pwm_duty_thresh);
	if (ret < 0) {
		pr_info("%s: fail to read iec_ta_pwm_duty_thresh\n", __func__);
		pdata->iec_ta_pwm_duty_thresh = 0x0;
	} else {
		pr_info("%s: wireless20_iec_ta_pwm_duty_thresh = %d\n", __func__, pdata->iec_ta_pwm_duty_thresh);
	}

}

#if defined(CONFIG_WIRELESS_IC_PARAM)
static void mfc_parse_param_value(struct mfc_charger_data *charger)
{
	unsigned int param_value = mfc_get_wrlic();

	charger->wireless_param_info = param_value;
	charger->wireless_chip_id_param = (param_value & 0xFF000000) >> 24;
	charger->wireless_fw_ver_param = (param_value & 0x00FFFF00) >> 8;
	charger->wireless_fw_mode_param = (param_value & 0x000000F0) >> 4;

	pr_info("%s: wireless_ic(0x%08X), chip_id(0x%02X), fw_ver(0x%04X), fw_mode(0x%01X)\n",
		__func__, param_value, charger->wireless_chip_id_param,
		charger->wireless_fw_ver_param, charger->wireless_fw_mode_param);
}
#endif

static int mfc_chg_parse_dt(struct device *dev,
		mfc_charger_platform_data_t *pdata)
{
	int ret = 0;
	struct device_node *np = dev->of_node;
	enum of_gpio_flags irq_gpio_flags;
	int len, i;
	const u32 *p;

	if (!np) {
		pr_err("%s: np NULL\n", __func__);
		return 1;
	}
	ret = of_property_read_string(np,
		"battery,wireless_charger_name", (char const **)&pdata->wireless_charger_name);
	if (ret < 0)
		pr_info("%s: Wireless Charger name is Empty\n", __func__);

	ret = of_property_read_string(np,
		"battery,charger_name", (char const **)&pdata->wired_charger_name);
	if (ret < 0)
		pr_info("%s: Charger name is Empty\n", __func__);

	ret = of_property_read_string(np,
		"battery,fuelgauge_name", (char const **)&pdata->fuelgauge_name);
	if (ret < 0)
		pr_info("%s: Fuelgauge name is Empty\n", __func__);

	ret = of_property_read_u32(np, "battery,phone_fod_thresh1",
					&pdata->phone_fod_thresh1);
	if (ret < 0) {
		pr_info("%s: fail to read phone_fod_thresh1\n", __func__);
		pdata->phone_fod_thresh1 = 0x07D0; /* default 2000mW */
	}

	ret = of_property_read_u32(np, "battery,buds_fod_thresh1",
					&pdata->buds_fod_thresh1);
	if (ret < 0) {
		pr_info("%s: fail to read buds_fod_thresh1\n", __func__);
		pdata->buds_fod_thresh1 = 0x07D0; /* default 2000mW */
	}

	ret = of_property_read_u32(np, "battery,buds_fod_ta_thresh",
					&pdata->buds_fod_ta_thresh);
	if (ret < 0) {
		pr_info("%s: fail to read buds_fod_ta_thresh\n", __func__);
		pdata->buds_fod_ta_thresh = 0x0320;  /* default 800mW */
	}

	ret = of_property_read_u32(np, "battery,cep_timeout",
					&pdata->cep_timeout);
	if (ret < 0) {
		pr_info("%s: fail to read cep_timeout\n", __func__);
		pdata->cep_timeout = 0;
	}

	ret = of_property_read_u32(np, "battery,tx_conflict_curr",
					&pdata->tx_conflict_curr);
	if (ret < 0) {
		pr_info("%s: fail to read tx_conflict_curr\n", __func__);
		pdata->tx_conflict_curr = 1200;
	}

	/* wpc_det */
	ret = pdata->wpc_det = of_get_named_gpio_flags(np, "battery,wpc_det",
			0, &irq_gpio_flags);
	if (ret < 0) {
		dev_err(dev, "%s: can't get wpc_det\r\n", __func__);
	} else {
		pdata->irq_wpc_det = gpio_to_irq(pdata->wpc_det);
		pr_info("%s: wpc_det = 0x%x, irq_wpc_det = 0x%x\n",
			__func__, pdata->wpc_det, pdata->irq_wpc_det);
	}
	/* wpc_int (This GPIO means MFC_AP_INT) */
	ret = pdata->wpc_int = of_get_named_gpio_flags(np, "battery,wpc_int",
			0, &irq_gpio_flags);
	if (ret < 0) {
		dev_err(dev, "%s: can't wpc_int\r\n", __func__);
	} else {
		pdata->irq_wpc_int = gpio_to_irq(pdata->wpc_int);
		pr_info("%s: wpc_int = 0x%x, irq_wpc_int = 0x%x\n",
			__func__, pdata->wpc_int, pdata->irq_wpc_int);
	}

	/* mst_pwr_en (MST PWR EN) */
	ret = pdata->mst_pwr_en = of_get_named_gpio_flags(np, "battery,mst_pwr_en",
			0, &irq_gpio_flags);
	if (ret < 0)
		dev_err(dev, "%s: can't mst_pwr_en\r\n", __func__);

	/* wpc_pdrc (This GPIO means VRECT_INT) */
	ret = pdata->wpc_pdrc = of_get_named_gpio_flags(np, "battery,wpc_pdrc",
			0, &irq_gpio_flags);
	if (ret < 0) {
		dev_err(dev, "%s : can't wpc_pdrc\r\n", __func__);
	} else {
		pdata->irq_wpc_pdrc = gpio_to_irq(pdata->wpc_pdrc);
		pr_info("%s wpc_pdrc = 0x%x, irq_wpc_pdrc = 0x%x\n",
			__func__, pdata->wpc_pdrc, pdata->irq_wpc_pdrc);
	}

	/* wpc_pdet_b (PDET_B) */
	ret = pdata->wpc_pdet_b = of_get_named_gpio_flags(np, "battery,wpc_pdet_b",
			0, &irq_gpio_flags);
	if (ret < 0) {
		dev_err(dev, "%s : can't wpc_pdet_b\r\n", __func__);
	} else {
		pdata->irq_wpc_pdet_b = gpio_to_irq(pdata->wpc_pdet_b);
		pr_info("%s wpc_pdet_b = 0x%x, irq_wpc_pdet_b = 0x%x\n",
			__func__, pdata->wpc_pdet_b, pdata->irq_wpc_pdet_b);
	}

	/* wpc_en (MFC EN) */
	ret = pdata->wpc_en = of_get_named_gpio_flags(np, "battery,wpc_en",
			0, &irq_gpio_flags);
	if (ret < 0)
		dev_err(dev, "%s: can't wpc_en\r\n", __func__);

	/* coil_sw_en (COIL SW EN N) */
	ret = pdata->coil_sw_en = of_get_named_gpio_flags(np, "battery,coil_sw_en",
			0, &irq_gpio_flags);
	if (ret < 0)
		dev_err(dev, "%s: can't coil_sw_en\r\n", __func__);

	/* ping_nen (PING nEN) */
	ret = pdata->ping_nen = of_get_named_gpio_flags(np, "battery,wpc_ping_nen",
			0, &irq_gpio_flags);
	if (ret < 0)
		dev_err(dev, "%s : can't wpc_ping_nen\r\n", __func__);

	p = of_get_property(np, "battery,wireless20_vout_list", &len);
	if (p) {
		len = len / sizeof(u32);
		pdata->len_wc20_list = len;
		pdata->wireless20_vout_list = kcalloc(len, sizeof(*pdata->wireless20_vout_list), GFP_KERNEL);
		ret = of_property_read_u32_array(np, "battery,wireless20_vout_list",
			pdata->wireless20_vout_list, len);

		for (i = 0; i < len; i++)
			pr_info("%s: wireless20_vout_list = %d ", __func__, pdata->wireless20_vout_list[i]);
		pr_info("%s: len_wc20_list = %d ", __func__, pdata->len_wc20_list);
	} else {
		pr_err("%s: there is no wireless20_vout_list\n", __func__);
	}

	p = of_get_property(np, "battery,wireless20_vrect_list", &len);
	if (p) {
		len = len / sizeof(u32);
		pdata->wireless20_vrect_list =
			kcalloc(len, sizeof(*pdata->wireless20_vrect_list), GFP_KERNEL);
		ret = of_property_read_u32_array(np, "battery,wireless20_vrect_list",
			pdata->wireless20_vrect_list, len);

		for (i = 0; i < len; i++)
			pr_info("%s: wireless20_vrect_list = %d ", __func__, pdata->wireless20_vrect_list[i]);
	} else {
		pr_err("%s: there is no wireless20_vrect_list\n", __func__);
	}

	p = of_get_property(np, "battery,wireless20_max_power_list", &len);
	if (p) {
		len = len / sizeof(u32);
		pdata->wireless20_max_power_list =
			kcalloc(len, sizeof(*pdata->wireless20_max_power_list), GFP_KERNEL);
		ret = of_property_read_u32_array(np, "battery,wireless20_max_power_list",
				pdata->wireless20_max_power_list, len);

		for (i = 0; i < len; i++)
			pr_info("%s: wireless20_max_power_list = %d\n",
				__func__, pdata->wireless20_max_power_list[i]);
	} else {
		pr_err("%s: there is no wireless20_max_power_list\n", __func__);
	}
	pdata->no_hv =
	    of_property_read_bool(np, "battery,wireless_no_hv");

	ret = of_property_read_u32(np, "battery,wpc_vout_ctrl_full",
			&pdata->wpc_vout_ctrl_full);
	if (ret)
		pr_err("%s: wpc_vout_ctrl_full is Empty\n", __func__);

	if (pdata->wpc_vout_ctrl_full)
		pdata->wpc_headroom_ctrl_full = of_property_read_bool(np, "battery,wpc_headroom_ctrl_full");

	pdata->mis_align_guide = of_property_read_bool(np, "battery,mis_align_guide");
	if (pdata->mis_align_guide) {
		ret = of_property_read_u32(np, "battery,mis_align_target_vout",
						&pdata->mis_align_target_vout);
		if (ret)
			pdata->mis_align_target_vout = 5000;

		ret = of_property_read_u32(np, "battery,mis_align_offset",
					&pdata->mis_align_offset);
		if (ret)
			pdata->mis_align_offset = 200;
		pr_info("%s: mis_align_guide, vout:%d, offset:%d\n", __func__,
			pdata->mis_align_target_vout, pdata->mis_align_offset);
	}
	pdata->unknown_cmb_ctrl = of_property_read_bool(np, "battery,unknown_cmb_ctrl");
	pdata->default_clamp_volt = of_property_read_bool(np, "battery,default_clamp_volt");
	if (pdata->default_clamp_volt)
		pr_info("%s: default_clamp_volt(%d)\n", __func__, pdata->default_clamp_volt);

#if defined(CONFIG_MST_PCR)
	ret = of_property_read_u32(np, "battery,mst_iset_pcr",
					&pdata->mst_iset_pcr);
	if (ret < 0) {
		pr_info("%s: fail to read mst_iset_pcr. default set 2.8A(0x06)\n", __func__);
		pdata->mst_iset_pcr = 0x06;  /* 2.8A */
	}
#endif

	ret = of_property_read_u32(np, "battery,fw_ver",
						&pdata->fw_ver);
	if (ret < 0)
		pdata->fw_ver = MFC_FW_BIN_VERSION;
	pr_info("%s: fw_ver(0x%x)\n", __func__, pdata->fw_ver);

	mfc_chg_parse_fod_data(np, pdata);
	mfc_chg_parse_iec_data(np, pdata);
	np = dev->of_node;

	return 0;
}

static int mfc_create_attrs(struct device *dev)
{
	int i, rc;

	for (i = 0; i < (int)ARRAY_SIZE(mfc_attrs); i++) {
		rc = device_create_file(dev, &mfc_attrs[i]);
		if (rc)
			goto create_attrs_failed;
	}
	return rc;

create_attrs_failed:
	dev_err(dev, "%s: failed (%d)\n", __func__, rc);
	while (i--)
		device_remove_file(dev, &mfc_attrs[i]);
	return rc;
}

ssize_t cps4038_show_attrs(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct mfc_charger_data *charger = power_supply_get_drvdata(psy);

	const ptrdiff_t offset = attr - mfc_attrs;
	int i = 0;

	dev_info(charger->dev, "%s\n", __func__);

	switch (offset) {
	case MFC_ADDR:
		i += scnprintf(buf + i, PAGE_SIZE - i, "0x%x\n", charger->addr);
		break;
	case MFC_SIZE:
		i += scnprintf(buf + i, PAGE_SIZE - i, "0x%x\n", charger->size);
		break;
	case MFC_DATA:
		if (charger->size == 0) {
			charger->size = 1;
		} else if (charger->size + charger->addr <= 0xFFFF) {
			u8 data;
			int j;

			for (j = 0; j < charger->size; j++) {
				if (mfc_reg_read(charger->client, charger->addr + j, &data) < 0) {
					dev_info(charger->dev, "%s: read fail\n", __func__);
					i += scnprintf(buf + i, PAGE_SIZE - i, "addr: 0x%x read fail\n",
						charger->addr + j);
					continue;
				}
				i += scnprintf(buf + i, PAGE_SIZE - i, "addr: 0x%x, data: 0x%x\n",
					charger->addr + j, data);
			}
		}
		break;
	case MFC_PACKET:
		break;
	default:
		return -EINVAL;
	}
	return i;
}

ssize_t cps4038_store_attrs(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct mfc_charger_data *charger = power_supply_get_drvdata(psy);
	const ptrdiff_t offset = attr - mfc_attrs;
	int x, ret;
	u8 header, data_com, data_val;

	dev_info(charger->dev, "%s\n", __func__);

	switch (offset) {
	case MFC_ADDR:
		if (sscanf(buf, "0x%4x\n", &x) == 1)
			charger->addr = x;
		ret = count;
		break;
	case MFC_SIZE:
		if (sscanf(buf, "%5d\n", &x) == 1)
			charger->size = x;
		ret = count;
		break;
	case MFC_DATA:
		if (sscanf(buf, "0x%10x", &x) == 1) {
			u8 data = x;

			if (mfc_reg_write(charger->client, charger->addr, data) < 0)
				dev_info(charger->dev, "%s: addr: 0x%x write fail\n", __func__, charger->addr);
		}
		ret = count;
		break;
	case MFC_PACKET:
	{
		u32 header_temp, data_com_temp, data_val_temp;

		if (sscanf(buf, "0x%4x 0x%4x 0x%4x\n", &header_temp, &data_com_temp, &data_val_temp) == 3) {
			header = (u8)header_temp;
			data_com = (u8)data_com_temp;
			data_val = (u8)data_val_temp;
			dev_info(charger->dev, "%s 0x%x, 0x%x, 0x%x\n", __func__, header, data_com, data_val);
			mfc_send_packet(charger, header, data_com, &data_val, 1);
		}
		ret = count;
	}
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static const struct power_supply_desc mfc_charger_power_supply_desc = {
	.name = "mfc-charger",
	.type = POWER_SUPPLY_TYPE_UNKNOWN,
	.properties = mfc_charger_props,
	.num_properties = ARRAY_SIZE(mfc_charger_props),
	.get_property = cps4038_chg_get_property,
	.set_property = cps4038_chg_set_property,
};

static void mfc_wpc_int_req_work(struct work_struct *work)
{
	struct mfc_charger_data *charger =
		container_of(work, struct mfc_charger_data, wpc_int_req_work.work);

	int ret = 0;

	pr_info("%s\n", __func__);
	/* wpc_irq */
	if (charger->pdata->irq_wpc_int) {
		msleep(100);
		ret = request_threaded_irq(charger->pdata->irq_wpc_int,
				mfc_wpc_irq_handler, mfc_wpc_irq_thread,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				"wpc-irq", charger);
		if (ret)
			pr_err("%s: Failed to Request IRQ\n", __func__);
	}
	if (ret < 0)
		free_irq(charger->pdata->irq_wpc_det, NULL);
}

static enum alarmtimer_restart mfc_phm_alarm(
	struct alarm *alarm, ktime_t now)
{
	struct mfc_charger_data *charger = container_of(alarm,
				struct mfc_charger_data, phm_alarm);

	pr_info("%s: forced escape to PHM\n", __func__);
	__pm_stay_awake(charger->wpc_tx_phm_ws);
	if (charger->is_suspend)
		charger->skip_phm_work_in_sleep = true;
	else
		queue_delayed_work(charger->wqueue, &charger->wpc_tx_phm_work, 0);

	return ALARMTIMER_NORESTART;
}

static int cps4038_charger_probe(
						struct i2c_client *client,
						const struct i2c_device_id *id)
{
	struct device_node *of_node = client->dev.of_node;
	struct mfc_charger_data *charger;
	mfc_charger_platform_data_t *pdata = client->dev.platform_data;
	struct power_supply_config mfc_cfg = {};
	int ret = 0;
	u8 int_state;

	dev_info(&client->dev,
		"%s: MFC cps4038 Charger Driver Loading\n", __func__);

	if (of_node) {
		pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		ret = mfc_chg_parse_dt(&client->dev, pdata);
		if (ret < 0)
			goto err_parse_dt;
	} else {
		pdata = client->dev.platform_data;
		return -ENOMEM;
	}

	charger = kcalloc(1, sizeof(*charger), GFP_KERNEL);
	if (charger == NULL) {
		ret = -ENOMEM;
		goto err_wpc_nomem;
	}
	charger->dev = &client->dev;

	ret = i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA |
		I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_I2C_BLOCK);
	if (!ret) {
		ret = i2c_get_functionality(client->adapter);
		dev_err(charger->dev, "I2C functionality is not supported.\n");
		ret = -ENODEV;
		goto err_i2cfunc_not_support;
	}

	is_shutdn = false;
	charger->client = client;
	charger->pdata = pdata;

	pr_info("%s: %s\n", __func__, charger->pdata->wireless_charger_name);

	i2c_set_clientdata(client, charger);
#if defined(CONFIG_WIRELESS_IC_PARAM)
	mfc_parse_param_value(charger);
#endif

	charger->pdata->cable_type = SEC_BATTERY_CABLE_NONE;
	charger->pdata->is_charging = 0;
	charger->tx_status = 0;
	charger->pdata->cs100_status = 0;
	charger->pdata->capacity = 101;
	charger->pdata->vout_status = MFC_VOUT_5V;
	charger->pdata->opfq_cnt = 0;
	charger->flicker_delay = 1000;
	charger->flicker_vout_threshold = MFC_VOUT_8V;
	charger->is_mst_on = MST_MODE_0;
	charger->chip_id = MFC_CHIP_CPS;
	charger->is_otg_on = false;
	charger->led_cover = 0;
	charger->vout_mode = WIRELESS_VOUT_OFF;
	charger->is_full_status = 0;
	charger->is_afc_tx = false;
	charger->pad_ctrl_by_lcd = false;
	charger->wc_tx_enable = false;
	charger->initial_wc_check = false;
	charger->wc_rx_connected = false;
	charger->wc_rx_fod = false;
	charger->adt_transfer_status = WIRELESS_AUTH_WAIT;
	charger->current_rx_power = TX_RX_POWER_0W;
	charger->tx_id = TX_ID_UNKNOWN;
	charger->tx_id_cnt = 0;
	charger->wc_ldo_status = MFC_LDO_ON;
	charger->tx_id_done = false;
	charger->wc_rx_type = NO_DEV;
	charger->is_suspend = false;
	charger->device_event = 0;
	charger->duty_min = 20;
	charger->wpc_en_flag = (WPC_EN_SYSFS | WPC_EN_CHARGING | WPC_EN_CCIC | WPC_EN_SLATE);
	charger->req_tx_id = false;
	charger->is_abnormal_pad = false;
	charger->afc_tx_done = false;
	charger->sleep_mode = false;
	charger->req_afc_delay = REQ_AFC_DLY;
	charger->mis_align_tx_try_cnt = 1;
	charger->wc_checking_align = false;
	charger->wc_align_check_start.tv_sec = 0;
	charger->vout_strength = 100;
	charger->reg_access_lock = false;
	charger->det_state = 0;
	charger->pdrc_state = 1;

	mutex_init(&charger->io_lock);
	mutex_init(&charger->wpc_en_lock);
	mutex_init(&charger->fw_lock);

	charger->wqueue = create_singlethread_workqueue("mfc_workqueue");
	if (!charger->wqueue) {
		pr_err("%s: Fail to Create Workqueue\n", __func__);
		goto err_pdata_free;
	}

	charger->wpc_ws = wakeup_source_register(&client->dev, "wpc_ws");
	charger->wpc_det_ws = wakeup_source_register(&client->dev, "wpc_det_ws");
	charger->wpc_rx_ws = wakeup_source_register(&client->dev, "wpc_rx_ws");
	charger->wpc_tx_ws = wakeup_source_register(&client->dev, "wpc_tx_ws");
	charger->wpc_update_ws = wakeup_source_register(&client->dev, "wpc_update_ws");
	charger->wpc_afc_vout_ws = wakeup_source_register(&client->dev, "wpc_afc_vout_ws");
	charger->wpc_vout_mode_ws = wakeup_source_register(&client->dev, "wpc_vout_mode_ws");
	charger->wpc_rx_det_ws = wakeup_source_register(&client->dev, "wpc_rx_det_ws");
	charger->wpc_tx_phm_ws = wakeup_source_register(&client->dev, "wpc_tx_phm_ws");
	charger->wpc_tx_id_ws = wakeup_source_register(&client->dev, "wpc_tx_id_ws");
	charger->wpc_tx_pwr_budg_ws = wakeup_source_register(&client->dev, "wpc_tx_pwr_budg_ws");
	charger->wpc_pdrc_ws = wakeup_source_register(&client->dev, "wpc_pdrc_ws");
	charger->align_check_ws = wakeup_source_register(&client->dev, "align_check_ws");
	charger->mode_change_ws = wakeup_source_register(&client->dev, "mode_change_ws");
	charger->wpc_cs100_ws = wakeup_source_register(&client->dev, "wpc_cs100_ws");
	charger->wpc_check_rx_power_ws = wakeup_source_register(&client->dev, "wpc_check_rx_power_ws");

	/* wpc_det */
	if (charger->pdata->irq_wpc_det) {
		INIT_DELAYED_WORK(&charger->wpc_det_work, mfc_wpc_det_work);
	}

	/* wpc_irq (INT_A) */
	if (charger->pdata->irq_wpc_int) {
		INIT_DELAYED_WORK(&charger->wpc_isr_work, mfc_wpc_isr_work);
		INIT_DELAYED_WORK(&charger->wpc_tx_isr_work, mfc_wpc_tx_isr_work);
		INIT_DELAYED_WORK(&charger->wpc_tx_id_work, mfc_wpc_tx_id_work);
		INIT_DELAYED_WORK(&charger->wpc_tx_pwr_budg_work, mfc_wpc_tx_pwr_budg_work);
		INIT_DELAYED_WORK(&charger->wpc_int_req_work, mfc_wpc_int_req_work);
	}
	INIT_DELAYED_WORK(&charger->wpc_vout_mode_work, mfc_wpc_vout_mode_work);
	INIT_DELAYED_WORK(&charger->wpc_afc_vout_work, mfc_wpc_afc_vout_work);
	INIT_DELAYED_WORK(&charger->wpc_fw_update_work, mfc_wpc_fw_update_work);
	INIT_DELAYED_WORK(&charger->wpc_i2c_error_work, mfc_wpc_i2c_error_work);
	INIT_DELAYED_WORK(&charger->wpc_rx_type_det_work, mfc_wpc_rx_type_det_work);
	INIT_DELAYED_WORK(&charger->wpc_rx_connection_work, mfc_wpc_rx_connection_work);
	INIT_DELAYED_WORK(&charger->wpc_tx_phm_work, mfc_tx_phm_work);
	INIT_DELAYED_WORK(&charger->wpc_rx_power_work, mfc_wpc_rx_power_work);
	INIT_DELAYED_WORK(&charger->wpc_init_work, mfc_wpc_init_work);
	INIT_DELAYED_WORK(&charger->align_check_work, mfc_wpc_align_check_work);
	INIT_DELAYED_WORK(&charger->mode_change_work, mfc_wpc_mode_change_work);
	INIT_DELAYED_WORK(&charger->wpc_cs100_work, mfc_cs100_work);
	INIT_DELAYED_WORK(&charger->wpc_check_rx_power_work, mfc_check_rx_power_work);

	/*
	 * Default Idle voltage of the INT_A is LOW.
	 * Prevent the un-wanted INT_A Falling handling.
	 * This is a work-around, and will be fixed by the revision.
	 */
	//INIT_DELAYED_WORK(&charger->mst_off_work, mfc_mst_off_work);

	alarm_init(&charger->phm_alarm, ALARM_BOOTTIME, mfc_phm_alarm);

	mfc_cfg.drv_data = charger;
	charger->psy_chg = power_supply_register(charger->dev, &mfc_charger_power_supply_desc, &mfc_cfg);
	if (IS_ERR(charger->psy_chg)) {
		ret = PTR_ERR(charger->psy_chg);
		pr_err("%s: Failed to Register psy_chg(%d)\n", __func__, ret);
		goto err_supply_unreg;
	}

	ret = mfc_create_attrs(&charger->psy_chg->dev);
	if (ret) {
		dev_err(charger->dev,
			"%s : Failed to create_attrs\n", __func__);
	}

	/* Enable interrupts after battery driver load */
	/* wpc_det */
	if (charger->pdata->irq_wpc_det) {
		ret = request_threaded_irq(charger->pdata->irq_wpc_det,
				mfc_wpc_irq_handler, mfc_wpc_det_irq_thread,
				IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING |
				IRQF_ONESHOT,
				"wpd-det-irq", charger);
		if (ret) {
			pr_err("%s: Failed to Request IRQ\n", __func__);
			goto err_irq_wpc_det;
		}
	}

	/* wpc_pdrc */
	if (charger->pdata->irq_wpc_pdrc) {
		INIT_DELAYED_WORK(&charger->wpc_pdrc_work, mfc_wpc_pdrc_work);
		ret = request_threaded_irq(charger->pdata->irq_wpc_pdrc,
				mfc_wpc_irq_handler, mfc_wpc_pdrc_irq_thread,
				IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING |
				IRQF_ONESHOT,
				"wpd-pdrc-irq", charger);
		if (ret) {
			pr_err("%s: Failed to Request pdrc IRQ\n", __func__);
			goto err_irq_wpc_det;
		}
	}

	/* wpc_pdet_b */
	if (charger->pdata->irq_wpc_pdet_b) {
		ret = request_threaded_irq(charger->pdata->irq_wpc_pdet_b,
				NULL, mfc_wpc_pdet_b_irq_thread,
				IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING |
				IRQF_ONESHOT,
				"wpd-pdet-b-irq", charger);
		if (ret) {
			pr_err("%s: Failed to Request pdet_b IRQ\n", __func__);
			goto err_irq_wpc_det;
		}
	}

	/* wpc_irq */
	queue_delayed_work(charger->wqueue, &charger->wpc_int_req_work, msecs_to_jiffies(100));

	int_state = gpio_get_value(charger->pdata->wpc_int);
	pr_info("%s: int_state = %d\n", __func__, int_state);
	if (gpio_get_value(charger->pdata->wpc_det)) {
		u8 irq_src[2];

		pr_info("%s: Charger interrupt occurred during lpm\n", __func__);
		charger->det_state = gpio_get_value(charger->pdata->wpc_det);

		mfc_reg_read(charger->client, MFC_INT_A_L_REG, &irq_src[0]);
		mfc_reg_read(charger->client, MFC_INT_A_H_REG, &irq_src[1]);
		/* clear interrupt */
		pr_info("%s: interrupt source(0x%x)\n", __func__, irq_src[1] << 8 | irq_src[0]);
		mfc_reg_write(charger->client, MFC_INT_A_CLEAR_L_REG, irq_src[0]); // clear int
		mfc_reg_write(charger->client, MFC_INT_A_CLEAR_H_REG, irq_src[1]); // clear int
		mfc_set_cmd_l_reg(charger, MFC_CMD_CLEAR_INT_MASK, MFC_CMD_CLEAR_INT_MASK); // command
		if (charger->pdata->wired_charger_name) {
			union power_supply_propval value;

			ret = psy_do_property(charger->pdata->wired_charger_name, get,
				POWER_SUPPLY_EXT_PROP_INPUT_CURRENT_LIMIT_WRL, value);
			charger->input_current = (ret) ? 500 : value.intval;
			pr_info("%s: updated input current (%d)\n",
				__func__, charger->input_current);
		}
		charger->req_afc_delay = 0;
		__pm_stay_awake(charger->wpc_det_ws);
		queue_delayed_work(charger->wqueue, &charger->wpc_det_work, 0);
		if (!int_state && !delayed_work_pending(&charger->wpc_isr_work)) {
			__pm_stay_awake(charger->wpc_rx_ws);
			queue_delayed_work(charger->wqueue, &charger->wpc_isr_work, msecs_to_jiffies(2000));
		}
	}

	sec_chg_set_dev_init(SC_DEV_WRL_CHG);

	dev_info(&client->dev, "%s: MFC cps4038 Charger Driver Loaded\n", __func__);
	device_init_wakeup(charger->dev, 1);
	return 0;

err_irq_wpc_det:
	power_supply_unregister(charger->psy_chg);
err_supply_unreg:
	wakeup_source_unregister(charger->wpc_ws);
	wakeup_source_unregister(charger->wpc_det_ws);
	wakeup_source_unregister(charger->wpc_rx_ws);
	wakeup_source_unregister(charger->wpc_tx_ws);
	wakeup_source_unregister(charger->wpc_update_ws);
	wakeup_source_unregister(charger->wpc_afc_vout_ws);
	wakeup_source_unregister(charger->wpc_vout_mode_ws);
	wakeup_source_unregister(charger->wpc_rx_det_ws);
	wakeup_source_unregister(charger->wpc_tx_phm_ws);
	wakeup_source_unregister(charger->wpc_tx_id_ws);
	wakeup_source_unregister(charger->wpc_pdrc_ws);
	wakeup_source_unregister(charger->wpc_cs100_ws);
err_pdata_free:
	mutex_destroy(&charger->io_lock);
	mutex_destroy(&charger->wpc_en_lock);
	mutex_destroy(&charger->fw_lock);
err_i2cfunc_not_support:
	kfree(charger);
err_wpc_nomem:
err_parse_dt:
	devm_kfree(&client->dev, pdata);

	return ret;
}

static int cps4038_charger_remove(struct i2c_client *client)
{
	struct mfc_charger_data *charger = i2c_get_clientdata(client);

	alarm_cancel(&charger->phm_alarm);

	return 0;
}

#if defined(CONFIG_PM)
static int mfc_charger_suspend(struct device *dev)
{
	struct mfc_charger_data *charger = dev_get_drvdata(dev);

	pr_info("%s: det(%d) int(%d)\n", __func__,
			gpio_get_value(charger->pdata->wpc_det),
			gpio_get_value(charger->pdata->wpc_int));

	charger->is_suspend = true;

	if (device_may_wakeup(charger->dev)) {
		enable_irq_wake(charger->pdata->irq_wpc_int);
		enable_irq_wake(charger->pdata->irq_wpc_det);
		if (charger->pdata->irq_wpc_pdrc)
			enable_irq_wake(charger->pdata->irq_wpc_pdrc);
		if (charger->pdata->irq_wpc_pdet_b)
			enable_irq_wake(charger->pdata->irq_wpc_pdet_b);
	}
#if !IS_ENABLED(CONFIG_ENABLE_WIRELESS_IRQ_IN_SLEEP)
	disable_irq(charger->pdata->irq_wpc_int);
	disable_irq(charger->pdata->irq_wpc_det);
	if (charger->pdata->irq_wpc_pdrc)
		disable_irq(charger->pdata->irq_wpc_pdrc);
	if (charger->pdata->irq_wpc_pdet_b)
		disable_irq(charger->pdata->irq_wpc_pdet_b);
#endif

	return 0;
}

static int mfc_charger_resume(struct device *dev)
{
	struct mfc_charger_data *charger = dev_get_drvdata(dev);

	pr_info("%s: det(%d) int(%d)\n", __func__,
			gpio_get_value(charger->pdata->wpc_det),
			gpio_get_value(charger->pdata->wpc_int));

	charger->is_suspend = false;

	if (device_may_wakeup(charger->dev)) {
		disable_irq_wake(charger->pdata->irq_wpc_int);
		disable_irq_wake(charger->pdata->irq_wpc_det);
		if (charger->pdata->irq_wpc_pdrc)
			disable_irq_wake(charger->pdata->irq_wpc_pdrc);
		if (charger->pdata->irq_wpc_pdet_b)
			disable_irq_wake(charger->pdata->irq_wpc_pdet_b);
	}
#if !IS_ENABLED(CONFIG_ENABLE_WIRELESS_IRQ_IN_SLEEP)
	enable_irq(charger->pdata->irq_wpc_int);
	enable_irq(charger->pdata->irq_wpc_det);
	if (charger->pdata->irq_wpc_pdrc)
		enable_irq(charger->pdata->irq_wpc_pdrc);
	if (charger->pdata->irq_wpc_pdet_b)
		enable_irq(charger->pdata->irq_wpc_pdet_b);
#else
	/* Level triggering makes infinite IRQ, Edge triggering is required */
	__pm_stay_awake(charger->wpc_ws);
	__pm_stay_awake(charger->wpc_det_ws);
	mfc_wpc_irq_thread(0, charger);
	if (charger->pdata->irq_wpc_pdrc) {
		__pm_stay_awake(charger->wpc_pdrc_ws);
		mfc_wpc_pdrc_irq_thread(0, charger);
	}
	mfc_wpc_det_irq_thread(0, charger);
#endif

	if (charger->skip_phm_work_in_sleep)
		queue_delayed_work(charger->wqueue, &charger->wpc_tx_phm_work, 0);

	return 0;
}
#else
#define mfc_charger_suspend NULL
#define mfc_charger_resume NULL
#endif

static void cps4038_charger_shutdown(struct i2c_client *client)
{
	struct mfc_charger_data *charger = i2c_get_clientdata(client);

	is_shutdn = true;
	pr_info("%s\n", __func__);

	cancel_delayed_work(&charger->wpc_vout_mode_work);
	alarm_cancel(&charger->phm_alarm);

	if (gpio_get_value(charger->pdata->wpc_det)) {
		pr_info("%s: forced 5V Vout\n", __func__);
		/* Prevent for unexpected FOD when reboot on morphie pad */
		mfc_set_vrect_adjust(charger, MFC_HEADROOM_6);
		mfc_set_vout(charger, MFC_VOUT_5V);
		mfc_set_pad_hv(charger, false);
	}
}

static const struct i2c_device_id cps4038_charger_id_table[] = {
	{ "cps4038-charger", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, cps4038_charger_id_table);

#ifdef CONFIG_OF
static const struct of_device_id cps4038_charger_match_table[] = {
	{ .compatible = "cps,cps4038-charger",},
	{},
};
MODULE_DEVICE_TABLE(of, cps4038_charger_match_table);
#else
#define cps4038_charger_match_table NULL
#endif

const struct dev_pm_ops cps4038_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(mfc_charger_suspend, mfc_charger_resume)
};

static struct i2c_driver cps4038_charger_driver = {
	.driver = {
		.name	= "cps4038-charger",
		.owner	= THIS_MODULE,
#if defined(CONFIG_PM)
		.pm = &cps4038_pm,
#endif /* CONFIG_PM */
		.of_match_table = cps4038_charger_match_table,
	},
	.shutdown	= cps4038_charger_shutdown,
	.probe	= cps4038_charger_probe,
	.remove	= cps4038_charger_remove,
	.id_table	= cps4038_charger_id_table,
};

static int __init cps4038_charger_init(void)
{
	pr_info("%s\n", __func__);
	return i2c_add_driver(&cps4038_charger_driver);
}

static void __exit cps4038_charger_exit(void)
{
	pr_info("%s\n", __func__);
	i2c_del_driver(&cps4038_charger_driver);
}

module_init(cps4038_charger_init);
module_exit(cps4038_charger_exit);

MODULE_DESCRIPTION("Samsung CPS4038 Charger Driver");
MODULE_AUTHOR("Samsung Electronics");
MODULE_LICENSE("GPL");
