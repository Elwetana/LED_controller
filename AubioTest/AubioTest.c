#define _CRT_SECURE_NO_WARNINGS


/*#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
*/

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include "aubio.h"

#define BLOCK_SIZE 4096     /* 1024 samples * 2 channels * 16 bits */
#define BLOCK_COUNT 8
#define INT_TO_FLOAT 3.0517578125e-05 // = 1. / 32768.

static void CALLBACK waveOutProc(HWAVEOUT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);
static void allocateBlocks();
static void freeBlocks(WAVEHDR* blockArray);
static void writeAudio(HWAVEOUT hWaveOut, LPSTR data, int size);

static CRITICAL_SECTION waveCriticalSection;
static WAVEHDR* waveBlocks;
static volatile int waveFreeBlockCount;
static volatile int lastBlockPlayed;
static int waveCurrentBlock;

static uint_t hop_s;
static uint_t win_s;
static aubio_tempo_t* atTempo;
static fvec_t* tempo_in;
static fvec_t* tempo_out;

/* playback loop */
static int lastBlockProcessed = -1;
static LONG64 currentFrame = 0;
static LONG64 currentSample = 0;
static float lastPhase = -1.0f;

HANDLE hStdout = NULL;
CONSOLE_SCREEN_BUFFER_INFO csbiInfo;


void allocateBlocks() //this BLOCK_SIZE and BLOCK_COUNT
{
    int size = BLOCK_SIZE;
    int count = BLOCK_COUNT;
    unsigned char* buffer;
    int i;
    DWORD totalBufferSize = (size + sizeof(WAVEHDR)) * count;
    /* allocate memory for the entire set in one go */
    if ((buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, totalBufferSize)) == NULL)
    {
        fprintf(stderr, "Memory allocation error\n");
        ExitProcess(1);
    }
    /* and set up the pointers to each bit */
    waveBlocks = (WAVEHDR*)buffer;
    buffer += sizeof(WAVEHDR) * count;
    for (i = 0; i < count; i++) {
        waveBlocks[i].dwBufferLength = size;
        waveBlocks[i].lpData = buffer;
        buffer += size;
    }
}

void init(INT16** sbuffer, HANDLE* hFile)
{
    /* initialise the module variables */
    allocateBlocks();
    *sbuffer = (INT16*)malloc(BLOCK_SIZE * BLOCK_COUNT);
    waveFreeBlockCount = BLOCK_COUNT;
    waveCurrentBlock = 0;
    lastBlockPlayed = -1;
    InitializeCriticalSection(&waveCriticalSection);

    char* fname;
    //fname = _strdup("d:/code/C++/rpi_ws281x/AubioTest/StairwayToHeaven.raw");
    fname = _strdup("d:/code/C++/rpi_ws281x/AubioTest/ureky.raw");

    /* try and open the file */
    if ((*hFile = CreateFile(fname, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE)
    {
        DWORD dw = GetLastError();
        char path[MAX_PATH];
        GetCurrentDirectory(MAX_PATH, path);
        fprintf(stderr, "unable to open file '%s' in path '%s' with error %i\n", fname, path, dw);
        ExitProcess(1);
    }

    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdout == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Cannot get console handle\n");
        exit(1);
    }
    if (!GetConsoleScreenBufferInfo(hStdout, &csbiInfo)) {
        fprintf(stderr, "Cannot get console info.\n");
        exit(2);
    }
    CONSOLE_CURSOR_INFO cciInfo;
    if (!GetConsoleCursorInfo(hStdout, &cciInfo)) {
        fprintf(stderr, "Cannot get cursor info\n");
        exit(3);
    }
    cciInfo.bVisible = 0;
    SetConsoleCursorInfo(hStdout, &cciInfo);
}

void writeAudio(HWAVEOUT hWaveOut, LPSTR data, int size)
{
    if (waveCurrentBlock < 0 || waveCurrentBlock >= BLOCK_COUNT) {
        fprintf(stderr, "Invalid current block, exiting\n");
        exit(-1);
    }

    WAVEHDR* current;
    int remain;
    current = &waveBlocks[waveCurrentBlock];
    while (size > 0) {
        /* first make sure the header we're going to use is unprepared */
        if (current->dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(hWaveOut, current, sizeof(WAVEHDR));
        if (size < (int)(BLOCK_SIZE - current->dwUser)) {
            memcpy(current->lpData + current->dwUser, data, size);
            current->dwUser += size;
            break;
        }
        remain = BLOCK_SIZE - current->dwUser;
        memcpy(current->lpData + current->dwUser, data, remain);
        size -= remain;
        data += remain;
        current->dwBufferLength = BLOCK_SIZE;
        waveOutPrepareHeader(hWaveOut, current, sizeof(WAVEHDR));
        //fprintf(stdout, "Writing data to card\n");
        waveOutWrite(hWaveOut, current, sizeof(WAVEHDR));
        EnterCriticalSection(&waveCriticalSection);
        waveFreeBlockCount--;
        LeaveCriticalSection(&waveCriticalSection);

        /*
         * point to the next block
         */
        waveCurrentBlock++;
        waveCurrentBlock %= BLOCK_COUNT;
        current = &waveBlocks[waveCurrentBlock];
        current->dwUser = 0;
    }
}

void freeBlocks(WAVEHDR* blockArray)
{
    /* and this is why allocateBlocks works the way it does */
    HeapFree(GetProcessHeap(), 0, blockArray);
}

static void CALLBACK waveOutProc(HWAVEOUT hWaveOut, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
    /* pointer to free block counter */
    int* freeBlockCounter = (int*)dwInstance; //waveFreeBlockCount
    /* ignore calls that occur due to openining and closing the device. */
    if (uMsg != WOM_DONE)
        return;
    //fprintf(stdout, "Block finished playing\n");
    if (*freeBlockCounter) {
        //fprintf(stdout, "Block finished playing before all blocks were submitted\n");
    }
    EnterCriticalSection(&waveCriticalSection);
    (*freeBlockCounter)++;                  //waveFreeBlockCount++
    lastBlockPlayed++;
    lastBlockPlayed %= BLOCK_COUNT;
    LeaveCriticalSection(&waveCriticalSection);
}


void process_audio(INT16 *sbuffer, int sample_rate)
{
    int freq_bands[] = { 0, 300, 2000 };
    struct s {
        float sum;
        int count;
    };
    static struct s* sums = NULL; 
    static struct s** bin_to_sum = NULL;
    static int n_bands;
    currentFrame++;
    currentSample += hop_s;
    for (uint_t iSample = 0; iSample < hop_s; iSample++) {
        float value = ((sbuffer[lastBlockPlayed * 2048 + 2 * iSample] + sbuffer[lastBlockPlayed * 2048 + 2 * iSample + 1]) / 2) * (float)INT_TO_FLOAT;
        fvec_set_sample(tempo_in, value, iSample);
    }
    int is_silence = aubio_silence_detection(tempo_in, aubio_tempo_get_silence(atTempo));
    if (!is_silence) {
        aubio_tempo_do(atTempo, tempo_in, tempo_out);
        cvec_t* fftgrain = aubio_tempo_get_fftgrain(atTempo);

        if (sums == NULL) {
            n_bands = sizeof(freq_bands) / sizeof(int);
            sums = malloc(sizeof(struct s) * n_bands);                  //this leaks memory, there is no free
            bin_to_sum = malloc(sizeof(struct s*) * fftgrain->length);  //this leaks memory, there is no free
            for (int iGrain = 0; iGrain < fftgrain->length; iGrain++) {
                smpl_t freq = aubio_bintofreq(iGrain, sample_rate, 2 * fftgrain->length + 1);
                int i_band = n_bands - 1;
                while (freq < freq_bands[i_band]) 
                    i_band--;
                bin_to_sum[iGrain] = sums + i_band;
            }
        }
        memset(sums, 0, sizeof(struct s)* n_bands);
        for (int iGrain = 0; iGrain < fftgrain->length; iGrain++) {
            bin_to_sum[iGrain]->sum += fftgrain->norm[iGrain];
            bin_to_sum[iGrain]->count++;
        }

        /*  TEMPO OUTPUT */
        static int tempo_x;
        uint_t last_beat = aubio_tempo_get_last(atTempo);
        smpl_t beat_length = aubio_tempo_get_period(atTempo);
        float samples_remaining = last_beat + beat_length - currentSample;
        if (samples_remaining > 0) {
            float phase = samples_remaining / (float)beat_length;
            if (phase > (lastPhase)) { //we have to clear the output
                csbiInfo.dwCursorPosition.X = 0;
                csbiInfo.dwCursorPosition.Y = 0;
                SetConsoleCursorPosition(hStdout, csbiInfo.dwCursorPosition);
                WriteFile(hStdout, "                                                         ", 50, NULL, NULL);
                tempo_x = 0;
            }
            csbiInfo.dwCursorPosition.Y = 0;
            csbiInfo.dwCursorPosition.X = tempo_x++;
            SetConsoleCursorPosition(hStdout, csbiInfo.dwCursorPosition);
            if (phase < 0.2) WriteFile(hStdout, "!", 1, NULL, NULL);
            else if (phase < 0.5) WriteFile(hStdout, "=", 1, NULL, NULL);
            else if (phase < 0.75) WriteFile(hStdout, "-", 1, NULL, NULL);
            else WriteFile(hStdout, ".", 1, NULL, NULL);
            //printf("%f %f\n", phase, lastPhase);
            lastPhase = phase;
        }

        /* SPECTRUM OUTPUT */
        for (int iBand = 0; iBand < n_bands; ++iBand) {
            csbiInfo.dwCursorPosition.X = 0;
            csbiInfo.dwCursorPosition.Y = 1 + iBand;
            SetConsoleCursorPosition(hStdout, csbiInfo.dwCursorPosition);
            WriteFile(hStdout, "                                                                                                                                      ", 120, NULL, NULL);
            csbiInfo.dwCursorPosition.X = 0;
            SetConsoleCursorPosition(hStdout, csbiInfo.dwCursorPosition);
            WriteFile(hStdout, "+", 1, NULL, NULL);
            char b[32];
            int nChar = sprintf(b, "%f", sums[iBand].sum); // sums[iBand].count);
            //WriteFile(hStdout, b, nChar, NULL, NULL);
            
            for (int i = 0; i < sums[iBand].sum / 200.0f && i < 119; i++) {
                WriteFile(hStdout, "=", 1, NULL, NULL);
            }
            if(sums[iBand].sum / 200.0f > 120 ) WriteFile(hStdout, "!", 1, NULL, NULL);
        }
    }
}

int main(int argc, char* argv[])
{
    HWAVEOUT hWaveOut;          /* device handle */
    HANDLE hFile = NULL;        /* file handle */
    WAVEFORMATEX wfx;           /* look this up in your documentation */
    char buffer[BLOCK_SIZE];    /* intermediate buffer for reading */
    INT16 *sbuffer = NULL;      /* buffer for aubio */

    /* set up the WAVEFORMATEX structure. */
    wfx.nSamplesPerSec = 44100;     /* sample rate */
    wfx.wBitsPerSample = 16;        /* sample size */
    wfx.nChannels = 2;              /* channels*/
    wfx.cbSize = 0;                 /* size of _extra_ info */
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nBlockAlign = (wfx.wBitsPerSample * wfx.nChannels) >> 3;
    wfx.nAvgBytesPerSec = wfx.nBlockAlign * wfx.nSamplesPerSec;

    init(&sbuffer, &hFile, &hStdout, &csbiInfo);
    if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, (DWORD_PTR)waveOutProc, (DWORD_PTR)&waveFreeBlockCount, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
    {
        fprintf(stderr, "%s: unable to open wave mapper device\n", argv[0]);
        ExitProcess(1);
    }
    hop_s = BLOCK_SIZE / wfx.wBitsPerSample * 8 / wfx.nChannels;
    win_s = 16 * hop_s;
    atTempo = new_aubio_tempo("default", win_s, hop_s, wfx.nSamplesPerSec);
    tempo_in = new_fvec(hop_s);
    tempo_out = new_fvec(1);

    /* playback loop */
    while (1) {
        DWORD readBytes;
        if (!ReadFile(hFile, buffer, sizeof(buffer), &readBytes, NULL)) {
            fprintf(stderr, "Can't read data");
            return 3;
        }
        if (readBytes == 0) {
            fprintf(stdout, "End of file reached\n");
            break;
        }
        if (readBytes < sizeof(buffer)) {
            printf("at end of buffer\n");
            memset(buffer + readBytes, 0, sizeof(buffer) - readBytes);
            printf("after memcpy\n");
        }

        memcpy(sbuffer + waveCurrentBlock * 2048, buffer, sizeof(buffer));

        /* wait for a block to become free */
        while (!waveFreeBlockCount) {
            Sleep(10);
        }
        if (lastBlockPlayed >= 0 && lastBlockProcessed != lastBlockPlayed) {
            process_audio(sbuffer, wfx.nSamplesPerSec);
            lastBlockProcessed = lastBlockPlayed;
        }
        writeAudio(hWaveOut, buffer, sizeof(buffer));
    }
    /*
     * wait for all blocks to complete
     */
    while (waveFreeBlockCount < BLOCK_COUNT)
        Sleep(10);
    /*
     * unprepare any blocks that are still prepared
     */
    for (int i = 0; i < waveFreeBlockCount; i++)
        if (waveBlocks[i].dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(hWaveOut, &waveBlocks[i], sizeof(WAVEHDR));
    DeleteCriticalSection(&waveCriticalSection);
    freeBlocks(waveBlocks);
    waveOutClose(hWaveOut);
    CloseHandle(hFile);
    free(sbuffer);
    return 0;
}

