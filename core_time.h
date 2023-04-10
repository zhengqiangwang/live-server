#ifndef CORE_TIME_H
#define CORE_TIME_H

#include "core.h"

//time and duration unit, in us.
typedef int64_t utime_t;

//the time unit in ms, for example 100 * UTIME_MILLISECONDS means 100ms
#define UTIME_MILLISECONDS 1000

//convert utime_t as ms
#define u2ms(us) ((us) / UTIME_MILLISECONDS)
#define u2msi(us) int((us) / UTIME_MILLISECONDS)

//them time duration = end - start. return 0, if start of end is 0.
utime_t Duration(utime_t start, utime_t end);

//the time unit in ms, for example 120 * UTIME_SECONDS means 120s.
#define UTIME_SECONDS 1000000LL

//the time unit in minutes, for example 3 * UTIME_MINUTES means 3m
#define UTIME_MINUTES 60000000LL

//the time unit in hours, for example 2 * UTIME_HOURS means 2h
#define UTIME_HOURS 3600000000LL

//never timeout
#define UTIME_NO_TIMEOUT ((utime_t) -1LL)

#endif // CORE_TIME_H
