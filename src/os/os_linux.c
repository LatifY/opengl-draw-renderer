#include "os.h"

#ifdef PLATFORM_LINUX

#include <unistd.h>
#include <time.h>

void os_time_init(void) { }
u64 os_now_usec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
void os_sleep_ms(u64 ms) {
    usleep(ms * 1000);
}

#endif

