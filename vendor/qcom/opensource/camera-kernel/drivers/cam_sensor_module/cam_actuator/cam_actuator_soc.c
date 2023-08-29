// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <cam_sensor_cmn_header.h>
#include <cam_sensor_util.h>
#include <cam_sensor_io.h>
#include <cam_req_mgr_util.h>
#include "cam_actuator_soc.h"
#include "cam_soc_util.h"

int32_t cam_actuator_parse_dt(struct cam_actuator_ctrl_t *a_ctrl,
	struct device *dev)
{
	int32_t                         i, rc = 0;
	struct cam_hw_soc_info          *soc_info = &a_ctrl->soc_info;
	struct cam_actuator_soc_private *soc_private =
		(struct cam_actuator_soc_private *)a_ctrl->soc_info.soc_private;
	struct cam_sensor_power_ctrl_t  *power_info = &soc_private->power_info;
	struct device_node              *of_node = NULL;
	struct device_node              *of_parent = NULL;
	uint32_t                        temp = 0;

	/* Initialize mutex */
	mutex_init(&(a_ctrl->actuator_mutex));

	rc = cam_soc_util_get_dt_properties(soc_info);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "parsing common soc dt(rc %d)", rc);
		return rc;
	}

	of_node = soc_info->dev->of_node;

	rc = of_property_read_bool(of_node, "i3c-target");
	if (rc) {
		a_ctrl->is_i3c_device = true;
		a_ctrl->io_master_info.master_type = I3C_MASTER;
	}

	CAM_DBG(CAM_SENSOR, "I3C Target: %s", CAM_BOOL_TO_YESNO(a_ctrl->is_i3c_device));

	if (a_ctrl->io_master_info.master_type == CCI_MASTER) {
		rc = of_property_read_u32(of_node, "cci-master",
			&(a_ctrl->cci_i2c_master));
		CAM_DBG(CAM_ACTUATOR, "cci-master %d, rc %d",
			a_ctrl->cci_i2c_master, rc);
		if ((rc < 0) || (a_ctrl->cci_i2c_master >= MASTER_MAX)) {
			CAM_ERR(CAM_ACTUATOR,
				"Wrong info: rc: %d, dt CCI master:%d",
				rc, a_ctrl->cci_i2c_master);
			rc = -EFAULT;
			return rc;
		}

		of_parent = of_get_parent(of_node);
		if (of_property_read_u32(of_parent, "cell-index",
				&a_ctrl->cci_num) < 0)
			/* Set default master 0 */
			a_ctrl->cci_num = CCI_DEVICE_0;
		a_ctrl->io_master_info.cci_client->cci_device = a_ctrl->cci_num;
		CAM_DBG(CAM_ACTUATOR, "cci-device %d", a_ctrl->cci_num);
	}

	rc = of_property_read_u32(of_node, "slave-addr", &temp);
	soc_private->i2c_info.slave_addr = temp;
	if (rc < 0) {
		CAM_DBG(CAM_ACTUATOR, "No slave-addr found");
		rc = 0;
	}

#if defined(CONFIG_SAMSUNG_OIS_MCU_STM32)
	if (of_property_read_bool(of_node, "use-mcu")) {
		CAM_INFO(CAM_ACTUATOR,
			"actuator%u with MCU", soc_info->index);
		a_ctrl->use_mcu = true;	
	}
#endif

	/* Initialize regulators to default parameters */
	for (i = 0; i < soc_info->num_rgltr; i++) {
		soc_info->rgltr[i] = devm_regulator_get(soc_info->dev,
					soc_info->rgltr_name[i]);
		if (IS_ERR_OR_NULL(soc_info->rgltr[i])) {
			rc = PTR_ERR(soc_info->rgltr[i]);
			rc = rc ? rc : -EINVAL;
			CAM_ERR(CAM_ACTUATOR, "get failed for regulator %s %d",
				 soc_info->rgltr_name[i], rc);
			return rc;
		}
		CAM_DBG(CAM_ACTUATOR, "get for regulator %s",
			soc_info->rgltr_name[i]);
	}
	if (!soc_info->gpio_data) {
		CAM_DBG(CAM_ACTUATOR, "No GPIO found");
		rc = 0;
		return rc;
	}

	if (!soc_info->gpio_data->cam_gpio_common_tbl_size) {
		CAM_DBG(CAM_ACTUATOR, "No GPIO found");
		return -EINVAL;
	}

	rc = cam_sensor_util_init_gpio_pin_tbl(soc_info,
		&power_info->gpio_num_info);
	if ((rc < 0) || (!power_info->gpio_num_info)) {
		CAM_ERR(CAM_ACTUATOR, "No/Error Actuator GPIOs");
		return -EINVAL;
	}
	return rc;
}
