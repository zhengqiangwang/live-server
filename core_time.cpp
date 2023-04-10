#include "core_time.h"

utime_t Duration(utime_t start, utime_t end)
{
    if(start == 0 | end == 0){
        return 0;
    }

    return end - start;
}
