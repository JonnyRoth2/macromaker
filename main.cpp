// MacroMaker - hold or auto-click multiple keys / mouse buttons at once.
//
// Pick any combination of keyboard keys and mouse buttons with the
// checkboxes, then press F8 (a global hotkey, works even while your game
// is focused) or the Start button. In "Hold" mode they are all held down
// until you toggle off; in "Click" mode they are pressed and released
// repeatedly, with a configurable time between clicks and a configurable
// hold time per click. Press F8 / Stop again to stop.
//
// Build:  see build.bat (MSVC) or build_mingw.bat (MinGW), or README.md.

#ifndef UNICODE
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <windows.h>
#include <stdlib.h>
#include <vector>

// ---- input item table -------------------------------------------------------

enum ItemType { ITEM_KEY, ITEM_MOUSE };

enum MouseBtn { M_LEFT = 1, M_RIGHT, M_MIDDLE };

struct Item {
    const char* label;
    ItemType    type;
    int         code;   // virtual-key code for keys, MouseBtn for mouse
    HWND        box;    // checkbox handle (filled in at creation)
};

// The set of things you can hold. Add/remove rows here to taste.
static Item g_items[] = {
    { "W",        ITEM_KEY, 'W' },
    { "A",        ITEM_KEY, 'A' },
    { "S",        ITEM_KEY, 'S' },
    { "D",        ITEM_KEY, 'D' },
    { "Q",        ITEM_KEY, 'Q' },
    { "E",        ITEM_KEY, 'E' },
    { "R",        ITEM_KEY, 'R' },
    { "F",        ITEM_KEY, 'F' },
    { "G",        ITEM_KEY, 'G' },
    { "C",        ITEM_KEY, 'C' },
    { "V",        ITEM_KEY, 'V' },
    { "X",        ITEM_KEY, 'X' },
    { "Z",        ITEM_KEY, 'Z' },
    { "1",        ITEM_KEY, '1' },
    { "2",        ITEM_KEY, '2' },
    { "3",        ITEM_KEY, '3' },
    { "Space",    ITEM_KEY, VK_SPACE },
    { "L-Shift",  ITEM_KEY, VK_LSHIFT },
    { "L-Ctrl",   ITEM_KEY, VK_LCONTROL },
    { "L-Alt",    ITEM_KEY, VK_LMENU },
    { "Tab",      ITEM_KEY, VK_TAB },
    { "Up",       ITEM_KEY, VK_UP },
    { "Down",     ITEM_KEY, VK_DOWN },
    { "Left",     ITEM_KEY, VK_LEFT },
    { "Right",    ITEM_KEY, VK_RIGHT },
    { "Mouse L",  ITEM_MOUSE, M_LEFT },
    { "Mouse R",  ITEM_MOUSE, M_RIGHT },
    { "Mouse M",  ITEM_MOUSE, M_MIDDLE },
};
static const int ITEM_COUNT = (int)(sizeof(g_items) / sizeof(g_items[0]));

// ---- control IDs / layout ---------------------------------------------------

#define ID_CHECK_BASE 1000
#define ID_START      2000
#define ID_REPEAT     2001
#define ID_MODE_HOLD  2002
#define ID_MODE_CLICK 2003
#define ID_INTERVAL   2004
#define ID_HOLDMS     2005
#define HOTKEY_TOGGLE 1

// Timer ids
#define TIMER_REPRESS 1   // hold mode: re-assert keydown (optional)
#define TIMER_CLICK   2   // click mode: fires every interval, presses down
#define TIMER_RELEASE 3   // click mode: fires once per click, releases

#define COLS        4
#define COL_W       115
#define ROW_H       26
#define GRID_X      15
#define GRID_Y      36

static HWND g_startBtn     = NULL;
static HWND g_repeatBox    = NULL;
static HWND g_status       = NULL;
static HWND g_modeHold     = NULL;
static HWND g_modeClick    = NULL;
static HWND g_intervalEdit = NULL;  // ms between clicks
static HWND g_holdEdit     = NULL;  // ms each click is held down

static bool             g_holding = false;
static bool             g_clickMode = false;   // mode captured when started
static UINT             g_holdMs = 50;         // hold time captured when started
static std::vector<int> g_active;   // item indices currently held down

// ---- low-level input --------------------------------------------------------

static bool IsExtendedKey(int vk) {
    switch (vk) {
        case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
        case VK_PRIOR: case VK_NEXT: case VK_HOME: case VK_END:
        case VK_INSERT: case VK_DELETE: case VK_RCONTROL: case VK_RMENU:
        case VK_NUMLOCK: case VK_DIVIDE:
            return true;
        default:
            return false;
    }
}

static void SendKey(int vk, bool down) {
    INPUT in = {};
    in.type       = INPUT_KEYBOARD;
    in.ki.wScan   = (WORD)MapVirtualKey(vk, MAPVK_VK_TO_VSC);
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    if (IsExtendedKey(vk))
        in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    SendInput(1, &in, sizeof(INPUT));
}

static void SendMouse(int btn, bool down) {
    INPUT in = {};
    in.type = INPUT_MOUSE;
    switch (btn) {
        case M_LEFT:   in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN   : MOUSEEVENTF_LEFTUP;   break;
        case M_RIGHT:  in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN  : MOUSEEVENTF_RIGHTUP;  break;
        case M_MIDDLE: in.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
    }
    SendInput(1, &in, sizeof(INPUT));
}

static void PressItem(const Item& it, bool down) {
    if (it.type == ITEM_KEY) SendKey(it.code, down);
    else                     SendMouse(it.code, down);
}

// ---- start / stop -----------------------------------------------------------

static bool RepeatEnabled() {
    return SendMessage(g_repeatBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static bool ClickModeSelected() {
    return SendMessage(g_modeClick, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

// Read a millisecond value out of an edit box, clamped to [lo, hi].
static UINT ReadMs(HWND edit, UINT lo, UINT hi, UINT fallback) {
    char buf[16] = {};
    GetWindowTextA(edit, buf, sizeof(buf));
    int v = atoi(buf);
    if (v <= 0) v = (int)fallback;
    if (v < (int)lo) v = (int)lo;
    if (v > (int)hi) v = (int)hi;
    return (UINT)v;
}

static void PressActive(bool down) {
    for (size_t i = 0; i < g_active.size(); ++i)
        PressItem(g_items[g_active[i]], down);
}

static void StartHolding(HWND hwnd) {
    g_active.clear();
    for (int i = 0; i < ITEM_COUNT; ++i)
        if (SendMessage(g_items[i].box, BM_GETCHECK, 0, 0) == BST_CHECKED)
            g_active.push_back(i);
    if (g_active.empty()) {
        SetWindowTextA(g_status, "Nothing selected - tick at least one box.");
        return;
    }

    g_holding   = true;
    g_clickMode = ClickModeSelected();
    SetWindowTextA(g_startBtn, "Stop (F8)");

    if (g_clickMode) {
        UINT interval = ReadMs(g_intervalEdit, 20, 3600000, 1000);
        g_holdMs      = ReadMs(g_holdEdit, 10, interval - 10, 50);
        char msg[96];
        wsprintfA(msg, "CLICKING every %u ms (held %u ms) - press F8 to stop.",
                  interval, g_holdMs);
        SetWindowTextA(g_status, msg);
        PressActive(true);                              // first click right away
        SetTimer(hwnd, TIMER_RELEASE, g_holdMs, NULL);
        SetTimer(hwnd, TIMER_CLICK, interval, NULL);
    } else {
        PressActive(true);
        SetWindowTextA(g_status, "HOLDING - press F8 to release.");
        if (RepeatEnabled())
            SetTimer(hwnd, TIMER_REPRESS, 30, NULL);   // re-assert keydown for games that need it
    }
}

static void StopHolding(HWND hwnd) {
    KillTimer(hwnd, TIMER_REPRESS);
    KillTimer(hwnd, TIMER_CLICK);
    KillTimer(hwnd, TIMER_RELEASE);
    PressActive(false);   // releasing an already-released input is harmless
    g_active.clear();
    g_holding = false;
    SetWindowTextA(g_startBtn, "Start (F8)");
    SetWindowTextA(g_status,   "Idle. Pick keys/buttons, then press F8 or Start.");
}

static void Toggle(HWND hwnd) {
    if (g_holding) StopHolding(hwnd);
    else           StartHolding(hwnd);
}

// ---- window -----------------------------------------------------------------

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        CreateWindowA("STATIC",
            "Tick the keys / buttons to hold together, then press F8 or Start.",
            WS_CHILD | WS_VISIBLE,
            15, 10, 440, 18, hwnd, NULL, NULL, NULL);

        for (int i = 0; i < ITEM_COUNT; ++i) {
            int col = i % COLS, row = i / COLS;
            g_items[i].box = CreateWindowA("BUTTON", g_items[i].label,
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                GRID_X + col * COL_W, GRID_Y + row * ROW_H, COL_W - 8, 22,
                hwnd, (HMENU)(INT_PTR)(ID_CHECK_BASE + i), NULL, NULL);
            SendMessage(g_items[i].box, WM_SETFONT, (WPARAM)font, TRUE);
        }

        int rows = (ITEM_COUNT + COLS - 1) / COLS;
        int y = GRID_Y + rows * ROW_H + 8;

        // Mode row: (o) Hold   ( ) Click every [1000] ms, hold [50] ms
        g_modeHold = CreateWindowA("BUTTON", "Hold",
            WS_CHILD | WS_VISIBLE | WS_GROUP | BS_AUTORADIOBUTTON,
            15, y, 55, 20, hwnd, (HMENU)ID_MODE_HOLD, NULL, NULL);
        SendMessage(g_modeHold, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(g_modeHold, BM_SETCHECK, BST_CHECKED, 0);

        g_modeClick = CreateWindowA("BUTTON", "Click every",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            80, y, 85, 20, hwnd, (HMENU)ID_MODE_CLICK, NULL, NULL);
        SendMessage(g_modeClick, WM_SETFONT, (WPARAM)font, TRUE);

        g_intervalEdit = CreateWindowA("EDIT", "1000",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_DISABLED | ES_NUMBER,
            168, y - 1, 55, 22, hwnd, (HMENU)ID_INTERVAL, NULL, NULL);
        SendMessage(g_intervalEdit, WM_SETFONT, (WPARAM)font, TRUE);

        HWND lbl1 = CreateWindowA("STATIC", "ms, hold",
            WS_CHILD | WS_VISIBLE, 229, y + 2, 52, 18, hwnd, NULL, NULL, NULL);
        SendMessage(lbl1, WM_SETFONT, (WPARAM)font, TRUE);

        g_holdEdit = CreateWindowA("EDIT", "50",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_DISABLED | ES_NUMBER,
            284, y - 1, 45, 22, hwnd, (HMENU)ID_HOLDMS, NULL, NULL);
        SendMessage(g_holdEdit, WM_SETFONT, (WPARAM)font, TRUE);

        HWND lbl2 = CreateWindowA("STATIC", "ms",
            WS_CHILD | WS_VISIBLE, 335, y + 2, 25, 18, hwnd, NULL, NULL, NULL);
        SendMessage(lbl2, WM_SETFONT, (WPARAM)font, TRUE);

        y += 28;

        g_startBtn = CreateWindowA("BUTTON", "Start (F8)",
            WS_CHILD | WS_VISIBLE | WS_GROUP | BS_DEFPUSHBUTTON,
            15, y, 120, 30, hwnd, (HMENU)ID_START, NULL, NULL);
        SendMessage(g_startBtn, WM_SETFONT, (WPARAM)font, TRUE);

        g_repeatBox = CreateWindowA("BUTTON", "Re-press while held (some games)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            150, y + 6, 300, 20, hwnd, (HMENU)ID_REPEAT, NULL, NULL);
        SendMessage(g_repeatBox, WM_SETFONT, (WPARAM)font, TRUE);

        g_status = CreateWindowA("STATIC",
            "Idle. Pick keys/buttons, then press F8 or Start.",
            WS_CHILD | WS_VISIBLE,
            15, y + 40, 440, 18, hwnd, NULL, NULL, NULL);
        SendMessage(g_status, WM_SETFONT, (WPARAM)font, TRUE);

        if (!RegisterHotKey(hwnd, HOTKEY_TOGGLE, 0, VK_F8))
            SetWindowTextA(g_status, "Couldn't grab F8 hotkey - use the Start button.");
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == ID_START && HIWORD(wp) == BN_CLICKED)
            Toggle(hwnd);
        if ((LOWORD(wp) == ID_MODE_HOLD || LOWORD(wp) == ID_MODE_CLICK)
            && HIWORD(wp) == BN_CLICKED) {
            BOOL click = ClickModeSelected();
            EnableWindow(g_intervalEdit, click);
            EnableWindow(g_holdEdit, click);
            EnableWindow(g_repeatBox, !click);
        }
        return 0;

    case WM_HOTKEY:
        if (wp == HOTKEY_TOGGLE)
            Toggle(hwnd);
        return 0;

    case WM_TIMER:
        switch (wp) {
        case TIMER_REPRESS:
            // Re-assert keydown for active keyboard items (mouse stays down on its own).
            for (size_t i = 0; i < g_active.size(); ++i)
                if (g_items[g_active[i]].type == ITEM_KEY)
                    SendKey(g_items[g_active[i]].code, true);
            break;
        case TIMER_CLICK:
            // Start of a new click: press down, schedule the release.
            PressActive(true);
            SetTimer(hwnd, TIMER_RELEASE, g_holdMs, NULL);
            break;
        case TIMER_RELEASE:
            KillTimer(hwnd, TIMER_RELEASE);   // one-shot
            PressActive(false);
            break;
        }
        return 0;

    case WM_CLOSE:
        if (g_holding) StopHolding(hwnd);   // never leave keys stuck down
        UnregisterHotKey(hwnd, HOTKEY_TOGGLE);
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "MacroMakerWnd";
    RegisterClassA(&wc);

    int rows = (ITEM_COUNT + COLS - 1) / COLS;
    int h = GRID_Y + rows * ROW_H + 8 + 28 + 40 + 18 + 60;

    HWND hwnd = CreateWindowA("MacroMakerWnd", "MacroMaker",
        (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX),
        CW_USEDEFAULT, CW_USEDEFAULT, COLS * COL_W + 50, h,
        NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    return 0;
}
