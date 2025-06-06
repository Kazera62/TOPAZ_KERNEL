
// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/drivers/cpufreq/cpufreq_sclashg.c
 *
 *  Extreme Performance CPU Governor: SclashG
 */

 #define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

 #include <linux/cpufreq.h>
 #include <linux/init.h>
 #include <linux/module.h>
 #include <linux/sched.h>
 #include <linux/sched/task.h>
 #include <linux/kernel.h>
 
 static void cpufreq_gov_sclashg_limits(struct cpufreq_policy *policy)
 {
     pr_info("SclashG: Setting CPU %u to max frequency: %u kHz\n",
             policy->cpu, policy->cpuinfo.max_freq);
 
     /* Paksa CPU selalu berjalan di frekuensi maksimum */
     __cpufreq_driver_target(policy, policy->cpuinfo.max_freq, CPUFREQ_RELATION_H);
 }
 
 static struct cpufreq_governor cpufreq_gov_sclashg = {
     .name       = "sclashg",
     .owner      = THIS_MODULE,
     .flags      = CPUFREQ_GOV_STRICT_TARGET,
     .limits     = cpufreq_gov_sclashg_limits,
 };
 
 #ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_SCLASHG
 struct cpufreq_governor *cpufreq_default_governor(void)
 {
     return &cpufreq_gov_sclashg;
 }
 #endif
 
 MODULE_AUTHOR("Custom SclashG Developer");
 MODULE_DESCRIPTION("Extreme Performance CPUFreq Governor - SclashG");
 MODULE_LICENSE("GPL");
 
 static int __init cpufreq_gov_sclashg_init(void)
 {
     pr_info("SclashG governor loaded: Maximum performance enabled!\n");
     return cpufreq_register_governor(&cpufreq_gov_sclashg);
 }
 
 static void __exit cpufreq_gov_sclashg_exit(void)
 {
     cpufreq_unregister_governor(&cpufreq_gov_sclashg);
 }
 
 module_init(cpufreq_gov_sclashg_init);
 module_exit(cpufreq_gov_sclashg_exit);
 