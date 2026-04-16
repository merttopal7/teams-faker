// teams-faker.cpp : Defines the entry point for the application.
//

// Modern Windows Common Controls (Visual Styles) for Better GUI Quality
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib")


#include "framework.h"
#include "teams-faker.h"
#include <tlhelp32.h>
#include <string>
#include <shellapi.h>
#include <thread>
#include <chrono>
#include <commctrl.h> // For INITCOMMONCONTROLSEX

#define MAX_LOADSTRING 100
#define WM_TRAYICON (WM_USER + 1)
#define ID_TIMER 1
#define IDC_BTN_START 101
#define IDC_BTN_STOP  102
#define IDC_CMB_INTERVAL 103

// Global Variables:
HINSTANCE hInst;                                
WCHAR szTitle[MAX_LOADSTRING];                  
WCHAR szWindowClass[MAX_LOADSTRING];            
NOTIFYICONDATAW nid = {};
bool bIsRunning = false;
int g_IntervalMs = 20000; 
int g_TimeLeft = 0; 

HWND hBtnStart = nullptr;
HWND hBtnStop  = nullptr;
HWND hCmbInterval = nullptr;

// GUI Fonts
HFONT g_hFont = nullptr;
HFONT g_hFontBold = nullptr;

// Status logs for UI
WCHAR szLastFoundWindow[512] = L"None";
WCHAR szSequenceStatus[100] = L"Waiting for next run...";

struct SearchCtx {
    HWND foundHwnd = nullptr;
    WCHAR foundTitle[512] = { 0 };
};

BOOL CALLBACK EnumWindowsProcSearch(HWND hwnd, LPARAM lParam)
{
    // Skip owned windows
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return TRUE;

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return TRUE;

    wchar_t exePath[MAX_PATH] = { 0 };
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(hProc, 0, exePath, &size)) {
        std::wstring path(exePath);
        for (auto& c : path) c = towlower(c); 

        // Very loose check just to make sure it's a Teams process (teams.exe, ms-teams.exe, msteams.exe, etc)
        if (path.find(L"teams") != std::wstring::npos) {
            
            wchar_t title[512] = { 0 };
            GetWindowTextW(hwnd, title, 512);

            std::wstring t(title);
            if (!t.empty()) {
                for (auto& c : t) c = towlower(c);

                // Title contains 'teams' and it's not our own app
                if (t.find(L"teams") != std::wstring::npos && t.find(L"teams faker") == std::wstring::npos) {
                    
                    // Skip tiny tooltip/shadow windows
                    LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
                    if ((exStyle & WS_EX_TOOLWINDOW) == 0) {
                        
                        SearchCtx* ctx = reinterpret_cast<SearchCtx*>(lParam);
                        
                        // We definitely prefer a VISIBLE window.
                        if (IsWindowVisible(hwnd)) {
                            ctx->foundHwnd = hwnd;
                            wcscpy_s(ctx->foundTitle, title);
                            CloseHandle(hProc);
                            return FALSE; // Found best match, stop.
                        } 
                        // If it's invisible (e.g. Teams minimized to system tray), save it as a backup
                        // but continue searching in case there's a visible one.
                        else if (ctx->foundHwnd == nullptr) {
                            ctx->foundHwnd = hwnd;
                            wcscpy_s(ctx->foundTitle, title);
                        }
                    }
                }
            }
        }
    }
    CloseHandle(hProc);
    return TRUE;
}

HWND FindTeamsWindow(WCHAR* outTitle, int maxLen)
{
    SearchCtx ctx;
    EnumWindows(EnumWindowsProcSearch, (LPARAM)&ctx);
    if (ctx.foundHwnd && outTitle) {
        wcscpy_s(outTitle, maxLen, ctx.foundTitle);
    }
    return ctx.foundHwnd;
}

BOOL CALLBACK EnumChildProcSearch(HWND hwnd, LPARAM lParam) {
    HWND* pTarget = reinterpret_cast<HWND*>(lParam);
    wchar_t className[256];
    GetClassNameW(hwnd, className, 256);
    std::wstring cls(className);
    
    // Getting the core render component inside the window
    if (cls.find(L"Chrome_RenderWidgetHostHWND") != std::wstring::npos ||
        cls.find(L"WebView2") != std::wstring::npos ||
        cls.find(L"CefBrowserWindow") != std::wstring::npos ||
        cls.find(L"Win32WebViewHost") != std::wstring::npos) {
        *pTarget = hwnd;
        return FALSE; 
    }
    return TRUE;
}

HWND GetDeepTarget(HWND mainHwnd) {
    HWND target = nullptr;
    EnumChildWindows(mainHwnd, EnumChildProcSearch, (LPARAM)&target);
    return target ? target : mainHwnd;
}

void SendKeyAction(HWND mainHwnd, HWND targetHwnd, WPARAM vk) {
    LPARAM lpDown   = 1 | (MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16);
    LPARAM lpUp     = lpDown | (1 << 30) | (1u << 31);
    
    LPARAM ctrlDown = 1 | (MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC) << 16);
    LPARAM ctrlUp   = ctrlDown | (1 << 30) | (1u << 31);

    // Trick window activation
    SendMessage(mainHwnd, WM_ACTIVATE, WA_ACTIVE, 0);

    DWORD myThreadId = GetCurrentThreadId();
    DWORD targetThreadId = GetWindowThreadProcessId(targetHwnd, NULL);
    bool attached = AttachThreadInput(myThreadId, targetThreadId, TRUE);
    
    BYTE ks[256];
    if (attached) {
        GetKeyboardState(ks);
        ks[VK_CONTROL] |= 0x80;
        ks[VK_LCONTROL] |= 0x80;
        SetKeyboardState(ks);
    }

    // Sequence for Key
    PostMessage(targetHwnd, WM_KEYDOWN, VK_CONTROL, ctrlDown);
    PostMessage(targetHwnd, WM_KEYDOWN, vk, lpDown);
    PostMessage(targetHwnd, WM_KEYUP, vk, lpUp);
    PostMessage(targetHwnd, WM_KEYUP, VK_CONTROL, ctrlUp);

    if (attached) {
        Sleep(50); 
        ks[VK_CONTROL] &= ~0x80;
        ks[VK_LCONTROL] &= ~0x80;
        SetKeyboardState(ks);
        AttachThreadInput(myThreadId, targetThreadId, FALSE);
    }

    SendMessage(mainHwnd, WM_ACTIVATE, WA_INACTIVE, 0);
}

// Executed in a separate thread so Wait(2) does not block GUI
void ExecuteTeamsSequence(HWND hWndMainApp)
{
    HWND mainHwnd = FindTeamsWindow(szLastFoundWindow, 512);
    if (!mainHwnd) {
        wcscpy_s(szLastFoundWindow, 512, L"Not Found (Is Teams Running?)");
        wcscpy_s(szSequenceStatus, 100, L"Failed (Target Missing)");
        InvalidateRect(hWndMainApp, NULL, TRUE);
        return; 
    }

    // 1. Send Ctrl + 3
    wcscpy_s(szSequenceStatus, 100, L"Sent Ctrl + 3");
    InvalidateRect(hWndMainApp, NULL, TRUE);
    
    HWND targetHwnd = GetDeepTarget(mainHwnd);
    SendKeyAction(mainHwnd, targetHwnd, 0x33); // '3' is 0x33

    // 2. Wait 2 seconds
    wcscpy_s(szSequenceStatus, 100, L"Waiting 2 seconds...");
    InvalidateRect(hWndMainApp, NULL, TRUE);
    
    Sleep(2000);

    // 3. Send Ctrl + 2
    wcscpy_s(szSequenceStatus, 100, L"Sent Ctrl + 2");
    InvalidateRect(hWndMainApp, NULL, TRUE);
    
    SendKeyAction(mainHwnd, targetHwnd, 0x32); // '2' is 0x32

    // Hold status text briefly 
    Sleep(1000);
    wcscpy_s(szSequenceStatus, 100, L"Waiting for next run...");
    InvalidateRect(hWndMainApp, NULL, TRUE);
}

// Forward declarations:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Enforce Single Instance
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"TeamsFaker_UniqueInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Application is already running. Find it and bring it to the foreground.
        HWND hExistingWnd = FindWindowW(L"TeamsFakerClass", L"Teams Faker");
        if (hExistingWnd) {
            ShowWindow(hExistingWnd, SW_RESTORE);
            SetForegroundWindow(hExistingWnd);
        }
        // Exit this new instance immediately
        return 0;
    }

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_TEAMSFAKER, szWindowClass, MAX_LOADSTRING);
    
    wcscpy_s(szTitle, MAX_LOADSTRING, L"Teams Faker");
    wcscpy_s(szWindowClass, MAX_LOADSTRING, L"TeamsFakerClass");

    MyRegisterClass(hInstance);

    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_TEAMSFAKER));
    MSG msg;

    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TEAMSFAKER));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_TEAMSFAKER);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

void AddTrayIcon(HWND hWnd)
{
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hWnd;
    nid.uID = 1001;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_SMALL));
    wcscpy_s(nid.szTip, L"Teams Faker - Idle");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

void UpdateTrayTooltip(bool running)
{
    wcscpy_s(nid.szTip, running ? L"Teams Faker - Active" : L"Teams Faker - Idle");
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; 

   // Window size adjusted to accommodate the new visual texts
   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW ^ WS_THICKFRAME ^ WS_MAXIMIZEBOX,
      CW_USEDEFAULT, 0, 460, 290, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);
   AddTrayIcon(hWnd);

   return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        HMENU hMenu = GetMenu(hWnd);
        if (hMenu) {
            RemoveMenu(hMenu, 1, MF_BYPOSITION);
            
            MENUITEMINFOW mii = { sizeof(MENUITEMINFOW) };
            mii.fMask = MIIM_STRING;
            mii.dwTypeData = (LPWSTR)L"&File";
            SetMenuItemInfoW(hMenu, 0, TRUE, &mii);
            
            ModifyMenuW(hMenu, IDM_EXIT, MF_BYCOMMAND | MF_STRING, IDM_EXIT, L"E&xit");
            
            DrawMenuBar(hWnd);
        }

        // Load Common Controls for Modern Win32 Visual Styles
        INITCOMMONCONTROLSEX icex;
        icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&icex);

        // Load Modern Font (Segoe UI) instead of ugly system default
        HDC hdc = GetDC(hWnd);
        int nHeight = -MulDiv(10, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        g_hFont = CreateFontW(nHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_hFontBold = CreateFontW(nHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, 
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        ReleaseDC(hWnd, hdc);

        hBtnStart = CreateWindowW(L"BUTTON", L"Start Action", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            30, 20, 110, 35, hWnd, (HMENU)IDC_BTN_START, hInst, NULL);
        hBtnStop = CreateWindowW(L"BUTTON", L"Stop Action", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            150, 20, 110, 35, hWnd, (HMENU)IDC_BTN_STOP, hInst, NULL);

        hCmbInterval = CreateWindowW(L"COMBOBOX", NULL, CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE,
            275, 26, 110, 150, hWnd, (HMENU)IDC_CMB_INTERVAL, hInst, NULL);

        // Add options
        SendMessageW(hCmbInterval, CB_ADDSTRING, 0, (LPARAM)L"5 seconds");
        SendMessageW(hCmbInterval, CB_ADDSTRING, 0, (LPARAM)L"10 seconds");
        SendMessageW(hCmbInterval, CB_ADDSTRING, 0, (LPARAM)L"20 seconds");
        SendMessageW(hCmbInterval, CB_ADDSTRING, 0, (LPARAM)L"50 seconds");
        SendMessageW(hCmbInterval, CB_ADDSTRING, 0, (LPARAM)L"100 seconds");
        
        // Set default to 20 seconds (index 2)
        SendMessageW(hCmbInterval, CB_SETCURSEL, 2, 0); 

        // Apply Fonts
        SendMessageW(hBtnStart, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hBtnStop, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hCmbInterval, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        EnableWindow(hBtnStop, FALSE); 
    }
    break;

    case WM_TIMER:
        if (wParam == ID_TIMER) {
            
            if (g_TimeLeft > 0) {
                g_TimeLeft--;
                InvalidateRect(hWnd, NULL, TRUE); // Redraw UI countdown
            }
            
            if (g_TimeLeft <= 0) {
                g_TimeLeft = g_IntervalMs / 1000;
                // Launch the thread to do actions
                std::thread([hWnd]() { ExecuteTeamsSequence(hWnd); }).detach();
            }

            // Anti-Idle: Move mouse if no system-wide input for 60 seconds
            LASTINPUTINFO lii = { sizeof(LASTINPUTINFO) };
            if (GetLastInputInfo(&lii)) {
                DWORD dwIdleTimeMs = GetTickCount() - lii.dwTime;
                if (dwIdleTimeMs >= 60000) { 
                    // Perform subtle mouse jiggle (1 pixel and back)
                    INPUT inputs[2] = {};
                    inputs[0].type = INPUT_MOUSE;
                    inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE;
                    inputs[0].mi.dx = 1;
                    inputs[0].mi.dy = 1;

                    inputs[1].type = INPUT_MOUSE;
                    inputs[1].mi.dwFlags = MOUSEEVENTF_MOVE;
                    inputs[1].mi.dx = -1;
                    inputs[1].mi.dy = -1;

                    SendInput(2, inputs, sizeof(INPUT));
                }
            }
        }
        break;
        
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE || (wParam & 0xFFF0) == SC_CLOSE) {
            ShowWindow(hWnd, SW_HIDE);
            return 0; 
        }
        return DefWindowProc(hWnd, message, wParam, lParam);

    case WM_TRAYICON:
        // Show options menu on BOTH left click and right click
        if (lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            
            // 1. Top Option: Open GUI
            AppendMenuW(hMenu, MF_STRING, 4, L"Open Teams Faker");
            AppendMenuW(hMenu, MF_SEPARATOR, 5, NULL);
            
            // 2. Middle Option: Start/Stop toggle
            AppendMenuW(hMenu, MF_STRING, 1, bIsRunning ? L"Stop Action" : L"Start Action");
            AppendMenuW(hMenu, MF_SEPARATOR, 2, NULL);
            
            // 3. Bottom Option: Exit
            AppendMenuW(hMenu, MF_STRING, 3, L"Exit Application");

            // Make 'Open Teams Faker' bold (default)
            SetMenuDefaultItem(hMenu, 4, FALSE);
            
            // Foreground window prevents menu hanging when clicked outside
            SetForegroundWindow(hWnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, nullptr);
            
            if (cmd == 4) {
                // Open Teams Faker
                ShowWindow(hWnd, SW_RESTORE);
                SetForegroundWindow(hWnd);
            }
            else if (cmd == 1) {
                // Start / Stop
                SendMessage(hWnd, WM_COMMAND, bIsRunning ? IDC_BTN_STOP : IDC_BTN_START, 0);
            }
            else if (cmd == 3) {
                // Exit
                DestroyWindow(hWnd); 
            }
            DestroyMenu(hMenu);
        }
        break;

    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            switch (wmId)
            {
            case IDC_BTN_START:
                if (!bIsRunning) {
                    bIsRunning = true;

                    // Get Interval Selection
                    int sel = SendMessageW(hCmbInterval, CB_GETCURSEL, 0, 0);
                    int msIntervals[] = { 5000, 10000, 20000, 50000, 100000 };
                    if (sel >= 0 && sel < 5) g_IntervalMs = msIntervals[sel];
                    else g_IntervalMs = 20000; // fallback to 20s

                    EnableWindow(hBtnStart, FALSE);
                    EnableWindow(hBtnStop, TRUE);
                    EnableWindow(hCmbInterval, FALSE);

                    // Setup 1 second timer for countdowns
                    g_TimeLeft = g_IntervalMs / 1000;
                    SetTimer(hWnd, ID_TIMER, 1000, nullptr); 
                    UpdateTrayTooltip(true);
                    
                    // Do first execution instantly
                    std::thread([hWnd]() { ExecuteTeamsSequence(hWnd); }).detach();
                    InvalidateRect(hWnd, NULL, TRUE);
                }
                break;

            case IDC_BTN_STOP:
                if (bIsRunning) {
                    bIsRunning = false;
                    EnableWindow(hBtnStart, TRUE);
                    EnableWindow(hBtnStop, FALSE);
                    EnableWindow(hCmbInterval, TRUE);

                    KillTimer(hWnd, ID_TIMER);
                    UpdateTrayTooltip(false);
                    
                    wcscpy_s(szLastFoundWindow, 512, L"None");
                    wcscpy_s(szSequenceStatus, 100, L"Waiting for next run...");
                    InvalidateRect(hWnd, NULL, TRUE);
                }
                break;

            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd); 
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            
            SetBkMode(hdc, TRANSPARENT);
            HFONT hOldFont = (HFONT)SelectObject(hdc, g_hFontBold);

            if (bIsRunning) {
                SetTextColor(hdc, RGB(0, 130, 0)); // Green
                WCHAR buf[100];
                swprintf_s(buf, 100, L"Status: ACTIVE (Running every %d sec)", g_IntervalMs / 1000);
                TextOutW(hdc, 30, 75, buf, lstrlenW(buf));

                SelectObject(hdc, g_hFont); // Normal font for countdown
                SetTextColor(hdc, RGB(0, 50, 150)); // Dark Blue
                swprintf_s(buf, 100, L"Next run in: %d seconds", g_TimeLeft);
                TextOutW(hdc, 30, 95, buf, lstrlenW(buf));

                SelectObject(hdc, g_hFontBold); // Bold for action feedback
                SetTextColor(hdc, RGB(220, 100, 0)); // Orange
                swprintf_s(buf, 100, L"Action: %s", szSequenceStatus);
                TextOutW(hdc, 30, 115, buf, lstrlenW(buf));

            } else {
                SetTextColor(hdc, RGB(200, 0, 0)); // Red
                LPCWSTR textStatus = L"Status: IDLE (Task Stopped)";
                TextOutW(hdc, 30, 75, textStatus, lstrlenW(textStatus));
            }

            SelectObject(hdc, g_hFont);
            SetTextColor(hdc, RGB(100, 100, 100)); // Grey
            LPCWSTR textTargetInfo = L"Target Window Details:";
            TextOutW(hdc, 30, 155, textTargetInfo, lstrlenW(textTargetInfo));

            SelectObject(hdc, g_hFontBold);
            SetTextColor(hdc, RGB(30, 30, 30)); // Dark
            RECT rectFoundName = { 30, 175, 410, 240 };
            DrawTextW(hdc, szLastFoundWindow, -1, &rectFoundName, DT_WORDBREAK | DT_LEFT);

            SelectObject(hdc, hOldFont);
            EndPaint(hWnd, &ps);
        }
        break;

    case WM_DESTROY:
        KillTimer(hWnd, ID_TIMER);
        Shell_NotifyIconW(NIM_DELETE, &nid);
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hFontBold) DeleteObject(g_hFontBold);
        PostQuitMessage(0);
        break;
        
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
