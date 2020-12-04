#include "faketime.h"

void clock_gettime(int clock_type, struct timespec* t)
{
    (void)clock_type;
    static LARGE_INTEGER frequency;
    if (frequency.QuadPart == 0)
        QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    t->tv_sec = now.QuadPart / frequency.QuadPart;
    t->tv_nsec = (long)((now.QuadPart % frequency.QuadPart) * (1e9 / (double)frequency.QuadPart));
}

void usleep(long usec)
{
    HANDLE timer;
    LARGE_INTEGER ft;

    ft.QuadPart = -(10 * usec); // Convert to 100 nanosecond interval, negative value indicates relative time

    timer = CreateWaitableTimer(NULL, TRUE, NULL);
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
}