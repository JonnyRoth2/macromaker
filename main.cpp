// MacroMaker - hold multiple keys / mouse buttons at once.
//
// Pick any combination of keyboard keys and mouse buttons with the
// checkboxes, then press F8 (a global hotkey, works even while your game
// is focused) or the Start button to hold them all down at the same time.
// Press F8 / Stop again to release.
//
// Build:  see build.bat (MSVC) or build_mingw.bat (MinGW), or README.md.

#ifndef UNICODE
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <windows.h>
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
#define HOTKEY_TOGGLE 1

#define COLS        4
#define COL_W       115
#define ROW_H       26
#define GRID_X      15
#define GRID_Y      36

static HWND g_startBtn  = NULL;
static HWND g_repeatBox = NULL;
static HWND g_status    = NULL;

static bool             g_holding = false;
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

static void StartHolding(HWND hwnd) {
    g_active.clear();
    for (int i = 0; i < ITEM_COUNT; ++i) {
        if (SendMessage(g_items[i].box, BM_GETCHECK, 0, 0) == BST_CHECKED) {
            PressItem(g_items[i], true);
            g_active.push_back(i);
        }
    }
    if (g_active.empty()) {
        SetWindowTextA(g_status, "Nothing selected - tick at least one box.");
        return;
    }
    g_holding = true;
    SetWindowTextA(g_startBtn, "Stop (F8)");
    SetWindowTextA(g_status,   "HOLDING - press F8 to release.");
    if (RepeatEnabled())
        SetTimer(hwnd, 1, 30, NULL);   // re-assert keydown for games that need it
}

static void StopHolding(HWND hwnd) {
    KillTimer(hwnd, 1);
    for (size_t i = 0; i < g_active.size(); ++i)
        PressItem(g_items[g_active[i]], false);
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

        g_startBtn = CreateWindowA("BUTTON", "Start (F8)",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
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
        return 0;

    case WM_HOTKEY:
        if (wp == HOTKEY_TOGGLE)
            Toggle(hwnd);
        return 0;

    case WM_TIMER:
        // Re-assert keydown for active keyboard items (mouse stays down on its own).
        for (size_t i = 0; i < g_active.size(); ++i)
            if (g_items[g_active[i]].type == ITEM_KEY)
                SendKey(g_items[g_active[i]].code, true);
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
    int h = GRID_Y + rows * ROW_H + 8 + 40 + 18 + 60;

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
