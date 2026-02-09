#include "arena.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TOTAL_ALLOCATIONS 100000000ULL
#define SAMPLE_INTERVAL 1024 // Power of 2 for bitwise masking
#define SAMPLE_SIZE (TOTAL_ALLOCATIONS / SAMPLE_INTERVAL)
#define BATCH_SIZE 32
#define ZOMBIE_POOL_SIZE 4096
#define NUM_RUNS 5

// Pre-computed decisions to keep the hot loop "pure"
// Size 65536 is large enough for entropy but fits in L2/L3 cache
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

// High-precision cycle counter
static inline uint64_t rdtsc() {
    unsigned int lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
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
    const size_t sz = arena_buf_size - 64;

    if (use_arena)
        prealloc_arena();

    struct timespec start_ts, end_ts;
    clock_gettime(CLOCK_MONOTONIC, &start_ts);

    while (total_done < TOTAL_ALLOCATIONS) {
        // Fast bitwise wrap for the table index
        uint32_t table_idx = (uint32_t)(total_done & 0xFFFF);

        for (int i = 0; i < BATCH_SIZE; i++) {
            uint64_t t1 = rdtsc();
            void *ptr = use_arena ? allocate(sz) : malloc(sz);
            uint64_t t2 = rdtsc();

            // Only sample on power-of-two intervals using bitwise AND
            if (!is_warmup && (total_done & (SAMPLE_INTERVAL - 1)) == 0 &&
                sample_count < SAMPLE_SIZE) {
                latencies[sample_count++] = (double)(t2 - t1);
            }

            if (__builtin_expect(ptr != NULL, 1)) {
                ((volatile char *)ptr)[0] = 'X';

                // Use pre-computed decision table instead of rand()
                if (__builtin_expect(table->should_zombie[table_idx], 0)) {
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

    // Cleanup zombies
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
    srand(1337);

    // Initialize decision table once
    DecisionTable *table = malloc(sizeof(DecisionTable));
    for (int i = 0; i < 65536; i++) {
        table->zombie_idx[i] = rand() % ZOMBIE_POOL_SIZE;
        table->should_zombie[i] = (rand() % 10 == 0); // 10% chance
    }

    RunResult malloc_results[NUM_RUNS];
    RunResult arena_results[NUM_RUNS];

    printf("Starting Purity Warmup (Decision Table mode)...\n");
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
