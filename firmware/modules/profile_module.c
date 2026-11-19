/* Time only in-memory algorithm calls; parsing, allocation and reset are excluded. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#include "module_api.h"

typedef struct { int32_t value; int peak; } input_t;
static double seconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec * 1e-9;
#endif
}

static unsigned long positive(const char *text)
{
    char *end;
    errno = 0;
    unsigned long n = strtoul(text, &end, 10);
    if (errno || !*text || *end || *text == '-' || n == 0 || n > 1000000UL) return 0;
    return n;
}

int main(int argc, char **argv)
{
    unsigned long repeat = 10;
    const char *filename = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--repeat") && i + 1 < argc) {
            repeat = positive(argv[++i]);
            if (!repeat) { fprintf(stderr, "repeat must be 1..1000000\n"); return 2; }
        } else if (!strcmp(argv[i], "--input") && i + 1 < argc) filename = argv[++i];
        else {
            fprintf(stderr, "Usage: %s [--input headerless.csv] [--repeat N]\n", argv[0]);
            return 2;
        }
    }
    size_t count = 0, capacity = 36000;
    input_t *input = malloc(capacity * sizeof(*input));
    if (!input) return 1;
    if (filename) {
        FILE *file = fopen(filename, "r");
        if (!file) { perror(filename); free(input); return 1; }
        char line[256];
        size_t line_no = 0;
        while (fgets(line, sizeof(line), file)) {
            ++line_no;
            char *end;
            errno = 0;
            long value = strtol(line, &end, 10);
            int valid = end != line && !errno;
            long peak = 0;
#if ECG_MODULE == 2
            if (*end != ',') valid = 0;
            else {
                char *start = end + 1;
                peak = strtol(start, &end, 10);
                if (end == start || errno || (peak != 0 && peak != 1)) valid = 0;
            }
#endif
            while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
#if ECG_MODULE == 1
            if (value < 0 || value > 4095) valid = 0;
#elif ECG_MODULE == 2
            if (value < INT16_MIN || value > INT16_MAX) valid = 0;
#else
            if (value < 0 || value > INT32_MAX || (count && value <= input[count-1].value)) valid = 0;
#endif
            if (*end || !valid || count >= 1000000) {
                fprintf(stderr, "Invalid input at line %zu (maximum 1000000 rows)\n", line_no);
                fclose(file); free(input); return 2;
            }
            if (count == capacity) {
                capacity *= 2;
                input_t *grown = realloc(input, capacity * sizeof(*input));
                if (!grown) { fclose(file); free(input); return 1; }
                input = grown;
            }
            input[count++] = (input_t){(int32_t)value, (int)peak};
        }
        int failed = ferror(file);
        fclose(file);
        if (failed || !count) { fprintf(stderr, "Input is empty or unreadable\n"); free(input); return 2; }
    } else {
        count = capacity;
        for (size_t i = 0; i < count; ++i) {
            unsigned phase = (unsigned)(i % 288);
            int value = phase == 108 ? 1000 : (phase == 107 || phase == 109 ? -250 : 0);
#if ECG_MODULE == 1
            value += 2048;
#elif ECG_MODULE == 3
            value = (int)((i + 1) * 800);
#endif
            input[i] = (input_t){value, phase == 108};
        }
    }
    uint32_t checksum = 0;
    double elapsed = 0;
    for (unsigned long run = 0; run < repeat; ++run) {
        module_reset();
        double start = seconds();
        for (size_t i = 0; i < count; ++i) {
            uint32_t time_ms = (uint32_t)((i * 1000ULL + 180) / 360);
            checksum += module_push(input[i].value, input[i].peak, time_ms);
        }
        elapsed += seconds() - start;
    }
    printf("module=%s platform=host input=%s rows=%zu repeat=%lu calls=%llu elapsed_ms=%.6f us_per_call=%.6f checksum=%u\n",
           MODULE_NAME, filename ? filename : "synthetic", count, repeat,
           (unsigned long long)count * repeat, elapsed * 1000,
           elapsed * 1e6 / ((double)count * repeat), (unsigned)checksum);
    free(input);
    return 0;
}
