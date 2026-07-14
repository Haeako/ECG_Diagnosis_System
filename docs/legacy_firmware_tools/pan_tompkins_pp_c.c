/*
 * Pan-Tompkins++-style C prototype for offline CSV testing and MCU porting.
 *
 * This is not a byte-for-byte port of the Python script because the Python
 * version uses offline SciPy operations such as filtfilt, Butterworth design,
 * interpolation and peakutils. This file keeps the embedded-friendly core:
 *
 *   ECG -> derivative -> square -> smoothing -> moving-window integration
 *       -> local maxima -> adaptive thresholds -> T-wave slope check
 *       -> search-back -> remap candidate to ECG extremum -> refractory merge
 *
 * It reads ESP32 CSV files and compares detected peaks against CSV is_peak.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_PATH_LEN 512

typedef struct {
    double timestamp_s;
    double ecg;
    int csv_peak;
} sample_t;

typedef struct {
    int *data;
    int count;
    int capacity;
} int_vec_t;

typedef struct {
    sample_t *samples;
    int count;
    int capacity;
} sample_vec_t;

typedef struct {
    char **items;
    int count;
    int capacity;
} str_vec_t;

typedef struct {
    int samples;
    int csv_peaks;
    int detected;
    int matched_csv;
    int false_positive;
    int missed_csv;
    double bpm_est;
} compare_result_t;

static void *xmalloc(size_t size)
{
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
    return ptr;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *out = (char *)xmalloc(n);
    memcpy(out, s, n);
    return out;
}

static void int_vec_push(int_vec_t *vec, int value)
{
    if (vec->count >= vec->capacity) {
        vec->capacity = vec->capacity ? vec->capacity * 2 : 64;
        vec->data = (int *)realloc(vec->data, (size_t)vec->capacity * sizeof(int));
        if (!vec->data) {
            fprintf(stderr, "out of memory\n");
            exit(2);
        }
    }
    vec->data[vec->count++] = value;
}

static void sample_vec_push(sample_vec_t *vec, sample_t value)
{
    if (vec->count >= vec->capacity) {
        vec->capacity = vec->capacity ? vec->capacity * 2 : 4096;
        vec->samples = (sample_t *)realloc(vec->samples, (size_t)vec->capacity * sizeof(sample_t));
        if (!vec->samples) {
            fprintf(stderr, "out of memory\n");
            exit(2);
        }
    }
    vec->samples[vec->count++] = value;
}

static void str_vec_push(str_vec_t *vec, const char *value)
{
    if (vec->count >= vec->capacity) {
        vec->capacity = vec->capacity ? vec->capacity * 2 : 64;
        vec->items = (char **)realloc(vec->items, (size_t)vec->capacity * sizeof(char *));
        if (!vec->items) {
            fprintf(stderr, "out of memory\n");
            exit(2);
        }
    }
    vec->items[vec->count++] = xstrdup(value);
}

static int cmp_str(const void *a, const void *b)
{
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    return strcmp(sa, sb);
}

static bool ends_with_csv(const char *name)
{
    size_t len = strlen(name);
    return len >= 4
        && tolower((unsigned char)name[len - 4]) == '.'
        && tolower((unsigned char)name[len - 3]) == 'c'
        && tolower((unsigned char)name[len - 2]) == 's'
        && tolower((unsigned char)name[len - 1]) == 'v';
}

static bool is_directory(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & S_IFDIR) != 0;
}

static const char *basename_ptr(const char *path)
{
    const char *a = strrchr(path, '/');
    const char *b = strrchr(path, '\\');
    const char *p = a > b ? a : b;
    return p ? p + 1 : path;
}

static void collect_csv_files(const char *path, str_vec_t *files)
{
    if (!is_directory(path)) {
        str_vec_push(files, path);
        return;
    }

    DIR *dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "cannot open directory %s: %s\n", path, strerror(errno));
        exit(1);
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        if (!ends_with_csv(entry->d_name)) {
            continue;
        }
        char full[MAX_PATH_LEN];
        snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);
        str_vec_push(files, full);
    }
    closedir(dir);
}

static int header_index(char **headers, int count, const char *name)
{
    for (int i = 0; i < count; ++i) {
        if (strcmp(headers[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

static int split_csv_line(char *line, char **fields, int max_fields)
{
    int count = 0;
    char *p = line;
    while (count < max_fields) {
        fields[count++] = p;
        char *comma = strchr(p, ',');
        if (!comma) {
            break;
        }
        *comma = '\0';
        p = comma + 1;
    }
    for (int i = 0; i < count; ++i) {
        char *s = fields[i];
        while (*s && isspace((unsigned char)*s)) {
            ++s;
        }
        fields[i] = s;
        char *end = s + strlen(s);
        while (end > s && isspace((unsigned char)end[-1])) {
            *--end = '\0';
        }
    }
    return count;
}

static sample_vec_t load_csv(const char *path, const char *column)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        exit(1);
    }

    char line[4096];
    if (!fgets(line, sizeof(line), fp)) {
        fprintf(stderr, "empty csv: %s\n", path);
        exit(1);
    }

    char *headers[64];
    int header_count = split_csv_line(line, headers, 64);
    int ts_idx = header_index(headers, header_count, "timestamp_ms");
    int ecg_idx = header_index(headers, header_count, column);
    int peak_idx = header_index(headers, header_count, "is_peak");
    if (ts_idx < 0 || ecg_idx < 0) {
        fprintf(stderr, "%s missing timestamp_ms or %s\n", path, column);
        exit(1);
    }

    sample_vec_t vec = {0};
    double first_ts = -1.0;
    while (fgets(line, sizeof(line), fp)) {
        char *fields[64];
        int n = split_csv_line(line, fields, 64);
        if (n <= ts_idx || n <= ecg_idx) {
            continue;
        }
        double ts = atof(fields[ts_idx]);
        if (first_ts < 0.0) {
            first_ts = ts;
        }
        sample_t s = {
            .timestamp_s = (ts - first_ts) / 1000.0,
            .ecg = atof(fields[ecg_idx]),
            .csv_peak = (peak_idx >= 0 && n > peak_idx) ? (atoi(fields[peak_idx]) != 0) : 0,
        };
        sample_vec_push(&vec, s);
    }
    fclose(fp);
    return vec;
}

static double max_abs_value(const double *x, int n)
{
    double m = 0.0;
    for (int i = 0; i < n; ++i) {
        double a = fabs(x[i]);
        if (a > m) {
            m = a;
        }
    }
    return m;
}

static void moving_average(const double *x, double *y, int n, int window)
{
    if (window < 1) {
        window = 1;
    }
    double acc = 0.0;
    for (int i = 0; i < n; ++i) {
        acc += x[i];
        if (i >= window) {
            acc -= x[i - window];
            y[i] = acc / window;
        } else {
            y[i] = acc / (i + 1);
        }
    }
}

static int find_local_max(const double *x, int n, int start, int end)
{
    if (start < 0) {
        start = 0;
    }
    if (end > n) {
        end = n;
    }
    if (start >= end) {
        return start < n ? start : n - 1;
    }
    int best = start;
    for (int i = start + 1; i < end; ++i) {
        if (x[i] > x[best]) {
            best = i;
        }
    }
    return best;
}

static int find_local_abs_extreme(const double *x, int n, int start, int end)
{
    if (start < 0) {
        start = 0;
    }
    if (end > n) {
        end = n;
    }
    if (start >= end) {
        return start < n ? start : n - 1;
    }
    int best = start;
    for (int i = start + 1; i < end; ++i) {
        if (fabs(x[i]) > fabs(x[best])) {
            best = i;
        }
    }
    return best;
}

static double mean_diff(const double *x, int start, int end)
{
    if (start < 0) {
        start = 0;
    }
    if (end <= start + 1) {
        return 0.0;
    }
    double acc = 0.0;
    int count = 0;
    for (int i = start + 1; i < end; ++i) {
        acc += x[i] - x[i - 1];
        ++count;
    }
    return count ? acc / count : 0.0;
}

static int_vec_t find_peaks_min_dist(const double *x, int n, int min_dist)
{
    int_vec_t peaks = {0};
    int last = -min_dist - 1;
    for (int i = 1; i < n - 1; ++i) {
        bool is_peak = x[i] >= x[i - 1] && x[i] > x[i + 1];
        if (!is_peak) {
            continue;
        }
        if (i - last < min_dist) {
            if (peaks.count > 0 && x[i] > x[peaks.data[peaks.count - 1]]) {
                peaks.data[peaks.count - 1] = i;
                last = i;
            }
        } else {
            int_vec_push(&peaks, i);
            last = i;
        }
    }
    return peaks;
}

static int_vec_t pan_tompkins_pp_light_detect(const sample_t *samples, int n, int fs)
{
    int_vec_t accepted = {0};
    if (n < fs) {
        return accepted;
    }

    double *ecg = (double *)xmalloc((size_t)n * sizeof(double));
    double mean = 0.0;
    for (int i = 0; i < n; ++i) {
        mean += samples[i].ecg;
    }
    mean /= n;
    for (int i = 0; i < n; ++i) {
        ecg[i] = samples[i].ecg - mean;
    }
    double scale = max_abs_value(ecg, n);
    if (scale < 1e-9) {
        free(ecg);
        return accepted;
    }
    for (int i = 0; i < n; ++i) {
        ecg[i] /= scale;
    }

    double *diff = (double *)xmalloc((size_t)n * sizeof(double));
    double *sq = (double *)xmalloc((size_t)n * sizeof(double));
    double *smooth = (double *)xmalloc((size_t)n * sizeof(double));
    double *mwi = (double *)xmalloc((size_t)n * sizeof(double));

    diff[0] = 0.0;
    for (int i = 1; i < n; ++i) {
        diff[i] = ecg[i] - ecg[i - 1];
        sq[i] = diff[i] * diff[i];
    }
    sq[0] = 0.0;

    moving_average(sq, smooth, n, (int)lrint(0.060 * fs));
    moving_average(smooth, mwi, n, (int)lrint(0.150 * fs));

    int_vec_t locs = find_peaks_min_dist(mwi, n, (int)lrint(0.231 * fs));
    if (locs.count == 0) {
        goto cleanup;
    }

    int train_n = 2 * fs + 1;
    if (train_n > n) {
        train_n = n;
    }
    double max_mwi = 0.0;
    double mean_mwi = 0.0;
    double max_ecg_abs = 0.0;
    double mean_ecg = 0.0;
    for (int i = 0; i < train_n; ++i) {
        if (mwi[i] > max_mwi) {
            max_mwi = mwi[i];
        }
        mean_mwi += mwi[i];
        if (fabs(ecg[i]) > max_ecg_abs) {
            max_ecg_abs = fabs(ecg[i]);
        }
        mean_ecg += ecg[i];
    }
    mean_mwi /= train_n;
    mean_ecg /= train_n;

    double THR_SIG = max_mwi / 3.0;
    double THR_NOISE = mean_mwi / 2.0;
    double SIG_LEV = THR_SIG;
    double NOISE_LEV = THR_NOISE;
    double THR_SIG1 = max_ecg_abs / 3.0;
    double THR_NOISE1 = fabs(mean_ecg) / 2.0;
    double SIG_LEV1 = THR_SIG1;
    double NOISE_LEV1 = THR_NOISE1;

    int_vec_t qrs_i = {0};
    int last_qrs_mwi = 0;

    for (int p = 0; p < locs.count; ++p) {
        int loc = locs.data[p];
        double pk = mwi[loc];
        int raw_start = loc - (int)lrint(0.150 * fs);
        int raw_peak = find_local_max(ecg, n, raw_start, loc + 1);
        double y_i = fabs(ecg[raw_peak]);

        double mean_rr = 0.0;
        if (qrs_i.count >= 9) {
            for (int k = qrs_i.count - 8; k < qrs_i.count; ++k) {
                mean_rr += (double)(qrs_i.data[k] - qrs_i.data[k - 1]);
            }
            mean_rr /= 8.0;
        }

        bool accepted_mwi = false;
        bool skip_t_wave = false;

        if (pk >= THR_SIG) {
            if (qrs_i.count >= 3) {
                bool close_to_prev = (loc - last_qrs_mwi) <= (int)lrint(0.360 * fs);
                if (mean_rr > 0.0 && (loc - last_qrs_mwi) <= (int)lrint(0.5 * mean_rr)) {
                    close_to_prev = true;
                }
                if (close_to_prev) {
                    double slope1 = mean_diff(mwi, loc - (int)lrint(0.070 * fs), loc + 1);
                    double slope2 = mean_diff(mwi, last_qrs_mwi - (int)lrint(0.070 * fs), last_qrs_mwi + 1);
                    if (fabs(slope1) <= fabs(0.6 * slope2)) {
                        skip_t_wave = true;
                    }
                }
            }

            if (!skip_t_wave) {
                accepted_mwi = true;
                int_vec_push(&qrs_i, loc);
                last_qrs_mwi = loc;
                SIG_LEV = 0.125 * pk + 0.875 * SIG_LEV;
                SIG_LEV1 = 0.125 * y_i + 0.875 * SIG_LEV1;

                int r = find_local_abs_extreme(ecg, n, raw_start, loc + (int)lrint(0.080 * fs));
                int_vec_push(&accepted, r);
            } else {
                NOISE_LEV = 0.125 * pk + 0.875 * NOISE_LEV;
                NOISE_LEV1 = 0.125 * y_i + 0.875 * NOISE_LEV1;
            }
        } else if (pk >= THR_NOISE) {
            NOISE_LEV = 0.125 * pk + 0.875 * NOISE_LEV;
            NOISE_LEV1 = 0.125 * y_i + 0.875 * NOISE_LEV1;
        } else {
            NOISE_LEV = 0.125 * pk + 0.875 * NOISE_LEV;
            NOISE_LEV1 = 0.125 * y_i + 0.875 * NOISE_LEV1;
        }

        if (!accepted_mwi && qrs_i.count > 0) {
            int search_gap = loc - qrs_i.data[qrs_i.count - 1];
            bool search_back = search_gap >= (int)lrint(1.4 * fs);
            if (!search_back && mean_rr > 0.0) {
                search_back = search_gap >= (int)lrint(1.66 * mean_rr) || search_gap > fs;
            }
            if (search_back) {
                int sb_start = qrs_i.data[qrs_i.count - 1] + (int)lrint(0.360 * fs);
                int sb_end = loc + 1;
                if (sb_start < sb_end && sb_end <= n) {
                    int sb_peak = find_local_max(mwi, n, sb_start, sb_end);
                    if (mwi[sb_peak] > THR_NOISE) {
                        int_vec_push(&qrs_i, sb_peak);
                        last_qrs_mwi = sb_peak;
                        SIG_LEV = 0.75 * mwi[sb_peak] + 0.25 * SIG_LEV;
                        int r = find_local_abs_extreme(ecg, n,
                            sb_peak - (int)lrint(0.150 * fs),
                            sb_peak + (int)lrint(0.080 * fs));
                        if (fabs(ecg[r]) > fabs(THR_NOISE1)) {
                            int_vec_push(&accepted, r);
                            SIG_LEV1 = 0.75 * fabs(ecg[r]) + 0.25 * SIG_LEV1;
                        }
                    }
                }
            }
        }

        THR_SIG = NOISE_LEV + 0.25 * fabs(SIG_LEV - NOISE_LEV);
        THR_NOISE = 0.4 * THR_SIG;
        THR_SIG1 = NOISE_LEV1 + 0.25 * fabs(SIG_LEV1 - NOISE_LEV1);
        THR_NOISE1 = 0.4 * THR_SIG1;
    }

    /* Final MCU-safe refractory merge on remapped ECG peaks. */
    int min_rr = (int)lrint(0.250 * fs);
    int out_count = 0;
    for (int i = 0; i < accepted.count; ++i) {
        int r = accepted.data[i];
        if (out_count == 0) {
            accepted.data[out_count++] = r;
            continue;
        }
        if (r - accepted.data[out_count - 1] < min_rr) {
            if (fabs(ecg[r]) > fabs(ecg[accepted.data[out_count - 1]])) {
                accepted.data[out_count - 1] = r;
            }
        } else {
            accepted.data[out_count++] = r;
        }
    }
    accepted.count = out_count;

    free(qrs_i.data);

cleanup:
    free(locs.data);
    free(ecg);
    free(diff);
    free(sq);
    free(smooth);
    free(mwi);
    return accepted;
}

static compare_result_t compare_peaks(const sample_vec_t *samples, const int_vec_t *detected, int fs)
{
    int_vec_t csv = {0};
    for (int i = 0; i < samples->count; ++i) {
        if (samples->samples[i].csv_peak) {
            int_vec_push(&csv, i);
        }
    }

    int tol = (int)lrint(0.080 * fs);
    bool *det_used = (bool *)calloc((size_t)detected->count, sizeof(bool));
    int matched = 0;
    for (int i = 0; i < csv.count; ++i) {
        int best_j = -1;
        int best_d = tol + 1;
        for (int j = 0; j < detected->count; ++j) {
            if (det_used[j]) {
                continue;
            }
            int d = abs(csv.data[i] - detected->data[j]);
            if (d <= tol && d < best_d) {
                best_d = d;
                best_j = j;
            }
        }
        if (best_j >= 0) {
            det_used[best_j] = true;
            ++matched;
        }
    }
    free(det_used);

    double duration = samples->count > 1
        ? samples->samples[samples->count - 1].timestamp_s - samples->samples[0].timestamp_s
        : 0.0;
    compare_result_t r = {
        .samples = samples->count,
        .csv_peaks = csv.count,
        .detected = detected->count,
        .matched_csv = matched,
        .false_positive = detected->count - matched,
        .missed_csv = csv.count - matched,
        .bpm_est = duration > 0.0 ? detected->count * 60.0 / duration : 0.0,
    };
    free(csv.data);
    return r;
}

static void ensure_dir(const char *path)
{
#ifdef _WIN32
    char cmd[MAX_PATH_LEN + 32];
    snprintf(cmd, sizeof(cmd), "mkdir \"%s\" >nul 2>nul", path);
    system(cmd);
#else
    char cmd[MAX_PATH_LEN + 32];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", path);
    system(cmd);
#endif
}

static void write_peak_csv(const char *out_dir, const char *input_path, const sample_vec_t *samples, const int_vec_t *peaks)
{
    char out[MAX_PATH_LEN];
    const char *base = basename_ptr(input_path);
    snprintf(out, sizeof(out), "%s/%s_c_peaks.csv", out_dir, base);
    FILE *fp = fopen(out, "w");
    if (!fp) {
        return;
    }
    fprintf(fp, "index,time_s,ecg\n");
    for (int i = 0; i < peaks->count; ++i) {
        int idx = peaks->data[i];
        if (idx >= 0 && idx < samples->count) {
            fprintf(fp, "%d,%.6f,%.6f\n", idx, samples->samples[idx].timestamp_s, samples->samples[idx].ecg);
        }
    }
    fclose(fp);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "pre_move_data";
    const char *column = argc > 2 ? argv[2] : "filtered";
    const int fs = argc > 3 ? atoi(argv[3]) : 360;
    const char *out_dir = "visualize/pan_tompkins_pp_c";

    str_vec_t files = {0};
    collect_csv_files(path, &files);
    qsort(files.items, (size_t)files.count, sizeof(char *), cmp_str);
    ensure_dir("visualize");
    ensure_dir(out_dir);

    char summary_path[MAX_PATH_LEN];
    snprintf(summary_path, sizeof(summary_path), "%s/summary.csv", out_dir);
    FILE *summary = fopen(summary_path, "w");
    if (!summary) {
        fprintf(stderr, "cannot write %s\n", summary_path);
        return 1;
    }
    fprintf(summary, "file,samples,csv_peaks,c_detected,matched_80ms,false_positive,missed_csv,bpm_est\n");

    int total_samples = 0;
    int total_csv = 0;
    int total_detected = 0;
    int total_matched = 0;
    int total_fp = 0;
    int total_missed = 0;

    printf("Loading %d file(s)\n", files.count);
    for (int i = 0; i < files.count; ++i) {
        sample_vec_t samples = load_csv(files.items[i], column);
        int_vec_t peaks = pan_tompkins_pp_light_detect(samples.samples, samples.count, fs);
        compare_result_t cr = compare_peaks(&samples, &peaks, fs);

        printf("%s: samples=%d csv=%d c_peak=%d matched=%d fp=%d missed=%d bpm=%.1f\n",
               basename_ptr(files.items[i]), cr.samples, cr.csv_peaks, cr.detected,
               cr.matched_csv, cr.false_positive, cr.missed_csv, cr.bpm_est);
        fprintf(summary, "%s,%d,%d,%d,%d,%d,%d,%.2f\n",
                basename_ptr(files.items[i]), cr.samples, cr.csv_peaks, cr.detected,
                cr.matched_csv, cr.false_positive, cr.missed_csv, cr.bpm_est);

        write_peak_csv(out_dir, files.items[i], &samples, &peaks);

        total_samples += cr.samples;
        total_csv += cr.csv_peaks;
        total_detected += cr.detected;
        total_matched += cr.matched_csv;
        total_fp += cr.false_positive;
        total_missed += cr.missed_csv;

        free(samples.samples);
        free(peaks.data);
    }
    fclose(summary);

    printf("Total samples: %d\n", total_samples);
    printf("Total CSV peaks: %d\n", total_csv);
    printf("Total C peaks: %d\n", total_detected);
    printf("Matched within 80 ms: %d\n", total_matched);
    printf("False positive vs CSV: %d\n", total_fp);
    printf("Missed CSV peaks: %d\n", total_missed);
    printf("Saved summary: %s\n", summary_path);

    for (int i = 0; i < files.count; ++i) {
        free(files.items[i]);
    }
    free(files.items);
    return 0;
}
