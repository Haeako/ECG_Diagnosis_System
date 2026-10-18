# ESP32 ECG pipeline profiling assessment

Date: 2026-10-18  
Target: ESP32-D0WD-V3, dual core, CPU configured at 160 MHz  
Firmware: ESP-IDF 6.0.1, profiling instrumentation enabled  
Capture: 8 reporting windows (~80 s), 27,975 timer ticks/samples

## Result

The pipeline sustained the configured sampling rate without losing a sample,
but it does **not yet meet a strict per-sample hard deadline**. One observed
pipeline execution took 3,337 us, exceeding the configured 2,770 us period by
567 us. The binary semaphore absorbed that single-period delay: timer ticks and
processed samples still matched exactly and no notification overrun occurred.

| Metric | Measured result | Assessment |
|---|---:|---|
| Sampling rate | ~361.01 Hz (100000 / 277) | Stable, but 0.28% above requested 360 Hz |
| ISR jitter p99 absolute | <= 10 us | Good |
| ISR jitter observed min/max | -489 / +489 us | Rare paired outlier; investigate interrupt masking |
| ISR-to-task dispatch p99 | <= 25 us | Good |
| Pipeline p95 / p99 | <= 250 / <= 250 us | Good typical latency |
| Pipeline observed max | 3,337 us | **Fails 2,770 us hard deadline** |
| SD write p95 / p99 | <= 100 us / <= 10 ms | Acceptable asynchronously |
| SD write observed max | 47,179 us | Large tail, correctly isolated in Core 0 writer |
| SD queue peak | 50.2% | No overflow; moderate headroom |
| BLE queue peak | 3.1% | Good |
| Timer notification overruns | 0 | Pass |
| SD drops / errors | 0 / 0 | Pass |
| BLE queue drops | 0 | Pass |
| CPU0 mean | 19.8% | Good headroom |
| CPU1 mean | 7.3% | Good headroom |
| State task minimum free stack | 664 B | **Low margin; increase or reduce stack use** |
| ECG task minimum free stack | 2,968 B | Good |
| SD task minimum free stack | 3,020 B | Good |
| BLE TX minimum free stack | 2,496 B | Good |

Percentiles are fixed-histogram upper bounds, not interpolated sample
percentiles. pipeline_max_us and sd_max_us are exact observed maxima.

## Engineering judgement

1. The architecture is throughput-safe in this run: 27,975/27,975 samples were
   processed and all recorded drop/error counters remained zero.
2. The 3,337 us pipeline outlier means the current evidence cannot claim a
   hard real-time 360 Hz guarantee. Instrument individual ADC/filter/SQI/AF/BLE/
   ring-buffer stages next to locate the outlier.
3. The +/-489 us ISR jitter pair suggests one interrupt was delayed and the next
   interval shortened by the same amount. Audit long critical sections and
   flash/cache-disabled regions. It did not cause a sample loss in this run.
4. SD has a heavy latency tail (47.2 ms max), but the separate writer task and
   buffer prevented it from blocking ECG processing. Longer soak tests should
   include FAT allocation/segment rotation and a nearly-full card.
5. State task stack margin of 664 B is too small for a robust production margin.
   Increase its allocation from 3,072 B (for example to 4,096 B), then repeat
   worst-path recovery and fault-injection tests.
6. The configured GPTimer integer divider generates ~361.01 Hz. For an accurate
   360 Hz timebase, use a resolution divisible by 360 (for example 360 kHz or
   3.6 MHz), or implement fractional alarm scheduling.

## Evidence

- pipeline_profile.csv: parsed measurement windows.
- pipeline_profile.log: complete serial capture including firmware health
  records and PERF_CSV source lines.
- Instrumentation reports ISR timing, ISR-to-task latency, pipeline response
  time, SD latency distribution/max, queue peaks, drops/errors, stack
  high-water marks, and per-core idle-derived CPU utilization.

This is an observed worst-case result (OWCET), not a mathematical WCET proof.
The next confidence step is a multi-hour soak plus controlled BLE traffic,
SD segment rotation, fault recovery, and CPU/interrupt stress.
