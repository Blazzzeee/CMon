#define _GNU_SOURCE
#include "arena.h"
#include <malloc.h>
#include <math.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TOTAL_ALLOCATIONS 1000000ULL
#define SAMPLE_INTERVAL 64
#define SAMPLE_SIZE (TOTAL_ALLOCATIONS / SAMPLE_INTERVAL)
#define BATCH_SIZE 32
#define ZOMBIE_POOL_SIZE 6400 * 2
#define NUM_RUNS 9

typedef struct {
    int zombie_idx[65536];
    int should_zombie[65536];
} DecisionTable;

typedef struct {
    double speed_m_ops;
    double p50;
    double p99;
    double max;
    double stddev;
} RunResult;

/* Pin to single CPU */
void pin_to_cpu(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    sched_setaffinity(0, sizeof(set), &set);
}

/* Serialized RDTSC begin */
static inline uint64_t rdtsc_begin(void) {
    unsigned int lo, hi;
    __asm__ volatile("cpuid\n\t"
                     "rdtsc\n\t"
                     : "=a"(lo), "=d"(hi)
                     : "a"(0)
                     : "%rbx", "%rcx");
    return ((uint64_t)hi << 32) | lo;
}

/* Serialized RDTSC end */
static inline uint64_t rdtsc_end(void) {
    unsigned int lo, hi;
    __asm__ volatile("rdtscp\n\t"
                     "mov %%eax, %0\n\t"
                     "mov %%edx, %1\n\t"
                     "cpuid\n\t"
                     : "=r"(lo), "=r"(hi)
                     :
                     : "%rax", "%rbx", "%rcx", "%rdx");
    return ((uint64_t)hi << 32) | lo;
}

int compare_latencies(const void *a, const void *b) {
    double d1 = *(const double *)a;
    double d2 = *(const double *)b;
    return (d1 > d2) - (d1 < d2);
}

RunResult run_benchmark(int use_arena, int is_warmup, DecisionTable *table) {
    uint64_t total_done = 0;
    void *ptrs[BATCH_SIZE] = {0};
    void *zombies[ZOMBIE_POOL_SIZE] = {0};
    double *latencies = malloc(sizeof(double) * SAMPLE_SIZE);
    size_t sample_count = 0;

    const size_t sz = 8 * 1024 * 1024; // 8 MB allocations

    if (use_arena) {
        arena_config(sz, 64);
        prealloc_arena();
    }

    struct timespec start_ts, end_ts;
    clock_gettime(CLOCK_MONOTONIC, &start_ts);

    while (total_done < TOTAL_ALLOCATIONS) {

        uint32_t table_idx = (uint32_t)(total_done & 0xFFFF);

        for (int i = 0; i < BATCH_SIZE; i++) {

            uint64_t t1 = rdtsc_begin();
            void *ptr = use_arena ? allocate(sz) : malloc(sz);

            if (ptr) {
                /* Random page touching to destroy locality */
                size_t pages = sz / 4096;
                for (int p = 0; p < 128; p++) {
                    size_t off = (size_t)(rand() % pages) * 4096;
                    ((volatile char *)ptr)[off] = 1;
                }
            }

            uint64_t t2 = rdtsc_end();

            if (!is_warmup && (total_done % SAMPLE_INTERVAL == 0) && sample_count < SAMPLE_SIZE) {
                latencies[sample_count++] = (double)(t2 - t1);
            }

            if (ptr) {
                if (table->should_zombie[table_idx]) {
                    int z_idx = table->zombie_idx[table_idx];
                    if (zombies[z_idx]) {
                        if (use_arena)
                            deallocate(zombies[z_idx]);
                        else
                            free(zombies[z_idx]);
                    }
                    zombies[z_idx] = ptr;
                } else {
                    ptrs[i] = ptr;
                }
            }
        }

        for (int i = 0; i < BATCH_SIZE; i++) {
            if (ptrs[i]) {
                if (use_arena)
                    deallocate(ptrs[i]);
                else
                    free(ptrs[i]);
                ptrs[i] = NULL;
            }
        }

        total_done += BATCH_SIZE;
    }

    clock_gettime(CLOCK_MONOTONIC, &end_ts);

    double elapsed = (end_ts.tv_sec - start_ts.tv_sec) + (end_ts.tv_nsec - start_ts.tv_nsec) * 1e-9;

    RunResult res = {0};

    if (!is_warmup) {
        qsort(latencies, sample_count, sizeof(double), compare_latencies);

        double sum = 0, sq_sum = 0;
        for (int i = 0; i < sample_count; i++) {
            sum += latencies[i];
            sq_sum += latencies[i] * latencies[i];
        }

        double mean = sum / sample_count;

        res.speed_m_ops = (total_done / 1000000.0) / elapsed;
        res.p50 = latencies[(int)(sample_count * 0.5)];
        res.p99 = latencies[(int)(sample_count * 0.99)];
        res.max = latencies[sample_count - 1];
        res.stddev = sqrt((sq_sum / sample_count) - (mean * mean));
    }

    for (int i = 0; i < ZOMBIE_POOL_SIZE; i++) {
        if (zombies[i]) {
            if (use_arena)
                deallocate(zombies[i]);
            else
                free(zombies[i]);
        }
    }

    if (use_arena)
        teardown_arena();

    free(latencies);
    malloc_trim(0); // force release

    return res;
}

void print_final_stats(const char *label, RunResult results[]) {
    double avg_speed = 0, avg_p50 = 0, avg_p99 = 0, avg_std = 0;

    for (int i = 0; i < NUM_RUNS; i++) {
        avg_speed += results[i].speed_m_ops;
        avg_p50 += results[i].p50;
        avg_p99 += results[i].p99;
        avg_std += results[i].stddev;
    }

    printf("\n=== %s FINAL AVERAGES (%d RUNS) ===\n", label, NUM_RUNS);
    printf("Speed:  %.2f M ops/sec\n", avg_speed / NUM_RUNS);
    printf("P50:    %.0f cycles\n", avg_p50 / NUM_RUNS);
    printf("P99:    %.0f cycles\n", avg_p99 / NUM_RUNS);
    printf("StdDev: %.2f\n", avg_std / NUM_RUNS);
}

int main() {
    pin_to_cpu(0);
    srand(1337);

    DecisionTable *table = malloc(sizeof(DecisionTable));

    for (int i = 0; i < 65536; i++) {
        table->zombie_idx[i] = rand() % ZOMBIE_POOL_SIZE;
        table->should_zombie[i] = (rand() % 5 != 0); // 80% stay live
    }

    RunResult malloc_results[NUM_RUNS];
    RunResult arena_results[NUM_RUNS];

    printf("Starting Warmup Runs...\n");
    run_benchmark(0, 1, table);
    run_benchmark(1, 1, table);

    for (int i = 0; i < NUM_RUNS; i++) {
        printf("Running Malloc iteration %d...\n", i + 1);
        malloc_results[i] = run_benchmark(0, 0, table);
    }
    for (int i = 0; i < NUM_RUNS; i++) {
        printf("Running Arena iteration %d...\n", i + 1);
        arena_results[i] = run_benchmark(1, 0, table);
    }

    print_final_stats("MALLOC", malloc_results);
    print_final_stats("ARENA ", arena_results);

    free(table);
    return 0;
}
