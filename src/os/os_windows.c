#include "os.h"

#ifdef PLATFORM_WIN32

#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

static u64 w32_ticks_per_sec = 1;
void os_time_init(void) {
    LARGE_INTEGER perf_freq;
    if (QueryPerformanceFrequency(&perf_freq)) {
        w32_ticks_per_sec = ((u64)perf_freq.HighPart << 32) | perf_freq.LowPart;
    } else {
        fprintf(stderr, "Failed to initialize time: could not get performance frequency\n");
    }
}
u64 os_now_usec(void) {
    u64 out = 0;
    LARGE_INTEGER perf_count;
    if (QueryPerformanceCounter(&perf_count)) {
        u64 ticks = ((u64)perf_count.HighPart << 32) | perf_count.LowPart;
        out = ticks * 1000000 / w32_ticks_per_sec;
    } else {
        fprintf(stderr, "Failed to retrive time in micro seconds\n");
    }
    return out;
}
void os_sleep_ms(u64 ms) {
    Sleep(ms);
}

#endif

