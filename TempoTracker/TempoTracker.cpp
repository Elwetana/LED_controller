#define _CRT_SECURE_NO_WARNINGS

// TempoTracker.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <Windows.h>
#include <conio.h>
#include <vector>

int main()
{
    LARGE_INTEGER frequency;
    LARGE_INTEGER startingTime, currentTime, elapsedTime;
    QueryPerformanceFrequency(&frequency);

    std::cout << "Hello World!\n";
    std::vector<long> beats;
    long last_beat = 0;
    while (1)
    {
        char c = _getch();
        if (c == 'q')
            break;
        if (c == 's')
        {
            QueryPerformanceCounter(&startingTime);
        }
        QueryPerformanceCounter(&currentTime);
        elapsedTime.QuadPart = currentTime.QuadPart - startingTime.QuadPart;

        // We now have the elapsed number of ticks, along with the
        // number of ticks-per-second. We use these values
        // to convert to the number of elapsed microseconds.
        // To guard against loss-of-precision, we convert
        // to microseconds *before* dividing by ticks-per-second.
        //

        elapsedTime.QuadPart *= 1e6;
        long elapsed_us = elapsedTime.QuadPart / frequency.QuadPart;
        beats.push_back(elapsed_us);
        double bpm = beats.size() * 1e6 * 60 / elapsed_us;
        printf("elapsed %li, avg bpm: %f, beat length: %li\n", elapsed_us, bpm, (elapsed_us - last_beat));
        last_beat = elapsed_us;
    }
    FILE* fout = fopen("beats.txt", "w");
    for(int i = 0; i < beats.size(); i++)
    {
        fprintf(fout, "%li\n", beats[i]);
    }
    fclose(fout);
}
