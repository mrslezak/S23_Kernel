TOUCH_DLKM_ENABLE := true
ifeq ($(TARGET_KERNEL_DLKM_DISABLE), true)
	ifeq ($(TARGET_KERNEL_DLKM_TOUCH_OVERRIDE), false)
		TOUCH_DLKM_ENABLE := false
	endif
endif

ifeq ($(TOUCH_DLKM_ENABLE),  true)
	PRODUCT_PACKAGES += $(KERNEL_MODULES_OUT)/nt36xxx-i2c.ko \
		$(KERNEL_MODULES_OUT)/goodix_ts.ko \
		$(KERNEL_MODULES_OUT)/atmel_mxt_ts.ko \
		$(KERNEL_MODULES_OUT)/synaptics_tcm_ts.ko
endif
