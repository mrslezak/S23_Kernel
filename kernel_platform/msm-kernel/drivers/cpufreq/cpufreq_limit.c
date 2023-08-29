/*
 * drivers/cpufreq/cpufreq_limit.c
 *
 * Remade according to cpufreq change
 * (refer to commit df0eea4488081e0698b0b58ccd1e8c8823e22841
 *                 18c49926c4bf4915e5194d1de3299c0537229f9f)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/cpufreq.h>
#include <linux/cpufreq_limit.h>
#include <linux/err.h>
#include <linux/suspend.h>
#include <linux/cpu.h>
#include <linux/kobject.h>
#include <linux/timer.h>
#include <linux/platform_device.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#endif
#include <trace/hooks/cpufreq.h>

#define MAX_BUF_SIZE	1024
#define MIN(a, b)     (((a) < (b)) ? (a) : (b))
#define MAX(a, b)     (((a) > (b)) ? (a) : (b))

/* adaptive boost from walt */
extern int cpufreq_walt_set_adaptive_freq(unsigned int cpu, unsigned int adaptive_low_freq,
					  unsigned int adaptive_high_freq);
extern int cpufreq_walt_get_adaptive_freq(unsigned int cpu, unsigned int *adaptive_low_freq,
					  unsigned int *adaptive_high_freq);
extern int cpufreq_walt_reset_adaptive_freq(unsigned int cpu);

static unsigned int __read_mostly lpcharge;
module_param(lpcharge, uint, 0444);

/* voltage based freq table */
#if IS_ENABLED(CONFIG_QTI_CPU_VOLTAGE_COOLING_DEVICE)
extern struct freq_voltage_base cflm_vbf;
#else
static struct freq_voltage_base cflm_vbf;
#endif

static DEFINE_MUTEX(cflm_mutex);
#define LIMIT_RELEASE	-1

/* boosted state */
#define BOOSTED	1
#define NOT_BOOSTED	0

#define NUM_CPUS	8
static unsigned int cflm_req_init[NUM_CPUS];
static struct freq_qos_request max_req[NUM_CPUS][CFLM_MAX_ITEM];
static struct freq_qos_request min_req[NUM_CPUS][CFLM_MAX_ITEM];
static struct kobject *cflm_kobj;

struct freq_map {
	unsigned int in;
	unsigned int out;
};

/* input info: freq, time(TBD) */
struct input_info {
	int boosted;
	int min;
	int max;
	u64 time_in_min_limit;
	u64 time_in_max_limit;
	u64 time_in_over_limit;
	ktime_t last_min_limit_time;
	ktime_t last_max_limit_time;
	ktime_t last_over_limit_time;
};
static struct input_info freq_input[CFLM_MAX_ITEM];

struct cflm_parameter {
	/* to make virtual freq table */
	struct cpufreq_frequency_table *cpuftbl_L;
	struct cpufreq_frequency_table *cpuftbl_b;
	unsigned int	unified_cpuftbl[50];
	unsigned int	freq_count;
	bool		table_initialized;

	/* cpu info: silver/gold/prime */
	unsigned int	s_first;
	unsigned int	s_fmin;
	unsigned int	s_fmax;
	unsigned int	g_first;
	unsigned int	g_fmin;
	unsigned int	g_fmax;
	unsigned int	p_first;
	unsigned int	p_fmin;
	unsigned int	p_fmax;

	/* exceptional case */
	unsigned int g_fmin_up;	/* fixed gold clock for performance */

	/* in virtual table little(silver)/big(gold & prime) */
	unsigned int	big_min_freq;
	unsigned int	big_max_freq;
	unsigned int	ltl_min_freq;
	unsigned int	ltl_max_freq;

	/* pre-defined value */
	struct freq_map *silver_boost_map;
	unsigned int	boost_map_size;
	struct freq_map *silver_limit_map;
	unsigned int	limit_map_size;
	unsigned int	silver_divider;

	/* current freq in virtual table */
	unsigned int	min_limit_val;
	unsigned int	max_limit_val;

	/* sched boost type */
	int		sched_boost_type;
	bool		sched_boost_cond;
	bool		sched_boost_enabled;

	/* over limit */
	unsigned int	over_limit;

	/* voltage based clock */
	bool		vol_based_clk;
	int		vbf_offset;	/* gold clock offset for perf */

	bool		aboost_enabled;
	unsigned int	aboost_silver_low;
};


/* TODO: move to dtsi? */
static struct cflm_parameter param = {
	.freq_count		= 0,
	.table_initialized	= false,

	.s_first		= 0,
	.g_first		= 3,
	.p_first		= 7,

	.g_fmin_up		= 0,	/* fixed gold clock for performance */

	.ltl_min_freq		= 0,		/* will be auto updated */
	.ltl_max_freq		= 0,		/* will be auto updated */
	.big_min_freq		= 0,		/* will be auto updated */
	.big_max_freq		= 0,		/* will be auto updated */

	.boost_map_size		= 0,
	.limit_map_size		= 0,
	.silver_divider		= 2,

	.min_limit_val		= -1,
	.max_limit_val		= -1,

	.sched_boost_type	= CONSERVATIVE_BOOST,
	.sched_boost_cond	= false,
	.sched_boost_enabled	= false,

	.over_limit		= 0,

	.vol_based_clk		= false,

	.aboost_enabled 	= true,
	.aboost_silver_low	= 902400,
};

static bool cflm_make_table(void)
{
	int i, count = 0;
	int freq_count = 0;
	unsigned int freq;
	bool ret = false;

	/* big cluster table */
	if (!param.cpuftbl_b)
		goto little;

	for (i = 0; param.cpuftbl_b[i].frequency != CPUFREQ_TABLE_END; i++)
		count = i;

	for (i = count; i >= 0; i--) {
		freq = param.cpuftbl_b[i].frequency;

		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;

		if (freq < param.big_min_freq ||
				freq > param.big_max_freq)
			continue;

		param.unified_cpuftbl[freq_count++] = freq;
	}

little:
	/* LITTLE cluster table */
	if (!param.cpuftbl_L)
		goto done;

	for (i = 0; param.cpuftbl_L[i].frequency != CPUFREQ_TABLE_END; i++)
		count = i;

	for (i = count; i >= 0; i--) {
		freq = param.cpuftbl_L[i].frequency / param.silver_divider;

		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;

		if (freq < param.ltl_min_freq ||
				freq > param.ltl_max_freq)
			continue;

		param.unified_cpuftbl[freq_count++] = freq;
	}

done:
	if (freq_count) {
		pr_debug("%s: unified table is made\n", __func__);
		param.freq_count = freq_count;
		ret = true;
	} else {
		pr_err("%s: cannot make unified table\n", __func__);
	}

	return ret;
}

/**
 * cflm_set_table - cpufreq table from dt via qcom-cpufreq
 */
static void cflm_set_table(int cpu, struct cpufreq_frequency_table *ftbl)
{
	int i, count = 0;
	unsigned int max_freq_b = 0, min_freq_b = UINT_MAX;
	unsigned int max_freq_l = 0, min_freq_l = UINT_MAX;

	if (param.table_initialized)
		return;

	if (cpu == param.s_first)
		param.cpuftbl_L = ftbl;
	else if (cpu == param.p_first)
		param.cpuftbl_b = ftbl;

	if (!param.cpuftbl_L)
		return;

	if (!param.cpuftbl_b)
		return;

	pr_info("%s: freq table is ready, update config\n", __func__);

	/* update little config */
	for (i = 0; param.cpuftbl_L[i].frequency != CPUFREQ_TABLE_END; i++)
		count = i;

	for (i = count; i >= 0; i--) {
		if (param.cpuftbl_L[i].frequency == CPUFREQ_ENTRY_INVALID)
			continue;

		if (param.cpuftbl_L[i].frequency < min_freq_l)
			min_freq_l = param.cpuftbl_L[i].frequency;

		if (param.cpuftbl_L[i].frequency > max_freq_l)
			max_freq_l = param.cpuftbl_L[i].frequency;
	}

	if (!param.ltl_min_freq)
		param.ltl_min_freq = min_freq_l / param.silver_divider;
	if (!param.ltl_max_freq)
		param.ltl_max_freq = max_freq_l / param.silver_divider;

	/* update big config */
	for (i = 0; param.cpuftbl_b[i].frequency != CPUFREQ_TABLE_END; i++)
		count = i;

	for (i = count; i >= 0; i--) {
		if (param.cpuftbl_b[i].frequency == CPUFREQ_ENTRY_INVALID)
			continue;

		if ((param.cpuftbl_b[i].frequency < min_freq_b) &&
			(param.cpuftbl_b[i].frequency > param.ltl_max_freq))
			min_freq_b = param.cpuftbl_b[i].frequency;

		if (param.cpuftbl_b[i].frequency > max_freq_b)
			max_freq_b = param.cpuftbl_b[i].frequency;
	}

	if (!param.big_min_freq)
		param.big_min_freq = min_freq_b;
	if (!param.big_max_freq)
		param.big_max_freq = max_freq_b;

	pr_info("%s: updated: little(%u-%u), big(%u-%u)\n", __func__,
			param.ltl_min_freq, param.ltl_max_freq,
			param.big_min_freq, param.big_max_freq);

	param.table_initialized = cflm_make_table();
}

/**
 * cflm_get_table - fill the cpufreq table to support HMP
 * @buf		a buf that has been requested to fill the cpufreq table
 */
static ssize_t cflm_get_table(char *buf)
{
	ssize_t len = 0;
	int i = 0;

	if (!param.freq_count)
		return len;

	for (i = 0; i < param.freq_count; i++)
		len += snprintf(buf + len, MAX_BUF_SIZE, "%u ",
					param.unified_cpuftbl[i]);

	len--;
	len += snprintf(buf + len, MAX_BUF_SIZE, "\n");

	pr_info("%s: %s\n", __func__, buf);

	return len;
}

static void cflm_update_boost(void)
{
	/* sched boost */
	if (param.sched_boost_cond) {
		if (!param.sched_boost_enabled) {
			pr_debug("%s: sched boost on, type(%d)\n", __func__, param.sched_boost_type);
			sched_set_boost(param.sched_boost_type);
			param.sched_boost_enabled = true;
		} else {
			pr_debug("%s: sched boost already on, do nothing\n", __func__);
		}
	} else {
		if (param.sched_boost_enabled) {
			pr_debug("%s: sched boost off(%d)\n", __func__, NO_BOOST);
			sched_set_boost(NO_BOOST);
			param.sched_boost_enabled = false;
		} else {
			pr_debug("%s: sched boost already off, do nothing\n", __func__);
		}
	}
}

static s32 cflm_freq_qos_read_value(struct freq_constraints *qos,
			enum freq_qos_req_type type)
{
	s32 ret;

	switch (type) {
	case FREQ_QOS_MIN:
		ret = IS_ERR_OR_NULL(qos) ?
			FREQ_QOS_MIN_DEFAULT_VALUE :
			READ_ONCE(qos->min_freq.target_value);
		break;
	case FREQ_QOS_MAX:
		ret = IS_ERR_OR_NULL(qos) ?
			FREQ_QOS_MAX_DEFAULT_VALUE :
			READ_ONCE(qos->max_freq.target_value);
		break;
	default:
		WARN_ON(1);
		ret = 0;
	}

	return ret;
}

static void cflm_update_current_freq(void)
{
	struct cpufreq_policy *policy;
	int s_min = 0, s_max = 0;
	int g_min = 0, g_max = 0;
	int p_min = 0, p_max = 0;

	policy = cpufreq_cpu_get(param.s_first);
	if (policy) {
		s_min = cflm_freq_qos_read_value(&policy->constraints, FREQ_QOS_MIN);
		s_max = cflm_freq_qos_read_value(&policy->constraints, FREQ_QOS_MAX);
		cpufreq_cpu_put(policy);
	}

	policy = cpufreq_cpu_get(param.g_first);
	if (policy) {
		g_min = cflm_freq_qos_read_value(&policy->constraints, FREQ_QOS_MIN);
		g_max = cflm_freq_qos_read_value(&policy->constraints, FREQ_QOS_MAX);
		cpufreq_cpu_put(policy);
	}

	policy = cpufreq_cpu_get(param.p_first);
	if (policy) {
		p_min = cflm_freq_qos_read_value(&policy->constraints, FREQ_QOS_MIN);
		p_max = cflm_freq_qos_read_value(&policy->constraints, FREQ_QOS_MAX);
		cpufreq_cpu_put(policy);
	}

	if (param.aboost_enabled) {
		unsigned int a_low = 0, a_high = 0;

		cpufreq_walt_get_adaptive_freq(param.s_first, &a_low, &a_high);
		pr_info("%s: s((%d, %d) ~ %d), g(%d ~ %d), p(%d ~ %d)\n",
				__func__, a_low, a_high, s_max, g_min, g_max, p_min, p_max);

	} else {
		pr_info("%s: s(%d ~ %d), g(%d ~ %d), p(%d ~ %d)\n",
				__func__, s_min, s_max, g_min, g_max, p_min, p_max);
	}

	/* sched boost condition */
	if ((param.sched_boost_type) && (p_min > 748800))	/* TEMP: hard coding */
		param.sched_boost_cond = true;
	else
		param.sched_boost_cond = false;
}

static bool cflm_max_lock_need_restore(void)
{
	if ((int)param.over_limit <= 0)
		return false;

	if (freq_input[CFLM_USERSPACE].min > 0) {
		if (freq_input[CFLM_USERSPACE].min > (int)param.ltl_max_freq) {
			pr_debug("%s: userspace minlock (%d) > ltl max (%d)\n",
					__func__, freq_input[CFLM_USERSPACE], param.ltl_max_freq);
			return false;
		}
	}

	if (freq_input[CFLM_TOUCH].min > 0) {
		if (freq_input[CFLM_TOUCH].min > (int)param.ltl_max_freq) {
			pr_debug("%s: touch minlock (%d) > ltl max (%d)\n",
					__func__, freq_input[CFLM_TOUCH], param.ltl_max_freq);
			return false;
		}
	}

	return true;
}

static bool cflm_high_pri_min_lock_required(void)
{
	if ((int)param.over_limit <= 0)
		return false;

	if (freq_input[CFLM_USERSPACE].min > 0) {
		if (freq_input[CFLM_USERSPACE].min > (int)param.ltl_max_freq) {
			pr_debug("%s: userspace minlock (%d) > ltl max (%d)\n",
					__func__, freq_input[CFLM_USERSPACE], param.ltl_max_freq);
			return true;
		}
	}

	if (freq_input[CFLM_TOUCH].min > 0) {
		if (freq_input[CFLM_TOUCH].min > (int)param.ltl_max_freq) {
			pr_debug("%s: touch minlock (%d) > ltl max (%d)\n",
					__func__, freq_input[CFLM_TOUCH], param.ltl_max_freq);
			return true;
		}
	}

	return false;
}

static unsigned int cflm_get_vol_matched_freq(unsigned int in_freq)
{
	int i;
	unsigned int out_freq = in_freq;

	if (param.vbf_offset > cflm_vbf.count || param.vbf_offset < 0) {
		pr_err("%s: bad condition(off(%d), cnt(%d))",
			__func__, param.vbf_offset, cflm_vbf.count);
		return out_freq;
	}

	/* start from offset */
	for (i = param.vbf_offset; i < cflm_vbf.count; i++) {
		if (cflm_vbf.table[PRIME_CPU][i] <= in_freq) {
			out_freq = cflm_vbf.table[GOLD_CPU][i - param.vbf_offset];
			break;
		}
	}

	pr_debug("%s: in(%d), out(%d)\n", __func__, in_freq, out_freq);

	return out_freq;
}

static int cflm_get_silver_boost(int freq)
{
	int i;

	for (i = 0; i < param.boost_map_size; i++)
		if (freq >= param.silver_boost_map[i].in)
			return param.silver_boost_map[i].out;
	return freq * param.silver_divider;
}

static int cflm_get_silver_limit(int freq)
{
	int i;

	/* prime limit condition */
	for (i = 0; i < param.limit_map_size; i++)
		if (freq >= param.silver_limit_map[i].in)
			return MIN(param.silver_limit_map[i].out, param.s_fmax);

	/* silver limit condition */
	return freq * param.silver_divider;
}

static int cflm_adaptive_boost(int first_cpu, int min)
{
	struct cpufreq_policy *policy;
	int cpu = 0;
	int ret = 0;

	if (!param.aboost_enabled)
		return -EINVAL;

	policy = cpufreq_cpu_get(first_cpu);
	if (!policy) {
		pr_err("no policy for cpu%d\n", first_cpu);
		return -EFAULT;
	}

	if (strcmp((policy->governor->name), "walt")) {
		pr_err("not supported gov(%s)\n", policy->governor->name);
		return -EFAULT;
	}

	/* now only for silver */
	for_each_cpu(cpu, policy->related_cpus) {
		if (min > 0) {
			pr_debug("%s: set aboost: cpu%d: %d, %d\n", __func__, cpu, param.aboost_silver_low, min);
			ret = cpufreq_walt_set_adaptive_freq(cpu, param.aboost_silver_low, min);
		} else {
			pr_debug("%s: clear aboost: cpu%d\n", __func__, cpu);
			ret = cpufreq_walt_reset_adaptive_freq(cpu);
		}
	}

	cpufreq_cpu_put(policy);

	return ret;
}

static void cflm_freq_decision(int type, int new_min, int new_max)
{
	int cpu = 0;
	int s_min = param.s_fmin;
	int s_max = param.s_fmax;
	int g_min = param.g_fmin;
	int g_max = param.g_fmax;
	int p_min = param.p_fmin;
	int p_max = param.p_fmax;

	bool need_update_user_max = false;
	int new_user_max = FREQ_QOS_MAX_DEFAULT_VALUE;

	pr_info("%s: input: type(%d), min(%d), max(%d)\n",
			__func__, type, new_min, new_max);

	/* update input freq */
	if (new_min != 0) {
		freq_input[type].min = new_min;
		if ((new_min == LIMIT_RELEASE || new_min == param.ltl_min_freq) &&
			freq_input[type].last_min_limit_time != 0) {
			freq_input[type].time_in_min_limit += ktime_to_ms(ktime_get()-
				freq_input[type].last_min_limit_time);
			freq_input[type].last_min_limit_time = 0;
			freq_input[type].boosted = NOT_BOOSTED;
			pr_debug("%s: type(%d), released(%d)\n", __func__, type, freq_input[type].boosted);
		}
		if (new_min != LIMIT_RELEASE && new_min != param.ltl_min_freq &&
			freq_input[type].last_min_limit_time == 0) {
			freq_input[type].last_min_limit_time = ktime_get();
			freq_input[type].boosted = BOOSTED;
			pr_debug("%s: type(%d), boosted(%d)\n", __func__, type, freq_input[type].boosted);
		}
	}

	if (new_max != 0) {
		freq_input[type].max = new_max;
		if ((new_max == LIMIT_RELEASE || new_max == param.big_max_freq) &&
			freq_input[type].last_max_limit_time != 0) {
			freq_input[type].time_in_max_limit += ktime_to_ms(ktime_get() -
				freq_input[type].last_max_limit_time);
			freq_input[type].last_max_limit_time = 0;
		}
		if (new_max != LIMIT_RELEASE && new_max != param.big_max_freq &&
			freq_input[type].last_max_limit_time == 0) {
			freq_input[type].last_max_limit_time = ktime_get();
		}
	}

	if (new_min > 0) {
		if (new_min < param.ltl_min_freq) {
			pr_err("%s: too low freq(%d), set to %d\n",
				__func__, new_min, param.ltl_min_freq);
			new_min = param.ltl_min_freq;
		}

		pr_debug("%s: new_min=%d, ltl_max=%d, over_limit=%d\n", __func__,
				new_min, param.ltl_max_freq, param.over_limit);
		if ((type == CFLM_USERSPACE || type == CFLM_TOUCH) &&
			cflm_high_pri_min_lock_required()) {
			if (freq_input[CFLM_USERSPACE].max > 0) {
				need_update_user_max = true;
				new_user_max = MAX((int)param.over_limit, freq_input[CFLM_USERSPACE].max);
				pr_debug("%s: override new_max %d => %d,  userspace_min=%d, touch_min=%d, ltl_max=%d\n",
						__func__, freq_input[CFLM_USERSPACE].max, new_user_max, freq_input[CFLM_USERSPACE].min,
						freq_input[CFLM_TOUCH].min, param.ltl_max_freq);
			}
		}

		/* boost @gold/prime */
		s_min = cflm_get_silver_boost(new_min);
		if (new_min > param.ltl_max_freq) {
			g_min = MIN(new_min, param.g_fmax);
			p_min = MIN(new_min, param.p_fmax);
		} else {
			g_min = param.g_fmin;
			p_min = param.p_fmin;
		}

		if (cflm_adaptive_boost(param.s_first, s_min) < 0)
			freq_qos_update_request(&min_req[param.s_first][type], s_min);	/* prevent adaptive boost fail */

		freq_qos_update_request(&min_req[param.g_first][type], g_min);
		freq_qos_update_request(&min_req[param.p_first][type], p_min);
	} else if (new_min == LIMIT_RELEASE) {
		for_each_possible_cpu(cpu) {
			freq_qos_update_request(&min_req[cpu][type],
						FREQ_QOS_MIN_DEFAULT_VALUE);
		}

		if (param.aboost_enabled) {
			int i;
			int aggr_state = 0;

			for (i = 0; i < CFLM_MAX_ITEM; i++)
				aggr_state += freq_input[i].boosted;

			if (aggr_state == 0) {
				cflm_adaptive_boost(param.s_first, 0);
				pr_debug("%s: aboost: clear\n", __func__);
			}
		}

		if ((type == CFLM_USERSPACE || type == CFLM_TOUCH) &&
			cflm_max_lock_need_restore()) { // if there is no high priority min lock and over limit is set
			if (freq_input[CFLM_USERSPACE].max > 0) {
				need_update_user_max = true;
				new_user_max = freq_input[CFLM_USERSPACE].max;
				pr_debug("%s: restore new_max => %d\n",
						__func__, new_user_max);
			}
		}
	}

	if (new_max > 0) {
		if (new_max > param.big_max_freq) {
			pr_err("%s: too high freq(%d), set to %d\n",
				__func__, new_max, param.big_max_freq);
			new_max = param.big_max_freq;
		}

		if ((type == CFLM_USERSPACE) && // if userspace maxlock is being set
			cflm_high_pri_min_lock_required()) {
			need_update_user_max = true;
			new_user_max = MAX((int)param.over_limit, freq_input[CFLM_USERSPACE].max);
			pr_debug("%s: force up new_max %d => %d, userspace_min=%d, touch_min=%d, ltl_max=%d\n",
					__func__, new_max, new_user_max, freq_input[CFLM_USERSPACE].min,
					freq_input[CFLM_TOUCH].min, param.ltl_max_freq);
		}

		s_max = cflm_get_silver_limit(new_max);
		if (new_max < param.big_min_freq) {
			/* if silver clock is limited as fmax,
			 * set promised clock for gold cluster
			 */
			if ((new_max == param.s_fmax / param.silver_divider) && (param.g_fmin_up > 0))
				g_max = param.g_fmin_up;
			else
				g_max = param.g_fmin;

			p_max = param.p_fmin;
		} else {
			p_max = MIN(new_max, param.p_fmax);
			if (param.vol_based_clk == true && cflm_vbf.count > 0)
				g_max = MIN(cflm_get_vol_matched_freq(p_max), param.g_fmax);
			else
				g_max = MIN(new_max, param.g_fmax);
		}

		freq_qos_update_request(&max_req[param.s_first][type], s_max);
		freq_qos_update_request(&max_req[param.g_first][type], g_max);
		freq_qos_update_request(&max_req[param.p_first][type], p_max);
	} else if (new_max == LIMIT_RELEASE) {
		for_each_possible_cpu(cpu)
			freq_qos_update_request(&max_req[cpu][type],
						FREQ_QOS_MAX_DEFAULT_VALUE);
	}

	if ((freq_input[type].min <= (int)param.ltl_max_freq || new_user_max != (int)param.over_limit) &&
		freq_input[type].last_over_limit_time != 0) {
		freq_input[type].time_in_over_limit += ktime_to_ms(ktime_get() -
			freq_input[type].last_over_limit_time);
		freq_input[type].last_over_limit_time = 0;
	}
	if (freq_input[type].min > (int)param.ltl_max_freq && new_user_max == (int)param.over_limit &&
		freq_input[type].last_over_limit_time == 0) {
		freq_input[type].last_over_limit_time = ktime_get();
	}

	if (need_update_user_max) {
		pr_debug("%s: update_user_max is true\n", __func__);
		if (new_user_max > param.big_max_freq) {
			pr_debug("%s: too high freq(%d), set to %d\n",
			__func__, new_user_max, param.big_max_freq);
			new_user_max = param.big_max_freq;
		}

		s_max = cflm_get_silver_limit(new_user_max);
		if (new_user_max < param.big_min_freq) {
			/* if silver clock is limited as fmax,
			 * set promised clock for gold cluster
			 */
			if ((new_user_max == param.s_fmax / param.silver_divider) && (param.g_fmin_up > 0))
				g_max = param.g_fmin_up;
			else
				g_max = param.g_fmin;

			p_max = param.p_fmin;
		} else {
			p_max = MIN(new_user_max, param.p_fmax);
			if (param.vol_based_clk == true)
				g_max = MIN(cflm_get_vol_matched_freq(p_max), param.g_fmax);
			else
				g_max = MIN(new_user_max, param.g_fmax);
		}

		pr_info("%s: freq_update_request : new userspace max %d %d %d\n", __func__, s_max, g_max, p_max);
		freq_qos_update_request(&max_req[param.s_first][CFLM_USERSPACE], s_max);
		freq_qos_update_request(&max_req[param.g_first][CFLM_USERSPACE], g_max);
		freq_qos_update_request(&max_req[param.p_first][CFLM_USERSPACE], p_max);
	}

	cflm_update_current_freq();

	cflm_update_boost();
}

static ssize_t cpufreq_table_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	ssize_t len = 0;

	len = cflm_get_table(buf);

	return len;
}

static ssize_t cpufreq_max_limit_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	return snprintf(buf, MAX_BUF_SIZE, "%d\n", param.max_limit_val);
}

static ssize_t cpufreq_max_limit_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t n)
{
	int freq;
	int ret = -EINVAL;

	ret = kstrtoint(buf, 10, &freq);
	if (ret < 0) {
		pr_err("%s: cflm: Invalid cpufreq format\n", __func__);
		goto out;
	}

	mutex_lock(&cflm_mutex);

	param.max_limit_val = freq;
	cflm_freq_decision(CFLM_USERSPACE, 0, freq);

	mutex_unlock(&cflm_mutex);
	ret = n;

out:
	return ret;
}

static ssize_t cpufreq_min_limit_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	return snprintf(buf, MAX_BUF_SIZE, "%d\n", param.min_limit_val);
}

static ssize_t cpufreq_min_limit_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t n)
{
	int freq;
	int ret = -EINVAL;

	ret = kstrtoint(buf, 10, &freq);
	if (ret < 0) {
		pr_err("%s: cflm: Invalid cpufreq format\n", __func__);
		goto out;
	}

	mutex_lock(&cflm_mutex);

	cflm_freq_decision(CFLM_USERSPACE, freq, 0);
	param.min_limit_val = freq;

	mutex_unlock(&cflm_mutex);
	ret = n;
out:
	return ret;
}

static ssize_t over_limit_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	return snprintf(buf, MAX_BUF_SIZE, "%d\n", param.over_limit);
}

static ssize_t over_limit_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t n)
{
	int freq;
	int ret = -EINVAL;

	ret = kstrtoint(buf, 10, &freq);
	if (ret < 0) {
		pr_err("%s: cflm: Invalid cpufreq format\n", __func__);
		goto out;
	}

	mutex_lock(&cflm_mutex);

	if (param.over_limit != freq) {
		param.over_limit = freq;
		if ((int)param.max_limit_val > 0)
			cflm_freq_decision(CFLM_USERSPACE, 0, param.max_limit_val);
	}

	mutex_unlock(&cflm_mutex);
	ret = n;
out:
	return ret;
}

static ssize_t limit_stat_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{

	ssize_t len = 0;
	int i, j = 0;

	mutex_lock(&cflm_mutex);
	for (i = 0; i < CFLM_MAX_ITEM; i++) {
		if (freq_input[i].last_min_limit_time != 0) {
			freq_input[i].time_in_min_limit += ktime_to_ms(ktime_get() -
				freq_input[i].last_min_limit_time);
			freq_input[i].last_min_limit_time = ktime_get();
		}

		if (freq_input[i].last_max_limit_time != 0) {
			freq_input[i].time_in_max_limit += ktime_to_ms(ktime_get() -
				freq_input[i].last_max_limit_time);
			freq_input[i].last_max_limit_time = ktime_get();
		}

		if (freq_input[i].last_over_limit_time != 0) {
			freq_input[i].time_in_over_limit += ktime_to_ms(ktime_get() -
				freq_input[i].last_over_limit_time);
			freq_input[i].last_over_limit_time = ktime_get();
		}
	}

	for (j = 0; j < CFLM_MAX_ITEM; j++) {
		len += snprintf(buf + len, MAX_BUF_SIZE - len, "%llu %llu %llu\n",
				freq_input[j].time_in_min_limit, freq_input[j].time_in_max_limit,
				freq_input[j].time_in_over_limit);
	}

	mutex_unlock(&cflm_mutex);
	return len;
}

static ssize_t vtable_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	ssize_t len = 0;
	int i = 0;
	struct cpufreq_policy *policy = cpufreq_cpu_get(param.g_first);
	unsigned int virt_clk = 0;

	if (!cflm_vbf.count)
		return len;

	if (param.vbf_offset > cflm_vbf.count) {
		pr_err("%s: bad condition(off(%d), cnt(%d))",
			__func__, param.vbf_offset, cflm_vbf.count);
		return len;
	}

	len += snprintf(buf + len, MAX_BUF_SIZE, "====================max=======================min============\n");
	len += snprintf(buf + len, MAX_BUF_SIZE, "  virt   |  prime   gold    silver |  prime   gold    silver\n");
	for (i = 0; i < param.freq_count; i++) {
		virt_clk = param.unified_cpuftbl[i];

		if (virt_clk > param.ltl_max_freq) {
			len += snprintf(buf + len, MAX_BUF_SIZE, " %7u | %7u %7u %7u | %7u %7u %7u \n",
				virt_clk,

				/* max = limit */
				virt_clk,
				cflm_get_vol_matched_freq(virt_clk),
				cflm_get_silver_limit(virt_clk),

				/* min = boost */
				virt_clk,
				cpufreq_driver_resolve_freq(policy, virt_clk),
				cflm_get_silver_boost(virt_clk));
		} else {
			len += snprintf(buf + len, MAX_BUF_SIZE, " %7u | %7u %7u %7u | %7u %7u %7u \n",
				virt_clk,

				/* max = limit */
				param.p_fmin,
				param.g_fmin,
				cflm_get_silver_limit(virt_clk),

				/* min = boost */
				0,
				0,
				cflm_get_silver_boost(virt_clk));
		}
	}
	len += snprintf(buf + len, MAX_BUF_SIZE, "=============================================================\n");

	cpufreq_cpu_put(policy);

	pr_info("%s: %s\n", __func__, buf);

	return len;
}

/* sysfs in /sys/power */
static struct kobj_attribute cpufreq_table = {
	.attr	= {
		.name = "cpufreq_table",
		.mode = 0444
	},
	.show	= cpufreq_table_show,
	.store	= NULL,
};

static struct kobj_attribute cpufreq_min_limit = {
	.attr	= {
		.name = "cpufreq_min_limit",
		.mode = 0644
	},
	.show	= cpufreq_min_limit_show,
	.store	= cpufreq_min_limit_store,
};

static struct kobj_attribute cpufreq_max_limit = {
	.attr	= {
		.name = "cpufreq_max_limit",
		.mode = 0644
	},
	.show	= cpufreq_max_limit_show,
	.store	= cpufreq_max_limit_store,
};

static struct kobj_attribute over_limit = {
	.attr	= {
		.name = "over_limit",
		.mode = 0644
	},
	.show	= over_limit_show,
	.store	= over_limit_store,
};

static struct kobj_attribute limit_stat = {
	.attr	= {
		.name = "limit_stat",
		.mode = 0644
	},
	.show	= limit_stat_show,
};

static struct kobj_attribute vtable = {
	.attr	= {
		.name = "vtable",
		.mode = 0444
	},
	.show	= vtable_show,
	.store	= NULL,
};

int set_freq_limit(unsigned int id, unsigned int freq)
{
	if (lpcharge) {
		pr_err("%s: not allowed in LPM\n", __func__);
		return 0;
	}

	mutex_lock(&cflm_mutex);

	pr_info("%s: cflm: id(%d) freq(%d)\n", __func__, (int)id, freq);

	cflm_freq_decision(id, freq, 0);

	mutex_unlock(&cflm_mutex);

	return 0;
}
EXPORT_SYMBOL(set_freq_limit);

#define cflm_attr_rw(_name)		\
static struct kobj_attribute _name##_attr =	\
__ATTR(_name, 0644, show_##_name, store_##_name)

#define show_one(file_name)			\
static ssize_t show_##file_name			\
(struct kobject *kobj, struct kobj_attribute *attr, char *buf)	\
{								\
	return scnprintf(buf, PAGE_SIZE, "%u\n", param.file_name);	\
}

#define store_one(file_name)					\
static ssize_t store_##file_name				\
(struct kobject *kobj, struct kobj_attribute *attr,		\
const char *buf, size_t count)					\
{								\
	int ret;						\
								\
	ret = sscanf(buf, "%u", &param.file_name);				\
	if (ret != 1)						\
		return -EINVAL;					\
								\
	return count;						\
}

show_one(sched_boost_type);
store_one(sched_boost_type);
cflm_attr_rw(sched_boost_type);

/* votlage based */
show_one(vol_based_clk);
store_one(vol_based_clk);
cflm_attr_rw(vol_based_clk);
show_one(vbf_offset);
store_one(vbf_offset);
cflm_attr_rw(vbf_offset);

/* adaptive boost */
show_one(aboost_enabled);
store_one(aboost_enabled);
cflm_attr_rw(aboost_enabled);
show_one(aboost_silver_low);
store_one(aboost_silver_low);
cflm_attr_rw(aboost_silver_low);

static ssize_t show_cflm_info(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	ssize_t len = 0;
	int i = 0;

	mutex_lock(&cflm_mutex);

	len += snprintf(buf, MAX_BUF_SIZE,
			"real: silver(%d ~ %d), gold(%d ~ %d), prime(%d ~ %d)\n",
			param.s_fmin, param.s_fmax,
			param.g_fmin, param.g_fmax,
			param.p_fmin, param.p_fmax);

	len += snprintf(buf + len, MAX_BUF_SIZE - len,
			"virt: little(%d ~ %d), big(%d ~ %d)\n",
			param.ltl_min_freq, param.ltl_max_freq,
			param.big_min_freq, param.big_max_freq);

	len += snprintf(buf + len, MAX_BUF_SIZE - len,
			"param: div(%d), sched boost(%d)\n",
			param.silver_divider, param.sched_boost_type);

	len += snprintf(buf + len, MAX_BUF_SIZE - len,
			"param: vbf(%d), offset(%d), aboost(%d)\n",
			param.vol_based_clk, param.vbf_offset, param.aboost_enabled);

	for (i = 0; i < CFLM_MAX_ITEM; i++) {
		len += snprintf(buf + len, MAX_BUF_SIZE - len,
				"requested: [%d] min(%d), max(%d)\n",
				i, freq_input[i].min, freq_input[i].max);
	}

	mutex_unlock(&cflm_mutex);

	return len;
}

static struct kobj_attribute cflm_info =
	__ATTR(info, 0444, show_cflm_info, NULL);

static struct attribute *cflm_attributes[] = {
	&cpufreq_table.attr,
	&cpufreq_min_limit.attr,
	&cpufreq_max_limit.attr,
	&over_limit.attr,
	&limit_stat.attr,
	&cflm_info.attr,
	&vtable.attr,
	&sched_boost_type_attr.attr,
	&vol_based_clk_attr.attr,
	&vbf_offset_attr.attr,
	&aboost_enabled_attr.attr,
	&aboost_silver_low_attr.attr,
	NULL,
};

static struct attribute_group cflm_attr_group = {
	.attrs = cflm_attributes,
};

#ifdef CONFIG_OF
static void cflm_parse_dt(struct platform_device *pdev)
{
	int size = 0;

	if (!pdev->dev.of_node) {
		pr_info("%s: no device tree\n", __func__);
		return;
	}

	/* voltage based */
	param.vol_based_clk = of_property_read_bool(pdev->dev.of_node, "limit,vol_based_clk");
	of_property_read_u32(pdev->dev.of_node, "limit,vbf_offset", &param.vbf_offset);
	pr_info("%s: param: voltage based clock: %s(offset %d)\n",
		__func__, param.vol_based_clk ? "true" : "false", param.vbf_offset);

	/* boost table */
	of_get_property(pdev->dev.of_node, "limit,silver_boost_table", &size);
	if (size) {
		param.silver_boost_map = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
		of_property_read_u32_array(pdev->dev.of_node, "limit,silver_boost_table",
				(u32 *)param.silver_boost_map, size / sizeof(u32));

		param.boost_map_size = size / sizeof(*param.silver_boost_map);
	}
	pr_info("%s: param: boost map size(%d)\n", __func__, param.boost_map_size);

	/* limit table */
	of_get_property(pdev->dev.of_node, "limit,silver_limit_table", &size);
	if (size) {
		param.silver_limit_map = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
		of_property_read_u32_array(pdev->dev.of_node, "limit,silver_limit_table",
				(u32 *)param.silver_limit_map, size / sizeof(u32));

		param.limit_map_size = size / sizeof(*param.silver_limit_map);
	}
	pr_info("%s: param: limit map size(%d)\n", __func__, param.limit_map_size);

	/* adaptive boost */
	param.aboost_enabled = of_property_read_bool(pdev->dev.of_node, "limit,aboost_enabled");
	of_property_read_u32(pdev->dev.of_node, "limit,aboost_silver_low", &param.aboost_silver_low);

	/* etc */
	of_property_read_u32(pdev->dev.of_node, "limit,gold_fmin_up", &param.g_fmin_up);
	of_property_read_u32(pdev->dev.of_node, "limit,little_max_freq", &param.ltl_max_freq);

	of_node_put(pdev->dev.of_node);
	pr_info("%s: param: g_fmin_up(%d), ltl_max_freq(%d)\n", __func__, param.g_fmin_up, param.ltl_max_freq);
};
#endif

int cflm_add_qos(void)
{
	struct cpufreq_policy *policy;
	unsigned int i = 0;
	unsigned int j = 0;
	int ret = 0;

	for_each_possible_cpu(i) {
		policy = cpufreq_cpu_get(i);
		if (!policy) {
			pr_err("no policy for cpu%d\n", i);
			ret = -EPROBE_DEFER;
			break;
		}

		for (j = 0; j < CFLM_MAX_ITEM; j++) {
			ret = freq_qos_add_request(&policy->constraints,
							&min_req[i][j],
				FREQ_QOS_MIN, policy->cpuinfo.min_freq);
			if (ret < 0) {
				pr_err("%s: failed to add min req(%d)\n", __func__, ret);
				break;
			}
			cflm_req_init[i] |= BIT(j*2);

			ret = freq_qos_add_request(&policy->constraints,
							&max_req[i][j],
				FREQ_QOS_MAX, policy->cpuinfo.max_freq);
			if (ret < 0) {
				pr_err("%s: failed to add max req(%d)\n", __func__, ret);
				break;
			}
			cflm_req_init[i] |= BIT(j*2+1);
		}
		if (ret < 0) {
			cpufreq_cpu_put(policy);
			break;
		}

		if (i == param.s_first) {
			param.s_fmin = policy->cpuinfo.min_freq;
			param.s_fmax = policy->cpuinfo.max_freq;
		}
		if (i == param.g_first) {
			param.g_fmin = policy->cpuinfo.min_freq;
			param.g_fmax = policy->cpuinfo.max_freq;
		}
		if (i == param.p_first) {
			param.p_fmin = policy->cpuinfo.min_freq;
			param.p_fmax = policy->cpuinfo.max_freq;
		}

		cflm_set_table(policy->cpu, policy->freq_table);

		cpufreq_cpu_put(policy);
	}

	return ret;
}

void cflm_remove_qos(void)
{
	unsigned int i = 0;
	unsigned int j = 0;
	int ret = 0;

	pr_info("%s\n", __func__);
	for_each_possible_cpu(i) {
		for (j = 0; j < CFLM_MAX_ITEM; j++) {
			if (cflm_req_init[i] & BIT(j*2)) {
				//pr_info("%s: try to remove min[%d][%d] req\n", __func__, i, j);
				ret = freq_qos_remove_request(&min_req[i][j]);
				if (ret < 0)
					pr_err("%s: failed to remove min_req (%d)\n", __func__, ret);
			}
			if (cflm_req_init[i] & BIT(j*2+1)) {
				//pr_info("%s: try to remove max[%d][%d] req\n", __func__, i, j);
				ret = freq_qos_remove_request(&max_req[i][j]);
				if (ret < 0)
					pr_err("%s: failed to remove max_req (%d)\n", __func__, ret);
			}
		}
		cflm_req_init[i] = 0U;
	}
}

int cflm_probe(struct platform_device *pdev)
{
	int ret;

	pr_info("%s\n", __func__);

	if (lpcharge) {
		pr_info("%s: dummy for LPM\n", __func__);
		return 0;
	}

#ifdef CONFIG_OF
	cflm_parse_dt(pdev);
#endif

	ret = cflm_add_qos();
	if (ret < 0)
		goto policy_not_ready;

	cflm_kobj = kobject_create_and_add("cpufreq_limit",
					&cpu_subsys.dev_root->kobj);
	if (!cflm_kobj) {
		pr_err("Unable to cread cflm_kobj\n");
		goto object_create_failed;
	}

	ret = sysfs_create_group(cflm_kobj, &cflm_attr_group);
	if (ret) {
		pr_err("Unable to create cflm group\n");
		goto group_create_failed;
	}

	pr_info("%s done\n", __func__);
	return ret;

group_create_failed:
	kobject_put(cflm_kobj);
object_create_failed:
	cflm_kobj = NULL;
policy_not_ready:
	cflm_remove_qos();
	return ret;
}

static int cflm_remove(struct platform_device *pdev)
{
	pr_info("%s\n", __func__);

	if (!lpcharge && cflm_kobj) {
		cflm_remove_qos();
		sysfs_remove_group(cflm_kobj, &cflm_attr_group);
		kobject_put(cflm_kobj);
		cflm_kobj = NULL;
	}
	return 0;
}

static const struct of_device_id cflm_match_table[] = {
	{ .compatible = "cpufreq_limit" },
	{}
};

static struct platform_driver cflm_driver = {
    .driver = {
        .name = "cpufreq_limit",
        .of_match_table = cflm_match_table,
    },
    .probe = cflm_probe,
    .remove = cflm_remove,
};

static int __init cflm_init(void)
{
	return platform_driver_register(&cflm_driver);
}

static void __exit cflm_exit(void)
{
    platform_driver_unregister(&cflm_driver);
}

MODULE_AUTHOR("Sangyoung Son <hello.son@samsung.com");
MODULE_DESCRIPTION("'cpufreq_limit' - A driver to limit cpu frequency");
MODULE_LICENSE("GPL");

late_initcall(cflm_init);
module_exit(cflm_exit);
