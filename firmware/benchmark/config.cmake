# Benchmark selection. Override any value using -DNAME=value in a fresh cache.
option(ECG_TEST_BPM "Test Fast-QRS with 10s context and production 5s BPM update" ON)
option(ECG_TEST_SQI "Test native SQI: 10s window, 1s hop" ON)
option(ECG_TEST_AF "Test native AF: 30 RR, stride15 RR" ON)
set(ECG_TEST_SECONDS 60 CACHE STRING "Logical dummy stream duration in seconds (minimum30)")
if(NOT ECG_TEST_SECONDS MATCHES "^[0-9]+$" OR ECG_TEST_SECONDS LESS 30 OR ECG_TEST_SECONDS GREATER 3600)
    message(FATAL_ERROR "ECG_TEST_SECONDS must be an integer from30 through3600")
endif()
