#include <Windows.h>

#define CLOCK_MONOTONIC_RAW       0
#define CLOCK_PROCESS_CPUTIME_ID  1

struct timespec {
    unsigned long tv_sec;
    unsigned long tv_nsec;
};

void clock_gettime(int clock_type, struct timespec* t)
{
    static LARGE_INTEGER frequency;
    if (frequency.QuadPart == 0)
        QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    t->tv_sec = now.QuadPart / frequency.QuadPart;
    t->tv_nsec = (now.QuadPart % frequency.QuadPart) * (1e9 / (double)frequency.QuadPart);
}

int time(char* p)
{
    return 1;
}

void usleep(long usec)
{
    HANDLE timer; 
    LARGE_INTEGER ft; 

    ft.QuadPart = -(10*usec); // Convert to 100 nanosecond interval, negative value indicates relative time

    timer = CreateWaitableTimer(NULL, TRUE, NULL); 
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0); 
    WaitForSingleObject(timer, INFINITE); 
    CloseHandle(timer);    
}