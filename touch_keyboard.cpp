// touch_keyboard.cpp - Touch Keyboard (Pure Win32 C++)
// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <imm.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "Imm32.lib")

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

int g_ww = 980, g_wh = 320;
int g_headerH = 36;
int g_keyGap = 4;
int g_keyHeight = 46;

#define KEY_AREA_X   6
#define KEY_AREA_W   (g_ww - 12)

#define TIMER_FOCUS     8820
#define TIMER_EXIT      8822
#define TIMER_SLIDE     8823
#define TIMER_LONGPRESS 8825
#define TIMER_REPEAT    8826
#define WM_TRAY         (WM_APP + 100)
#define WM_FOCUS_EVENT  (WM_APP + 101)

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#define ID_MENU_TOGGLE 10001
#define ID_MENU_AUTO   10002
#define ID_MENU_MODE   10003
#define ID_MENU_THEME  10004
#define ID_MENU_ABOUT  10008
#define ID_MENU_EXIT   10009

// ========== Theme System ==========
struct ThemeColors {
    DWORD bg;
    DWORD hdr;
    DWORD key;
    DWORD keyBorder;
    DWORD dark;
    DWORD hover;
    DWORD hot;
    DWORD text;
    DWORD dim;
    DWORD subtext;
    DWORD bubbleBg;
    DWORD bubbleItem;
    DWORD bubbleBorder;
};

// Win11 Dark Theme (BGR format for GDI)
static const ThemeColors g_darkTheme = {
    0x1F1F1F,  // bg
    0x181818,  // hdr
    0x2C2C2C,  // key
    0x3A3A3A,  // keyBorder
    0x242424,  // dark
    0x383838,  // hover
    0xD47800,  // hot (Win11 accent blue #0078D4 in BGR)
    0xF5F5F5,  // text
    0xA0A0A0,  // dim
    0xFFB040,  // subtext (cyan-blue #40B0FF in BGR)
    0x2B2B2B,  // bubbleBg
    0x333333,  // bubbleItem
    0x555555   // bubbleBorder
};

// Win11 Light Theme (BGR format for GDI)
static const ThemeColors g_lightTheme = {
    0xF3F3F3,  // bg
    0xEAEAEA,  // hdr
    0xFFFFFF,  // key
    0xD6D6D6,  // keyBorder
    0xE8E8E8,  // dark
    0xF0F0F0,  // hover
    0xD47800,  // hot (Win11 accent blue #0078D4 in BGR)
    0x1A1A1A,  // text
    0x666666,  // dim
    0xCC7700,  // subtext (darker cyan for light bg)
    0xF5F5F5,  // bubbleBg
    0xFFFFFF,  // bubbleItem
    0xCCCCCC   // bubbleBorder
};

// Theme mode: 0 = follow system, 1 = force dark, 2 = force light
static int g_themeMode = 0;
static const ThemeColors* g_theme = &g_darkTheme;

static BOOL IsSystemDarkMode() {
    HKEY hKey;
    DWORD val = 1, sz = sizeof(val);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&val, &sz);
        RegCloseKey(hKey);
    }
    return (val == 0);
}

static void ApplyTheme() {
    if (g_themeMode == 1) {
        g_theme = &g_darkTheme;
    } else if (g_themeMode == 2) {
        g_theme = &g_lightTheme;
    } else {
        g_theme = IsSystemDarkMode() ? &g_darkTheme : &g_lightTheme;
    }
}

// Convenience macros to access current theme colors
#define C_BG           (g_theme->bg)
#define C_HDR          (g_theme->hdr)
#define C_KEY          (g_theme->key)
#define C_KEY_BORDER   (g_theme->keyBorder)
#define C_DARK         (g_theme->dark)
#define C_HOVER        (g_theme->hover)
#define C_HOT          (g_theme->hot)
#define C_WHITE        (g_theme->text)
#define C_DIM          (g_theme->dim)
#define C_SUBTEXT      (g_theme->subtext)
#define C_BUBBLE_BG    (g_theme->bubbleBg)
#define C_BUBBLE_ITEM  (g_theme->bubbleItem)
#define C_BUBBLE_BORDER (g_theme->bubbleBorder)

enum KeyType {
    K_NORMAL, K_LETTER, K_MOD, K_CAPS, K_IME, K_MODE123,
    K_SPECIAL, K_ARROW, K_SPACE, K_HIDE, K_DOCK, K_MIN, K_CLOSE, K_9KEY,
    K_LANG, K_EMOJI
};

struct KeyDef { int x, y, w, h; short vk; KeyType type; };

struct PopupBubble {
    BOOL active;
    int keyIdx;
    int selIdx;
    int cx, cy;
    wchar_t chars[5];
    int count;
};

// C++ 函数前置声明
static void ShowKB(BOOL show, BOOL isManual = FALSE);
static void ToggleKB();
static void SwitchMode();
static void PromptCloseAction(HWND hWnd);
static void RecreateFontsAndLayout();
static double GetSystemDpiScale();
static void InitWindowSizeForDpi();
static void SendKey(BYTE vk, BOOL sh, BOOL ct, BOOL al);

// Global state
HINSTANCE   g_hInst = 0;
HWND        g_hWnd = 0;
HICON       g_hTrayIcon = 0;
BOOL        g_vis = FALSE;
BOOL        g_manualShow = FALSE;
BOOL        g_sh = FALSE, g_ct = FALSE, g_al = FALSE, g_cp = FALSE;
BOOL        g_sym = FALSE, g_af = TRUE;
BOOL        g_shortPress = FALSE;
DWORD       g_lht = 0;
int         g_hk = -1, g_pk = -1;
int         g_repeatKeyIdx = -1;
BOOL        g_tracking = FALSE;
BOOL        g_tray = FALSE;
int         g_slideFrom = 0, g_slideTo = 0, g_slideStep = -1;
HWINEVENTHOOK g_winHook = 0;
HANDLE      g_mutex = 0;
#define SLIDE_STEPS 8
#define SLIDE_MS 12
HFONT       g_f12 = 0, g_f13b = 0, g_f14 = 0, g_f14b = 0, g_f16b = 0, g_f18b = 0;
NOTIFYICONDATAA g_nid;

// 9-key state
BOOL        g_9key = FALSE;
PopupBubble g_bubble = {0};

// IME 中英文状态：FALSE=英文(英), TRUE=中文(简体)
BOOL        g_langCN = FALSE;

#define MAX_KEYS 120
KeyDef g_keys[MAX_KEYS];
int g_nk = 0;

static const wchar_t* g_symText[26] = {
    L"1",L"2",L"3",L"4",L"5",L"6",L"7",L"8",L"9",L"0",
    L"@",L"#",L"$",L"%",L"&",L"-",L"+",L"(",L")",
    L"*",L"\"",L":",L";",L"!",L"?",L"~"
};

static const wchar_t* g_9keyChars[10] = {
    L"0+-_", L"1.,?!", L"abc2", L"def3", L"ghi4", L"jkl5", L"mno6", L"pqrs7", L"tuv8", L"wxyz9"
};

static double GetSystemDpiScale() {
    HDC hdc = GetDC(NULL);
    int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);
    if (dpiY < 96) dpiY = 96;
    return (double)dpiY / 96.0;
}

static void InitWindowSizeForDpi() {
    double dpiScale = GetSystemDpiScale();
    if (g_9key) {
        g_ww = (int)(420.0 * dpiScale);
        g_wh = (int)(360.0 * dpiScale);
    } else {
        g_ww = (int)(980.0 * dpiScale);
        g_wh = (int)(320.0 * dpiScale);
    }
}

static int AddKey(int x, int y, int w, int h, short vk, KeyType type) {
    if (g_nk >= MAX_KEYS) return g_nk;
    KeyDef* k = &g_keys[g_nk++];
    k->x = x; k->y = y; k->w = w; k->h = h; k->vk = vk; k->type = type;
    return g_nk;
}

static void Build9Keys() {
    g_nk = 0;
    double dpiScale = GetSystemDpiScale();
    double baseH = 360.0 * dpiScale;
    double baseW = 420.0 * dpiScale;

    double scaleY = (double)g_wh / baseH;
    double scaleX = (double)g_ww / baseW;

    g_headerH = (int)(36.0 * dpiScale * scaleY); if (g_headerH < 28) g_headerH = 28;
    g_keyGap = (int)(4.0 * dpiScale * scaleX); if (g_keyGap < 2) g_keyGap = 2;

    int topPad = (int)(18.0 * dpiScale * scaleY);
    int y = g_headerH + topPad;
    int cols = 4;
    int cw = (KEY_AREA_W - (cols - 1) * g_keyGap) / cols;
    int rem = KEY_AREA_W - (cols - 1) * g_keyGap - cw * cols;
    int kh = (g_wh - g_headerH - topPad - 6 - 4 * g_keyGap) / 4;
    if (kh < 25) kh = 25;

    int w[4];
    for (int i = 0; i < cols; i++) w[i] = cw + (i < rem ? 1 : 0);

    // Row 0: 1, 2, 3, Backspace
    short v0[4] = {0x31, 0x32, 0x33, 0x08};
    KeyType t0[4] = {K_9KEY, K_9KEY, K_9KEY, K_SPECIAL};
    int x = KEY_AREA_X;
    for (int i = 0; i < 4; i++) { AddKey(x, y, w[i], kh, v0[i], t0[i]); x += w[i] + g_keyGap; }
    y += kh + g_keyGap;

    // Row 1: 4, 5, 6, Enter
    short v1[4] = {0x34, 0x35, 0x36, 0x0D};
    KeyType t1[4] = {K_9KEY, K_9KEY, K_9KEY, K_SPECIAL};
    x = KEY_AREA_X;
    for (int i = 0; i < 4; i++) { AddKey(x, y, w[i], kh, v1[i], t1[i]); x += w[i] + g_keyGap; }
    y += kh + g_keyGap;

    // Row 2: 7, 8, 9, Tab
    short v2[4] = {0x37, 0x38, 0x39, 0x09};
    KeyType t2[4] = {K_9KEY, K_9KEY, K_9KEY, K_SPECIAL};
    x = KEY_AREA_X;
    for (int i = 0; i < 4; i++) { AddKey(x, y, w[i], kh, v2[i], t2[i]); x += w[i] + g_keyGap; }
    y += kh + g_keyGap;

    // Row 3: &123, 0, 空格, Shift
    short v3[4] = {0, 0x30, 0x20, 0x10};
    KeyType t3[4] = {K_MODE123, K_9KEY, K_SPACE, K_MOD};
    x = KEY_AREA_X;
    for (int i = 0; i < 4; i++) { AddKey(x, y, w[i], kh, v3[i], t3[i]); x += w[i] + g_keyGap; }
}

// ========== IME 中英文状态检测与切换 ==========
// 通过 ImmGetConversionStatus 读取前台窗口的输入法转换模式
static BOOL DetectImeChinese() {
    HWND fg = GetForegroundWindow();
    if (!fg) return g_langCN;
    DWORD tid = GetWindowThreadProcessId(fg, NULL);
    HIMC himc = ImmGetContext(fg);
    if (!himc) return g_langCN;
    DWORD conv = 0, sent = 0;
    BOOL ok = ImmGetConversionStatus(himc, &conv, &sent);
    ImmReleaseContext(fg, himc);
    (void)tid;
    if (!ok) return g_langCN;
    // 中文输入法处于“ native/中文”转换模式即视为中文状态
    return (conv & IME_CMODE_NATIVE) != 0;
}

// 切换中英文：发送右 Shift 脉冲（微软拼音/搜狗默认中英切换键）
static void ToggleImeLang() {
    SendKey(VK_RSHIFT, FALSE, FALSE, FALSE);
    Sleep(60);
    g_langCN = DetectImeChinese();
    if (g_hWnd) InvalidateRect(g_hWnd, 0, TRUE);
}

// 打开 Win10/11 表情面板 (Win + .)
static void OpenEmojiPanel() {
    INPUT inputs[4] = {};
    inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_LWIN;
    inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = VK_OEM_PERIOD;
    inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = VK_OEM_PERIOD; inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = VK_LWIN; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, inputs, sizeof(INPUT));
}

static void BuildKeys() {
    if (g_9key) { Build9Keys(); return; }
    g_nk = 0;
    
    double dpiScale = GetSystemDpiScale();
    double baseW = 980.0 * dpiScale;
    double baseH = 320.0 * dpiScale;

    double scaleX = (double)g_ww / baseW;
    double scaleY = (double)g_wh / baseH;

    g_headerH = (int)(36.0 * dpiScale * scaleY); if (g_headerH < 28) g_headerH = 28;
    g_keyGap = (int)(4.0 * dpiScale * scaleX); if (g_keyGap < 2) g_keyGap = 2;
    g_keyHeight = (g_wh - g_headerH - 8 - 5 * g_keyGap) / 5;
    if (g_keyHeight < 20) g_keyHeight = 20;

    int y = g_headerH + g_keyGap + 2;

    // ===== Win10 屏幕键盘风格布局 =====
    // Row 0: Esc, `, 1-0, -, =, Backspace  (15 keys)
    {
        int wEsc = (int)(50 * dpiScale * scaleX);
        int wBksp = (int)(68 * dpiScale * scaleX);
        int fixed = wEsc + wBksp;
        int aw = (KEY_AREA_W - fixed - 14 * g_keyGap) / 13;
        int rem = KEY_AREA_W - fixed - 14 * g_keyGap - aw * 13;
        int w[15]; w[0] = wEsc;
        for (int i = 1; i <= 13; i++) w[i] = aw + (i <= rem ? 1 : 0);
        w[14] = wBksp;
        short v[15] = {0x1B,0xC0,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x30,0xBD,0xBB,0x08};
        KeyType t[15] = {K_SPECIAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_SPECIAL};
        int x = KEY_AREA_X;
        for (int i = 0; i < 15; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
    }
    y += g_keyHeight + g_keyGap;

    // Row 1: Tab, q-p, [, ], \, Del  (15 keys)
    {
        int wTab = (int)(68 * dpiScale * scaleX);
        int wDel = (int)(68 * dpiScale * scaleX);
        int fixed = wTab + wDel;
        int aw = (KEY_AREA_W - fixed - 14 * g_keyGap) / 13;
        int rem = KEY_AREA_W - fixed - 14 * g_keyGap - aw * 13;
        int w[15]; w[0] = wTab;
        for (int i = 1; i <= 13; i++) w[i] = aw + (i <= rem ? 1 : 0);
        w[14] = wDel;
        short v[15] = {0x09,0x51,0x57,0x45,0x52,0x54,0x59,0x55,0x49,0x4F,0x50,0xDB,0xDD,0xDC,0x2E};
        KeyType t[15] = {K_SPECIAL,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_NORMAL,K_NORMAL,K_NORMAL,K_SPECIAL};
        int x = KEY_AREA_X;
        for (int i = 0; i < 15; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
    }
    y += g_keyHeight + g_keyGap;

    // Row 2: Caps, a-l, ;, ', Enter  (13 keys)
    {
        int wCaps = (int)(80 * dpiScale * scaleX);
        int wEnter = (int)(90 * dpiScale * scaleX);
        int fixed = wCaps + wEnter;
        int aw = (KEY_AREA_W - fixed - 12 * g_keyGap) / 11;
        int rem = KEY_AREA_W - fixed - 12 * g_keyGap - aw * 11;
        int w[13]; w[0] = wCaps;
        for (int i = 1; i <= 11; i++) w[i] = aw + (i <= rem ? 1 : 0);
        w[12] = wEnter;
        short v[13] = {0x14,0x41,0x53,0x44,0x46,0x47,0x48,0x4A,0x4B,0x4C,0xBA,0xDE,0x0D};
        KeyType t[13] = {K_CAPS,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_NORMAL,K_NORMAL,K_SPECIAL};
        int x = KEY_AREA_X;
        for (int i = 0; i < 13; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
    }
    y += g_keyHeight + g_keyGap;

    // Row 3: LShift, z-m, ,, ., /, ↑, RShift  (13 keys)
    {
        int wLSh = (int)(95 * dpiScale * scaleX);
        int wRSh = (int)(70 * dpiScale * scaleX);
        int wUp = (int)(52 * dpiScale * scaleX);
        int fixed = wLSh + wRSh + wUp;
        int aw = (KEY_AREA_W - fixed - 12 * g_keyGap) / 10;
        int rem = KEY_AREA_W - fixed - 12 * g_keyGap - aw * 10;
        int w[13]; w[0] = wLSh;
        for (int i = 1; i <= 10; i++) w[i] = aw + (i <= rem ? 1 : 0);
        w[11] = wUp; w[12] = wRSh;
        short v[13] = {0xA0,0x5A,0x58,0x43,0x56,0x42,0x4E,0x4D,0xBC,0xBE,0xBF,0x26,0xA1};
        KeyType t[13] = {K_MOD,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_NORMAL,K_NORMAL,K_NORMAL,K_ARROW,K_MOD};
        int x = KEY_AREA_X;
        for (int i = 0; i < 13; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
    }
    y += g_keyHeight + g_keyGap;

    // Row 4: Fn, Ctrl, Win, Alt, 空格, Alt, Ctrl, 英/简体, ←, ↓, →  (11 keys)
    {
        int wFn  = (int)(50 * dpiScale * scaleX);
        int wCtl = (int)(58 * dpiScale * scaleX);
        int wWin = (int)(50 * dpiScale * scaleX);
        int wAlt = (int)(58 * dpiScale * scaleX);
        int wLang = (int)(68 * dpiScale * scaleX);
        int wArw = (int)(52 * dpiScale * scaleX);
        int fixed = wFn + wCtl + wWin + wAlt + wAlt + wCtl + wLang + wArw * 3;
        int spaceW = KEY_AREA_W - fixed - 10 * g_keyGap;
        if (spaceW < 60) spaceW = 60;
        int w[11] = {wFn, wCtl, wWin, wAlt, spaceW, wAlt, wCtl, wLang, wArw, wArw, wArw};
        short v[11] = {0, 0x11, 0x5B, 0x12, 0x20, 0x12, 0x11, 0, 0x25, 0x28, 0x27};
        KeyType t[11] = {K_SPECIAL, K_MOD, K_SPECIAL, K_MOD, K_SPACE, K_MOD, K_MOD, K_LANG, K_ARROW, K_ARROW, K_ARROW};
        int x = KEY_AREA_X;
        for (int i = 0; i < 11; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
    }
}

static void SwitchMode() {
    g_9key = !g_9key;
    if (!g_9key) g_sym = FALSE;  // 全键盘无 &123 符号层，复位符号模式
    InitWindowSizeForDpi();
    RECT work = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int sx = work.left + ((work.right - work.left) - g_ww) / 2;
    int sy = work.bottom - g_wh - 6;
    SetWindowPos(g_hWnd, HWND_TOPMOST, sx, sy, g_ww, g_wh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    RecreateFontsAndLayout();
    InvalidateRect(g_hWnd, 0, TRUE);
}

static HFONT MakeFont(int size, BOOL bold) {
    HDC hdc = GetDC(0);
    int h = -MulDiv(size, 96, 72);
    ReleaseDC(0, hdc);
    return CreateFontW(h, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
}

static void RecreateFontsAndLayout() {
    if (g_f12) DeleteObject(g_f12);
    if (g_f13b) DeleteObject(g_f13b);
    if (g_f14) DeleteObject(g_f14);
    if (g_f14b) DeleteObject(g_f14b);
    if (g_f16b) DeleteObject(g_f16b);
    if (g_f18b) DeleteObject(g_f18b);

    double dpiScale = GetSystemDpiScale();
    double baseH = g_9key ? (360.0 * dpiScale) : (320.0 * dpiScale);
    double scaleY = (double)g_wh / baseH;
    if (scaleY < 0.4) scaleY = 0.4;

    double finalFontScale = dpiScale * scaleY;

    g_f12  = MakeFont((int)(12 * finalFontScale), 0);
    g_f13b = MakeFont((int)(13 * finalFontScale), 1);
    g_f14  = MakeFont((int)(14 * finalFontScale), 0);
    g_f14b = MakeFont((int)(14 * finalFontScale), 1);
    g_f16b = MakeFont((int)(16 * finalFontScale), 1);
    g_f18b = MakeFont((int)(18 * finalFontScale), 1);

    BuildKeys();
}

static void Fill(HDC dc, int x, int y, int w, int h, DWORD c) {
    RECT r = {x, y, x + w, y + h};
    HBRUSH br = CreateSolidBrush(c);
    FillRect(dc, &r, br);
    DeleteObject(br);
}

static void DrawRoundRect(HDC dc, int x, int y, int w, int h, DWORD fillC, DWORD borderC, int radius) {
    HPEN p = CreatePen(PS_SOLID, 1, borderC);
    HPEN op = (HPEN)SelectObject(dc, p);
    HBRUSH b = CreateSolidBrush(fillC);
    HBRUSH ob = (HBRUSH)SelectObject(dc, b);
    RoundRect(dc, x, y, x + w, y + h, radius, radius);
    SelectObject(dc, ob); DeleteObject(b);
    SelectObject(dc, op); DeleteObject(p);
}

static void DrawTextC(HDC dc, int x, int y, int w, int h, const wchar_t* s, HFONT f, DWORD c) {
    RECT r = {x, y, x + w, y + h};
    SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static wchar_t GetSymForKey(short vk, BOOL shifted) {
    struct { short vk; wchar_t n, s; } map[] = {
        {0x31,L'1',L'!'},{0x32,L'2',L'@'},{0x33,L'3',L'#'},{0x34,L'4',L'$'},{0x35,L'5',L'%'},
        {0x36,L'6',L'^'},{0x37,L'7',L'&'},{0x38,L'8',L'*'},{0x39,L'9',L'('},{0x30,L'0',L')'},
        {0xBD,L'-',L'_'},{0xBB,L'=',L'+'},{0xDB,L'[',L'{'},{0xDD,L']',L'}'},{0xDC,L'\\',L'|'},
        {0xBA,L';',L':'},{0xDE,L'\'',L'"'},{0xBC,L',',L'<'},{0xBE,L'.',L'>'},{0xBF,L'/',L'?'},
    };
    for (size_t i = 0; i < sizeof(map)/sizeof(map[0]); i++)
        if (map[i].vk == vk) return shifted ? map[i].s : map[i].n;
    return 0;
}

static const wchar_t* LetterKeyText(short vk) {
    if (g_sym) {
        int idx = (vk >= 0x41 && vk <= 0x5A) ? (vk - 0x41) : 0;
        if (idx < 26) return g_symText[idx];
        return L"?";
    }
    BOOL upper = g_cp;
    if (g_sh) upper = !upper;
    static wchar_t buf[2];
    buf[0] = (vk >= 0x41 && vk <= 0x5A) ? (upper ? vk : vk + 32) : vk;
    buf[1] = 0;
    return buf;
}

static const wchar_t* KeyText(const KeyDef* k) {
    static wchar_t buf[16];
    if (k->type == K_LETTER) {
        return LetterKeyText(k->vk);
    }
    if (k->type == K_NORMAL) {
        wchar_t ch = GetSymForKey(k->vk, g_sh);
        if (ch) { buf[0] = ch; buf[1] = 0; return buf; }
    }
    switch (k->vk) {
        case 0x1B: return L"Esc";
        case 0x2E: return L"Del";
        case 0x08: return L"\x2190";
        case 0x09: return L"Tab";
        case 0x0D: return L"Enter";
        case 0x14: return L"Caps";
        case 0x10: case 0xA0: case 0xA1: return L"Shift";
        case 0x11: return L"Ctrl";
        case 0x12: return L"Alt";
        case 0x5B: return L"Win";
        case 0x20: return L"\x7A7A\x683C";
        case 0x25: return L"\x2190";
        case 0x26: return L"\x2191";
        case 0x27: return L"\x2192";
        case 0x28: return L"\x2193";
        case 0xC0: return L"\x60";
    }
    if (k->type == K_LANG) return g_langCN ? L"\x7B80\x4F53" : L"\x82F1";
    if (k->type == K_MODE123) return g_sym ? L"abc" : L"&123";
    if (k->type == K_HIDE) return L"\x6536\x8D77";
    if (k->type == K_SPACE) return L"\x7A7A\x683C";
    if (k->type == K_SPECIAL && k->vk == 0) return L"Fn";
    return L"";
}

static BOOL IsActive(const KeyDef* k) {
    if (k->vk == 0x14 && g_cp) return TRUE;
    if ((k->vk == 0x10 || k->vk == 0xA0 || k->vk == 0xA1) && g_sh && !g_shortPress) return TRUE;
    if (k->vk == 0x11 && g_ct && !g_shortPress) return TRUE;
    if (k->vk == 0x12 && g_al && !g_shortPress) return TRUE;
    if (k->type == K_MODE123 && g_sym) return TRUE;
    if (k->type == K_LANG && g_langCN) return TRUE;
    return FALSE;
}

// ========== IME-Compatible Input Injection ==========
// 修复 #2: 使用 SendInput 替代已废弃的 keybd_event()
// 修复 #5: 使用 MapVirtualKeyW (Unicode 版本) 并正确设置扫描码与扩展键标志
static void SendKey(BYTE vk, BOOL sh, BOOL ct, BOOL al) {
    INPUT inputs[10] = {};
    int count = 0;

    // 使用 MapVirtualKeyW 获取正确扫描码（修复 #5: 部分 IME 依赖正确扫描码）
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);

    // 判断扩展键（右 Shift/Ctrl/Alt, 方向键, Win 等）
    BOOL isExtended = (vk == VK_RSHIFT || vk == VK_RCONTROL || vk == VK_RMENU ||
                       vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
                       vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT ||
                       vk == VK_INSERT || vk == VK_DELETE || vk == VK_LWIN || vk == VK_RWIN);

    DWORD extFlag = isExtended ? KEYEVENTF_EXTENDEDKEY : 0;

    // 按下修饰键
    if (ct) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_CONTROL;
        inputs[count].ki.wScan = (WORD)MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC);
        count++;
    }
    if (al) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_MENU;
        inputs[count].ki.wScan = (WORD)MapVirtualKeyW(VK_MENU, MAPVK_VK_TO_VSC);
        inputs[count].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
        count++;
    }
    if (sh) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_SHIFT;
        inputs[count].ki.wScan = (WORD)MapVirtualKeyW(VK_SHIFT, MAPVK_VK_TO_VSC);
        count++;
    }

    // 目标键 down + up（以 VK 形式发送，TSF/IME 可正确拦截 WM_KEYDOWN）
    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = vk;
    inputs[count].ki.wScan = (WORD)sc;
    inputs[count].ki.dwFlags = extFlag;
    count++;

    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = vk;
    inputs[count].ki.wScan = (WORD)sc;
    inputs[count].ki.dwFlags = extFlag | KEYEVENTF_KEYUP;
    count++;

    // 释放修饰键
    if (sh) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_SHIFT;
        inputs[count].ki.wScan = (WORD)MapVirtualKeyW(VK_SHIFT, MAPVK_VK_TO_VSC);
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        count++;
    }
    if (al) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_MENU;
        inputs[count].ki.wScan = (WORD)MapVirtualKeyW(VK_MENU, MAPVK_VK_TO_VSC);
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY;
        count++;
    }
    if (ct) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_CONTROL;
        inputs[count].ki.wScan = (WORD)MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC);
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        count++;
    }

    SendInput(count, inputs, sizeof(INPUT));
}

// SendChar 仅用于不需要 IME 组合的纯 Unicode 字符（如九宫格选字中的标点符号）
// 修复 #1/#3/#4: 字母和数字不再走此路径，改为通过 SendKey(VK) 发送
static void SendChar(wchar_t ch) {
    INPUT in[2] = {};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wScan = ch;
    in[0].ki.dwFlags = KEYEVENTF_UNICODE;
    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wScan = ch;
    in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

// 将字符映射为虚拟键码 + Shift 状态，通过 SendKey 发送以经过 IME/TSF 管线
// 修复 #3: 符号模式下字母键通过 VK 发送
// 修复 #4: 九宫格模式字母/数字通过 VK 发送
static void SendCharViaVK(wchar_t ch) {
    BYTE vk = 0;
    BOOL needShift = FALSE;

    if (ch >= L'a' && ch <= L'z') {
        vk = (BYTE)(ch - L'a' + 'A');  // VK_A ~ VK_Z
    } else if (ch >= L'A' && ch <= L'Z') {
        vk = (BYTE)ch;
        needShift = TRUE;
    } else if (ch >= L'0' && ch <= L'9') {
        vk = (BYTE)ch;  // VK_0 ~ VK_9 (0x30~0x39)
    } else {
        // 标点符号映射到对应物理键位
        switch (ch) {
            case L'!': vk = 0x31; needShift = TRUE; break;
            case L'@': vk = 0x32; needShift = TRUE; break;
            case L'#': vk = 0x33; needShift = TRUE; break;
            case L'$': vk = 0x34; needShift = TRUE; break;
            case L'%': vk = 0x35; needShift = TRUE; break;
            case L'^': vk = 0x36; needShift = TRUE; break;
            case L'&': vk = 0x37; needShift = TRUE; break;
            case L'*': vk = 0x38; needShift = TRUE; break;
            case L'(': vk = 0x39; needShift = TRUE; break;
            case L')': vk = 0x30; needShift = TRUE; break;
            case L'-': case L'_': vk = 0xBD; needShift = (ch == L'_'); break;
            case L'=': case L'+': vk = 0xBB; needShift = (ch == L'+'); break;
            case L'[': case L'{': vk = 0xDB; needShift = (ch == L'{'); break;
            case L']': case L'}': vk = 0xDD; needShift = (ch == L'}'); break;
            case L'\\': case L'|': vk = 0xDC; needShift = (ch == L'|'); break;
            case L';': case L':': vk = 0xBA; needShift = (ch == L':'); break;
            case L'\'': case L'"': vk = 0xDE; needShift = (ch == L'"'); break;
            case L',': case L'<': vk = 0xBC; needShift = (ch == L'<'); break;
            case L'.': case L'>': vk = 0xBE; needShift = (ch == L'>'); break;
            case L'/': case L'?': vk = 0xBF; needShift = (ch == L'?'); break;
            case L'~': vk = 0xC0; needShift = TRUE; break;
            case L'`': vk = 0xC0; needShift = FALSE; break;
            case L' ': vk = VK_SPACE; break;
            default:
                // 无法映射的字符（如中文等），回退到 KEYEVENTF_UNICODE
                SendChar(ch);
                return;
        }
    }

    SendKey(vk, needShift, FALSE, FALSE);
}

static void DoKeyAction(const KeyDef* k, BOOL isDown) {
    if (!k) return;
    switch (k->type) {
    case K_LETTER:
        if (g_ct || g_al) {
            SendKey(k->vk, g_sh, g_ct, g_al);
            g_ct = FALSE; g_al = FALSE; g_sh = FALSE;
        } else if (g_sym) {
            // 修复 #3: 符号模式下通过 VK 发送，让 IME 可拦截
            int idx = (k->vk >= 0x41 && k->vk <= 0x5A) ? k->vk - 0x41 : 0;
            SendCharViaVK(g_symText[idx][0]);
        } else {
            BOOL us = g_sh ? !(GetKeyState(VK_CAPITAL) & 1) : FALSE;
            SendKey(k->vk, us, FALSE, FALSE);
            if (g_sh) g_sh = FALSE;
        }
        break;
    case K_NORMAL:
        SendKey(k->vk, g_sh, g_ct, g_al);
        g_sh = FALSE; g_ct = FALSE; g_al = FALSE;
        break;
    case K_SPECIAL:
        if (k->vk == 0) break;  // Fn 键：占位，无动作
        SendKey(k->vk, g_sh, g_ct, g_al);
        if (k->vk != 0x14 && k->vk != 0x5B) { g_sh = FALSE; g_ct = FALSE; g_al = FALSE; }
        break;
    case K_MOD:
        if (g_shortPress) {
            SendKey(k->vk, FALSE, FALSE, FALSE);
            if (k->vk == 0x10 || k->vk == 0xA0 || k->vk == 0xA1) g_sh = FALSE;
            if (k->vk == 0x11) g_ct = FALSE;
            if (k->vk == 0x12) g_al = FALSE;
        } else {
            if (k->vk == 0x10 || k->vk == 0xA0 || k->vk == 0xA1) g_sh = !g_sh;
            else if (k->vk == 0x11) g_ct = !g_ct;
            else if (k->vk == 0x12) g_al = !g_al;
        }
        break;
    case K_CAPS:
        SendKey(0x14, FALSE, FALSE, FALSE);
        g_cp = (GetKeyState(VK_CAPITAL) & 1) != 0;
        break;
    case K_9KEY:
        if (!isDown) {
            // 修复 #4: 九宫格模式通过 VK 发送字母/数字，经过 IME 组合管线
            int d = (k->vk >= 0x30 && k->vk <= 0x39) ? (k->vk - 0x30) : 0;
            SendCharViaVK(g_9keyChars[d][0]);
        }
        break;
    case K_MODE123: g_sym = !g_sym; break;
    case K_IME: SwitchMode(); break;
    case K_ARROW: SendKey(k->vk, FALSE, FALSE, FALSE); break;
    case K_SPACE: SendKey(0x20, FALSE, g_ct, g_al); g_ct = FALSE; g_al = FALSE; break;
    case K_LANG: ToggleImeLang(); break;
    case K_EMOJI: OpenEmojiPanel(); break;
    case K_HIDE: ShowKB(FALSE); break;
    default: break;
    }
}

// 触摸屏专属：仅九宫格模式触发四周环绕选字
static void TriggerLongPressBubble(int keyIdx) {
    if (!g_9key) return;
    if (keyIdx < 0 || keyIdx >= g_nk) return;
    const KeyDef* k = &g_keys[keyIdx];
    if (k->type != K_9KEY) return;

    g_bubble.keyIdx = keyIdx;
    g_bubble.count = 5;
    g_bubble.selIdx = 0;
    g_bubble.cx = k->x + k->w / 2;
    g_bubble.cy = k->y + k->h / 2;

    double dpiScale = GetSystemDpiScale();
    int itemW = (int)(56 * dpiScale), itemH = (int)(56 * dpiScale);
    int R = (int)(62 * dpiScale);

    int minCx = itemW / 2 + R + 6;
    int maxCx = g_ww - (itemW / 2 + R + 6);
    int minCy = g_headerH + itemH / 2 + R + 4;
    int maxCy = g_wh - (itemH / 2 + R + 4);

    if (g_bubble.cx < minCx) g_bubble.cx = minCx;
    if (g_bubble.cx > maxCx) g_bubble.cx = maxCx;
    if (g_bubble.cy < minCy) g_bubble.cy = minCy;
    if (g_bubble.cy > maxCy) g_bubble.cy = maxCy;

    memset(g_bubble.chars, 0, sizeof(g_bubble.chars));

    int d = (k->vk >= 0x30 && k->vk <= 0x39) ? (k->vk - 0x30) : 0;
    const wchar_t* set = g_9keyChars[d];
    if (d == 1) { // 1.,?!
        g_bubble.chars[0] = L'1'; g_bubble.chars[1] = L'.';
        g_bubble.chars[2] = L','; g_bubble.chars[3] = L'?'; g_bubble.chars[4] = L'!';
    } else if (d == 0) { // 0+-_
        g_bubble.chars[0] = L'0'; g_bubble.chars[1] = L'+';
        g_bubble.chars[2] = L'-'; g_bubble.chars[3] = L'_'; g_bubble.chars[4] = L'0';
    } else {
        int len = (int)wcslen(set);
        g_bubble.chars[0] = set[0];
        g_bubble.chars[1] = set[0];
        g_bubble.chars[2] = (len > 1) ? set[1] : set[0];
        g_bubble.chars[3] = (len > 2) ? set[2] : set[0];
        g_bubble.chars[4] = (len > 3) ? set[3] : set[0];
    }

    g_bubble.active = TRUE;
    InvalidateRect(g_hWnd, 0, TRUE);
}

// ========== Header Layout & Dynamic DPI Positioning ==========
#define HDR_DOCK  1000
#define HDR_IME   1001
#define HDR_AUTO  1002
#define HDR_MIN   1003
#define HDR_CLOSE 1004
#define HDR_SHORT 1005

static int HitHeader(int x, int y) {
    if (y < 0 || y >= g_headerH) return -1;

    double dpiScale = GetSystemDpiScale();
    double baseW = g_9key ? (420.0 * dpiScale) : (980.0 * dpiScale);
    double scaleX = (double)g_ww / baseW;

    int rMargin = (int)(6 * dpiScale * scaleX);
    int gap     = (int)(6 * dpiScale * scaleX);

    int wClose = (int)(36 * dpiScale * scaleX);
    int wMin   = (int)(36 * dpiScale * scaleX);
    int wAuto  = (int)(96 * dpiScale * scaleX);
    int wMode  = (int)(76 * dpiScale * scaleX);
    int wShort = (int)(66 * dpiScale * scaleX);
    int wMenu  = (int)(48 * dpiScale * scaleX);

    int xClose = g_ww - rMargin - wClose;
    int xMin   = xClose - gap - wMin;
    int xAuto  = xMin - gap - wAuto;
    int xMode  = xAuto - gap - wMode;
    int xShort = xMode - gap - wShort;
    int xMenu  = (int)(6 * dpiScale * scaleX);

    if (x >= xClose && x < g_ww) return HDR_CLOSE;
    if (x >= xMin && x < xClose) return HDR_MIN;
    if (x >= xAuto && x < xMin) return HDR_AUTO;
    if (x >= xMode && x < xAuto) return HDR_IME;
    if (x >= xShort && x < xMode) return HDR_SHORT;
    if (x >= xMenu && x < xMenu + wMenu + gap) return HDR_DOCK;

    return -1;
}

static void DrawHeader(HDC dc) {
    Fill(dc, 0, 0, g_ww, g_headerH, C_HDR);

    double dpiScale = GetSystemDpiScale();
    double baseW = g_9key ? (420.0 * dpiScale) : (980.0 * dpiScale);
    double scaleX = (double)g_ww / baseW;
    double scaleY = (double)g_wh / (g_9key ? (360.0 * dpiScale) : (320.0 * dpiScale));

    int rMargin = (int)(6 * dpiScale * scaleX);
    int gap     = (int)(6 * dpiScale * scaleX);
    int btnH    = g_headerH - (int)(12 * dpiScale * scaleY);
    if (btnH < 22) btnH = 22;
    int btnY    = (g_headerH - btnH) / 2;

    int wClose = (int)(36 * dpiScale * scaleX);
    int wMin   = (int)(36 * dpiScale * scaleX);
    int wAuto  = (int)(96 * dpiScale * scaleX);
    int wMode  = (int)(76 * dpiScale * scaleX);
    int wShort = (int)(66 * dpiScale * scaleX);
    int wMenu  = (int)(48 * dpiScale * scaleX);

    int xClose = g_ww - rMargin - wClose;
    int xMin   = xClose - gap - wMin;
    int xAuto  = xMin - gap - wAuto;
    int xMode  = xAuto - gap - wMode;
    int xShort = xMode - gap - wShort;
    int xMenu  = (int)(6 * dpiScale * scaleX);

    // 左侧：实体 [ 菜单 ] 胶囊按钮
    DrawRoundRect(dc, xMenu, btnY, wMenu, btnH, C_KEY, C_KEY_BORDER, btnH / 2);
    DrawTextC(dc, xMenu, btnY, wMenu, btnH, L"\x83DC\x5355", g_f12, C_WHITE);

    // 标题
    int xTitle = xMenu + wMenu + gap;
    int wTitle = xShort - xTitle - gap;
    if (wTitle > 40) {
        DrawTextC(dc, xTitle, 0, wTitle, g_headerH, L"", g_f12, C_DIM);
    }

    // 右侧：短按开关
    DWORD shortBg = g_shortPress ? C_HOT : C_KEY;
    DrawRoundRect(dc, xShort, btnY, wShort, btnH, shortBg, C_KEY_BORDER, btnH / 2);
    DrawTextC(dc, xShort, btnY, wShort, btnH, g_shortPress ? L"\x77ED\x6309:\x5F00" : L"\x77ED\x6309:\x5173", g_f12, C_WHITE);

    // 模式切换胶囊按钮
    DWORD modeBg = g_9key ? C_HOT : C_KEY;
    DrawRoundRect(dc, xMode, btnY, wMode, btnH, modeBg, C_KEY_BORDER, btnH / 2);
    DrawTextC(dc, xMode, btnY, wMode, btnH, g_9key ? L"\x5168\x952E\x76D8" : L"\x4E5D\x5BAB\x683C", g_f12, C_WHITE);

    // 自动呼出胶囊按钮
    DWORD autoBg = g_af ? C_HOT : C_KEY;
    DrawRoundRect(dc, xAuto, btnY, wAuto, btnH, autoBg, C_KEY_BORDER, 12);
    DrawTextC(dc, xAuto, btnY, wAuto, btnH, g_af ? L"\x81EA\x52A8\x547C\x51FA:\x5F00" : L"\x81EA\x52A8\x547C\x51FA:\x5173", g_f12, C_WHITE);

    // 最小化与关闭按钮
    DrawTextC(dc, xMin, 0, wMin, g_headerH, L"\x229F", g_f14b, C_DIM);
    DrawTextC(dc, xClose, 0, wClose, g_headerH, L"\x2715", g_f12, C_DIM);
}

static void DrawKeys(HDC dc) {
    for (int i = 0; i < g_nk; i++) {
        const KeyDef* k = &g_keys[i];
        BOOL active = IsActive(k);
        BOOL pressed = (i == g_pk);
        BOOL hover = (i == g_hk);

        DWORD bg = C_KEY;
        if (active || pressed) bg = C_HOT;
        else if (hover) bg = C_HOVER;
        else {
            int dt[] = {K_SPECIAL, K_CAPS, K_MOD, K_ARROW, K_HIDE, K_MODE123};
            for (size_t j = 0; j < sizeof(dt)/sizeof(dt[0]); j++)
                if (k->type == dt[j]) { bg = C_DARK; break; }
        }

        DrawRoundRect(dc, k->x, k->y, k->w, k->h, bg, C_KEY_BORDER, 8);

        if (k->type == K_9KEY) {
            int d = (k->vk >= 0x30 && k->vk <= 0x39) ? (k->vk - 0x30) : 0;
            wchar_t digit[2] = {(wchar_t)(L'0' + d), 0};
            
            RECT rd = {k->x, k->y + 4, k->x + k->w, k->y + (int)(30 * GetSystemDpiScale())};
            SelectObject(dc, g_f18b);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, C_WHITE);
            DrawTextW(dc, digit, -1, &rd, DT_CENTER | DT_SINGLELINE);

            RECT rl = {k->x, k->y + (int)(32 * GetSystemDpiScale()), k->x + k->w, k->y + k->h - 4};
            SelectObject(dc, g_f12);
            SetTextColor(dc, C_SUBTEXT);
            DrawTextW(dc, g_9keyChars[d], -1, &rl, DT_CENTER | DT_SINGLELINE);
        } else {
            const wchar_t* txt = KeyText(k);
            HFONT f = g_f14b;
            if (k->type == K_HIDE || k->type == K_ARROW) f = g_f14b;
            if (k->vk == 0x08) f = g_f18b;
            if (k->vk == 0x0D) f = g_f13b;
            if (k->vk == 0x20 || k->type == K_SPACE) f = g_f14b;
            DrawTextC(dc, k->x, k->y, k->w, k->h, txt, f, C_WHITE);
        }
    }

    if (g_bubble.active && g_bubble.count > 0) {
        double dpiScale = GetSystemDpiScale();
        int itemW = (int)(56 * dpiScale), itemH = (int)(56 * dpiScale);
        int R = (int)(62 * dpiScale);

        struct Pos { int x, y; } pos[5] = {
            {g_bubble.cx - itemW/2, g_bubble.cy - itemH/2},     // 0: 中
            {g_bubble.cx - itemW/2, g_bubble.cy - itemH/2 - R}, // 1: 上
            {g_bubble.cx - itemW/2 + R, g_bubble.cy - itemH/2}, // 2: 右
            {g_bubble.cx - itemW/2, g_bubble.cy - itemH/2 + R}, // 3: 下
            {g_bubble.cx - itemW/2 - R, g_bubble.cy - itemH/2}  // 4: 左
        };

        for (int i = 0; i < 5; i++) {
            if (g_bubble.chars[i] == 0) continue;
            BOOL sel = (i == g_bubble.selIdx);
            
            DWORD fillC = sel ? C_HOT : C_BUBBLE_ITEM;
            DWORD borderC = sel ? C_WHITE : C_BUBBLE_BORDER;
            DrawRoundRect(dc, pos[i].x, pos[i].y, itemW, itemH, fillC, borderC, 10);

            wchar_t str[2] = {g_bubble.chars[i], 0};
            DrawTextC(dc, pos[i].x, pos[i].y, itemW, itemH, str, g_f18b, C_WHITE);
        }
    }
}

static int HitKey(int x, int y) {
    for (int i = 0; i < g_nk; i++) {
        const KeyDef* k = &g_keys[i];
        if (x >= k->x && x < k->x + k->w && y >= k->y && y < k->y + k->h)
            return i;
    }
    return -1;
}

static void ShowKB(BOOL show, BOOL isManual) {
    if (!g_hWnd) return;
    KillTimer(g_hWnd, TIMER_SLIDE);
    RECT work = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int sx = work.left + ((work.right - work.left) - g_ww) / 2;
    int targetY = work.bottom - g_wh - 6;

    if (show) {
        if (isManual) g_manualShow = TRUE;
        if (g_vis) {
            SetWindowPos(g_hWnd, HWND_TOPMOST, sx, targetY, g_ww, g_wh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            return;
        }
        g_vis = TRUE;
        g_slideFrom = work.bottom;
        g_slideTo = targetY;
        g_slideStep = 0;
        SetWindowPos(g_hWnd, HWND_TOPMOST, sx, g_slideFrom, g_ww, g_wh, SWP_SHOWWINDOW | SWP_NOACTIVATE);
        SetTimer(g_hWnd, TIMER_SLIDE, SLIDE_MS, 0);
    } else {
        if (!g_vis) return;
        g_manualShow = FALSE;
        g_lht = GetTickCount();
        RECT rc; GetWindowRect(g_hWnd, &rc);
        g_slideFrom = rc.top;
        g_slideTo = work.bottom;
        g_slideStep = 0;
        SetTimer(g_hWnd, TIMER_SLIDE, SLIDE_MS, 0);
    }
}

static void ToggleKB() { ShowKB(!g_vis, TRUE); }

static void PromptCloseAction(HWND hWnd) {
    int ret = MessageBoxW(hWnd,
        L"\x8BF7\x9009\x62E9\x89E6\x6478\x952E\x76D8\x9000\x51FA\x65B9\x5F0F\xFF1A\n\n"
        L"\x3010\x662F(Y)\x3011\x5B8C\x5168\x9000\x51FA\x7A0B\x5E8F\n"
        L"\x3010\x5426(N)\x3011\x9690\x85CF\x5230\x7CFB\x7EDF\x6258\x76D8",
        L"\x89E6\x6478\x952E\x76D8",
        MB_YESNOCANCEL | MB_ICONQUESTION);
    if (ret == IDYES) {
        DestroyWindow(hWnd);
    } else if (ret == IDNO) {
        ShowKB(FALSE, FALSE);
    }
}

static HICON LoadMainIcon(int size) {
    HICON h = (HICON)LoadImageA(g_hInst, MAKEINTRESOURCE(100), IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
    if (!h) {
        h = (HICON)LoadImageA(NULL, "winres\\main.ico", IMAGE_ICON, size, size, LR_LOADFROMFILE);
    }
    if (!h) {
        h = (HICON)LoadImageA(NULL, "main.ico", IMAGE_ICON, size, size, LR_LOADFROMFILE);
    }
    if (!h) {
        h = LoadIconA(NULL, IDI_APPLICATION);
    }
    return h;
}

static void AddTray() {
    if (g_tray) return;
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hWnd;
    g_nid.uID = 1003;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    if (!g_hTrayIcon) g_hTrayIcon = LoadMainIcon(16);
    g_nid.hIcon = g_hTrayIcon;
    strcpy(g_nid.szTip, "\xE8\xA7\xA6\xE6\x91\xB8\xE9\x94\xAE\xE7\x9B\x98");
    Shell_NotifyIconA(NIM_ADD, &g_nid);
    g_tray = TRUE;
}

static void ShowAboutDialog(HWND hWnd) {
    MessageBoxW(hWnd,
        L"Touch Keyboard\n"
        L"\x540D\x79F0\xFF1A\x89E6\x6478\x952E\x76D8\n"
        L"\x4F5C\x8005\xFF1A\x6C5F\x5357\x4E00\x6839\x8471\n\n"
        L"\x6781\x901F\x89E6\x63A7\x4E0E\x9AD8\x6E05\x5C4F\x663E\x8F93\x5165\x5DE5\x5177\n\n"
        L"\x3010\x547D\x4EE4\x884C\x53C2\x6570\x8BF4\x660E (CLI Parameters)\x3011\n"
        L"  -show      : \x542F\x52A8\x65F6\x76F4\x63A5\x5F39\x51FA\x663E\x793A\x952E\x76D8\n"
        L"  -hide      : \x542F\x52A8\x65F6\x9759\x9ED8\x9690\x85CF\x5230\x7CFB\x7EDF\x6258\x76D8\n"
        L"  -min / -tray: \x6700\x5C0F\x5316\x9A7B\x7559\x6258\x76D8\n"
        L"  -touchonly : \x89E6\x6478\x5C4F\x4E13\x5C5E\xFF0C\x975E\x89E6\x6478\x8BBE\x5907\x81EA\x52A8\x9000\x51FA\n"
        L"  -auto      : \x9ED8\x8BA4\x542F\x7528\x70B9\x51FB\x7F16\x8F91\x6846\x81EA\x52A8\x547C\x51FA\n"
        L"  -noauto    : \x9ED8\x8BA4\x5173\x95ED\x70B9\x51FB\x7F16\x8F91\x6846\x81EA\x52A8\x547C\x51FA\n"
        L"  -short     : \x9ED8\x8BA4\x5F00\x542F\x77ED\x6309\x89E6\x53D1\x6A21\x5F0F\n"
        L"  -9key / -t9: \x9ED8\x8BA4\x542F\x52A8\x4E5D\x5BAB\x683C\x89E6\x6478\x6A21\x5F0F\n"
        L"  -full      : \x9ED8\x8BA4\x542F\x52A8\x5168\x952E\x76D8\x6A21\x5F0F\n"
        L"  -dark      : \x5F3A\x5236\x6DF1\x8272\x4E3B\x9898\n"
        L"  -light     : \x5F3A\x5236\x6D45\x8272\x4E3B\x9898\n"
        L"  -theme:system : \x8DDF\x968F\x7CFB\x7EDF\x4E3B\x9898\xFF08\x9ED8\x8BA4\xFF09\n\n"
        L"\x3010 4K / \x9AD8 DPI \x9002\x914D\x3011\n"
        L"  \x9876\x680F\x6309\x94AE\x4E0E\x952E\x76D8\x5168\x76D8\x652F\x6301 4K (200%~250% DPI) \x52A8\x6001\x63A8\x7B97\x77E2\x91CF\x5BF9\x9F50\xFF01",
        L"\x5173\x4E8E\x4E0E\x53C2\x6570\x8BF4\x660E",
        MB_OK | MB_ICONINFORMATION);
}

static void ShowMenu(HWND hWnd) {
    POINT pt; GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_MENU_TOGGLE, g_vis ? L"\x9690\x85CF\x89E6\x6478\x952E\x76D8" : L"\x663E\x793A\x89E6\x6478\x952E\x76D8");
    AppendMenuW(m, MF_STRING, ID_MENU_MODE, g_9key ? L"\x5207\x6362\x4E3A\x5168\x952E\x76D8" : L"\x5207\x6362\x4E3A\x4E5D\x5BAB\x683C");
    AppendMenuW(m, MF_STRING, ID_MENU_AUTO, g_af ? L"\x7981\x7528\x81EA\x52A8\x547C\x51FA" : L"\x542F\x7528\x81EA\x52A8\x547C\x51FA");

    // 主题切换子菜单
    HMENU themeMenu = CreatePopupMenu();
    AppendMenuW(themeMenu, MF_STRING | (g_themeMode == 0 ? MF_CHECKED : 0), ID_MENU_THEME + 1, L"\x8DDF\x968F\x7CFB\x7EDF");
    AppendMenuW(themeMenu, MF_STRING | (g_themeMode == 1 ? MF_CHECKED : 0), ID_MENU_THEME + 2, L"\x6DF1\x8272\x4E3B\x9898");
    AppendMenuW(themeMenu, MF_STRING | (g_themeMode == 2 ? MF_CHECKED : 0), ID_MENU_THEME + 3, L"\x6D45\x8272\x4E3B\x9898");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)themeMenu, L"\x4E3B\x9898");

    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_MENU_ABOUT, L"\x5173\x4E8E\x4E0E\x547D\x4EE4\x884C\x53C2\x6570\x8BF4\x660E");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_MENU_EXIT, L"\x9000\x51FA\x952E\x76D8");

    SetForegroundWindow(hWnd);
    int id = TrackPopupMenu(m, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(m);

    if (id == ID_MENU_TOGGLE) {
        ToggleKB();
    } else if (id == ID_MENU_MODE) {
        SwitchMode();
    } else if (id == ID_MENU_AUTO) {
        g_af = !g_af;
        InvalidateRect(hWnd, 0, TRUE);
    } else if (id == ID_MENU_THEME + 1) {
        g_themeMode = 0; ApplyTheme(); InvalidateRect(hWnd, 0, TRUE);
    } else if (id == ID_MENU_THEME + 2) {
        g_themeMode = 1; ApplyTheme(); InvalidateRect(hWnd, 0, TRUE);
    } else if (id == ID_MENU_THEME + 3) {
        g_themeMode = 2; ApplyTheme(); InvalidateRect(hWnd, 0, TRUE);
    } else if (id == ID_MENU_ABOUT) {
        ShowAboutDialog(hWnd);
    } else if (id == ID_MENU_EXIT) {
        PromptCloseAction(hWnd);
    }
}

static BOOL IsInputControl(HWND hw) {
    if (!hw || !IsWindow(hw)) return FALSE;
    char buf[128] = {0};
    GetClassNameA(hw, buf, 128);

    if (strstr(buf, "Shell_") || strstr(buf, "Progman") || strstr(buf, "WorkerW") ||
        strstr(buf, "Taskbar") || strstr(buf, "TrayNotify") || strstr(buf, "MSTaskSwWClass"))
        return FALSE;

    if (strstr(buf, "Edit") || strstr(buf, "Rich") || strstr(buf, "Scintilla") ||
        strstr(buf, "TextBox") || strstr(buf, "Console") || strstr(buf, "Omnibox") ||
        strstr(buf, "Search") || strstr(buf, "InputSite") || strstr(buf, "TXGuiFoundation") ||
        strstr(buf, "Chrome_") || strstr(buf, "Qt5") || strstr(buf, "Afx"))
        return TRUE;

    return FALSE;
}

static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    if (!g_af || !g_hWnd) return;
    if (!hwnd || hwnd == g_hWnd) return;

    if (event == EVENT_OBJECT_FOCUS || event == EVENT_SYSTEM_FOREGROUND || (event == EVENT_OBJECT_SHOW && idObject == -8)) {
        HWND fg = GetForegroundWindow();
        if (!fg || fg == g_hWnd) return;

        DWORD tid = GetWindowThreadProcessId(fg, NULL);
        GUITHREADINFO gi = {sizeof(gi)};
        BOOL isText = FALSE;

        if (GetGUIThreadInfo(tid, &gi)) {
            HWND fh = gi.hwndFocus ? gi.hwndFocus : fg;
            if ((gi.flags & GUI_CARETBLINKING) != 0 || gi.hwndCaret != NULL || IsInputControl(fh) || IsInputControl(fg)) {
                isText = TRUE;
            }
        }

        if (isText && !g_vis && (GetTickCount() - g_lht >= 1000)) {
            PostMessage(g_hWnd, WM_FOCUS_EVENT, TRUE, 0);
        }
    }
}

static void OnLDown(HWND hWnd, int x, int y) {
    int hh = HitHeader(x, y);
    if (hh >= 0) {
        switch (hh) {
        case HDR_DOCK: ShowMenu(hWnd); break;
        case HDR_IME: SwitchMode(); break;
        case HDR_AUTO: g_af = !g_af; InvalidateRect(hWnd, 0, TRUE); break;
        case HDR_SHORT: g_shortPress = !g_shortPress; InvalidateRect(hWnd, 0, TRUE); break;
        case HDR_MIN: ShowKB(FALSE, FALSE); break;
        case HDR_CLOSE: PromptCloseAction(hWnd); break;
        }
        return;
    }

    int ki = HitKey(x, y);
    if (ki < 0) return;
    g_pk = ki;
    g_bubble.active = FALSE;
    const KeyDef* k = &g_keys[ki];

    if (g_9key && k->type == K_9KEY) {
        if (g_shortPress) {
            TriggerLongPressBubble(ki);
        } else {
            SetTimer(hWnd, TIMER_LONGPRESS, 180, NULL);
        }
    } else {
        DoKeyAction(k, TRUE);

        if (k->vk == 0x08 || k->vk == 0x2E || k->vk == 0x20 || k->type == K_ARROW) {
            g_repeatKeyIdx = ki;
            SetTimer(hWnd, TIMER_REPEAT, 350, NULL);
        }
    }
    InvalidateRect(hWnd, 0, TRUE);
}

static void OnLUp(HWND hWnd, int x, int y) {
    KillTimer(hWnd, TIMER_LONGPRESS);
    KillTimer(hWnd, TIMER_REPEAT);

    if (g_bubble.active) {
        if (g_bubble.selIdx >= 0 && g_bubble.selIdx < 5) {
            wchar_t ch = g_bubble.chars[g_bubble.selIdx];
            // 修复 #4: 气泡选字也通过 VK 发送，经过 IME 管线
            if (ch != 0) SendCharViaVK(ch);
        }
        g_bubble.active = FALSE;
        g_pk = -1;
        InvalidateRect(hWnd, 0, TRUE);
        return;
    }

    if (g_pk >= 0) {
        int ki = g_pk;
        g_pk = -1;
        const KeyDef* k = &g_keys[ki];
        if (g_9key && k->type == K_9KEY) {
            DoKeyAction(k, FALSE);
        }
        InvalidateRect(hWnd, 0, TRUE);
    }
}

static void OnMMove(HWND hWnd, int x, int y) {
    if (g_bubble.active) {
        int dx = x - g_bubble.cx;
        int dy = y - g_bubble.cy;
        int distSq = dx * dx + dy * dy;
        int newSel = 0;

        if (distSq > 22 * 22) {
            if (abs(dx) > abs(dy)) {
                newSel = (dx > 0) ? 2 : 4;
            } else {
                newSel = (dy > 0) ? 3 : 1;
            }
        }

        if (newSel != g_bubble.selIdx) {
            g_bubble.selIdx = newSel;
            InvalidateRect(hWnd, 0, TRUE);
        }
        return;
    }

    int nk = HitKey(x, y);
    if (nk != g_hk) {
        g_hk = nk;
        InvalidateRect(hWnd, 0, TRUE);
    }
}

static BOOL IsTouchDevice() {
    int maxTouches = GetSystemMetrics(95);
    if (maxTouches > 0) return TRUE;
    int digitizer = GetSystemMetrics(94);
    if ((digitizer & 0x80) != 0) return TRUE;
    return FALSE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        g_hWnd = hWnd;
        InitWindowSizeForDpi();
        RecreateFontsAndLayout();
        SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST);
        SetLayeredWindowAttributes(hWnd, 0, 255, LWA_ALPHA);
        
        g_winHook = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_SHOW, 0, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
        SetTimer(hWnd, TIMER_FOCUS, 200, 0);
        return 0;
    }
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_GETMINMAXINFO: {
        LPMINMAXINFO mmi = (LPMINMAXINFO)l;
        double dpiScale = GetSystemDpiScale();
        if (g_9key) {
            mmi->ptMinTrackSize.x = (int)(280 * dpiScale);
            mmi->ptMinTrackSize.y = (int)(240 * dpiScale);
        } else {
            mmi->ptMinTrackSize.x = (int)(500 * dpiScale);
            mmi->ptMinTrackSize.y = (int)(200 * dpiScale);
        }
        return 0;
    }
    case WM_DPICHANGED: {
        RECT* prcNew = (RECT*)l;
        SetWindowPos(hWnd, HWND_TOPMOST,
            prcNew->left, prcNew->top,
            prcNew->right - prcNew->left,
            prcNew->bottom - prcNew->top,
            SWP_NOACTIVATE);
        RecreateFontsAndLayout();
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }
    case WM_SIZE: {
        g_ww = LOWORD(l);
        g_wh = HIWORD(l);
        if (g_ww > 0 && g_wh > 0) {
            RecreateFontsAndLayout();
            InvalidateRect(hWnd, NULL, TRUE);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hWnd, &ps);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, g_ww, g_wh);
        HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
        
        Fill(mem, 0, 0, g_ww, g_wh, C_BG);
        DrawHeader(mem);
        DrawKeys(mem);
        
        BitBlt(dc, 0, 0, g_ww, g_wh, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        ScreenToClient(hWnd, &pt);
        int b = (int)(8 * GetSystemDpiScale());

        if (pt.x < b && pt.y < b) return HTTOPLEFT;
        if (pt.x >= g_ww - b && pt.y < b) return HTTOPRIGHT;
        if (pt.x < b && pt.y >= g_wh - b) return HTBOTTOMLEFT;
        if (pt.x >= g_ww - b && pt.y >= g_wh - b) return HTBOTTOMRIGHT;
        if (pt.x < b) return HTLEFT;
        if (pt.x >= g_ww - b) return HTRIGHT;
        if (pt.y < b) return HTTOP;
        if (pt.y >= g_wh - b) return HTBOTTOM;

        if (pt.y >= 0 && pt.y < g_headerH) {
            if (HitHeader(pt.x, pt.y) < 0) return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_LBUTTONDOWN: OnLDown(hWnd, GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
    case WM_LBUTTONUP: OnLUp(hWnd, GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
    case WM_MOUSEMOVE: OnMMove(hWnd, GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
    case WM_FOCUS_EVENT:
        if (g_af && !g_vis && (GetTickCount() - g_lht >= 1000)) {
            ShowKB(TRUE, FALSE);
        }
        return 0;
    case WM_SETTINGCHANGE: {
        // 跟随系统主题自动切换（仅 themeMode==0 时生效）
        if (g_themeMode == 0 && l != 0) {
            const wchar_t* section = (const wchar_t*)l;
            if (wcscmp(section, L"ImmersiveColorSet") == 0) {
                const ThemeColors* oldTheme = g_theme;
                ApplyTheme();
                if (g_theme != oldTheme) {
                    InvalidateRect(hWnd, 0, TRUE);
                }
            }
        }
        return 0;
    }
    case WM_TIMER:
        if (w == TIMER_LONGPRESS) {
            KillTimer(hWnd, TIMER_LONGPRESS);
            if (g_pk >= 0) TriggerLongPressBubble(g_pk);
        } else if (w == TIMER_REPEAT) {
            SetTimer(hWnd, TIMER_REPEAT, 40, NULL);
            if (g_pk >= 0 && g_pk == g_repeatKeyIdx) {
                const KeyDef* k = &g_keys[g_pk];
                DoKeyAction(k, TRUE);
            } else {
                KillTimer(hWnd, TIMER_REPEAT);
            }
        } else if (w == TIMER_SLIDE) {
            g_slideStep++;
            int ny = g_slideFrom + (g_slideTo - g_slideFrom) * g_slideStep / SLIDE_STEPS;
            RECT work = {0};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            int sx = work.left + ((work.right - work.left) - g_ww) / 2;
            SetWindowPos(hWnd, HWND_TOPMOST, sx, ny, g_ww, g_wh, SWP_NOACTIVATE);
            if (g_slideStep >= SLIDE_STEPS) {
                KillTimer(hWnd, TIMER_SLIDE);
                g_slideStep = -1;
                SetWindowPos(hWnd, HWND_TOPMOST, sx, g_slideTo, g_ww, g_wh, SWP_NOACTIVATE);
                if (g_slideTo >= work.bottom) {
                    g_vis = FALSE;
                    ShowWindow(hWnd, SW_HIDE);
                }
            }
        } else if (w == TIMER_FOCUS) {
            // 周期性刷新中英文状态，保证语言键标签与实际输入法同步
            if (g_vis) {
                BOOL cn = DetectImeChinese();
                if (cn != g_langCN) {
                    g_langCN = cn;
                    InvalidateRect(hWnd, 0, TRUE);
                }
            }

            if (!g_af || GetTickCount() - g_lht < 1000) return 0;

            HWND fg = GetForegroundWindow();
            if (!fg || fg == g_hWnd) return 0;

            DWORD tid = GetWindowThreadProcessId(fg, NULL);
            GUITHREADINFO gi = {sizeof(gi)};
            BOOL hasFocusInput = FALSE;

            if (GetGUIThreadInfo(tid, &gi)) {
                HWND fh = gi.hwndFocus ? gi.hwndFocus : fg;
                if (IsInputControl(fh) || IsInputControl(fg) || gi.hwndCaret != NULL || (gi.flags & GUI_CARETBLINKING) != 0) {
                    hasFocusInput = TRUE;
                }
            }

            if (hasFocusInput && !g_vis) {
                ShowKB(TRUE, FALSE);
            } else if (!hasFocusInput && g_vis && !g_manualShow) {
                POINT pt; GetCursorPos(&pt);
                if (WindowFromPoint(pt) != g_hWnd) {
                    ShowKB(FALSE, FALSE);
                }
            }
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_MENU_TOGGLE: ToggleKB(); break;
        case ID_MENU_AUTO: g_af = !g_af; InvalidateRect(hWnd, 0, TRUE); break;
        case ID_MENU_THEME + 1: g_themeMode = 0; ApplyTheme(); InvalidateRect(hWnd, 0, TRUE); break;
        case ID_MENU_THEME + 2: g_themeMode = 1; ApplyTheme(); InvalidateRect(hWnd, 0, TRUE); break;
        case ID_MENU_THEME + 3: g_themeMode = 2; ApplyTheme(); InvalidateRect(hWnd, 0, TRUE); break;
        case ID_MENU_ABOUT: ShowAboutDialog(hWnd); break;
        case ID_MENU_EXIT: PromptCloseAction(hWnd); break;
        }
        return 0;
    case WM_TRAY:
        if (l == WM_LBUTTONUP || l == WM_LBUTTONDBLCLK) {
            ToggleKB();
        } else if (l == WM_RBUTTONUP) {
            ShowMenu(hWnd);
        }
        return 0;
    case WM_CLOSE: PromptCloseAction(hWnd); return 0;
    case WM_DESTROY:
        KillTimer(hWnd, TIMER_FOCUS);
        KillTimer(hWnd, TIMER_SLIDE);
        KillTimer(hWnd, TIMER_LONGPRESS);
        KillTimer(hWnd, TIMER_REPEAT);
        if (g_winHook) { UnhookWinEvent(g_winHook); g_winHook = 0; }
        DeleteObject(g_f12); DeleteObject(g_f13b); DeleteObject(g_f14);
        DeleteObject(g_f14b); DeleteObject(g_f16b); DeleteObject(g_f18b);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, w, l);
}

int WINAPI WinMain(HINSTANCE hI, HINSTANCE, LPSTR cmd, int) {
    g_hInst = hI;

    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (hUser32) {
        typedef BOOL (WINAPI *SetDpiAwareProc)();
        SetDpiAwareProc pSetDPIAware = (SetDpiAwareProc)GetProcAddress(hUser32, "SetProcessDPIAware");
        if (pSetDPIAware) pSetDPIAware();
    }

    BOOL fShow   = (strstr(cmd, "-show") != NULL);
    BOOL fHide   = (strstr(cmd, "-hide") != NULL || strstr(cmd, "-min") != NULL || strstr(cmd, "-tray") != NULL);
    BOOL tOnly   = (strstr(cmd, "-touchonly") != NULL);
    BOOL f9Key   = (strstr(cmd, "-9key") != NULL || strstr(cmd, "-t9") != NULL);
    BOOL fFull   = (strstr(cmd, "-full") != NULL || strstr(cmd, "-qwerty") != NULL);
    BOOL fAuto   = (strstr(cmd, "-auto") != NULL);
    BOOL fNoAuto = (strstr(cmd, "-noauto") != NULL);
    BOOL fShort  = (strstr(cmd, "-short") != NULL);
    BOOL fDark   = (strstr(cmd, "-dark") != NULL);
    BOOL fLight  = (strstr(cmd, "-light") != NULL);

    // 主题参数解析
    if (fDark) g_themeMode = 1;
    else if (fLight) g_themeMode = 2;
    else g_themeMode = 0;  // 默认跟随系统
    ApplyTheme();

    BOOL isTouch = IsTouchDevice();

    if (tOnly && !isTouch) return 0;

    if (f9Key) g_9key = TRUE;
    if (fFull) g_9key = FALSE;

    if (fNoAuto) g_af = FALSE;
    else if (fAuto) g_af = TRUE;

    if (fShort) g_shortPress = TRUE;

    g_mutex = CreateMutexW(0, FALSE, L"UI_TouchKeyboard_Mutex");
    if (g_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_mutex);
        HWND ew = FindWindowW(L"UI_TouchKeyboard", 0);
        if (ew) {
            if (!fHide) ShowWindow(ew, SW_SHOW);
            SetForegroundWindow(ew);
        }
        return 0;
    }

    HICON hAppIcon = LoadMainIcon(32);
    WNDCLASSEXW wc = {sizeof(wc), CS_DBLCLKS, WndProc, 0, 0, hI,
        hAppIcon, LoadCursor(0, IDC_ARROW), (HBRUSH)GetStockObject(BLACK_BRUSH), 0, L"UI_TouchKeyboard", hAppIcon};
    RegisterClassExW(&wc);

    InitWindowSizeForDpi();

    RECT work = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    HWND hWnd = CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST,
        L"UI_TouchKeyboard", L"\x89E6\x6478\x952E\x76D8", WS_POPUP,
        work.left + ((work.right - work.left) - g_ww) / 2,
        work.bottom - g_wh - 6,
        g_ww, g_wh, 0, 0, hI, 0);
    if (!hWnd) return 1;

    if (isTouch || fHide) {
        AddTray();
    }

    if (!fHide) {
        ShowKB(TRUE, TRUE);
    } else {
        g_vis = FALSE;
        ShowWindow(hWnd, SW_HIDE);
    }

    MSG msg;
    while (GetMessage(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
