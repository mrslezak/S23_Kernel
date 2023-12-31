# SPDX-License-Identifier: GPL-2.0-only

# Localized KCONFIG settings
CONFIG_SPECTRA_ISP := y
CONFIG_SPECTRA_ICP := y
CONFIG_SPECTRA_JPEG := y
CONFIG_SPECTRA_CUSTOM := y
CONFIG_SPECTRA_SENSOR := y
CONFIG_USE_RPMH_DRV_API := y

ifeq ($(PROJECT_NAME), $(filter $(PROJECT_NAME),dm1q dm2q dm3q))
CONFIG_SAMSUNG_OIS_MCU_STM32 := y
CONFIG_CAMERA_SYSFS_V2 := y
CONFIG_CAMERA_FRAME_CNT_DBG := y
CONFIG_CAMERA_RF_MIPI := y
CONFIG_SAMSUNG_DEBUG_SENSOR_I2C := y
CONFIG_SAMSUNG_DEBUG_SENSOR_FPS := y
CONFIG_SAMSUNG_DEBUG_HW_INFO := y
CONFIG_CAMERA_HYPERLAPSE_300X := y
CONFIG_SAMSUNG_DEBUG_SENSOR_TIMING_REC := y
endif

ifeq ($(PROJECT_NAME), $(filter $(PROJECT_NAME),dm1q))
CONFIG_SEC_DM1Q_PROJECT := y
CONFIG_SENSOR_RETENTION := y
CONFIG_CAMERA_ADAPTIVE_MIPI := y
CONFIG_CAMERA_CDR_TEST := y
CONFIG_CAMERA_HW_ERROR_DETECT := y
endif

ifeq ($(PROJECT_NAME), $(filter $(PROJECT_NAME),dm2q))
CONFIG_SEC_DM2Q_PROJECT := y
CONFIG_SENSOR_RETENTION := y
CONFIG_CAMERA_ADAPTIVE_MIPI := y
CONFIG_CAMERA_CDR_TEST := y
CONFIG_CAMERA_HW_ERROR_DETECT := y
endif

ifeq ($(PROJECT_NAME), $(filter $(PROJECT_NAME),dm3q))
CONFIG_SEC_DM3Q_PROJECT := y
CONFIG_SAMSUNG_ACTUATOR_PREVENT_SHAKING := y
CONFIG_SENSOR_RETENTION := y
CONFIG_CAMERA_ADAPTIVE_MIPI := y
CONFIG_CAMERA_CDR_TEST := y
CONFIG_SAMSUNG_WACOM_NOTIFIER := y
CONFIG_CAMERA_HW_ERROR_DETECT := y
endif

ifneq ($(filter jpn%, $(PROJECT_REGION)),)
CONFIG_FLASH_CURRENT_JAPAN := y
else ifeq ($(PROJECT_REGION), $(filter $(PROJECT_REGION),jpn_dcmw jpn_kdiw jpn_rktw jpn_openw))
CONFIG_FLASH_CURRENT_JAPAN := y
endif

# Flags to pass into C preprocessor
ccflags-y += -DCONFIG_SPECTRA_ISP=1
ccflags-y += -DCONFIG_SPECTRA_ICP=1
ccflags-y += -DCONFIG_SPECTRA_JPEG=1
ccflags-y += -DCONFIG_SPECTRA_CUSTOM=1
ccflags-y += -DCONFIG_SPECTRA_SENSOR=1
ccflags-y += -DCONFIG_USE_RPMH_DRV_API=1

ifeq ($(PROJECT_NAME), $(filter $(PROJECT_NAME),dm1q dm2q dm3q))
ccflags-y += -DCONFIG_SAMSUNG_OIS_MCU_STM32=1
ccflags-y += -DCONFIG_CAMERA_SYSFS_V2=1
ccflags-y += -DCONFIG_CAMERA_FRAME_CNT_DBG=1
ccflags-y += -DCONFIG_CAMERA_FRAME_CNT_CHECK=1
ccflags-y += -DCONFIG_SAMSUNG_FRONT_EEPROM=1
ccflags-y += -DCONFIG_SAMSUNG_REAR_DUAL=1
ccflags-y += -DCONFIG_SAMSUNG_REAR_TRIPLE=1
ccflags-y += -DCONFIG_USE_CAMERA_HW_BIG_DATA=1
#ccflags-y += -DCONFIG_CAMERA_SKIP_SECURE_PAGE_FAULT=1
ccflags-y += -DCONFIG_SAMSUNG_ACTUATOR_READ_HALL_VALUE=1
ccflags-y += -DCONFIG_CAMERA_RF_MIPI=1
ccflags-y += -DCONFIG_SAMSUNG_DEBUG_SENSOR_I2C=1
ccflags-y += -DCONFIG_SAMSUNG_DEBUG_SENSOR_FPS=1
ccflags-y += -DCONFIG_SAMSUNG_DEBUG_HW_INFO=1
ccflags-y += -DCONFIG_CAMERA_HYPERLAPSE_300X=1
ccflags-y += -DCONFIG_SAMSUNG_DEBUG_SENSOR_TIMING_REC=1
endif

ifeq ($(PROJECT_NAME), $(filter $(PROJECT_NAME),dm1q))
ccflags-y += -DCONFIG_SEC_DM1Q_PROJECT=1
ccflags-y += -DCONFIG_SENSOR_RETENTION=1
ccflags-y += -DCONFIG_CAMERA_ADAPTIVE_MIPI=1
ccflags-y += -DCONFIG_CAMERA_CDR_TEST=1
ccflags-y += -DCONFIG_CAMERA_HW_ERROR_DETECT=1
endif

ifeq ($(PROJECT_NAME), $(filter $(PROJECT_NAME),dm2q))
ccflags-y += -DCONFIG_SEC_DM2Q_PROJECT=1
ccflags-y += -DCONFIG_SENSOR_RETENTION=1
ccflags-y += -DCONFIG_CAMERA_ADAPTIVE_MIPI=1
ccflags-y += -DCONFIG_CAMERA_CDR_TEST=1
ccflags-y += -DCONFIG_CAMERA_HW_ERROR_DETECT=1
endif

ifeq ($(PROJECT_NAME), $(filter $(PROJECT_NAME),dm3q))
ccflags-y += -DCONFIG_SEC_DM3Q_PROJECT=1
ccflags-y += -DCONFIG_SAMSUNG_REAR_QUADRA=1
ccflags-y += -DCONFIG_SAMSUNG_ACTUATOR_PREVENT_SHAKING=1
ccflags-y += -DCONFIG_SENSOR_RETENTION=1
ccflags-y += -DCONFIG_CAMERA_ADAPTIVE_MIPI=1
ccflags-y += -DCONFIG_SAMSUNG_READ_BPC_FROM_OTP=1
ccflags-y += -DCONFIG_CAMERA_CDR_TEST=1
ccflags-y += -DCONFIG_SAMSUNG_WACOM_NOTIFIER=1
ccflags-y += -DCONFIG_CAMERA_HW_ERROR_DETECT=1
endif

ifneq ($(filter jpn%, $(PROJECT_REGION)),)
ccflags-y += -DCONFIG_FLASH_CURRENT_JAPAN=1
else ifeq ($(PROJECT_REGION), $(filter $(PROJECT_REGION),jpn_dcmw jpn_kdiw jpn_rktw jpn_openw))
ccflags-y += -DCONFIG_FLASH_CURRENT_JAPAN=1
endif

# External Dependencies
KBUILD_CPPFLAGS += -DCONFIG_MSM_MMRM=1
ifeq ($(CONFIG_QCOM_VA_MINIDUMP), y)
KBUILD_CPPFLAGS += -DCONFIG_QCOM_VA_MINIDUMP=1
endif

