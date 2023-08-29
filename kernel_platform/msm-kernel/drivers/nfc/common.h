/******************************************************************************
 * Copyright (C) 2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2022 NXP
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
#ifndef _COMMON_H_
#define _COMMON_H_

#include <linux/cdev.h>
#include <linux/fs.h>

#if IS_ENABLED(CONFIG_SAMSUNG_NFC)
#include <linux/clk.h>
#include "nfc_wakelock.h"
#ifdef CONFIG_SEC_NFC_LOGGER
#ifdef CONFIG_NFC_NXP_COMBINED
#include "../nfc_logger/nfc_logger.h"
#else
#include "nfc_logger/nfc_logger.h"
#endif
#endif
#endif

#include "i2c_drv.h"

/* Max device count for this driver */
#define DEV_COUNT			1
/* i2c device class */
#if IS_ENABLED(CONFIG_SAMSUNG_NFC)
#define CLASS_NAME			"nfc_sec"
#else
#define CLASS_NAME			"nfc"
#endif

/* NFC character device name, this will be in /dev/ */
#define NFC_CHAR_DEV_NAME		"pn547"

/* NCI packet details */
#define NCI_CMD				(0x20)
#define NCI_RSP				(0x40)
#define NCI_HDR_LEN			(3)
#define NCI_HDR_IDX			(0)
#define NCI_HDR_OID_IDX			(1)
#define NCI_PAYLOAD_IDX			(3)
#define NCI_PAYLOAD_LEN_IDX		(2)

/* FW DNLD packet details */
#define DL_HDR_LEN			(2)
#define DL_CRC_LEN			(2)

#define MAX_NCI_PAYLOAD_LEN		(255)
#define MAX_NCI_BUFFER_SIZE		(NCI_HDR_LEN + MAX_NCI_PAYLOAD_LEN)
#define MAX_DL_PAYLOAD_LEN		(550)
#define MAX_DL_BUFFER_SIZE		(DL_HDR_LEN + DL_CRC_LEN + \
					MAX_DL_PAYLOAD_LEN)


/* Retry count for normal write */
#define NO_RETRY			(1)
/* Maximum retry count for standby writes */
#define MAX_RETRY_COUNT			(3)
#define MAX_WRITE_IRQ_COUNT		(5)
#define MAX_IRQ_WAIT_TIME		(90)
#define WAKEUP_SRC_TIMEOUT		(2000)

/* command response timeout */
#define NCI_CMD_RSP_TIMEOUT_MS		(2000)
/* Time to wait for NFCC to be ready again after any change in the GPIO */
#define NFC_GPIO_SET_WAIT_TIME_US	(15000)
/* Time to wait before retrying writes */
#define WRITE_RETRY_WAIT_TIME_US	(3000)
/* Time to wait before retrying read for some specific usecases */
#define READ_RETRY_WAIT_TIME_US		(3500)
#define NFC_MAGIC			(0xE9)

/* Ioctls */
/* The type should be aligned with MW HAL definitions */
#define NFC_SET_PWR			_IOW(NFC_MAGIC, 0x01, uint64_t)
#define ESE_SET_PWR			_IOW(NFC_MAGIC, 0x02, uint64_t)
#define ESE_GET_PWR			_IOR(NFC_MAGIC, 0x03, uint64_t)
#define NFC_GET_GPIO_STATUS		_IOR(NFC_MAGIC, 0x05, uint64_t)

#if IS_ENABLED(CONFIG_SAMSUNG_NFC)
#define CONFIG_SAMSUNG_NFC_DEBUG
#define FEATURE_CORE_RESET_NTF_CHECK

#define DTS_IRQ_GPIO_STR    "pn547,irq-gpio"
#define DTS_VEN_GPIO_STR    "pn547,ven-gpio"
#define DTS_FWDN_GPIO_STR   "pn547,firm-gpio"

enum ap_vendors {
	AP_VENDOR_NONE,
	AP_VENDOR_SLSI,
	AP_VENDOR_QCT,
	AP_VENDOR_MTK,
	AP_VENDOR_ERR
};

enum lpm_status {
	LPM_NO_SUPPORT = -1,
	LPM_FALSE,
	LPM_TRUE
};
#else
#define DTS_IRQ_GPIO_STR		"nxp,sn-irq"
#define DTS_VEN_GPIO_STR		"nxp,sn-ven-rstn"
#define DTS_FWDN_GPIO_STR		"nxp,sn-dwl-req"
#endif

/* Each GPIO occupies consecutive two bits */
#define GPIO_POS_SHIFT_VAL 2
/* Two bits to indicate GPIO status (Invalid(-2), Set(1) or Reset(0)) */
#define GPIO_STATUS_MASK_BITS 3

#ifndef CONFIG_SEC_NFC_LOGGER
#define NFC_LOG_ERR(fmt, ...)		pr_err("sec_nfc: "fmt, ##__VA_ARGS__)
#define NFC_LOG_INFO(fmt, ...)		pr_info("sec_nfc: "fmt, ##__VA_ARGS__)
#define NFC_LOG_INFO_WITH_DATE(fmt, ...) pr_info("sec_nfc: "fmt, ##__VA_ARGS__)
#define NFC_LOG_DBG(fmt, ...)		pr_debug("sec_nfc: "fmt, ##__VA_ARGS__)
#define NFC_LOG_REC(fmt, ...)		do { } while (0)

#define nfc_print_hex_dump(a, b, c)	do { } while (0)
#define nfc_logger_init()		do { } while (0)
#define nfc_logger_deinit()		do { } while (0)
#define nfc_logger_set_max_count(a)	do { } while (0)
#define nfc_logger_register_nfc_stauts_func(a)	do { } while (0)
#endif /* CONFIG_SEC_NFC_LOGGER */

enum nfcc_ioctl_request {
	/* NFC disable request with VEN LOW */
	NFC_POWER_OFF = 0,
	/* NFC enable request with VEN Toggle */
	NFC_POWER_ON,
	/* firmware download request with VEN Toggle */
	NFC_FW_DWL_VEN_TOGGLE,
	/* ISO reset request */
	NFC_ISO_RESET,
	/* request for firmware download gpio HIGH */
	NFC_FW_DWL_HIGH,
	/* VEN hard reset request */
	NFC_VEN_FORCED_HARD_RESET,
	/* request for firmware download gpio LOW */
	NFC_FW_DWL_LOW,
};

/* nfc platform interface type */
enum interface_flags {
	/* I2C physical IF for NFCC */
	PLATFORM_IF_I2C = 0,
};

/* nfc state flags */
enum nfc_state_flags {
	/* nfc in unknown state */
	NFC_STATE_UNKNOWN = 0,
	/* nfc in download mode */
	NFC_STATE_FW_DWL = 0x1,
	/* nfc booted in NCI mode */
	NFC_STATE_NCI = 0x2,
	/* nfc booted in Fw teared mode */
	NFC_STATE_FW_TEARED = 0x4,
};
/*
 * Power state for IBI handing, mainly needed to defer the IBI handling
 *  for the IBI received in suspend state to do it later in resume call
 */
enum pm_state_flags {
	PM_STATE_NORMAL = 0,
	PM_STATE_SUSPEND,
	PM_STATE_IBI_BEFORE_RESUME,
};

/* Enum for GPIO values */
enum gpio_values {
	GPIO_INPUT = 0x0,
	GPIO_OUTPUT = 0x1,
	GPIO_HIGH = 0x2,
	GPIO_OUTPUT_HIGH = 0x3,
	GPIO_IRQ = 0x4,
};

#if IS_ENABLED(CONFIG_SAMSUNG_NFC)
#define PLATFORM_DEFAULT_GPIO_CNT 3
#endif
/* NFC GPIO variables */
struct platform_gpio {
	int irq;
	int ven;
	int dwl_req;
#if IS_ENABLED(CONFIG_SAMSUNG_NFC)
	int clk_req;
	int clk_req_irq;
	bool clk_req_irq_enabled;
#endif
};

/* NFC Struct to get all the required configs from DTS */
struct platform_configs {
	struct platform_gpio gpio;
#if IS_ENABLED(CONFIG_SAMSUNG_NFC)
	bool clk_req_wake;
	bool clk_req_all_trigger;
	bool change_clkreq_for_acpm;
	int ap_vendor;
	struct regulator *nfc_pvdd;
	struct clk *nfc_clock;
	bool late_pvdd_en;
	bool disable_clk_irq_during_wakeup;
#endif
};

/* cold reset Features specific Parameters */
struct cold_reset {
	bool rsp_pending;	/* cmd rsp pending status */
	bool in_progress;	/* for cold reset when gurad timer in progress */
	bool reset_protection;	/* reset protection enabled/disabled */
	uint8_t status;		/* status from response buffer */
	uint8_t rst_prot_src;	/* reset protection source (SPI, NFC) */
	struct timer_list timer;
	wait_queue_head_t read_wq;
};

/* Device specific structure */
struct nfc_dev {
	wait_queue_head_t read_wq;
	struct mutex read_mutex;
	struct mutex write_mutex;
	uint8_t *read_kbuf;
	uint8_t *write_kbuf;
	struct mutex dev_ref_mutex;
	unsigned int dev_ref_count;
	struct class *nfc_class;
	struct device *nfc_device;
	struct cdev c_dev;
	dev_t devno;
	/* Interface flag */
	uint8_t interface;
	/* nfc state flags */
	uint8_t nfc_state;
	/* NFC VEN pin state */
	bool nfc_ven_enabled;
	bool release_read;
	union {
		struct i2c_dev i2c_dev;
	};
	struct platform_configs configs;
	struct cold_reset cold_reset;

#if IS_ENABLED(CONFIG_SAMSUNG_NFC)
	struct nfc_wake_lock nfc_wake_lock;
	struct nfc_wake_lock nfc_clk_wake_lock;
	bool clk_req_wakelock;
	bool screen_cfg;
	bool screen_on_cmd;
	bool screen_off_cmd;
	int screen_off_rsp_count;
#endif

	/* function pointers for the common i2c functionality */
	int (*nfc_read)(struct nfc_dev *dev, char *buf, size_t count,
			int timeout);
	int (*nfc_write)(struct nfc_dev *dev, const char *buf,
			 const size_t count, int max_retry_cnt);
	int (*nfc_enable_intr)(struct nfc_dev *dev);
	int (*nfc_disable_intr)(struct nfc_dev *dev);
#if IS_ENABLED(CONFIG_SAMSUNG_NFC)
	void (*nfc_enable_clk_intr)(struct nfc_dev *dev);
	void (*nfc_disable_clk_intr)(struct nfc_dev *dev);
#endif
};

int nfc_dev_open(struct inode *inode, struct file *filp);
int nfc_dev_flush(struct file *pfile, fl_owner_t id);
int nfc_dev_close(struct inode *inode, struct file *filp);
long nfc_dev_compat_ioctl(struct file *pfile, unsigned int cmd,
		      unsigned long arg);
long nfc_dev_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg);
int nfc_parse_dt(struct device *dev, struct platform_configs *nfc_configs,
		 uint8_t interface);
int nfc_misc_register(struct nfc_dev *nfc_dev,
			const struct file_operations *nfc_fops, int count,
			char *devname, char *classname);
void nfc_misc_unregister(struct nfc_dev *nfc_dev, int count);
int configure_gpio(unsigned int gpio, int flag);
void gpio_set_ven(struct nfc_dev *nfc_dev, int value);
void gpio_free_all(struct nfc_dev *nfc_dev);
int validate_nfc_state_nci(struct nfc_dev *nfc_dev);
#if IS_ENABLED(CONFIG_SAMSUNG_NFC)
int nfc_regulator_onoff(struct nfc_dev *nfc_dev, int onoff);
void nfc_power_control(struct nfc_dev *nfc_dev);
void nfc_print_status(void);
void nfc_probe_done(struct nfc_dev *nfc_dev);
bool nfc_check_pvdd_status(void);
enum lpm_status nfc_get_lpcharge(void);
#ifdef CONFIG_MAKE_NODE_USING_PLATFORM_DEVICE
void nfc_parse_dt_for_platform_device(struct device *dev);
#endif
#endif
#endif /* _COMMON_H_ */
