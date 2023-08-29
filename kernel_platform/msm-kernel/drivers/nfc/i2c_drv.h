/******************************************************************************
 * Copyright (C) 2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2021 NXP
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 ******************************************************************************/
#ifndef _I2C_DRV_H_
#define _I2C_DRV_H_

#include <linux/i2c.h>

/* kept same as dts */
#define NFC_I2C_DRV_STR			"pn547"
#define NFC_I2C_DEV_ID			"pn547"

/* Interface specific parameters */
struct i2c_dev {
	struct i2c_client *client;
	/* IRQ parameters */
	bool irq_enabled;
	spinlock_t irq_enabled_lock;
	/* NFC_IRQ wake-up state */
	bool irq_wake_up;
};

long nfc_i2c_dev_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg);
int nfc_i2c_dev_probe(struct i2c_client *client,
		      const struct i2c_device_id *id);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
int nfc_i2c_dev_remove(struct i2c_client *client);
#else
void nfc_i2c_dev_remove(struct i2c_client *client);
#endif
int nfc_i2c_dev_suspend(struct device *device);
int nfc_i2c_dev_resume(struct device *device);
#ifdef CONFIG_CLK_ACPM_INIT
extern acpm_init_eint_nfc_clk_req(u32 eint_num);
#endif

#endif /* _I2C_DRV_H_ */
