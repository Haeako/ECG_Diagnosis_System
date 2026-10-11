#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "af_model_int8.h"
#include "af_model_test_vectors.h"

#define BENCHMARK_ITERATIONS 200000U

int main(void)
{
    uint32_t mismatches = 0;
    uint64_t total_comparisons = 0;
    volatile uint32_t checksum = 0;
    for (size_t i = 0; i < AF_MODEL_TEST_VECTOR_COUNT; ++i) {
        af_model_result_t result = af_model_predict_int8(af_test_vectors[i]);
        mismatches += result.score != af_test_scores[i];
    }
    clock_t begin = clock();
    for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; ++i) {
        size_t vector = i % AF_MODEL_TEST_VECTOR_COUNT;
        af_model_result_t result = af_model_predict_int8(af_test_vectors[vector]);
        checksum += result.score;
        total_comparisons += result.comparisons;
    }
    clock_t end = clock();
    double elapsed_us = 1e6 * (double)(end - begin) / CLOCKS_PER_SEC;
    printf("test_vectors=%u mismatches=%lu\n", AF_MODEL_TEST_VECTOR_COUNT, (unsigned long)mismatches);
    printf("model_bytes=%zu working_ram_bytes=%zu\n", af_model_flash_bytes(), af_model_working_ram_bytes());
    printf("mean_comparisons=%.2f max_comparisons=%u\n", (double)total_comparisons / BENCHMARK_ITERATIONS, AF_MODEL_TREE_COUNT * 8U);
    printf("host_latency_us=%.4f iterations=%u checksum=%lu\n", elapsed_us / BENCHMARK_ITERATIONS, BENCHMARK_ITERATIONS, (unsigned long)checksum);
    return mismatches ? 1 : 0;
}
