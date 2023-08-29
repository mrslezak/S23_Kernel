// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/of_gpio.h>
#include <linux/of_platform.h>

#include "dp_parser.h"
#include "dp_debug.h"
#if defined(CONFIG_SECDP)
#include "secdp.h"

static struct dp_parser *g_dp_parser;
#endif

static void dp_parser_unmap_io_resources(struct dp_parser *parser)
{
	int i = 0;
	struct dp_io *io = &parser->io;

	for (i = 0; i < io->len; i++)
		msm_dss_iounmap(&io->data[i].io);
}

static int dp_parser_reg(struct dp_parser *parser)
{
	int rc = 0, i = 0;
	u32 reg_count;
	struct platform_device *pdev = parser->pdev;
	struct dp_io *io = &parser->io;
	struct device *dev = &pdev->dev;

	DP_ENTER("\n");

	reg_count = of_property_count_strings(dev->of_node, "reg-names");
	if (reg_count <= 0) {
		DP_ERR("no reg defined\n");
		return -EINVAL;
	}

	io->len = reg_count;
	io->data = devm_kzalloc(dev, sizeof(struct dp_io_data) * reg_count,
			GFP_KERNEL);
	if (!io->data)
		return -ENOMEM;

	for (i = 0; i < reg_count; i++) {
		of_property_read_string_index(dev->of_node,
				"reg-names", i,	&io->data[i].name);
		rc = msm_dss_ioremap_byname(pdev, &io->data[i].io,
			io->data[i].name);
		if (rc) {
			DP_ERR("unable to remap %s resources\n",
				io->data[i].name);
			goto err;
		}
	}

	return 0;
err:
	dp_parser_unmap_io_resources(parser);
	return rc;
}

static const char *dp_get_phy_aux_config_property(u32 cfg_type)
{
	switch (cfg_type) {
	case PHY_AUX_CFG0:
		return "qcom,aux-cfg0-settings";
	case PHY_AUX_CFG1:
		return "qcom,aux-cfg1-settings";
	case PHY_AUX_CFG2:
		return "qcom,aux-cfg2-settings";
	case PHY_AUX_CFG3:
		return "qcom,aux-cfg3-settings";
	case PHY_AUX_CFG4:
		return "qcom,aux-cfg4-settings";
	case PHY_AUX_CFG5:
		return "qcom,aux-cfg5-settings";
	case PHY_AUX_CFG6:
		return "qcom,aux-cfg6-settings";
	case PHY_AUX_CFG7:
		return "qcom,aux-cfg7-settings";
	case PHY_AUX_CFG8:
		return "qcom,aux-cfg8-settings";
	case PHY_AUX_CFG9:
		return "qcom,aux-cfg9-settings";
	default:
		return "unknown";
	}
}

static void dp_parser_phy_aux_cfg_reset(struct dp_parser *parser)
{
	int i = 0;

	for (i = 0; i < PHY_AUX_CFG_MAX; i++)
		parser->aux_cfg[i] = (const struct dp_aux_cfg){ 0 };
}

static int dp_parser_aux(struct dp_parser *parser)
{
	struct device_node *of_node = parser->pdev->dev.of_node;
	int len = 0, i = 0, j = 0, config_count = 0;
	const char *data;
	int const minimum_config_count = 1;

	DP_ENTER("\n");

	for (i = 0; i < PHY_AUX_CFG_MAX; i++) {
		const char *property = dp_get_phy_aux_config_property(i);

		data = of_get_property(of_node, property, &len);
		if (!data) {
			DP_ERR("Unable to read %s\n", property);
			goto error;
		}

		config_count = len - 1;
		if ((config_count < minimum_config_count) ||
			(config_count > DP_AUX_CFG_MAX_VALUE_CNT)) {
			DP_ERR("Invalid config count (%d) configs for %s\n",
					config_count, property);
			goto error;
		}

		parser->aux_cfg[i].offset = data[0];
		parser->aux_cfg[i].cfg_cnt = config_count;
		DP_DEBUG("%s offset=0x%x, cfg_cnt=%d\n",
				property,
				parser->aux_cfg[i].offset,
				parser->aux_cfg[i].cfg_cnt);
		for (j = 1; j < len; j++) {
			parser->aux_cfg[i].lut[j - 1] = data[j];
			DP_DEBUG("%s lut[%d]=0x%x\n",
					property,
					i,
					parser->aux_cfg[i].lut[j - 1]);
		}
	}
		return 0;

error:
	dp_parser_phy_aux_cfg_reset(parser);
	return -EINVAL;
}

#if defined(CONFIG_SECDP_DBG)
int secdp_aux_cfg_show(char *buf)
{
	struct dp_parser *parser = g_dp_parser;
	struct dp_aux_cfg *cfg = parser->aux_cfg;
	int i, rc = 0;

	for (i = 0; i < PHY_AUX_CFG_MAX; i++) {
		rc += scnprintf(buf + rc, PAGE_SIZE - rc,
				"%s: offset=0x%x, value=0x%02x\n",
				dp_phy_aux_config_type_to_string(i),
				cfg[i].offset,
				cfg[i].lut[cfg[i].current_index]);
	}

	return rc;
}

int secdp_aux_cfg_store(char *buf)
{
	struct dp_parser *parser = g_dp_parser;
	struct dp_aux_cfg *cfg = parser->aux_cfg;
	char *tok;
	u32  value;
	int  i, rc = 0;

	for (i = 0; i < PHY_AUX_CFG_MAX; i++) {
		tok = strsep(&buf, ",");
		if (!tok)
			continue;

		rc = kstrtouint(tok, 16, &value);
		if (rc) {
			DP_ERR("error: %s rc:%d\n", tok, rc);
			break;
		}

		cfg[i].lut[cfg[i].current_index] = value;

		DP_DEBUG("offset=0x%x, value=0x%02x\n", cfg[i].offset,
			cfg[i].lut[cfg[i].current_index]);
	}

	return rc;
}
#endif

static int dp_parser_misc(struct dp_parser *parser)
{
	int rc = 0, len = 0, i = 0;
	const char *data = NULL;

	struct device_node *of_node = parser->pdev->dev.of_node;

	DP_ENTER("\n");

	data = of_get_property(of_node, "qcom,logical2physical-lane-map", &len);
	if (data && (len == DP_MAX_PHY_LN)) {
		for (i = 0; i < len; i++)
			parser->l_map[i] = data[i];
	}

	data = of_get_property(of_node, "qcom,pn-swap-lane-map", &len);
	if (data && (len == DP_MAX_PHY_LN)) {
		for (i = 0; i < len; i++)
			parser->l_pnswap |= (data[i] & 0x01) << i;
	}

	rc = of_property_read_u32(of_node,
		"qcom,max-pclk-frequency-khz", &parser->max_pclk_khz);
	if (rc)
		parser->max_pclk_khz = DP_MAX_PIXEL_CLK_KHZ;

	rc = of_property_read_u32(of_node,
		"qcom,max-lclk-frequency-khz", &parser->max_lclk_khz);
	if (rc)
		parser->max_lclk_khz = DP_MAX_LINK_CLK_KHZ;

	return 0;
}

static int dp_parser_msm_hdcp_dev(struct dp_parser *parser)
{
	struct device_node *node;
	struct platform_device *pdev;

	DP_ENTER("\n");

	node = of_find_compatible_node(NULL, NULL, "qcom,msm-hdcp");
	if (!node) {
		// This is a non-fatal error, module initialization can proceed
		DP_WARN("couldn't find msm-hdcp node\n");
		return 0;
	}

	pdev = of_find_device_by_node(node);
	if (!pdev) {
		// This is a non-fatal error, module initialization can proceed
		DP_WARN("couldn't find msm-hdcp pdev\n");
		return 0;
	}

	parser->msm_hdcp_dev = &pdev->dev;

	return 0;
}

static int dp_parser_pinctrl(struct dp_parser *parser)
{
	int rc = 0;
	struct dp_pinctrl *pinctrl = &parser->pinctrl;

	DP_ENTER("\n");

	pinctrl->pin = devm_pinctrl_get(&parser->pdev->dev);

	if (IS_ERR_OR_NULL(pinctrl->pin)) {
		DP_DEBUG("failed to get pinctrl, rc=%d\n", rc);
		goto error;
	}

	pinctrl->state_active = pinctrl_lookup_state(pinctrl->pin,
					"mdss_dp_active");
	if (IS_ERR_OR_NULL(pinctrl->state_active)) {
		rc = PTR_ERR(pinctrl->state_active);
		DP_ERR("failed to get pinctrl active state, rc=%d\n", rc);
		goto error;
	}

	pinctrl->state_suspend = pinctrl_lookup_state(pinctrl->pin,
					"mdss_dp_sleep");
	if (IS_ERR_OR_NULL(pinctrl->state_suspend)) {
		rc = PTR_ERR(pinctrl->state_suspend);
		DP_ERR("failed to get pinctrl suspend state, rc=%d\n", rc);
		goto error;
	}
error:
	return rc;
}

static int dp_parser_gpio(struct dp_parser *parser)
{
	int i = 0;
	struct device *dev = &parser->pdev->dev;
	struct device_node *of_node = dev->of_node;
	struct dss_module_power *mp = &parser->mp[DP_CORE_PM];
	static const char * const dp_gpios[] = {
		"qcom,aux-en-gpio",
		"qcom,aux-sel-gpio",
		"qcom,usbplug-cc-gpio",
	};

	DP_ENTER("\n");

	if (of_find_property(of_node, "qcom,dp-gpio-aux-switch", NULL))
		parser->gpio_aux_switch = true;
	mp->gpio_config = devm_kzalloc(dev,
		sizeof(struct dss_gpio) * ARRAY_SIZE(dp_gpios), GFP_KERNEL);
	if (!mp->gpio_config)
		return -ENOMEM;

	mp->num_gpio = ARRAY_SIZE(dp_gpios);

	for (i = 0; i < ARRAY_SIZE(dp_gpios); i++) {
		mp->gpio_config[i].gpio = of_get_named_gpio(of_node,
			dp_gpios[i], 0);

		if (!gpio_is_valid(mp->gpio_config[i].gpio)) {
			DP_DEBUG("%s gpio not specified\n", dp_gpios[i]);
			/* In case any gpio was not specified, we think gpio
			 * aux switch also was not specified.
			 */
			parser->gpio_aux_switch = false;
			continue;
		}

		strlcpy(mp->gpio_config[i].gpio_name, dp_gpios[i],
			sizeof(mp->gpio_config[i].gpio_name));

		mp->gpio_config[i].value = 0;
	}

#if defined(CONFIG_SECDP)
	for (i = 0; i < ARRAY_SIZE(dp_gpios); i++) {
		DP_INFO("name:%s gpio:%u value:%u\n",
			mp->gpio_config[i].gpio_name,
			mp->gpio_config[i].gpio, mp->gpio_config[i].value);
	}
#endif

	return 0;
}

static const char *dp_parser_supply_node_name(enum dp_pm_type module)
{
	switch (module) {
	case DP_CORE_PM:	return "qcom,core-supply-entries";
	case DP_CTRL_PM:	return "qcom,ctrl-supply-entries";
	case DP_PHY_PM:		return "qcom,phy-supply-entries";
	case DP_PLL_PM:		return "qcom,pll-supply-entries";
	default:		return "???";
	}
}

static int dp_parser_get_vreg(struct dp_parser *parser,
		enum dp_pm_type module)
{
	int i = 0, rc = 0;
	u32 tmp = 0;
	const char *pm_supply_name = NULL;
	struct device_node *supply_node = NULL;
	struct device_node *of_node = parser->pdev->dev.of_node;
	struct device_node *supply_root_node = NULL;
	struct dss_module_power *mp = &parser->mp[module];

	mp->num_vreg = 0;
	pm_supply_name = dp_parser_supply_node_name(module);
#if defined(CONFIG_SECDP)
	DP_DEBUG("pm_supply_name: %s\n", pm_supply_name);
#endif
	supply_root_node = of_get_child_by_name(of_node, pm_supply_name);
	if (!supply_root_node) {
		DP_DEBUG("no supply entry present: %s\n", pm_supply_name);
		goto novreg;
	}

	mp->num_vreg = of_get_available_child_count(supply_root_node);

	if (mp->num_vreg == 0) {
		DP_DEBUG("no vreg\n");
		goto novreg;
	} else {
		DP_DEBUG("vreg found. count=%d\n", mp->num_vreg);
	}

	mp->vreg_config = devm_kzalloc(&parser->pdev->dev,
		sizeof(struct dss_vreg) * mp->num_vreg, GFP_KERNEL);
	if (!mp->vreg_config) {
		rc = -ENOMEM;
		goto error;
	}

	for_each_child_of_node(supply_root_node, supply_node) {
		const char *st = NULL;
		/* vreg-name */
		rc = of_property_read_string(supply_node,
			"qcom,supply-name", &st);
		if (rc) {
			DP_ERR("error reading name. rc=%d\n",
				 rc);
			goto error;
		}
		snprintf(mp->vreg_config[i].vreg_name,
			ARRAY_SIZE((mp->vreg_config[i].vreg_name)), "%s", st);
		/* vreg-min-voltage */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-min-voltage", &tmp);
		if (rc) {
			DP_ERR("error reading min volt. rc=%d\n",
				rc);
			goto error;
		}
		mp->vreg_config[i].min_voltage = tmp;

		/* vreg-max-voltage */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-max-voltage", &tmp);
		if (rc) {
			DP_ERR("error reading max volt. rc=%d\n",
				rc);
			goto error;
		}
		mp->vreg_config[i].max_voltage = tmp;

		/* enable-load */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-enable-load", &tmp);
		if (rc) {
			DP_ERR("error reading enable load. rc=%d\n",
				rc);
			goto error;
		}
		mp->vreg_config[i].enable_load = tmp;

		/* disable-load */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-disable-load", &tmp);
		if (rc) {
			DP_ERR("error reading disable load. rc=%d\n",
				rc);
			goto error;
		}
		mp->vreg_config[i].disable_load = tmp;

		DP_DEBUG("%s min=%d, max=%d, enable=%d, disable=%d\n",
			mp->vreg_config[i].vreg_name,
			mp->vreg_config[i].min_voltage,
			mp->vreg_config[i].max_voltage,
			mp->vreg_config[i].enable_load,
			mp->vreg_config[i].disable_load
			);
		++i;
	}

	return rc;

error:
	if (mp->vreg_config) {
		devm_kfree(&parser->pdev->dev, mp->vreg_config);
		mp->vreg_config = NULL;
	}
novreg:
	mp->num_vreg = 0;

	return rc;
}

static void dp_parser_put_vreg_data(struct device *dev,
	struct dss_module_power *mp)
{
	if (!mp) {
		DEV_ERR("invalid input\n");
		return;
	}

	if (mp->vreg_config) {
		devm_kfree(dev, mp->vreg_config);
		mp->vreg_config = NULL;
	}
	mp->num_vreg = 0;
}

#if defined(CONFIG_SECDP)
static struct regulator *secdp_get_aux_pullup_vreg(struct device *dev)
{
	struct regulator *vreg = NULL;

	vreg = devm_regulator_get(dev, "aux-pullup");
	if (IS_ERR(vreg)) {
		DP_ERR("unable to get aux-pullup vdd supply\n");
		return NULL;
	}

	DP_INFO("get aux-pullup vdd success\n");
	return vreg;
}
#endif

static int dp_parser_regulator(struct dp_parser *parser)
{
	int i, rc = 0;
	struct platform_device *pdev = parser->pdev;

	/* Parse the regulator information */
	for (i = DP_CORE_PM; i < DP_MAX_PM; i++) {
		rc = dp_parser_get_vreg(parser, i);
		if (rc) {
			DP_ERR("get_dt_vreg_data failed for %s. rc=%d\n",
				dp_parser_pm_name(i), rc);
			i--;
			for (; i >= DP_CORE_PM; i--)
				dp_parser_put_vreg_data(&pdev->dev,
					&parser->mp[i]);
			break;
		}
	}

#if defined(CONFIG_SECDP)
	parser->aux_pullup_vreg = secdp_get_aux_pullup_vreg(&pdev->dev);
#endif

	return rc;
}

static bool dp_parser_check_prefix(const char *clk_prefix, const char *clk_name)
{
	return !!strnstr(clk_name, clk_prefix, strlen(clk_name));
}

static void dp_parser_put_clk_data(struct device *dev,
	struct dss_module_power *mp)
{
	if (!mp) {
		DEV_ERR("%s: invalid input\n", __func__);
		return;
	}

	if (mp->clk_config) {
		devm_kfree(dev, mp->clk_config);
		mp->clk_config = NULL;
	}

	mp->num_clk = 0;
}

static void dp_parser_put_gpio_data(struct device *dev,
	struct dss_module_power *mp)
{
	if (!mp) {
		DEV_ERR("%s: invalid input\n", __func__);
		return;
	}

	if (mp->gpio_config) {
		devm_kfree(dev, mp->gpio_config);
		mp->gpio_config = NULL;
	}

	mp->num_gpio = 0;
}

static int dp_parser_init_clk_data(struct dp_parser *parser)
{
	int num_clk = 0, i = 0, rc = 0;
	int core_clk_count = 0, link_clk_count = 0;
	int strm0_clk_count = 0, strm1_clk_count = 0;
	const char *core_clk = "core";
	const char *strm0_clk = "strm0";
	const char *strm1_clk = "strm1";
	const char *link_clk = "link";
	const char *clk_name;
	struct device *dev = &parser->pdev->dev;
	struct dss_module_power *core_power = &parser->mp[DP_CORE_PM];
	struct dss_module_power *strm0_power = &parser->mp[DP_STREAM0_PM];
	struct dss_module_power *strm1_power = &parser->mp[DP_STREAM1_PM];
	struct dss_module_power *link_power = &parser->mp[DP_LINK_PM];

	num_clk = of_property_count_strings(dev->of_node, "clock-names");
	if (num_clk <= 0) {
		DP_ERR("no clocks are defined\n");
		rc = -EINVAL;
		goto exit;
	}

	for (i = 0; i < num_clk; i++) {
		of_property_read_string_index(dev->of_node,
				"clock-names", i, &clk_name);

		if (dp_parser_check_prefix(core_clk, clk_name))
			core_clk_count++;

		if (dp_parser_check_prefix(strm0_clk, clk_name))
			strm0_clk_count++;

		if (dp_parser_check_prefix(strm1_clk, clk_name))
			strm1_clk_count++;

		if (dp_parser_check_prefix(link_clk, clk_name))
			link_clk_count++;
	}

	/* Initialize the CORE power module */
	if (core_clk_count <= 0) {
		DP_ERR("no core clocks are defined\n");
		rc = -EINVAL;
		goto exit;
	}

	core_power->num_clk = core_clk_count;
	core_power->clk_config = devm_kzalloc(dev,
			sizeof(struct dss_clk) * core_power->num_clk,
			GFP_KERNEL);
	if (!core_power->clk_config) {
		rc = -EINVAL;
		goto exit;
	}

	/* Initialize the STREAM0 power module */
	if (strm0_clk_count <= 0) {
		DP_DEBUG("no strm0 clocks are defined\n");
	} else {
		strm0_power->num_clk = strm0_clk_count;
		strm0_power->clk_config = devm_kzalloc(dev,
			sizeof(struct dss_clk) * strm0_power->num_clk,
			GFP_KERNEL);
		if (!strm0_power->clk_config) {
			strm0_power->num_clk = 0;
			rc = -EINVAL;
			goto strm0_clock_error;
		}
	}

	/* Initialize the STREAM1 power module */
	if (strm1_clk_count <= 0) {
		DP_DEBUG("no strm1 clocks are defined\n");
	} else {
		strm1_power->num_clk = strm1_clk_count;
		strm1_power->clk_config = devm_kzalloc(dev,
			sizeof(struct dss_clk) * strm1_power->num_clk,
			GFP_KERNEL);
		if (!strm1_power->clk_config) {
			strm1_power->num_clk = 0;
			rc = -EINVAL;
			goto strm1_clock_error;
		}
	}

	/* Initialize the link power module */
	if (link_clk_count <= 0) {
		DP_ERR("no link clocks are defined\n");
		rc = -EINVAL;
		goto link_clock_error;
	}

	link_power->num_clk = link_clk_count;
	link_power->clk_config = devm_kzalloc(dev,
			sizeof(struct dss_clk) * link_power->num_clk,
			GFP_KERNEL);
	if (!link_power->clk_config) {
		link_power->num_clk = 0;
		rc = -EINVAL;
		goto link_clock_error;
	}

	return rc;

link_clock_error:
	dp_parser_put_clk_data(dev, strm1_power);
strm1_clock_error:
	dp_parser_put_clk_data(dev, strm0_power);
strm0_clock_error:
	dp_parser_put_clk_data(dev, core_power);
exit:
	return rc;
}

static int dp_parser_clock(struct dp_parser *parser)
{
	int rc = 0, i = 0;
	int num_clk = 0;
	int core_clk_index = 0, link_clk_index = 0;
	int core_clk_count = 0, link_clk_count = 0;
	int strm0_clk_index = 0, strm1_clk_index = 0;
	int strm0_clk_count = 0, strm1_clk_count = 0;
	int clock_mmrm = 0;
	const char *clk_name;
	const char *core_clk = "core";
	const char *strm0_clk = "strm0";
	const char *strm1_clk = "strm1";
	const char *link_clk = "link";
	struct device *dev = &parser->pdev->dev;
	struct dss_module_power *core_power;
	struct dss_module_power *strm0_power;
	struct dss_module_power *strm1_power;
	struct dss_module_power *link_power;

	core_power = &parser->mp[DP_CORE_PM];
	strm0_power = &parser->mp[DP_STREAM0_PM];
	strm1_power = &parser->mp[DP_STREAM1_PM];
	link_power = &parser->mp[DP_LINK_PM];

	rc =  dp_parser_init_clk_data(parser);
	if (rc) {
		DP_ERR("failed to initialize power data\n");
		rc = -EINVAL;
		goto exit;
	}

	core_clk_count = core_power->num_clk;
	link_clk_count = link_power->num_clk;
	strm0_clk_count = strm0_power->num_clk;
	strm1_clk_count = strm1_power->num_clk;

	num_clk = of_property_count_strings(dev->of_node, "clock-names");

	for (i = 0; i < num_clk; i++) {
		of_property_read_string_index(dev->of_node, "clock-names",
				i, &clk_name);

		if (dp_parser_check_prefix(core_clk, clk_name) &&
				core_clk_index < core_clk_count) {
			struct dss_clk *clk =
				&core_power->clk_config[core_clk_index];
			strlcpy(clk->clk_name, clk_name, sizeof(clk->clk_name));
			clk->type = DSS_CLK_AHB;
			core_clk_index++;
		} else if (dp_parser_check_prefix(link_clk, clk_name) &&
			   link_clk_index < link_clk_count) {
			struct dss_clk *clk =
				&link_power->clk_config[link_clk_index];
			strlcpy(clk->clk_name, clk_name, sizeof(clk->clk_name));
			link_clk_index++;
			clock_mmrm = 0;
			of_property_read_u32_index(dev->of_node, "clock-mmrm", i, &clock_mmrm);
			if (clock_mmrm) {
				clk->type = DSS_CLK_MMRM;
				clk->mmrm.clk_id = clock_mmrm;
			} else if (!strcmp(clk_name, "link_clk_src")) {
				clk->type = DSS_CLK_PCLK;
			} else {
				clk->type = DSS_CLK_AHB;
			}
		} else if (dp_parser_check_prefix(strm0_clk, clk_name) &&
			   strm0_clk_index < strm0_clk_count) {
			struct dss_clk *clk =
				&strm0_power->clk_config[strm0_clk_index];
			strlcpy(clk->clk_name, clk_name, sizeof(clk->clk_name));
			strm0_clk_index++;

			clk->type = DSS_CLK_PCLK;
		} else if (dp_parser_check_prefix(strm1_clk, clk_name) &&
			   strm1_clk_index < strm1_clk_count) {
			struct dss_clk *clk =
				&strm1_power->clk_config[strm1_clk_index];
			strlcpy(clk->clk_name, clk_name, sizeof(clk->clk_name));
			strm1_clk_index++;

			clk->type = DSS_CLK_PCLK;
		}
	}

	DP_DEBUG("clock parsing successful\n");

exit:
	return rc;
}

static int dp_parser_catalog(struct dp_parser *parser)
{
	int rc;
	u32 version;
	struct device *dev = &parser->pdev->dev;

	rc = of_property_read_u32(dev->of_node, "qcom,phy-version", &version);

	if (!rc)
		parser->hw_cfg.phy_version = version;

	return 0;
}

static int dp_parser_mst(struct dp_parser *parser)
{
	struct device *dev = &parser->pdev->dev;
	int i;

	parser->has_mst = of_property_read_bool(dev->of_node,
			"qcom,mst-enable");
#if (defined(CONFIG_SECDP) && !defined(CONFIG_SECDP_MST))
	parser->has_mst = false;
	DP_DEBUG("[secdp] mst disable!\n");
#endif
	parser->has_mst_sideband = parser->has_mst;

	DP_DEBUG("mst parsing successful. mst:%d\n", parser->has_mst);

	for (i = 0; i < MAX_DP_MST_STREAMS; i++) {
		of_property_read_u32_index(dev->of_node,
				"qcom,mst-fixed-topology-ports", i,
				&parser->mst_fixed_port[i]);
	}

	return 0;
}

static void dp_parser_dsc(struct dp_parser *parser)
{
	struct device *dev = &parser->pdev->dev;

	parser->dsc_feature_enable = of_property_read_bool(dev->of_node,
			"qcom,dsc-feature-enable");
#if defined(SECDP_MAX_HBR2)
	parser->dsc_feature_enable = false;
#endif

	parser->dsc_continuous_pps = of_property_read_bool(dev->of_node,
			"qcom,dsc-continuous-pps");

	DP_DEBUG("dsc parsing successful. dsc:%d\n",
			parser->dsc_feature_enable);
	DP_DEBUG("cont_pps:%d\n",
			parser->dsc_continuous_pps);
}

static void dp_parser_qos(struct dp_parser *parser)
{
	struct device *dev = &parser->pdev->dev;
	u32 mask, latency;
	int rc;

	rc = of_property_read_u32(dev->of_node, "qcom,qos-cpu-latency-us", &latency);
	if (rc)
		return;

	rc = of_property_read_u32(dev->of_node, "qcom,qos-cpu-mask", &mask);
	if (rc)
		return;

	parser->qos_cpu_mask = mask;
	parser->qos_cpu_latency = latency;

	DP_DEBUG("qos parsing successful. mask:%x latency:%ld\n", mask, latency);
}

static void dp_parser_fec(struct dp_parser *parser)
{
	struct device *dev = &parser->pdev->dev;

	parser->fec_feature_enable = of_property_read_bool(dev->of_node,
			"qcom,fec-feature-enable");
#if defined(SECDP_MAX_HBR2)
	parser->fec_feature_enable = false;
#endif

	DP_DEBUG("fec parsing successful. fec:%d\n",
			parser->fec_feature_enable);
}

static void dp_parser_widebus(struct dp_parser *parser)
{
	struct device *dev = &parser->pdev->dev;

	parser->has_widebus = of_property_read_bool(dev->of_node,
			"qcom,widebus-enable");

	DP_DEBUG("widebus parsing successful. widebus:%d\n",
			parser->has_widebus);
}

#if defined(CONFIG_SECDP)
static void secdp_parse_phy_param(struct dp_parser *parser);
#if IS_ENABLED(CONFIG_COMBO_REDRIVER_PS5169)
static void secdp_parse_ps5169_param(struct dp_parser *parser);
#endif

static void secdp_parse_misc(struct dp_parser *parser)
{
	struct device *dev = &parser->pdev->dev;
	struct device_node *of_node = dev->of_node;
	const char *data;
	int len = 0;

#if IS_ENABLED(CONFIG_COMBO_REDRIVER_PS5169)
	secdp_parse_ps5169_param(parser);
#endif
	secdp_parse_phy_param(parser);

	parser->cc_dir_inv = of_property_read_bool(dev->of_node,
			"secdp,cc-dir-inv");
	DP_DEBUG("secdp,cc-dir-inv: %d\n", parser->cc_dir_inv);

	parser->aux_sel_inv = of_property_read_bool(dev->of_node,
			"secdp,aux-sel-inv");
	DP_DEBUG("secdp,aux-sel-inv: %d\n", parser->aux_sel_inv);

	data = of_get_property(of_node, "secdp,redrv", &len);
	if (data) {
		if (!strncmp(data, "ptn36502", len))
			parser->use_redrv = SECDP_REDRV_PTN36502;
		else if (!strncmp(data, "ps5169", len))
			parser->use_redrv = SECDP_REDRV_PS5169;
		else
			parser->use_redrv = SECDP_REDRV_NONE;
	}
	DP_DEBUG("secdp,redrv: %s, %s\n", data,
		secdp_redrv_to_string(parser->use_redrv));

	data = of_get_property(of_node, "secdp,dex-dft-res", &len);
	if (data) {
		if (!strncmp(data, "3440x1440", len))
			parser->dex_dft_res = DEX_RES_3440X1440;
	}
	DP_DEBUG("secdp,dex-dft-res: %s, %s\n", data,
		secdp_dex_res_to_string(parser->dex_dft_res));

	parser->prefer_support = of_property_read_bool(dev->of_node,
			"secdp,prefer-res");
	DP_DEBUG("secdp,prefer-res: %d\n", parser->prefer_support);

	parser->mrr_fps_nolimit = of_property_read_bool(dev->of_node,
			"secdp,mrr-fps-nolimit");
	DP_DEBUG("secdp,mrr_fps_nolimit: %d\n", parser->mrr_fps_nolimit);
}

static const char *secdp_get_phy_pre_emphasis(u32 lvl)
{
	switch (lvl) {
	case PHY_PRE_EMP0:
		return "secdp,vm-pre-emphasis-0";
	case PHY_PRE_EMP1:
		return "secdp,vm-pre-emphasis-1";
	case PHY_PRE_EMP2:
		return "secdp,vm-pre-emphasis-2";
	case PHY_PRE_EMP3:
		return "secdp,vm-pre-emphasis-3";
	default:
		return "secdp,vm-pre-emphasis-unknown";
	}
}

static const char *secdp_get_phy_voltage_swing(u32 lvl)
{
	switch (lvl) {
	case PHY_VOLTAGE_SWING0:
		return "secdp,vm-voltage-swing-0";
	case PHY_VOLTAGE_SWING1:
		return "secdp,vm-voltage-swing-1";
	case PHY_VOLTAGE_SWING2:
		return "secdp,vm-voltage-swing-2";
	case PHY_VOLTAGE_SWING3:
		return "secdp,vm-voltage-swing-3";
	default:
		return "secdp,vm-voltage-swing-unknown";
	}
}

static int _secdp_parse_phy_old(struct dp_parser *parser)
{
	struct device_node *of_node = parser->pdev->dev.of_node;
	int len = 0, i = 0, j = 0;
	const char *data;

	DP_ENTER("\n");

	for (i = 0; i < MAX_PRE_EMP_LEVELS; i++) {
		const char *property = secdp_get_phy_pre_emphasis(i);

		data = of_get_property(of_node, property, &len);
		if (!data || len != 4) {
			DP_ERR("Unable to read %s, len:%d\n", property, len);
			goto error;
		}

		for (j = 0; j < 4; j++)
			parser->vm_pre_emphasis[i][j] = data[j];
	}

	for (i = 0; i < MAX_VOLTAGE_LEVELS; i++) {
		const char *property = secdp_get_phy_voltage_swing(i);

		data = of_get_property(of_node, property, &len);
		if (!data || len != 4) {
			DP_ERR("Unable to read %s, len:%d\n", property, len);
			goto error;
		}

		for (j = 0; j < 4; j++)
			parser->vm_voltage_swing[i][j] = data[j];
	}
	return 0;
error:
	return -EINVAL;
}

static const char *secdp_get_phy_pre_emphasis_hbr2_hbr3(u32 lvl)
{
	switch (lvl) {
	case PHY_PRE_EMP0:
		return "secdp,pre-emp-hbr2-hbr3-0";
	case PHY_PRE_EMP1:
		return "secdp,pre-emp-hbr2-hbr3-1";
	case PHY_PRE_EMP2:
		return "secdp,pre-emp-hbr2-hbr3-2";
	case PHY_PRE_EMP3:
		return "secdp,pre-emp-hbr2-hbr3-3";
	default:
		return "secdp,pre-emp-hbr2-hbr3-unknown";
	}
}

static const char *secdp_get_phy_voltage_swing_hbr2_hbr3(u32 lvl)
{
	switch (lvl) {
	case PHY_VOLTAGE_SWING0:
		return "secdp,swing-hbr2-hbr3-0";
	case PHY_VOLTAGE_SWING1:
		return "secdp,swing-hbr2-hbr3-1";
	case PHY_VOLTAGE_SWING2:
		return "secdp,swing-hbr2-hbr3-2";
	case PHY_VOLTAGE_SWING3:
		return "secdp,swing-hbr2-hbr3-3";
	default:
		return "secdp,swing-hbr2-hbr3-unknown";
	}
}

static int _secdp_parse_phy_hbr2_hbr3(struct dp_parser *parser)
{
	struct device_node *of_node = parser->pdev->dev.of_node;
	int len = 0, i = 0, j = 0;
	const char *data;

	DP_ENTER("\n");

	for (i = 0; i < MAX_PRE_EMP_LEVELS; i++) {
		const char *property = secdp_get_phy_pre_emphasis_hbr2_hbr3(i);

		data = of_get_property(of_node, property, &len);
		if (!data || len != 4) {
			DP_ERR("Unable to read %s, len:%d\n", property, len);
			goto error;
		}

		for (j = 0; j < 4; j++)
			parser->dp_pre_emp_hbr2_hbr3[i][j] = data[j];
	}

	for (i = 0; i < MAX_VOLTAGE_LEVELS; i++) {
		const char *property = secdp_get_phy_voltage_swing_hbr2_hbr3(i);

		data = of_get_property(of_node, property, &len);
		if (!data || len != 4) {
			DP_ERR("Unable to read %s, len:%d\n", property, len);
			goto error;
		}

		for (j = 0; j < 4; j++)
			parser->dp_swing_hbr2_hbr3[i][j] = data[j];
	}
	return 0;
error:
	return -EINVAL;
}

static const char *secdp_get_phy_pre_emphasis_hbr_rbr(u32 lvl)
{
	switch (lvl) {
	case PHY_PRE_EMP0:
		return "secdp,pre-emp-hbr-rbr-0";
	case PHY_PRE_EMP1:
		return "secdp,pre-emp-hbr-rbr-1";
	case PHY_PRE_EMP2:
		return "secdp,pre-emp-hbr-rbr-2";
	case PHY_PRE_EMP3:
		return "secdp,pre-emp-hbr-rbr-3";
	default:
		return "secdp,pre-emp-hbr-rbr-unknown";
	}
}

static const char *secdp_get_phy_voltage_swing_hbr_rbr(u32 lvl)
{
	switch (lvl) {
	case PHY_VOLTAGE_SWING0:
		return "secdp,swing-hbr-rbr-0";
	case PHY_VOLTAGE_SWING1:
		return "secdp,swing-hbr-rbr-1";
	case PHY_VOLTAGE_SWING2:
		return "secdp,swing-hbr-rbr-2";
	case PHY_VOLTAGE_SWING3:
		return "secdp,swing-hbr-rbr-3";
	default:
		return "secdp,swing-hbr-rbr-unknown";
	}
}

static int _secdp_parse_phy_hbr_rbr(struct dp_parser *parser)
{
	struct device_node *of_node = parser->pdev->dev.of_node;
	int len = 0, i = 0, j = 0;
	const char *data;

	DP_ENTER("\n");

	for (i = 0; i < MAX_PRE_EMP_LEVELS; i++) {
		const char *property = secdp_get_phy_pre_emphasis_hbr_rbr(i);

		data = of_get_property(of_node, property, &len);
		if (!data || len != 4) {
			DP_ERR("Unable to read %s, len:%d\n", property, len);
			goto error;
		}

		for (j = 0; j < 4; j++)
			parser->dp_pre_emp_hbr_rbr[i][j] = data[j];
	}

	for (i = 0; i < MAX_VOLTAGE_LEVELS; i++) {
		const char *property = secdp_get_phy_voltage_swing_hbr_rbr(i);

		data = of_get_property(of_node, property, &len);
		if (!data || len != 4) {
			DP_ERR("Unable to read %s, len:%d\n", property, len);
			goto error;
		}

		for (j = 0; j < 4; j++)
			parser->dp_swing_hbr_rbr[i][j] = data[j];
	}
	return 0;
error:
	return -EINVAL;
}


static void secdp_parse_phy_param(struct dp_parser *parser)
{
	_secdp_parse_phy_old(parser);
	_secdp_parse_phy_hbr2_hbr3(parser);
	_secdp_parse_phy_hbr_rbr(parser);
}

/*********************************************
 ***         default DP PHY params         ***
 ***    referred from dp_catalog_v420.c    ***
 *********************************************/
static u8 const vm_pre_emphasis[MAX_VOLTAGE_LEVELS][MAX_PRE_EMP_LEVELS] = {
	{0x00, 0x0D, 0x15, 0xFF}, /* pe0,   0 db */
	{0x00, 0x0D, 0x15, 0xFF}, /* pe1, 3.5 db */
	{0x00, 0x0C, 0xFF, 0xFF}, /* pe2, 6.0 db */
	{0xFF, 0xFF, 0xFF, 0xFF}  /* pe3, 9.5 db */
};

/* voltage swing, 0.2v and 1.0v are not support */
static u8 const vm_voltage_swing[MAX_VOLTAGE_LEVELS][MAX_PRE_EMP_LEVELS] = {
	{0x07, 0x0F, 0x14, 0xFF}, /* sw0, 0.4v  */
	{0x11, 0x1D, 0x1F, 0xFF}, /* sw1, 0.6 v */
	{0x18, 0x1F, 0xFF, 0xFF}, /* sw1, 0.8 v */
	{0xFF, 0xFF, 0xFF, 0xFF}  /* sw1, 1.2 v, optional */
};

static u8 const dp_pre_emp_hbr2_hbr3[MAX_VOLTAGE_LEVELS][MAX_PRE_EMP_LEVELS] = {
	{0x00, 0x0C, 0x14, 0x1A}, /* pe0,   0 db */
	{0x00, 0x0C, 0x13, 0xFF}, /* pe1, 3.5 db */
	{0x00, 0x0C, 0xFF, 0xFF}, /* pe2, 6.0 db */
	{0xFF, 0xFF, 0xFF, 0xFF}  /* pe3, 9.5 db */
};

static u8 const dp_swing_hbr2_hbr3[MAX_VOLTAGE_LEVELS][MAX_PRE_EMP_LEVELS] = {
	{0x09, 0x11, 0x19, 0x1F}, /* sw0, 0.4v */
	{0x12, 0x1A, 0x1F, 0xFF}, /* sw1, 0.6v */
	{0x1C, 0x1F, 0xFF, 0xFF}, /* sw1, 0.8v */
	{0xFF, 0xFF, 0xFF, 0xFF}  /* sw1, 1.2v */
};

static u8 const dp_pre_emp_hbr_rbr[MAX_VOLTAGE_LEVELS][MAX_PRE_EMP_LEVELS] = {
	{0x00, 0x0B, 0x13, 0x19}, /* pe0,   0 db */
	{0x00, 0x0C, 0x14, 0xFF}, /* pe1, 3.5 db */
	{0x00, 0x0B, 0xFF, 0xFF}, /* pe2, 6.0 db */
	{0xFF, 0xFF, 0xFF, 0xFF}  /* pe3, 9.5 db */
};

static u8 const dp_swing_hbr_rbr[MAX_VOLTAGE_LEVELS][MAX_PRE_EMP_LEVELS] = {
	{0x0A, 0x11, 0x19, 0x1F}, /* sw0, 0.4v */
	{0x12, 0x1A, 0x1F, 0xFF}, /* sw1, 0.6v */
	{0x1D, 0x1F, 0xFF, 0xFF}, /* sw1, 0.8v */
	{0xFF, 0xFF, 0xFF, 0xFF}  /* sw1, 1.2v */
};

static void secdp_set_default_phy_param(struct dp_parser *parser,
			enum secdp_hw_ver_t hw, enum secdp_phy_param_t vxpx)
{
	int i, j;

	for (i = 0; i < MAX_VOLTAGE_LEVELS; i++) {
		for (j = 0; j < MAX_PRE_EMP_LEVELS; j++) {
			if (hw == DP_HW_MAX || hw == DP_HW_LEGACY) {
				if (vxpx == DP_PARAM_MAX || vxpx == DP_PARAM_PX) {
					parser->vm_pre_emphasis[i][j] =
						vm_pre_emphasis[i][j];
				}
				if (vxpx == DP_PARAM_MAX || vxpx == DP_PARAM_VX) {
					parser->vm_voltage_swing[i][j] =
						vm_voltage_swing[i][j];
				}
			}
			if (hw == DP_HW_MAX || hw == DP_HW_V123_HBR2_HBR3) {
				if (vxpx == DP_PARAM_MAX || vxpx == DP_PARAM_PX) {
					parser->dp_pre_emp_hbr2_hbr3[i][j] =
						dp_pre_emp_hbr2_hbr3[i][j];
				}
				if (vxpx == DP_PARAM_MAX || vxpx == DP_PARAM_VX) {
					parser->dp_swing_hbr2_hbr3[i][j] =
						dp_swing_hbr2_hbr3[i][j];
				}
			}
			if (hw == DP_HW_MAX || hw == DP_HW_V123_HBR_RBR) {
				if (vxpx == DP_PARAM_MAX || vxpx == DP_PARAM_PX) {
					parser->dp_pre_emp_hbr_rbr[i][j] =
						dp_pre_emp_hbr_rbr[i][j];
				}
				if (vxpx == DP_PARAM_MAX || vxpx == DP_PARAM_VX) {
					parser->dp_swing_hbr_rbr[i][j] =
						dp_swing_hbr_rbr[i][j];
				}
			}
		}
	}
}
#endif

#if defined(CONFIG_SECDP_DBG)
static u8 *_secdp_get_phy_param(enum secdp_hw_ver_t hw,
			enum secdp_phy_param_t vxpx, int idx)
{
	struct dp_parser *parser = g_dp_parser;
	u8 *val = NULL;

	switch (hw) {
	case DP_HW_LEGACY:
		switch (vxpx) {
		case DP_PARAM_VX:
			val = parser->vm_voltage_swing[idx];
			break;
		case DP_PARAM_PX:
			val = parser->vm_pre_emphasis[idx];
			break;
		default:
			DP_ERR("unknown vxpx type: %d\n", vxpx);
			break;
		}
		break;
	case DP_HW_V123_HBR2_HBR3:
		switch (vxpx) {
		case DP_PARAM_VX:
			val = parser->dp_swing_hbr2_hbr3[idx];
			break;
		case DP_PARAM_PX:
			val = parser->dp_pre_emp_hbr2_hbr3[idx];
			break;
		default:
			DP_ERR("unknown vxpx type: %d\n", vxpx);
			break;
		}
		break;
	case DP_HW_V123_HBR_RBR:
		switch (vxpx) {
		case DP_PARAM_VX:
			val = parser->dp_swing_hbr_rbr[idx];
			break;
		case DP_PARAM_PX:
			val = parser->dp_pre_emp_hbr_rbr[idx];
			break;
		default:
			DP_ERR("unknown vxpx type: %d\n", vxpx);
			break;
		}
		break;
	default:
		DP_ERR("unknown hw:%d\n", hw);
		break;
	}

	return val;
}

int secdp_parse_vxpx_show(enum secdp_hw_ver_t hw,
			enum secdp_phy_param_t vxpx, char *buf)
{
	u8 *val[MAX_VOLTAGE_LEVELS];
	int i, rc = 0;

	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "\n%s | %s\n=====\n",
				secdp_hw_to_string(hw),
				secdp_phy_type_to_string(vxpx));

	for (i = 0; i < MAX_VOLTAGE_LEVELS; i++) {
		val[i] = _secdp_get_phy_param(hw, vxpx, i);
		if (!val[i])
			break;

		rc += scnprintf(buf + rc, PAGE_SIZE - rc,
				"%02x,%02x,%02x,%02x",
				val[i][0], val[i][1], val[i][2], val[i][3]);

		if (i < MAX_VOLTAGE_LEVELS - 1)
			rc += scnprintf(buf + rc, PAGE_SIZE - rc, ",\n");
		else
			rc += scnprintf(buf + rc, PAGE_SIZE - rc, "\n");
	}

	return rc;
}

int secdp_parse_vxpx_store(enum secdp_hw_ver_t hw,
			enum secdp_phy_param_t vxpx, char *buf)
{
	struct dp_parser *parser = g_dp_parser;
	u8   *val[MAX_VOLTAGE_LEVELS];
	char *tok;
	u32  value;
	int  i, j, rc = 0;

	if (!strncmp(buf, "reset_all", strlen("reset_all"))) {
		DP_DEBUG("[all] reset!\n");
		secdp_set_default_phy_param(parser, DP_HW_MAX, DP_PARAM_MAX);
		goto end;
	}

	if (!strncmp(buf, "reset", strlen("reset"))) {
		DP_DEBUG("[%s,%s] reset!\n",
			secdp_hw_to_string(hw), secdp_phy_type_to_string(vxpx));
		secdp_set_default_phy_param(parser, hw, vxpx);
		goto end;
	}

	DP_DEBUG("[%s,%s] set new params!\n",
		secdp_hw_to_string(hw), secdp_phy_type_to_string(vxpx));

	for (i = 0; i < MAX_VOLTAGE_LEVELS; i++) {
		val[i] = _secdp_get_phy_param(hw, vxpx, i);
		if (!val[i]) {
			rc = -EINVAL;
			break;
		}

		for (j = 0; j < MAX_PRE_EMP_LEVELS; j++) {
			tok = strsep(&buf, ",");
			if (!tok)
				continue;

			rc = kstrtouint(tok, 16, &value);
			if (rc) {
				DP_ERR("error: %s rc:%d\n", tok, rc);
				goto end;
			}

			val[i][j] = value;
		}
	}
end:
	return rc;
}

int secdp_show_phy_param(char *buf)
{
	int  hw, rc = 0;

	for (hw = 0; hw < DP_HW_MAX; hw++) {
		rc += secdp_parse_vxpx_show(hw, DP_PARAM_PX, buf + rc);
		rc += secdp_parse_vxpx_show(hw, DP_PARAM_VX, buf + rc);
	}

	return rc;
}
#endif

#if defined(CONFIG_SECDP) && IS_ENABLED(CONFIG_COMBO_REDRIVER_PS5169)
static const char *secdp_get_ps5169_rbr_eq0(u32 lvl)
{
	switch (lvl) {
	case PHY_PS5169_EMP0:
		return "secdp,redrv-rbr-eq0-0";
	case PHY_PS5169_EMP1:
		return "secdp,redrv-rbr-eq0-1";
	case PHY_PS5169_EMP2:
		return "secdp,redrv-rbr-eq0-2";
	case PHY_PS5169_EMP3:
		return "secdp,redrv-rbr-eq0-3";
	default:
		return "secdp,redrv-rbr-eq0-unknown";
	}
}

static const char *secdp_get_ps5169_rbr_eq1(u32 lvl)
{
	switch (lvl) {
	case PHY_PS5169_SWING0:
		return "secdp,redrv-rbr-eq1-0";
	case PHY_PS5169_SWING1:
		return "secdp,redrv-rbr-eq1-1";
	case PHY_PS5169_SWING2:
		return "secdp,redrv-rbr-eq1-2";
	case PHY_PS5169_SWING3:
		return "secdp,redrv-rbr-eq1-3";
	default:
		return "secdp,redrv-rbr-eq1-unknown";
	}
}

static int _secdp_parse_ps5169_rbr(struct dp_parser *parser)
{
	struct device_node *of_node = parser->pdev->dev.of_node;
	int len = 0, i = 0, j = 0;
	const char *data;

	DP_ENTER("\n");

	for (i = 0; i < MAX_PS5169_SWING_LEVELS; i++) {
		const char *property = secdp_get_ps5169_rbr_eq0(i);

		data = of_get_property(of_node, property, &len);
		if (!data || len != 4) {
			DP_ERR("Unable to read %s, len:%d\n", property, len);
			goto error;
		}

		for (j = 0; j < MAX_PS5169_EMP_LEVELS; j++)
			parser->ps5169_rbr_eq0[i][j] = data[j];
	}

	for (i = 0; i < MAX_PS5169_SWING_LEVELS; i++) {
		const char *property = secdp_get_ps5169_rbr_eq1(i);

		data = of_get_property(of_node, property, &len);
		if (!data || len != 4) {
			DP_ERR("Unable to read %s, len:%d\n", property, len);
			goto error;
		}

		for (j = 0; j < MAX_PS5169_EMP_LEVELS; j++)
			parser->ps5169_rbr_eq1[i][j] = data[j];
	}
	return 0;
error:
	return -EINVAL;
}

static const char *secdp_get_ps5169_hbr_eq0(u32 lvl)
{
	switch (lvl) {
	case PHY_PS5169_EMP0:
		return "secdp,redrv-hbr-eq0-0";
	case PHY_PS5169_EMP1:
		return "secdp,redrv-hbr-eq0-1";
	case PHY_PS5169_EMP2:
		return "secdp,redrv-hbr-eq0-2";
	case PHY_PS5169_EMP3:
		return "secdp,redrv-hbr-eq0-3";
	default:
		return "secdp,redrv-hbr-eq0-unknown";
	}
}

static const char *secdp_get_ps5169_hbr_eq1(u32 lvl)
{
	switch (lvl) {
	case PHY_PS5169_SWING0:
		return "secdp,redrv-hbr-eq1-0";
	case PHY_PS5169_SWING1:
		return "secdp,redrv-hbr-eq1-1";
	case PHY_PS5169_SWING2:
		return "secdp,redrv-hbr-eq1-2";
	case PHY_PS5169_SWING3:
		return "secdp,redrv-hbr-eq1-3";
	default:
		return "secdp,redrv-hbr-eq1-unknown";
	}
}

static int _secdp_parse_ps5169_hbr(struct dp_parser *parser)
{
	struct device_node *of_node = parser->pdev->dev.of_node;
	int len = 0, i = 0, j = 0;
	const char *data;

	DP_ENTER("\n");

	for (i = 0; i < MAX_PS5169_SWING_LEVELS; i++) {
		const char *property = secdp_get_ps5169_hbr_eq0(i);

		data = of_get_property(of_node, property, &len);
		if (!data || len != 4) {
			DP_ERR("Unable to read %s, len:%d\n", property, len);
			goto error;
		}

		for (j = 0; j < MAX_PS5169_EMP_LEVELS; j++)
			parser->ps5169_hbr_eq0[i][j] = data[j];
	}

	for (i = 0; i < MAX_PS5169_SWING_LEVELS; i++) {
		const char *property = secdp_get_ps5169_hbr_eq1(i);

		data = of_get_property(of_node, property, &len);
		if (!data || len != 4) {
			DP_ERR("Unable to read %s, len:%d\n", property, len);
			goto error;
		}

		for (j = 0; j < MAX_PS5169_EMP_LEVELS; j++)
			parser->ps5169_hbr_eq1[i][j] = data[j];
	}
	return 0;
error:
	return -EINVAL;
}

static const char *secdp_get_ps5169_hbr2_eq0(u32 lvl)
{
	switch (lvl) {
	case PHY_PS5169_EMP0:
		return "secdp,redrv-hbr2-eq0-0";
	case PHY_PS5169_EMP1:
		return "secdp,redrv-hbr2-eq0-1";
	case PHY_PS5169_EMP2:
		return "secdp,redrv-hbr2-eq0-2";
	case PHY_PS5169_EMP3:
		return "secdp,redrv-hbr2-eq0-3";
	default:
		return "secdp,redrv-hbr2-eq0-unknown";
	}
}

static const char *secdp_get_ps5169_hbr2_eq1(u32 lvl)
{
	switch (lvl) {
	case PHY_PS5169_SWING0:
		return "secdp,redrv-hbr2-eq1-0";
	case PHY_PS5169_SWING1:
		return "secdp,redrv-hbr2-eq1-1";
	case PHY_PS5169_SWING2:
		return "secdp,redrv-hbr2-eq1-2";
	case PHY_PS5169_SWING3:
		return "secdp,redrv-hbr2-eq1-3";
	default:
		return "secdp,redrv-hbr2-eq1-unknown";
	}
}

static int _secdp_parse_ps5169_hbr2(struct dp_parser *parser)
{
	struct device_node *of_node = parser->pdev->dev.of_node;
	int len = 0, i = 0, j = 0;
	const char *data;

	DP_ENTER("\n");

	for (i = 0; i < MAX_PS5169_SWING_LEVELS; i++) {
		const char *property = secdp_get_ps5169_hbr2_eq0(i);

		data = of_get_property(of_node, property, &len);
		if (!data || len != 4) {
			DP_ERR("Unable to read %s, len:%d\n", property, len);
			goto error;
		}

		for (j = 0; j < MAX_PS5169_EMP_LEVELS; j++)
			parser->ps5169_hbr2_eq0[i][j] = data[j];
	}

	for (i = 0; i < MAX_PS5169_SWING_LEVELS; i++) {
		const char *property = secdp_get_ps5169_hbr2_eq1(i);

		data = of_get_property(of_node, property, &len);
		if (!data || len != 4) {
			DP_ERR("Unable to read %s, len:%d\n", property, len);
			goto error;
		}

		for (j = 0; j < MAX_PS5169_EMP_LEVELS; j++)
			parser->ps5169_hbr2_eq1[i][j] = data[j];
	}
	return 0;
error:
	return -EINVAL;
}

static const char *secdp_get_ps5169_hbr3_eq0(u32 lvl)
{
	switch (lvl) {
	case PHY_PS5169_EMP0:
		return "secdp,redrv-hbr3-eq0-0";
	case PHY_PS5169_EMP1:
		return "secdp,redrv-hbr3-eq0-1";
	case PHY_PS5169_EMP2:
		return "secdp,redrv-hbr3-eq0-2";
	case PHY_PS5169_EMP3:
		return "secdp,redrv-hbr3-eq0-3";
	default:
		return "secdp,redrv-hbr3-eq0-unknown";
	}
}

static const char *secdp_get_ps5169_hbr3_eq1(u32 lvl)
{
	switch (lvl) {
	case PHY_PS5169_SWING0:
		return "secdp,redrv-hbr3-eq1-0";
	case PHY_PS5169_SWING1:
		return "secdp,redrv-hbr3-eq1-1";
	case PHY_PS5169_SWING2:
		return "secdp,redrv-hbr3-eq1-2";
	case PHY_PS5169_SWING3:
		return "secdp,redrv-hbr3-eq1-3";
	default:
		return "secdp,redrv-hbr3-eq1-unknown";
	}
}

static int _secdp_parse_ps5169_hbr3(struct dp_parser *parser)
{
	struct device_node *of_node = parser->pdev->dev.of_node;
	int len = 0, i = 0, j = 0;
	const char *data;

	DP_ENTER("\n");

	for (i = 0; i < MAX_PS5169_SWING_LEVELS; i++) {
		const char *property = secdp_get_ps5169_hbr3_eq0(i);

		data = of_get_property(of_node, property, &len);
		if (!data || len != 4) {
			DP_ERR("Unable to read %s, len:%d\n", property, len);
			goto error;
		}

		for (j = 0; j < MAX_PS5169_EMP_LEVELS; j++)
			parser->ps5169_hbr3_eq0[i][j] = data[j];
	}

	for (i = 0; i < MAX_PS5169_SWING_LEVELS; i++) {
		const char *property = secdp_get_ps5169_hbr3_eq1(i);

		data = of_get_property(of_node, property, &len);
		if (!data || len != 4) {
			DP_ERR("Unable to read %s, len:%d\n", property, len);
			goto error;
		}

		for (j = 0; j < MAX_PS5169_EMP_LEVELS; j++)
			parser->ps5169_hbr3_eq1[i][j] = data[j];
	}
	return 0;
error:
	return -EINVAL;
}

static void secdp_parse_ps5169_param(struct dp_parser *parser)
{
	_secdp_parse_ps5169_rbr(parser);
	_secdp_parse_ps5169_hbr(parser);
	_secdp_parse_ps5169_hbr2(parser);
	_secdp_parse_ps5169_hbr3(parser);
}

/*********************************************
 ***    default PS5169 DP EQ0/EQ1 params   ***
 *********************************************/
#define EQ0 0x20
#define EQ1 0x06

static u8 const ps5169_rbr_eq0[MAX_PS5169_SWING_LEVELS][MAX_PS5169_EMP_LEVELS] = {
	{EQ0, EQ0, EQ0, EQ0},
	{EQ0, EQ0, EQ0, EQ0},
	{EQ0, EQ0, EQ0, EQ0},
	{EQ0, EQ0, EQ0, EQ0}
};

/* voltage swing, 0.2v and 1.0v are not support */
static u8 const ps5169_rbr_eq1[MAX_PS5169_SWING_LEVELS][MAX_PS5169_EMP_LEVELS] = {
	{EQ1, EQ1, EQ1, EQ1},
	{EQ1, EQ1, EQ1, EQ1},
	{EQ1, EQ1, EQ1, EQ1},
	{EQ1, EQ1, EQ1, EQ1}
};

static u8 const ps5169_hbr_eq0[MAX_PS5169_SWING_LEVELS][MAX_PS5169_EMP_LEVELS] = {
	{EQ0, EQ0, EQ0, EQ0},
	{EQ0, EQ0, EQ0, EQ0},
	{EQ0, EQ0, EQ0, EQ0},
	{EQ0, EQ0, EQ0, EQ0}
};

static u8 const ps5169_hbr_eq1[MAX_PS5169_SWING_LEVELS][MAX_PS5169_EMP_LEVELS] = {
	{EQ1, EQ1, EQ1, EQ1},
	{EQ1, EQ1, EQ1, EQ1},
	{EQ1, EQ1, EQ1, EQ1},
	{EQ1, EQ1, EQ1, EQ1}
};

static u8 const ps5169_hbr2_eq0[MAX_PS5169_SWING_LEVELS][MAX_PS5169_EMP_LEVELS] = {
	{EQ0, EQ0, EQ0, EQ0},
	{EQ0, EQ0, EQ0, EQ0},
	{EQ0, EQ0, EQ0, EQ0},
	{EQ0, EQ0, EQ0, EQ0}
};

static u8 const ps5169_hbr2_eq1[MAX_PS5169_SWING_LEVELS][MAX_PS5169_EMP_LEVELS] = {
	{EQ1, EQ1, EQ1, EQ1},
	{EQ1, EQ1, EQ1, EQ1},
	{EQ1, EQ1, EQ1, EQ1},
	{EQ1, EQ1, EQ1, EQ1}
};

static u8 const ps5169_hbr3_eq0[MAX_PS5169_SWING_LEVELS][MAX_PS5169_EMP_LEVELS] = {
	{EQ0, EQ0, EQ0, EQ0},
	{EQ0, EQ0, EQ0, EQ0},
	{EQ0, EQ0, EQ0, EQ0},
	{EQ0, EQ0, EQ0, EQ0}
};

static u8 const ps5169_hbr3_eq1[MAX_PS5169_SWING_LEVELS][MAX_PS5169_EMP_LEVELS] = {
	{EQ1, EQ1, EQ1, EQ1},
	{EQ1, EQ1, EQ1, EQ1},
	{EQ1, EQ1, EQ1, EQ1},
	{EQ1, EQ1, EQ1, EQ1}
};

static void secdp_set_default_ps5169_param(struct dp_parser *parser,
			enum secdp_ps5169_eq_t eq, enum secdp_ps5169_link_rate_t link_rate)
{
	int i, j;

	for (i = 0; i < MAX_PS5169_SWING_LEVELS; i++) {
		for (j = 0; j < MAX_PS5169_EMP_LEVELS; j++) {
			if (eq == DP_PS5169_EQ_MAX || eq == DP_PS5169_EQ0) {
				if (link_rate == DP_PS5169_RATE_MAX ||
						link_rate == DP_PS5169_RATE_RBR) {
					parser->ps5169_rbr_eq0[i][j] = ps5169_rbr_eq0[i][j];
				}
				if (link_rate == DP_PS5169_RATE_MAX ||
						link_rate == DP_PS5169_RATE_HBR) {
					parser->ps5169_hbr_eq0[i][j] = ps5169_hbr_eq0[i][j];
				}
				if (link_rate == DP_PS5169_RATE_MAX ||
						link_rate == DP_PS5169_RATE_HBR2) {
					parser->ps5169_hbr2_eq0[i][j] = ps5169_hbr2_eq0[i][j];
				}
				if (link_rate == DP_PS5169_RATE_MAX ||
						link_rate == DP_PS5169_RATE_HBR3) {
					parser->ps5169_hbr3_eq0[i][j] = ps5169_hbr3_eq0[i][j];
				}
			}
			if (eq == DP_PS5169_EQ_MAX || eq == DP_PS5169_EQ1) {
				if (link_rate == DP_PS5169_RATE_MAX ||
						link_rate == DP_PS5169_RATE_RBR) {
					parser->ps5169_rbr_eq1[i][j] = ps5169_rbr_eq1[i][j];
				}
				if (link_rate == DP_PS5169_RATE_MAX ||
						link_rate == DP_PS5169_RATE_HBR) {
					parser->ps5169_hbr_eq1[i][j] = ps5169_hbr_eq1[i][j];
				}
				if (link_rate == DP_PS5169_RATE_MAX ||
						link_rate == DP_PS5169_RATE_HBR2) {
					parser->ps5169_hbr2_eq1[i][j] = ps5169_hbr2_eq1[i][j];
				}
				if (link_rate == DP_PS5169_RATE_MAX ||
						link_rate == DP_PS5169_RATE_HBR3) {
					parser->ps5169_hbr3_eq1[i][j] = ps5169_hbr3_eq1[i][j];
				}
			}
		}
	}
}

#if defined(CONFIG_SECDP_DBG)
static u8 *_secdp_get_ps5169_param(enum secdp_ps5169_eq_t eq,
			enum secdp_ps5169_link_rate_t link_rate, int idx)
{
	struct dp_parser *parser = g_dp_parser;
	u8 *val = NULL;

	switch (eq) {
	case DP_PS5169_EQ0:
		switch (link_rate) {
		case DP_PS5169_RATE_RBR:
			val = parser->ps5169_rbr_eq0[idx];
			break;
		case DP_PS5169_RATE_HBR:
			val = parser->ps5169_hbr_eq0[idx];
			break;
		case DP_PS5169_RATE_HBR2:
			val = parser->ps5169_hbr2_eq0[idx];
			break;
		case DP_PS5169_RATE_HBR3:
			val = parser->ps5169_hbr3_eq0[idx];
			break;
		default:
			DP_ERR("unknown rate: %d\n", link_rate);
			break;
		}
		break;
	case DP_PS5169_EQ1:
		switch (link_rate) {
		case DP_PS5169_RATE_RBR:
			val = parser->ps5169_rbr_eq1[idx];
			break;
		case DP_PS5169_RATE_HBR:
			val = parser->ps5169_hbr_eq1[idx];
			break;
		case DP_PS5169_RATE_HBR2:
			val = parser->ps5169_hbr2_eq1[idx];
			break;
		case DP_PS5169_RATE_HBR3:
			val = parser->ps5169_hbr3_eq1[idx];
			break;
		default:
			DP_ERR("unknown rate: %d\n", link_rate);
			break;
		}
		break;
	default:
		DP_ERR("unknown eq:%d\n", eq);
		break;
	}

	return val;
}

int secdp_parse_ps5169_show(enum secdp_ps5169_eq_t eq,
		enum secdp_ps5169_link_rate_t link_rate, char *buf)
{
	u8 *val[MAX_PS5169_SWING_LEVELS];
	int i, rc = 0;

	rc += scnprintf(buf + rc, PAGE_SIZE - rc, "\n%s | %s\n=====\n",
				secdp_ps5169_eq_to_string(eq),
				secdp_ps5169_rate_to_string(link_rate));

	for (i = 0; i < MAX_PS5169_SWING_LEVELS; i++) {
		val[i] = _secdp_get_ps5169_param(eq, link_rate, i);
		if (!val[i])
			break;

		rc += scnprintf(buf + rc, PAGE_SIZE - rc,
				"%02x,%02x,%02x,%02x",
				val[i][0], val[i][1], val[i][2], val[i][3]);

		if (i < MAX_PS5169_SWING_LEVELS - 1)
			rc += scnprintf(buf + rc, PAGE_SIZE - rc, ",\n");
		else
			rc += scnprintf(buf + rc, PAGE_SIZE - rc, "\n");
	}

	return rc;
}

int secdp_parse_ps5169_store(enum secdp_ps5169_eq_t eq,
		enum secdp_ps5169_link_rate_t link_rate, char *buf)
{
	struct dp_parser *parser = g_dp_parser;
	u8   *val[MAX_PS5169_SWING_LEVELS];
	char *tok;
	u32  value;
	int  i, j, rc = 0;

	if (!strncmp(buf, "reset_all", strlen("reset_all"))) {
		DP_DEBUG("[all] reset!\n");
		secdp_set_default_ps5169_param(parser, DP_PS5169_EQ_MAX, DP_PS5169_RATE_MAX);
		goto end;
	}

	if (!strncmp(buf, "reset", strlen("reset"))) {
		DP_DEBUG("[%s,%s] reset!\n", secdp_ps5169_eq_to_string(eq),
			secdp_ps5169_rate_to_string(link_rate));
		secdp_set_default_ps5169_param(parser, eq, link_rate);
		goto end;
	}

	DP_DEBUG("[%s,%s] set new params!\n", secdp_ps5169_eq_to_string(eq),
		secdp_ps5169_rate_to_string(link_rate));

	for (i = 0; i < MAX_PS5169_SWING_LEVELS; i++) {
		val[i] = _secdp_get_ps5169_param(eq, link_rate, i);
		if (!val[i]) {
			rc = -EINVAL;
			break;
		}

		for (j = 0; j < MAX_PS5169_EMP_LEVELS; j++) {
			tok = strsep(&buf, ",");
			if (!tok)
				continue;

			rc = kstrtouint(tok, 16, &value);
			if (rc) {
				DP_ERR("error: %s rc:%d\n", tok, rc);
				goto end;
			}

			val[i][j] = value;
		}
	}
end:
	return rc;
}

int secdp_show_ps5169_param(char *buf)
{
	int  eq, rc = 0;

	for (eq = 0; eq < DP_PS5169_EQ_MAX; eq++) {
		rc += secdp_parse_ps5169_show(eq, DP_PS5169_RATE_RBR, buf + rc);
		rc += secdp_parse_ps5169_show(eq, DP_PS5169_RATE_HBR, buf + rc);
		rc += secdp_parse_ps5169_show(eq, DP_PS5169_RATE_HBR2, buf + rc);
		rc += secdp_parse_ps5169_show(eq, DP_PS5169_RATE_HBR3, buf + rc);
	}

	return rc;
}
#endif/*CONFIG_SECDP_DBG*/
#endif/*CONFIG_COMBO_REDRIVER_PS5169*/

static int dp_parser_parse(struct dp_parser *parser)
{
	int rc = 0;

	if (!parser) {
		DP_ERR("invalid input\n");
		rc = -EINVAL;
		goto err;
	}

	DP_ENTER("\n");

	rc = dp_parser_reg(parser);
	if (rc)
		goto err;

	rc = dp_parser_aux(parser);
	if (rc)
		goto err;

	rc = dp_parser_misc(parser);
	if (rc)
		goto err;

	rc = dp_parser_clock(parser);
	if (rc)
		goto err;

	rc = dp_parser_regulator(parser);
	if (rc)
		goto err;

	rc = dp_parser_gpio(parser);
	if (rc)
		goto err;

	rc = dp_parser_catalog(parser);
	if (rc)
		goto err;

	rc = dp_parser_pinctrl(parser);
	if (rc)
		goto err;

	rc = dp_parser_msm_hdcp_dev(parser);
	if (rc)
		goto err;

	rc = dp_parser_mst(parser);
	if (rc)
		goto err;

	dp_parser_dsc(parser);
	dp_parser_fec(parser);
	dp_parser_widebus(parser);
	dp_parser_qos(parser);
#if defined(CONFIG_SECDP)
	secdp_parse_misc(parser);
#endif
err:
	return rc;
}

static struct dp_io_data *dp_parser_get_io(struct dp_parser *dp_parser,
				char *name)
{
	int i = 0;
	struct dp_io *io;

	if (!dp_parser) {
		DP_ERR("invalid input\n");
		goto err;
	}

	io = &dp_parser->io;

	for (i = 0; i < io->len; i++) {
		struct dp_io_data *data = &io->data[i];

		if (!strcmp(data->name, name))
			return data;
	}
err:
	return NULL;
}

static void dp_parser_get_io_buf(struct dp_parser *dp_parser, char *name)
{
	int i = 0;
	struct dp_io *io;

	if (!dp_parser) {
		DP_ERR("invalid input\n");
		return;
	}

	io = &dp_parser->io;

	for (i = 0; i < io->len; i++) {
		struct dp_io_data *data = &io->data[i];

		if (!strcmp(data->name, name)) {
			if (!data->buf)
				data->buf = devm_kzalloc(&dp_parser->pdev->dev,
					data->io.len, GFP_KERNEL);
		}
	}
}

static void dp_parser_clear_io_buf(struct dp_parser *dp_parser)
{
	int i = 0;
	struct dp_io *io;

	if (!dp_parser) {
		DP_ERR("invalid input\n");
		return;
	}

	io = &dp_parser->io;

	for (i = 0; i < io->len; i++) {
		struct dp_io_data *data = &io->data[i];

		if (data->buf)
			devm_kfree(&dp_parser->pdev->dev, data->buf);

		data->buf = NULL;
	}
}

struct dp_parser *dp_parser_get(struct platform_device *pdev)
{
	struct dp_parser *parser;

	parser = devm_kzalloc(&pdev->dev, sizeof(*parser), GFP_KERNEL);
	if (!parser)
		return ERR_PTR(-ENOMEM);

	parser->parse = dp_parser_parse;
	parser->get_io = dp_parser_get_io;
	parser->get_io_buf = dp_parser_get_io_buf;
	parser->clear_io_buf = dp_parser_clear_io_buf;
	parser->pdev = pdev;

#if defined(CONFIG_SECDP)
	secdp_set_default_phy_param(parser, DP_HW_MAX, DP_PARAM_MAX);
#if IS_ENABLED(CONFIG_COMBO_REDRIVER_PS5169)
	secdp_set_default_ps5169_param(parser, DP_PS5169_EQ_MAX, DP_PS5169_RATE_MAX);
#endif
	g_dp_parser = parser;
#endif
	return parser;
}

void dp_parser_put(struct dp_parser *parser)
{
	int i = 0;
	struct dss_module_power *power = NULL;

	if (!parser) {
		DP_ERR("invalid parser module\n");
		return;
	}

	power = parser->mp;

	for (i = 0; i < DP_MAX_PM; i++) {
		dp_parser_put_clk_data(&parser->pdev->dev, &power[i]);
		dp_parser_put_vreg_data(&parser->pdev->dev, &power[i]);
		dp_parser_put_gpio_data(&parser->pdev->dev, &power[i]);
	}

	dp_parser_clear_io_buf(parser);
	devm_kfree(&parser->pdev->dev, parser->io.data);
	devm_kfree(&parser->pdev->dev, parser);

#if defined(CONFIG_SECDP)
	g_dp_parser = NULL;
#endif
}
