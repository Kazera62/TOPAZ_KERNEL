// SPDX-License-Identifier: GPL-2.0
/*
 * CASS ULTRA++ TWEAKED for Snapdragon 685 (sapphire/sapphiren)
 * MAX Performance, Responsiveness, and Latency Aggression
 * Author: GEIS22 + Sultan Alsawaf (base)
 */

 #include <linux/sched.h>
 #include <linux/sched/idle.h>
 #include <linux/sched/rt.h>
 #include <linux/sched/topology.h>
 #include <linux/sched/clock.h>
 #include <linux/sched/cpufreq.h>
 #include <linux/sched/numa_balancing.h>
 #include <linux/sched/stat.h>
 #include <linux/smp.h>
 #include <linux/cpumask.h>
 #include <linux/cpu.h>
 #include <linux/rcupdate.h>
 #include <linux/cpuidle.h>
 #include <trace/events/sched.h>
 
 #ifndef UCLAMP_MIN
 #define UCLAMP_MIN 0
 #define uclamp_eff_value(p, clamp_id) 0
 #endif
 
 #define A73_MIN_CAP 640
 #define UTIL_THRESH_FORCE_BIG 512
 #define MAX_UTIL_DIFF 96
 
 struct cass_cpu_cand {
	 int cpu;
	 unsigned int exit_lat;
	 unsigned long cap;
	 unsigned long cap_max;
	 unsigned long cap_no_therm;
	 unsigned long cap_orig;
	 unsigned long eff_util;
	 unsigned long hard_util;
	 unsigned long util;
 };
 
 static __always_inline
 void cass_cpu_util(struct cass_cpu_cand *c, int this_cpu, bool sync)
 {
	 struct rq *rq = cpu_rq(c->cpu);
	 struct cfs_rq *cfs_rq = &rq->cfs;
	 unsigned long est;
 
	 c->util = READ_ONCE(cfs_rq->avg.util_avg);
	 if (sched_feat(UTIL_EST)) {
		 est = READ_ONCE(cfs_rq->avg.util_est.enqueued);
		 if (est > c->util) {
			 sync = false;
			 c->util = est;
		 }
	 }
 
	 if (sync && c->cpu == this_cpu && !rt_task(current))
		 c->util -= min(c->util, task_util(current));
 
	 c->hard_util = cpu_util_rt(rq) + cpu_util_dl(rq) + cpu_util_irq(rq);
	 c->cap = c->cap_max - min(c->hard_util, c->cap_max - 1);
	 c->cap_no_therm = c->cap_orig;
 }
 
 static __always_inline
 bool cass_cpu_better(const struct cass_cpu_cand *a,
			  const struct cass_cpu_cand *b, unsigned long p_util,
			  int this_cpu, int prev_cpu, bool sync)
 {
	 long res;
 #define cass_cmp(a, b) ({ res = (a) - (b); })
 #define cass_eq(a, b) ({ res = (a) == (b); })
 
	 if (cass_cmp(b->eff_util / b->cap_max, a->eff_util / a->cap_max))
		 goto done;
 
	 if (b->eff_util > b->cap_max && a->eff_util > a->cap_max &&
		 cass_cmp(b->eff_util * SCHED_CAPACITY_SCALE / b->cap_max,
			 a->eff_util * SCHED_CAPACITY_SCALE / a->cap_max))
		 goto done;
 
	 if (cass_cmp(fits_capacity(p_util, a->cap_max),
			  fits_capacity(p_util, b->cap_max)))
		 goto done;
 
	 if (cass_cmp(b->util, a->util))
		 goto done;
 
	 if (cass_cmp(cpus_share_cache(a->cpu, prev_cpu),
			  cpus_share_cache(b->cpu, prev_cpu)))
		 goto done;
 
	 if (sync && cass_eq(a->cpu, this_cpu))
		 goto done;
 
	 if (cass_cmp(a->cap, b->cap))
		 goto done;
 
	 if (cass_cmp(b->exit_lat, a->exit_lat))
		 goto done;
 
	 if (cass_eq(a->cpu, prev_cpu) || !cass_cmp(b->cpu, prev_cpu))
		 goto done;
 
	 return false;
 
 done:
	 return res > 0;
 }
 
 static int cass_best_cpu(struct task_struct *p, int prev_cpu, bool sync, bool rt)
 {
	 struct cass_cpu_cand cands[2], *best = cands;
	 int this_cpu = raw_smp_processor_id();
	 unsigned long p_util = rt ? 0 : task_util_est(p);
	 unsigned long uc_min = uclamp_eff_value(p, UCLAMP_MIN);
	 bool has_idle = false;
	 int cidx = 0, cpu;
 
	 for_each_cpu_and(cpu, p->cpus_ptr, cpu_active_mask) {
		 struct cass_cpu_cand *curr = &cands[cidx];
		 struct cpuidle_state *idle_state;
		 struct rq *rq = cpu_rq(cpu);
 
		 curr->cap_orig = arch_scale_cpu_capacity(cpu);
		 curr->cap_max = curr->cap_orig;
 
		 // Hardcore: skip anything not A73 if load is high
		 if ((uc_min >= UTIL_THRESH_FORCE_BIG || p_util >= UTIL_THRESH_FORCE_BIG) && curr->cap_max < A73_MIN_CAP)
			 continue;
 
		 // Force all sync wake to stay in current CPU
		 if (sync && cpu != this_cpu)
			 continue;
 
		 if ((sync && cpu == this_cpu && rq->nr_running == 1) ||
			 available_idle_cpu(cpu) || sched_idle_cpu(cpu)) {
			 if (!has_idle && uc_min <= arch_scale_freq_capacity(cpu)) {
				 best = curr;
				 has_idle = true;
			 }
			 curr->exit_lat = 1;
			 idle_state = idle_get_state(rq);
			 if (idle_state)
				 curr->exit_lat += idle_state->exit_latency;
		 } else {
			 if (has_idle && uc_min < 512)
				 continue;
			 curr->exit_lat = 0;
		 }
 
		 curr->cpu = cpu;
		 cass_cpu_util(curr, this_cpu, sync);
 
		 if (cpu != task_cpu(p))
			 curr->util += p_util;
 
		 curr->eff_util = max(curr->util + curr->hard_util, uc_min);
 
		 if (curr->util < uc_min)
			 curr->util = uc_min;
 
		 curr->util = curr->util * SCHED_CAPACITY_SCALE / curr->cap_no_therm;
 
		 // Ultra++: skip CPUs that are overloaded
		 if (curr->eff_util >= curr->cap_max)
			 continue;
 
		 if (best != curr && abs((long)(curr->util - best->util)) > MAX_UTIL_DIFF)
			 continue;
 
		 if (best == curr ||
			 cass_cpu_better(curr, best, p_util, this_cpu, prev_cpu, sync)) {
			 best = curr;
			 cidx ^= 1;
		 }
	 }
 
	 return best->cpu;
 }
 
 static int cass_select_task_rq(struct task_struct *p, int prev_cpu,
					int wake_flags, bool rt)
 {
	 bool sync;
 
	 if (wake_flags & SD_BALANCE_EXEC)
		 return prev_cpu;
 
	 if (unlikely(!cpumask_intersects(p->cpus_ptr, cpu_active_mask)))
		 return cpumask_first(p->cpus_ptr);
 
	 if (!rt && !(wake_flags & SD_BALANCE_FORK))
		 sync_entity_load_avg(&p->se);
 
	 sync = (wake_flags & WF_SYNC) && !(current->flags & PF_EXITING);
	 return cass_best_cpu(p, prev_cpu, sync, rt);
 }
 
 static int cass_select_task_rq_fair(struct task_struct *p, int prev_cpu,
					 int wake_flags)
 {
	 return cass_select_task_rq(p, prev_cpu, wake_flags, false);
 }
 
 int cass_select_task_rq_rt(struct task_struct *p, int prev_cpu, int wake_flags)
 {
	 return cass_select_task_rq(p, prev_cpu, wake_flags, true);
 }
 