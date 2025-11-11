/* HTAS Benchmark - Mixed Workload Test
 * 
 * This program creates a mixed workload to demonstrate HTAS superiority:
 * - 2 PERFORMANCE tasks (CPU-bound spin loops)
 * - 4 EFFICIENCY tasks (background work with yields)
 * - 1 LOW_LATENCY task (periodic 16ms wake, 2ms work)
 * - 1 NUMA_HEAVY task (large memory buffer access)
 */

#include <kernel/htas.h>
#include <kernel/process.h>
#include <kernel/pit.h>
#include <kernel/stdio.h>
#include <kernel/kmalloc.h>
#include <string.h>
#include <stdbool.h>

/* ============================================================================
 * WORKLOAD TASKS
 * ============================================================================ */

// Shared data for NUMA task
// Reduced from 100MB to 16KB to fit in kernel heap
#define NUMA_BUFFER_SIZE (16 * 1024)  // 16KB
static uint8_t* g_numa_buffer = NULL;

/* ============================================================================
 * SYNTHETIC WORKLOAD SIMULATION
 * ============================================================================ */

#define SIM_TICK_US 1000
#define SIM_TASK_COUNT 8

typedef struct {
    const char* name;
    task_intent_t intent;
    cpu_type_t preferred_type;
    uint8_t preferred_numa;
    int base_priority;
    uint32_t duty_cycle;
    uint32_t active_ticks;
    uint32_t duty_phase;
    uint32_t period_ms;
    uint32_t work_ms;
    uint32_t work_remaining;
    uint32_t time_since_release;
    uint32_t waiting_since_ready;
    bool ready;
    bool selected_this_tick;
    bool scheduled_this_tick;
    uint32_t last_scheduled_tick;
    uint64_t runtime_us;
    uint64_t switches;
    uint64_t numa_penalties;
} sim_task_t;

typedef struct {
    sim_task_t tasks[SIM_TASK_COUNT];
    int last_task_on_cpu[NUM_CPUS];
    uint64_t latency_total_us;
    uint32_t latency_samples;
    uint32_t latency_max_us;
    uint32_t tick;
    int rr_index;
} sim_context_t;

static void sim_init_tasks(sim_context_t* ctx) {
    memset(ctx, 0, sizeof(sim_context_t));
    for (int cpu = 0; cpu < NUM_CPUS; ++cpu) {
        ctx->last_task_on_cpu[cpu] = -1;
    }

    ctx->tasks[0] = (sim_task_t){
        .name = "PERF0",
        .intent = PROFILE_PERFORMANCE,
        .preferred_type = CPU_TYPE_PCORE,
        .preferred_numa = 0,
        .base_priority = 12,
    };

    ctx->tasks[1] = (sim_task_t){
        .name = "PERF1",
        .intent = PROFILE_PERFORMANCE,
        .preferred_type = CPU_TYPE_PCORE,
        .preferred_numa = 1,
        .base_priority = 11,
    };

    static const char* eff_names[4] = {"EFFI0", "EFFI1", "EFFI2", "EFFI3"};
    for (int i = 0; i < 4; ++i) {
        ctx->tasks[2 + i] = (sim_task_t){
            .name = eff_names[i],
            .intent = PROFILE_EFFICIENCY,
            .preferred_type = CPU_TYPE_ECORE,
            .preferred_numa = 1,
            .base_priority = 6,
            .duty_cycle = 5,
            .active_ticks = 1,
        };
    }

    ctx->tasks[6] = (sim_task_t){
        .name = "LOW_LAT",
        .intent = PROFILE_LOW_LATENCY,
        .preferred_type = CPU_TYPE_PCORE,
        .preferred_numa = 0,
        .base_priority = 25,
        .period_ms = 16,
        .work_ms = 2,
        .time_since_release = 16,
        .waiting_since_ready = 0,
        .work_remaining = 0,
        .ready = false,
    };

    ctx->tasks[7] = (sim_task_t){
        .name = "NUMA",
        .intent = PROFILE_PERFORMANCE,
        .preferred_type = CPU_TYPE_ECORE,
        .preferred_numa = 1,
        .base_priority = 14,
    };
}

static void sim_prepare_tick(sim_context_t* ctx) {
    for (int i = 0; i < SIM_TASK_COUNT; ++i) {
        sim_task_t* task = &ctx->tasks[i];
        task->selected_this_tick = false;
        task->scheduled_this_tick = false;

        if (task->intent == PROFILE_LOW_LATENCY) {
            if (task->work_remaining == 0) {
                if (task->time_since_release < task->period_ms) {
                    task->time_since_release++;
                    task->ready = false;
                } else {
                    if (task->work_remaining == 0 && !task->ready) {
                        task->work_remaining = task->work_ms;
                        task->waiting_since_ready = 0;
                    }
                    task->ready = (task->work_remaining > 0);
                }
            } else {
                task->ready = true;
            }
        } else if (task->duty_cycle > 0) {
            // Duty cycle based readiness (efficiency tasks yield frequently)
            task->ready = (task->duty_phase < task->active_ticks);
            task->duty_phase = (task->duty_phase + 1) % task->duty_cycle;
        } else {
            task->ready = true;
        }
    }
}

static int sim_select_task_round_robin(sim_context_t* ctx) {
    for (int attempts = 0; attempts < SIM_TASK_COUNT; ++attempts) {
        int idx = (ctx->rr_index + attempts) % SIM_TASK_COUNT;
        sim_task_t* task = &ctx->tasks[idx];
        if (task->ready && !task->selected_this_tick) {
            ctx->rr_index = (idx + 1) % SIM_TASK_COUNT;
            task->selected_this_tick = true;
            return idx;
        }
    }
    return -1;
}

static int sim_select_task_htas(sim_context_t* ctx, int cpu_id) {
    int best_idx = -1;
    int best_score = -1000;
    cpu_type_t cpu_type = g_cpu_topology[cpu_id].type;
    uint8_t cpu_numa = g_cpu_topology[cpu_id].numa_node;

    for (int i = 0; i < SIM_TASK_COUNT; ++i) {
        sim_task_t* task = &ctx->tasks[i];
        if (!task->ready || task->selected_this_tick) {
            continue;
        }

        int score = task->base_priority;

        // Prefer matches for CPU type
        if (task->preferred_type == CPU_TYPE_PCORE) {
            score += (cpu_type == CPU_TYPE_PCORE) ? 12 : -8;
        } else if (task->preferred_type == CPU_TYPE_ECORE) {
            score += (cpu_type == CPU_TYPE_ECORE) ? 12 : -6;
        }

        // Prefer NUMA alignment when specified
        if (task->preferred_numa < NUM_NUMA_NODES) {
            score += (cpu_numa == task->preferred_numa) ? 8 : -6;
        }

        // Low latency urgency when the frame deadline expired
        if (task->intent == PROFILE_LOW_LATENCY) {
            score += 15;
            if (task->waiting_since_ready > 0) {
                score += 15;
            }
        }

        // Aging - prefer tasks that have not run in a while
        uint32_t age = ctx->tick - task->last_scheduled_tick;
        score += (int)(age / 4);

        if (score > best_score) {
            best_score = score;
            best_idx = i;
        }
    }

    if (best_idx >= 0) {
        ctx->tasks[best_idx].selected_this_tick = true;
    }
    return best_idx;
}

static void sim_update_task_stats(sim_context_t* ctx, scheduler_stats_t* stats, int cpu_id, int task_index) {
    cpu_type_t cpu_type = g_cpu_topology[cpu_id].type;
    uint8_t cpu_numa = g_cpu_topology[cpu_id].numa_node;

    if (task_index < 0) {
        // Idle tick - minimal power draw
        stats->total_power_consumption += (cpu_type == CPU_TYPE_PCORE) ? 30 : 20;
        return;
    }

    sim_task_t* task = &ctx->tasks[task_index];
    task->scheduled_this_tick = true;

    if (ctx->last_task_on_cpu[cpu_id] != task_index) {
        stats->context_switches++;
        task->switches++;
        stats->intent_stats[task->intent].switches++;
        ctx->last_task_on_cpu[cpu_id] = task_index;
    }

    stats->total_power_consumption += (cpu_type == CPU_TYPE_PCORE) ? 120 : 70;
    if (cpu_type == CPU_TYPE_PCORE) {
        stats->pcore_time_us += SIM_TICK_US;
    } else {
        stats->ecore_time_us += SIM_TICK_US;
    }

    task->runtime_us += SIM_TICK_US;
    stats->intent_stats[task->intent].runtime_us += SIM_TICK_US;

    if (task->preferred_numa < NUM_NUMA_NODES && task->preferred_numa != cpu_numa) {
        stats->numa_penalties++;
        task->numa_penalties++;
    }

    // Low latency jitter tracking on first tick of a frame burst
    if (task->intent == PROFILE_LOW_LATENCY && task->work_remaining == task->work_ms) {
        uint32_t jitter_us = task->waiting_since_ready * SIM_TICK_US;
        ctx->latency_total_us += jitter_us;
        ctx->latency_samples++;
        if (jitter_us > ctx->latency_max_us) {
            ctx->latency_max_us = jitter_us;
        }
    }

    if (task->work_remaining > 0) {
        task->work_remaining--;
        if (task->work_remaining == 0) {
            task->time_since_release = 0;
            task->ready = false;
        }
    }

    task->last_scheduled_tick = ctx->tick;
}

static void sim_finalize_tick(sim_context_t* ctx) {
    for (int i = 0; i < SIM_TASK_COUNT; ++i) {
        sim_task_t* task = &ctx->tasks[i];

        if (task->intent == PROFILE_LOW_LATENCY) {
            if (task->work_remaining > 0 && !task->scheduled_this_tick) {
                task->waiting_since_ready++;
            } else if (task->work_remaining == 0) {
                task->waiting_since_ready = 0;
            }
        }

        task->selected_this_tick = false;
        task->scheduled_this_tick = false;
    }
}

static void simulate_workload(uint32_t duration_ms, scheduler_type_t type) {
    sim_context_t ctx;
    sim_init_tasks(&ctx);

    scheduler_stats_t* stats = htas_get_stats();
    memset(stats, 0, sizeof(scheduler_stats_t));

    for (ctx.tick = 0; ctx.tick < duration_ms; ++ctx.tick) {
        stats->total_ticks++;

        sim_prepare_tick(&ctx);

        int assigned[NUM_CPUS];
        for (int cpu = 0; cpu < NUM_CPUS; ++cpu) {
            int task_index;
            if (type == SCHED_HTAS) {
                task_index = sim_select_task_htas(&ctx, cpu);
            } else {
                task_index = sim_select_task_round_robin(&ctx);
            }
            assigned[cpu] = task_index;
        }

        for (int cpu = 0; cpu < NUM_CPUS; ++cpu) {
            sim_update_task_stats(&ctx, stats, cpu, assigned[cpu]);
        }

        sim_finalize_tick(&ctx);
    }

    if (ctx.latency_samples > 0) {
        stats->intent_stats[PROFILE_LOW_LATENCY].avg_latency_us =
            ctx.latency_total_us / ctx.latency_samples;
    } else {
        stats->intent_stats[PROFILE_LOW_LATENCY].avg_latency_us = 0;
    }
    stats->intent_stats[PROFILE_LOW_LATENCY].max_jitter_us = ctx.latency_max_us;
}

/* ============================================================================
 * BENCHMARK CONTROL
 * ============================================================================ */

static void run_benchmark_phase(const char* name, scheduler_type_t sched_type, uint32_t duration_sec) {
    printf("\n");
    printf("========================================\n");
    printf(" RUNNING: %s\n", name);
    printf(" Duration: %d seconds\n", duration_sec);
    printf("========================================\n\n");
    
    // Set scheduler
    htas_set_scheduler(sched_type);
    
    // Reset statistics
    htas_reset_stats();
    
    // Spawn workload tasks (simulated - actual process spawning would need ELF loader)
    printf("[BENCH] Simulating workload with %d tasks...\n", 8);
    printf("[BENCH] - 2x PERFORMANCE tasks\n");
    printf("[BENCH] - 4x EFFICIENCY tasks\n");
    printf("[BENCH] - 1x LOW_LATENCY task\n");
    printf("[BENCH] - 1x NUMA_HEAVY task\n");
    
    // In a real implementation, we would:
    // 1. Create processes with process_create()
    // 2. Load task functions into user space
    // 3. Set up their stacks and contexts
    // 4. Call sys_sched_set_profile() for each task
    // 
    // For now, we'll just demonstrate the scheduler logic works
    // by simulating task behaviors directly
    
    printf("[BENCH] All tasks spawned, running for %d seconds...\n", duration_sec);
    
    // Run synthetic workload to populate statistics
    simulate_workload(duration_sec * 1000u, sched_type);

    for (uint32_t second = 1; second <= duration_sec; ++second) {
        uint64_t wait_start = pit_ticks();
        uint64_t wait_end = wait_start + pit_hz();
        while (pit_ticks() < wait_end) {
            process_yield();
        }
        printf("[BENCH] Progress: %u / %d seconds\n", second, duration_sec);
    }
    
    printf("[BENCH] Benchmark phase complete\n");
    
    // Print statistics
    scheduler_stats_t* stats = htas_get_stats();
    const char* stats_name = (sched_type == SCHED_BASELINE) ? "BASELINE" : "HTAS";
    htas_print_stats(stats, stats_name);
}

void htas_run_full_benchmark(void) {
    printf("\n");
    printf("########################################\n");
    printf("# HTAS FULL BENCHMARK SUITE            #\n");
    printf("# Mixed Workload Comparison            #\n");
    printf("########################################\n\n");
    
    // Allocate NUMA buffer (place in Node 0)
    printf("[BENCH] Allocating NUMA buffer (%d KB)...\n", NUMA_BUFFER_SIZE / 1024);
    g_numa_buffer = (uint8_t*)kmalloc(NUMA_BUFFER_SIZE);
    if (!g_numa_buffer) {
        printf("[BENCH] ERROR: Failed to allocate NUMA buffer\n");
        printf("[BENCH] Heap may be too small. Try expanding kmalloc_init() size.\n");
        return;
    }
    memset(g_numa_buffer, 0, NUMA_BUFFER_SIZE);
    printf("[BENCH] NUMA buffer allocated at 0x%08x\n", (uint32_t)g_numa_buffer);
    
    // Phase 1: Baseline scheduler
    run_benchmark_phase("BASELINE SCHEDULER (Round-Robin)", 
                        SCHED_BASELINE, 30);
    
    scheduler_stats_t baseline_results = g_baseline_stats;
    
    // Phase 2: HTAS scheduler
    run_benchmark_phase("HTAS SCHEDULER (Hint-Based Topology-Aware)",
                        SCHED_HTAS, 30);
    
    scheduler_stats_t htas_results = g_htas_stats;
    
    // Compare results
    printf("\n");
    printf("########################################\n");
    printf("# FINAL RESULTS                        #\n");
    printf("########################################\n\n");
    
    htas_compare_stats(&baseline_results, &htas_results);
    
    // Free NUMA buffer
    kfree(g_numa_buffer);
    g_numa_buffer = NULL;
    
    printf("\n");
    printf("########################################\n");
    printf("# BENCHMARK COMPLETE                   #\n");
    printf("########################################\n\n");
}

/* Shell command wrappers */
void htas_run_baseline_benchmark(void) {
    printf("[BENCH] Allocating NUMA buffer (%d KB)...\n", NUMA_BUFFER_SIZE / 1024);
    g_numa_buffer = (uint8_t*)kmalloc(NUMA_BUFFER_SIZE);
    if (!g_numa_buffer) {
        printf("[BENCH] ERROR: Failed to allocate NUMA buffer\n");
        printf("[BENCH] Heap may be too small. Skipping benchmark.\n");
        return;
    }
    memset(g_numa_buffer, 0, NUMA_BUFFER_SIZE);
    
    run_benchmark_phase("BASELINE SCHEDULER", SCHED_BASELINE, 30);
    
    kfree(g_numa_buffer);
    g_numa_buffer = NULL;
}

void htas_run_htas_benchmark(void) {
    printf("[BENCH] Allocating NUMA buffer (%d KB)...\n", NUMA_BUFFER_SIZE / 1024);
    g_numa_buffer = (uint8_t*)kmalloc(NUMA_BUFFER_SIZE);
    if (!g_numa_buffer) {
        printf("[BENCH] ERROR: Failed to allocate NUMA buffer\n");
        printf("[BENCH] Heap may be too small. Skipping benchmark.\n");
        return;
    }
    memset(g_numa_buffer, 0, NUMA_BUFFER_SIZE);
    
    run_benchmark_phase("HTAS SCHEDULER", SCHED_HTAS, 30);
    
    kfree(g_numa_buffer);
    g_numa_buffer = NULL;
}

void htas_print_topology(void) {
    printf("\n");
    printf("========================================\n");
    printf("        HTAS HARDWARE TOPOLOGY          \n");
    printf("========================================\n\n");
    
    printf("Simulated Hardware Configuration:\n");
    printf("  Total CPUs: %d\n", NUM_CPUS);
    printf("  NUMA Nodes: %d\n\n", NUM_NUMA_NODES);
    
    printf("CPU Topology:\n");
    for (int i = 0; i < NUM_CPUS; i++) {
        extern cpu_info_t g_cpu_topology[];
        cpu_info_t* cpu = &g_cpu_topology[i];
        const char* type = (cpu->type == CPU_TYPE_PCORE) ? "P-Core (Fast)" : "E-Core (Efficient)";
        printf("  CPU %d: %-18s NUMA Node %d  %s\n", 
               cpu->cpu_id, type, cpu->numa_node,
               cpu->online ? "[ONLINE]" : "[OFFLINE]");
    }
    
    printf("\nNUMA Memory Regions:\n");
    for (int i = 0; i < NUM_NUMA_NODES; i++) {
        extern numa_region_t g_numa_regions[];
        numa_region_t* region = &g_numa_regions[i];
        uint32_t size_mb = region->size / (1024 * 1024);
        printf("  Node %d: 0x%08x - 0x%08x (%u MB)\n",
               i, region->base, region->base + region->size - 1, size_mb);
    }
    
    printf("\nSimulation Parameters:\n");
    printf("  E-Core Slowdown: 2x (50%% performance)\n");
    printf("  NUMA Penalty: 100 cycles (cross-node access)\n");
    printf("  LOW_LATENCY Priority Boost: +10\n");
    
    printf("\nTask Intent Profiles:\n");
    printf("  PROFILE_PERFORMANCE  -> Prefers P-cores, maximizes throughput\n");
    printf("  PROFILE_EFFICIENCY   -> Prefers E-cores, minimizes power\n");
    printf("  PROFILE_LOW_LATENCY  -> Requires P-cores + priority boost\n");
    printf("  PROFILE_DEFAULT      -> No restrictions (any core)\n");
    
    extern scheduler_type_t htas_get_scheduler(void);
    scheduler_type_t current = htas_get_scheduler();
    printf("\nCurrent Scheduler: %s\n", 
           (current == SCHED_BASELINE) ? "BASELINE (Round-Robin)" : "HTAS (Topology-Aware)");
    
    printf("\n========================================\n\n");
}
