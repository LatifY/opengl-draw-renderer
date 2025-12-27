#ifndef OS_H
#define OS_H

#include "base/base.h"

void os_time_init(void);
u64 os_now_usec(void);
void os_sleep_ms(u64 ms);

#endif // OS_H

