/*
 * kunit_notifier.c
 * initialize, register, unregister kunit test modules
 */
#include <kunit/test.h>
#include "kunit_notifier.h"

BLOCKING_NOTIFIER_HEAD(kunit_notify_chain);

kunit_notifier_chain_init(abc_common_test_module);
kunit_notifier_chain_init(abc_hub_test_module);
kunit_notifier_chain_init(abc_spec_type1_test_module);
kunit_notifier_chain_init(sec_battery_test_module);
kunit_notifier_chain_init(sec_adc_test_module);
kunit_notifier_chain_init(sec_battery_dt_test_module);
kunit_notifier_chain_init(sec_battery_sysfs_test_module);
kunit_notifier_chain_init(sec_battery_thermal_test_module);
kunit_notifier_chain_init(sec_battery_ttf_test_module);
kunit_notifier_chain_init(sec_battery_vote_test_module);
kunit_notifier_chain_init(sec_battery_wc_test_module);
kunit_notifier_chain_init(sec_pd_test_module);
kunit_notifier_chain_init(sec_step_charging_test_module);
kunit_notifier_chain_init(usb_typec_manager_notifier_test_module);
kunit_notifier_chain_init(sec_cmd_test_module);

int test_executor_init(void)
{
#ifndef CONFIG_UML
	int noti = 0;
#endif
	/* Trigger the built-in kunit tests */
	if (!kunit_run_all_tests())
		pr_warn("Running built-in kunit tests are unsuccessful.\n");
#ifndef CONFIG_UML
	/* Trigger the module kunit tests */
	noti = blocking_notifier_call_chain(&kunit_notify_chain, 0, NULL);
	if (noti == NOTIFY_OK || noti == NOTIFY_DONE)
		return 0;
	pr_warn("Running kunit_notifier_calls are unsuccessful. errno: 0x%x", noti);
#endif
	return 0;
}
EXPORT_SYMBOL_KUNIT(test_executor_init);

int register_kunit_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&kunit_notify_chain, nb);
}
EXPORT_SYMBOL_KUNIT(register_kunit_notifier);

int unregister_kunit_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&kunit_notify_chain, nb);
}
EXPORT_SYMBOL_KUNIT(unregister_kunit_notifier);

static __init int kunit_notifier_init(void)
{
	pr_info("%s\n", __func__);

	kunit_notifier_chain_register(abc_common_test_module);
	kunit_notifier_chain_register(abc_hub_test_module);
	kunit_notifier_chain_register(abc_spec_type1_test_module);
	kunit_notifier_chain_register(sec_battery_test_module);
	kunit_notifier_chain_register(sec_adc_test_module);
	kunit_notifier_chain_register(sec_battery_dt_test_module);
	kunit_notifier_chain_register(sec_battery_sysfs_test_module);
	kunit_notifier_chain_register(sec_battery_thermal_test_module);
	kunit_notifier_chain_register(sec_battery_ttf_test_module);
	kunit_notifier_chain_register(sec_battery_vote_test_module);
	kunit_notifier_chain_register(sec_battery_wc_test_module);
	kunit_notifier_chain_register(sec_pd_test_module);
	kunit_notifier_chain_register(sec_step_charging_test_module);
	kunit_notifier_chain_register(usb_typec_manager_notifier_test_module);
	kunit_notifier_chain_register(sec_cmd_test_module);
	return 0;
}

static void __exit kunit_notifier_exit(void)
{
	kunit_notifier_chain_unregister(abc_common_test_module);
	kunit_notifier_chain_unregister(abc_hub_test_module);
	kunit_notifier_chain_unregister(abc_spec_type1_test_module);
	kunit_notifier_chain_unregister(sec_battery_test_module);
	kunit_notifier_chain_unregister(usb_typec_manager_notifier_test_module);
	kunit_notifier_chain_unregister(sec_cmd_test_module);
}

module_init(kunit_notifier_init);
module_exit(kunit_notifier_exit);

MODULE_DESCRIPTION("Samsung KUnit Driver");
MODULE_AUTHOR("Samsung Electronics");
MODULE_LICENSE("GPL");
