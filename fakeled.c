#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fakeled.h"

//#define CSV_OUTPUT
#define WIN_OUTPUT

#ifdef CSV_OUTPUT
FILE* _fake_led_output;
char* line;
#endif // CSV_OUTPUT

#ifdef WIN_OUTPUT
LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
const wchar_t CLASS_NAME[] = L"LED output";
HWND window;
int* bgr_leds;
int n_leds;
int n_cols;
const int led_height = 32;
const int led_width = 32;
const int led_row_space = 32;
const int led_col_space = 32;
const int max_win_width = 1920;
int window_init = 0;
#endif // WIN_OUTPUT

ws2811_return_t ws2811_init(ws2811_t *ws2811)
{
    ws2811_channel_t *channel = &ws2811->channel[0];
    channel->leds = malloc(sizeof(ws2811_led_t) * channel->count);
    
#ifdef CSV_OUTPUT
    line = malloc(sizeof(char) * 12 * channel->count);
    _fake_led_output = fopen("led_output.csv", "w");
#endif // CSV_OUTPUT

#ifdef WIN_OUTPUT
    n_leds = channel->count;
    bgr_leds = malloc(sizeof(int) * n_leds);
    // n * led_width + (n-1) * led_col_space = max_win_width
    // n * (lw + lcs) - lcs = mww
    // n = (mww + lcs) / (lw + lcs)
    n_cols = 25; // min(n_leds, (max_win_width + led_col_space) / (led_width + led_col_space));
    int window_width = n_cols * led_width + (n_cols - 1) * led_col_space;
    int window_height = (n_leds / n_cols + ((n_leds % n_cols) > 0)) * (led_height + led_row_space) - led_row_space;
    //RECT rect;
    //SetRect(&rect, 0, 32, 0 + window_width, 32 + window_height);
    //AdjustWindowRectEx(&rect, WS_CHILD, 0, 0); -- this does not work, I don't know why and I am just going to hardcode the adjustments
    HINSTANCE hInstance = GetModuleHandle(0);
    WNDCLASS wc = { 0 };
    wc.hInstance = hInstance;
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);
    window = CreateWindowEx(0,
        CLASS_NAME,
        L"Fake LED output",
        WS_OVERLAPPEDWINDOW,
        10, 32, window_width + 16, window_height + 39,  //x,y,w,h
        NULL, // if you make this hwndConsole, then the console becomes this window's parent. Then, this window wouldn't get an entry in the taskbar
        NULL,
        hInstance, 
        NULL);
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
#endif // WIN_OUTPUT

    return WS2811_SUCCESS;
}

void ws2811_fini(ws2811_t *ws2811) 
{
    free(ws2811->channel[0].leds);
#ifdef CSV_OUTPUT
    free(line);
    fclose(_fake_led_output);
#endif // CSV_OUTPUT

#ifdef WIN_OUTPUT
    free(bgr_leds);
#endif // WIN_OUTPUT
}

ws2811_return_t ws2811_render(ws2811_t *ws2811)
{
#ifdef CSV_OUTPUT
    strcpy(line, "");
    ws2811_channel_t *channel = &ws2811->channel[0];
    for(int i = 0; i < channel->count; ++i)
    {
        char s[12];
        sprintf(s, "%x,", channel->leds[i]);
        strcat(line, s);
    }
    strcat(line, "\n");
    fputs(line, _fake_led_output);
#endif // CSV_OUTPUT

#ifdef WIN_OUTPUT
    ws2811_channel_t* channel = &ws2811->channel[0];
    for (int i = 0; i < channel->count; ++i)
    {
        int rgb = channel->leds[i];
        int b = 0xFF & rgb;
        int g = 0xFF00 & rgb;
        int r = 0xFF0000 & rgb;
        bgr_leds[i] = (r >> 16) | g | (b << 16);
    }
    InvalidateRect(window, NULL, 1);
    MSG msg;
    while (PeekMessage(&msg, window, 0, 0, 1))
    {
        //GetMessage(&msg, window, 0, 0)
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
#endif // WIN_OUTPUT

    return WS2811_SUCCESS;
}

const char * ws2811_get_return_t_str(const ws2811_return_t state)
{
    (void)state;
    return "x";
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_CREATE:
        ///cout << "WINDOW SPEAKING TO YOU THROUGH THE CONSOLE." << endl;
        //cout << "I'm alive!" << endl << endl;
        Beep(40, 40);
        return 0;
        break;

    case WM_PAINT:  // paint event
    {
        //cout << "PAINT EVENT!  time to repaint!!" << endl;
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (!window_init)
        {
            FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_BACKGROUND + 1));
            window_init = 1;
        }
        //RECT rect;
        for (int i = 0; i < n_leds; ++i)
        {
            int col = i % n_cols;
            int x = col * (led_width + led_col_space);
            int row = i / n_cols;
            int y = row * (led_height + led_row_space);
            //SetRect(&rect, x, y, x + led_width, y + led_height);
            HBRUSH brush = CreateSolidBrush(bgr_leds[i]);
            SelectObject(hdc, brush);
            Ellipse(hdc, x, y, x + led_width, y + led_height);
            //FillRect(hdc, &rect, brush);
            DeleteObject(brush);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    break;

    case WM_LBUTTONDOWN:    // left clicking on the client area of the window (white part)
        //cout << "WHY R YA MOUSE DOWN'IN ME AT x=" << LOWORD(lparam) << " y=" << HIWORD(lparam) << " SON" << endl;
        return 0;
        break;

    case WM_NCLBUTTONDOWN:  // NONCLIENT area leftbutton down (click on "title bar" part of window)
        //cout << "AAAH!! YER GONNA MOVE ME SON.  CAREFUL SON." << endl;
        //return 0;     // this is an interesting one.
        // try UNCOMMENTING the return 0; statement here.
        // Notice that you can NO LONGER move or manipulate
        // the window by clicking on its "title bar"
        // if you return 0; from here.  The reason for that
        // is the window movement is actually handled by
        // DefWindowProc().  That's why its so important
        // to remember to pass events you don't handle to
        // DefWindowProc() -- if you don't then the Window
        // won't act in the "default" way you're so used to
        // other windows acting in (in fact, it won't even
        // show up properly)
        break;

    case WM_CHAR:   // character key
        //cout << "WHY R U CHARRING ME WITH " << (char)wparam << " FOR SON" << endl;
        return 0;
        break;

    case WM_MOVE:   // moving the window
        //cout << "WHY R U MOVIN' ME TO x=" << LOWORD(lparam) << " y=" << HIWORD(lparam) << " FOR SON" << endl;
        return 0;
        break;

    case WM_SIZE:
        //cout << "WHY R YA SIZIN' ME TO SIZE width=" << LOWORD(lparam) << " height=" << HIWORD(lparam) << " FOR SON" << endl;
        return 0;
        break;

    case WM_DESTROY:    // killing the window
        //cout << "NOOO!!  I . . . shall . . . return !!" << endl;
        PostQuitMessage(0);
        return 0;
        break;
    }

    return DefWindowProc(hwnd, message, wparam, lparam);
}