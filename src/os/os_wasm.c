#ifdef __EMSCRIPTEN__

#include "os.h"

#include <time.h>
#include <emscripten.h>

void os_time_init() { };

u64 os_now_usec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

void os_sleep_ms(u64 ms) {
    emscripten_sleep(ms);
}

#endif // __EMSCRIPTEN__
