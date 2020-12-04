#define WIN32_LEAN_AND_MEAN
#include <czmq.h>
#include <Windows.h>

#define CLOCK_MONOTONIC_RAW       0
#define CLOCK_PROCESS_CPUTIME_ID  1

void clock_gettime(int clock_type, struct timespec* t);
void usleep(long usec);