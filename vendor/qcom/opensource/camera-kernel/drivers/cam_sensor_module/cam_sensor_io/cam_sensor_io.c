// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "cam_sensor_io.h"
#include "cam_sensor_i2c.h"
#include "cam_sensor_i3c.h"

#if IS_ENABLED(CONFIG_SEC_ABC)
#include <linux/sti/abc_common.h>
#endif
#if defined(CONFIG_CAMERA_ADAPTIVE_MIPI) && defined(CONFIG_CAMERA_RF_MIPI)
#include "cam_sensor_mipi.h"
#endif
#if defined(CONFIG_CAMERA_CDR_TEST)
#include <linux/ktime.h>
extern char cdr_result[40];
extern uint64_t cdr_start_ts;
extern uint64_t cdr_end_ts;
static void cam_io_cdr_store_result()
{
	cdr_end_ts	= ktime_get();
	cdr_end_ts = cdr_end_ts / 1000 / 1000;
	sprintf(cdr_result, "%d,%lld\n", 1, cdr_end_ts-cdr_start_ts);
	CAM_INFO(CAM_CSIPHY, "[CDR_DBG] i2c_fail, time(ms): %llu", cdr_end_ts-cdr_start_ts);
}
#endif
#if defined(CONFIG_CAMERA_HW_ERROR_DETECT) && defined(CONFIG_CAMERA_ADAPTIVE_MIPI) && defined(CONFIG_CAMERA_RF_MIPI)
extern char rear_i2c_rfinfo[30];
static void camera_io_rear_i2c_rfinfo()
{
	struct cam_cp_noti_info rf_info;

	get_rf_info(&rf_info);
	CAM_INFO(CAM_CSIPHY,
		"[RF_MIPI_DBG] rat : %d, band : %d, channel : %d",
		rf_info.rat, rf_info.band, rf_info.channel);
	sprintf(rear_i2c_rfinfo, "%d,%d,%d\n", rf_info.rat, rf_info.band, rf_info.channel);
}
#endif

int32_t camera_io_dev_poll(struct camera_io_master *io_master_info,
	uint32_t addr, uint16_t data, uint32_t data_mask,
	enum camera_sensor_i2c_type addr_type,
	enum camera_sensor_i2c_type data_type,
	uint32_t delay_ms)
{
	int16_t mask = data_mask & 0xFF;
	int32_t rc = 0;

	if (!io_master_info) {
		CAM_ERR(CAM_SENSOR, "Invalid Args");
		return -EINVAL;
	}

	switch (io_master_info->master_type) {
	case CCI_MASTER:
		rc = cam_cci_i2c_poll(io_master_info->cci_client,
			addr, data, mask, data_type, addr_type, delay_ms);
		break;

	case I2C_MASTER:
		rc = cam_qup_i2c_poll(io_master_info->client,
			addr, data, data_mask, addr_type, data_type, delay_ms);
		break;

	case I3C_MASTER:
		rc = cam_qup_i3c_poll(io_master_info->i3c_client,
			addr, data, data_mask, addr_type, data_type, delay_ms);
		break;

	default:
		rc = -EINVAL;
		CAM_ERR(CAM_SENSOR, "Invalid Master Type: %d", io_master_info->master_type);
		break;
	}

#if IS_ENABLED(CONFIG_SEC_ABC)
	if (rc < 0) {
#if defined(CONFIG_CAMERA_HW_ERROR_DETECT) && defined(CONFIG_CAMERA_ADAPTIVE_MIPI) && defined(CONFIG_CAMERA_RF_MIPI)
		camera_io_rear_i2c_rfinfo();
#endif
		sec_abc_send_event("MODULE=camera@WARN=i2c_fail");
#if defined(CONFIG_CAMERA_CDR_TEST)
		cam_io_cdr_store_result();
#endif
	}
#endif

	return rc;
}

int32_t camera_io_dev_erase(struct camera_io_master *io_master_info,
	uint32_t addr, uint32_t size)
{
	int32_t rc = 0;

	if (!io_master_info) {
		CAM_ERR(CAM_SENSOR, "Invalid Args");
		return -EINVAL;
	}

	if (size == 0)
		return 0;

	switch (io_master_info->master_type) {
	case SPI_MASTER:
		CAM_DBG(CAM_SENSOR, "Calling SPI Erase");
		rc = cam_spi_erase(io_master_info, addr, CAMERA_SENSOR_I2C_TYPE_WORD, size);
		break;

	case I2C_MASTER:
	case CCI_MASTER:
	case I3C_MASTER:
		CAM_ERR(CAM_SENSOR, "Erase not supported on Master Type: %d",
			io_master_info->master_type);
		rc = -EINVAL;
		break;

	default:
		rc = -EINVAL;
		CAM_ERR(CAM_SENSOR, "Invalid Master Type: %d", io_master_info->master_type);
		break;
	}

#if IS_ENABLED(CONFIG_SEC_ABC)
	if (rc < 0) {
#if defined(CONFIG_CAMERA_HW_ERROR_DETECT) && defined(CONFIG_CAMERA_ADAPTIVE_MIPI) && defined(CONFIG_CAMERA_RF_MIPI)
		camera_io_rear_i2c_rfinfo();
#endif
		sec_abc_send_event("MODULE=camera@WARN=i2c_fail");
#if defined(CONFIG_CAMERA_CDR_TEST)
		cam_io_cdr_store_result();
#endif
	}
#endif

	return rc;
}

int32_t camera_io_dev_read(struct camera_io_master *io_master_info,
	uint32_t addr, uint32_t *data,
	enum camera_sensor_i2c_type addr_type,
	enum camera_sensor_i2c_type data_type,
	bool is_probing)
{
	int32_t rc = 0;

	if (!io_master_info) {
		CAM_ERR(CAM_SENSOR, "Invalid Args");
		return -EINVAL;
	}

	switch (io_master_info->master_type) {
	case SPI_MASTER:
		rc = cam_spi_read(io_master_info, addr, data, addr_type, data_type);
		break;

	case I2C_MASTER:
		rc = cam_qup_i2c_read(io_master_info->client,
			addr, data, addr_type, data_type);
		break;

	case CCI_MASTER:
		rc = cam_cci_i2c_read(io_master_info->cci_client,
			addr, data, addr_type, data_type, is_probing);
		break;

	case I3C_MASTER:
		rc = cam_qup_i3c_read(io_master_info->i3c_client,
			addr, data, addr_type, data_type);
		break;

	default:
		rc = -EINVAL;
		CAM_ERR(CAM_SENSOR, "Invalid Master Type: %d", io_master_info->master_type);
		break;
	}

#if IS_ENABLED(CONFIG_SEC_ABC)
	if (rc < 0) {
#if defined(CONFIG_CAMERA_HW_ERROR_DETECT) && defined(CONFIG_CAMERA_ADAPTIVE_MIPI) && defined(CONFIG_CAMERA_RF_MIPI)
		camera_io_rear_i2c_rfinfo();
#endif
		sec_abc_send_event("MODULE=camera@WARN=i2c_fail");
#if defined(CONFIG_CAMERA_CDR_TEST)
		cam_io_cdr_store_result();
#endif
	}
#endif

	return rc;
}

int32_t camera_io_dev_read_seq(struct camera_io_master *io_master_info,
	uint32_t addr, uint8_t *data,
	enum camera_sensor_i2c_type addr_type,
	enum camera_sensor_i2c_type data_type, int32_t num_bytes)
{
	int32_t rc = 0;

	switch (io_master_info->master_type) {
	case CCI_MASTER:
		rc = cam_camera_cci_i2c_read_seq(io_master_info->cci_client,
			addr, data, addr_type, data_type, num_bytes);
		break;

	case I2C_MASTER:
		rc = cam_qup_i2c_read_seq(io_master_info->client,
			addr, data, addr_type, num_bytes);
		break;

	case SPI_MASTER:
		rc = cam_spi_read_seq(io_master_info, addr, data, addr_type, num_bytes);
		break;

	case I3C_MASTER:
		rc = cam_qup_i3c_read_seq(io_master_info->i3c_client,
			addr, data, addr_type, num_bytes);
		break;

	default:
		rc = -EINVAL;
		CAM_ERR(CAM_SENSOR, "Invalid Master Type: %d", io_master_info->master_type);
		break;
	}

#if IS_ENABLED(CONFIG_SEC_ABC)
	if (rc < 0) {
#if defined(CONFIG_CAMERA_HW_ERROR_DETECT) && defined(CONFIG_CAMERA_ADAPTIVE_MIPI) && defined(CONFIG_CAMERA_RF_MIPI)
		camera_io_rear_i2c_rfinfo();
#endif
		sec_abc_send_event("MODULE=camera@WARN=i2c_fail");
#if defined(CONFIG_CAMERA_CDR_TEST)
		cam_io_cdr_store_result();
#endif
	}
#endif

	return rc;
}

int32_t camera_io_dev_write(struct camera_io_master *io_master_info,
	struct cam_sensor_i2c_reg_setting *write_setting)
{
	int32_t rc = 0;

	if (!write_setting || !io_master_info) {
		CAM_ERR(CAM_SENSOR,
			"Input parameters not valid ws: %pK ioinfo: %pK",
			write_setting, io_master_info);
		return -EINVAL;
	}

	if (!write_setting->reg_setting) {
		CAM_ERR(CAM_SENSOR, "Invalid Register Settings");
		return -EINVAL;
	}

	switch (io_master_info->master_type) {
	case CCI_MASTER:
		rc = cam_cci_i2c_write_table(io_master_info, write_setting);
		break;

	case I2C_MASTER:
		rc = cam_qup_i2c_write_table(io_master_info, write_setting);
		break;

	case SPI_MASTER:
		rc = cam_spi_write_table(io_master_info, write_setting);
		break;

	case I3C_MASTER:
		rc = cam_qup_i3c_write_table(io_master_info, write_setting);
		break;

	default:
		rc = -EINVAL;
		CAM_ERR(CAM_SENSOR, "Invalid Master Type:%d", io_master_info->master_type);
		break;
	}

#if IS_ENABLED(CONFIG_SEC_ABC)
	if (rc < 0) {
#if defined(CONFIG_CAMERA_HW_ERROR_DETECT) && defined(CONFIG_CAMERA_ADAPTIVE_MIPI) && defined(CONFIG_CAMERA_RF_MIPI)
		camera_io_rear_i2c_rfinfo();
#endif
		sec_abc_send_event("MODULE=camera@WARN=i2c_fail");
#if defined(CONFIG_CAMERA_CDR_TEST)
		cam_io_cdr_store_result();
#endif
	}
#endif

	return rc;
}

int32_t camera_io_dev_write_continuous(struct camera_io_master *io_master_info,
	struct cam_sensor_i2c_reg_setting *write_setting,
	uint8_t cam_sensor_i2c_write_flag)
{
	int32_t rc = 0;

	if (!write_setting || !io_master_info) {
		CAM_ERR(CAM_SENSOR,
			"Input parameters not valid ws: %pK ioinfo: %pK",
			write_setting, io_master_info);
		return -EINVAL;
	}

	if (!write_setting->reg_setting) {
		CAM_ERR(CAM_SENSOR, "Invalid Register Settings");
		return -EINVAL;
	}

	switch (io_master_info->master_type) {
	case CCI_MASTER:
		rc = cam_cci_i2c_write_continuous_table(io_master_info,
			write_setting, cam_sensor_i2c_write_flag);
		break;

	case I2C_MASTER:
		rc = cam_qup_i2c_write_continuous_table(io_master_info,
			write_setting, cam_sensor_i2c_write_flag);
		break;

	case SPI_MASTER:
		rc = cam_spi_write_table(io_master_info, write_setting);
		break;

	case I3C_MASTER:
		rc = cam_qup_i3c_write_continuous_table(io_master_info,
			write_setting, cam_sensor_i2c_write_flag);
		break;

	default:
		rc = -EINVAL;
		CAM_ERR(CAM_SENSOR, "Invalid Master Type:%d", io_master_info->master_type);
		break;
	}

#if IS_ENABLED(CONFIG_SEC_ABC)
	if (rc < 0) {
#if defined(CONFIG_CAMERA_HW_ERROR_DETECT) && defined(CONFIG_CAMERA_ADAPTIVE_MIPI) && defined(CONFIG_CAMERA_RF_MIPI)
		camera_io_rear_i2c_rfinfo();
#endif
		sec_abc_send_event("MODULE=camera@WARN=i2c_fail");
#if defined(CONFIG_CAMERA_CDR_TEST)
		cam_io_cdr_store_result();
#endif
	}
#endif

	return rc;
}

int32_t camera_io_init(struct camera_io_master *io_master_info)
{
	int32_t rc = 0;

	if (!io_master_info) {
		CAM_ERR(CAM_SENSOR, "Invalid Args");
		return -EINVAL;
	}

	switch (io_master_info->master_type) {
	case CCI_MASTER:
		io_master_info->cci_client->cci_subdev = cam_cci_get_subdev(
						io_master_info->cci_client->cci_device);
		rc = cam_sensor_cci_i2c_util(io_master_info->cci_client, MSM_CCI_INIT);
		break;

	case I2C_MASTER:
	case SPI_MASTER:
	case I3C_MASTER:
		rc = 0;
		break;

	default:
		rc = -EINVAL;
		CAM_ERR(CAM_SENSOR, "Invalid Master Type:%d", io_master_info->master_type);
		break;
	}

#if IS_ENABLED(CONFIG_SEC_ABC)
	if (rc < 0) {
#if defined(CONFIG_CAMERA_HW_ERROR_DETECT) && defined(CONFIG_CAMERA_ADAPTIVE_MIPI) && defined(CONFIG_CAMERA_RF_MIPI)
		camera_io_rear_i2c_rfinfo();
#endif
		sec_abc_send_event("MODULE=camera@WARN=i2c_fail");
#if defined(CONFIG_CAMERA_CDR_TEST)
		cam_io_cdr_store_result();
#endif
	}
#endif

	return rc;
}

int32_t camera_io_release(struct camera_io_master *io_master_info)
{
	int32_t rc = 0;

	if (!io_master_info) {
		CAM_ERR(CAM_SENSOR, "Invalid Args");
		return -EINVAL;
	}

	switch (io_master_info->master_type) {
	case CCI_MASTER:
		rc = cam_sensor_cci_i2c_util(io_master_info->cci_client, MSM_CCI_RELEASE);
		break;

	case I2C_MASTER:
	case SPI_MASTER:
	case I3C_MASTER:
		rc = 0;
		break;

	default:
		rc = -EINVAL;
		CAM_ERR(CAM_SENSOR, "Invalid Master Type:%d", io_master_info->master_type);
		break;
	}

#if IS_ENABLED(CONFIG_SEC_ABC)
	if (rc < 0) {
#if defined(CONFIG_CAMERA_HW_ERROR_DETECT) && defined(CONFIG_CAMERA_ADAPTIVE_MIPI) && defined(CONFIG_CAMERA_RF_MIPI)
		camera_io_rear_i2c_rfinfo();
#endif
		sec_abc_send_event("MODULE=camera@WARN=i2c_fail");
#if defined(CONFIG_CAMERA_CDR_TEST)
		cam_io_cdr_store_result();
#endif
	}
#endif

	return rc;
}
