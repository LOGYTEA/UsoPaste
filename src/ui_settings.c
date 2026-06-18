#include "app.h"

#define SETTINGS_W  440
#define SETTINGS_H  480

#define IDC_HOTKEY_LABEL    201
#define IDC_HOTKEY_INPUT    202
#define IDC_AUTOSTART       203
#define IDC_RETENTION_7     204
#define IDC_RETENTION_30    205
#define IDC_RETENTION_90    206
#define IDC_RETENTION_NONE  207
#define IDC_CLEAR_HISTORY   208
#define IDC_SAVE            209
#define IDC_CANCEL          210

/* Hotkey capture state */
static bool g_capturing = false;
static UINT g_capture_mod = 0;
static UINT g_capture_vk = 0;

static HWND g_hwndSettings = NULL;
static HFONT g_setFont = NULL;

/* Dark-themed control subclass */
static WNDPROC g_origBtnProc = NULL;

static LRESULT CALLBACK dark_btn_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wp;
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(COLOR_SURFACE);
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        HBRUSH bg = CreateSolidBrush(COLOR_SURFACE);
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        HPEN pen = CreatePen(PS_SOLID, 1, COLOR_BORDER);
        HPEN old_pen = SelectObject(hdc, pen);
        HBRUSH old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
        SelectObject(hdc, old_pen);
        SelectObject(hdc, old_brush);
        DeleteObject(pen);

        wchar_t text[128];
        GetWindowTextW(hwnd, text, 128);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, COLOR_TEXT);
        HFONT f = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
        if (f) SelectObject(hdc, f);
        DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    if (g_origBtnProc)
        return CallWindowProcW(g_origBtnProc, hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK settings_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wp;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_app.brush_bg);
        return 1;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, COLOR_TEXT);
        SetBkColor(hdc, COLOR_BG);
        return (LRESULT)g_app.brush_bg;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);

        if (id == IDC_HOTKEY_INPUT && code == BN_CLICKED) {
            g_capturing = true;
            g_capture_mod = 0;
            g_capture_vk = 0;
            SetWindowTextW((HWND)lp, L"\x8BF7\x6309\x4E0B\x5FEB\x6377\x952E..."); /* 请按下快捷键... */
            SetFocus((HWND)lp);
            return 0;
        }

        if (id == IDC_SAVE && code == BN_CLICKED) {
            /* Read settings */
            g_app.config.hotkey_modifiers = g_capture_mod ? g_capture_mod : g_app.config.hotkey_modifiers;
            g_app.config.hotkey_vk = g_capture_vk ? g_capture_vk : g_app.config.hotkey_vk;
            hotkey_to_string(g_app.config.hotkey_modifiers, g_app.config.hotkey_vk,
                             g_app.config.hotkey_str, 64);

            /* Auto-start */
            bool autoStart = (SendMessageW(GetDlgItem(hwnd, IDC_AUTOSTART), BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (autoStart != g_app.config.auto_start) {
                config_set_autostart(autoStart);
                g_app.config.auto_start = autoStart;
            }

            /* Retention */
            if (SendMessageW(GetDlgItem(hwnd, IDC_RETENTION_7), BM_GETCHECK, 0, 0) == BST_CHECKED)
                g_app.config.retention_days = 7;
            else if (SendMessageW(GetDlgItem(hwnd, IDC_RETENTION_30), BM_GETCHECK, 0, 0) == BST_CHECKED)
                g_app.config.retention_days = 30;
            else if (SendMessageW(GetDlgItem(hwnd, IDC_RETENTION_90), BM_GETCHECK, 0, 0) == BST_CHECKED)
                g_app.config.retention_days = 90;
            else
                g_app.config.retention_days = 0;

            config_save(&g_app.config);

            /* Re-register hotkey */
            hotkey_unregister(g_app.hwndMain);
            hotkey_register(g_app.hwndMain, g_app.config.hotkey_modifiers, g_app.config.hotkey_vk);

            /* Run retention cleanup */
            if (g_app.config.retention_days > 0)
                db_run_retention(g_app.config.retention_days);

            /* Refresh popup */
            popup_rebuild_filter();

            settings_close();
            return 0;
        }

        if (id == IDC_CANCEL && code == BN_CLICKED) {
            settings_close();
            return 0;
        }

        if (id == IDC_CLEAR_HISTORY && code == BN_CLICKED) {
            int ret = MessageBoxW(hwnd,
                L"\x786E\x5B9A\x8981\x6E05\x9664\x6240\x6709\x5386\x53F2\x8BB0\x5F55\x5417\xFF1F", /* 确定要清除所有历史记录吗？ */
                L"ClipManager", MB_YESNO | MB_ICONQUESTION);
            if (ret == IDYES) {
                db_clear_all();
                items_clear();
                g_app.filtered_count = 0;
                popup_rebuild_filter();
            }
            return 0;
        }
        break;
    }

    case WM_KEYDOWN: {
        if (g_capturing) {
            UINT mod = 0;
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mod |= MOD_CONTROL;
            if (GetAsyncKeyState(VK_MENU) & 0x8000)     mod |= MOD_ALT;
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000)    mod |= MOD_SHIFT;
            if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000))
                mod |= MOD_WIN;

            UINT vk = (UINT)wp;

            /* Ignore modifier-only keys */
            if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU ||
                vk == VK_LWIN || vk == VK_RWIN ||
                vk == VK_LCONTROL || vk == VK_RCONTROL ||
                vk == VK_LSHIFT || vk == VK_RSHIFT ||
                vk == VK_LMENU || vk == VK_RMENU) {
                return 0;
            }

            if (mod == 0) {
                MessageBoxW(hwnd,
                    L"\x5FEB\x6377\x952E\x5FC5\x987B\x5305\x542B\x81F3\x5C11\x4E00\x4E2A\x4FEE\x9970\x952E (Ctrl/Alt/Shift/Win)",
                    L"ClipManager", MB_OK | MB_ICONWARNING);
                g_capturing = false;
                SetWindowTextW(GetDlgItem(hwnd, IDC_HOTKEY_INPUT), g_app.config.hotkey_str);
                return 0;
            }

            g_capture_mod = mod;
            g_capture_vk = vk;
            g_capturing = false;

            wchar_t display[64];
            hotkey_to_string(mod, vk, display, 64);
            SetWindowTextW(GetDlgItem(hwnd, IDC_HOTKEY_INPUT), display);

            /* Warn about Win key conflicts */
            if (mod & MOD_WIN) {
                MessageBoxW(hwnd,
                    L"Win\x7EC4\x5408\x952E\x53EF\x80FD\x4E0E\x7CFB\x7EDF\x5FEB\x6377\x952E\x51B2\x7A81",
                    L"ClipManager", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        settings_close();
        return 0;

    case WM_DESTROY:
        if (g_setFont) { DeleteObject(g_setFont); g_setFont = NULL; }
        g_hwndSettings = NULL;
        g_app.hwndSettings = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void settings_show(void) {
    if (g_hwndSettings) {
        SetForegroundWindow(g_hwndSettings);
        return;
    }

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = settings_wndproc;
    wc.hInstance = g_app.hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_app.brush_bg;
    wc.lpszClassName = APP_CLASS_SETTINGS;
    RegisterClassExW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x = (sw - SETTINGS_W) / 2;
    int y = (sh - SETTINGS_H) / 2;

    g_hwndSettings = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        APP_CLASS_SETTINGS,
        L"ClipManager - \x8BBE\x7F6E", /* 设置 */
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, SETTINGS_W, SETTINGS_H,
        g_app.hwndMain, NULL, g_app.hInstance, NULL);

    if (!g_hwndSettings) return;
    g_app.hwndSettings = g_hwndSettings;

    g_setFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft YaHei UI");
    if (!g_setFont)
        g_setFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");

    int cx = 20, cy = 20, lw = SETTINGS_W - 40;

    /* Section: Hotkey */
    HWND lbl = CreateWindowExW(0, L"STATIC",
        L"\x5168\x5C40\x5FEB\x6377\x952E:", /* 全局快捷键: */
        WS_CHILD | WS_VISIBLE, cx, cy, lw, 20, g_hwndSettings, NULL, g_app.hInstance, NULL);
    SendMessageW(lbl, WM_SETFONT, (WPARAM)g_setFont, TRUE);
    cy += 28;

    HWND btn_hotkey = CreateWindowExW(0, L"BUTTON", g_app.config.hotkey_str,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        cx, cy, lw, 32, g_hwndSettings, (HMENU)(LONG_PTR)IDC_HOTKEY_INPUT, g_app.hInstance, NULL);
    SendMessageW(btn_hotkey, WM_SETFONT, (WPARAM)g_setFont, TRUE);
    g_origBtnProc = (WNDPROC)SetWindowLongPtrW(btn_hotkey, GWLP_WNDPROC, (LONG_PTR)dark_btn_proc);
    cy += 48;

    /* Section: Auto-start */
    HWND chk = CreateWindowExW(0, L"BUTTON",
        L"\x5F00\x673A\x81EA\x542F\x52A8", /* 开机自启动 */
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        cx, cy, lw, 24, g_hwndSettings, (HMENU)(LONG_PTR)IDC_AUTOSTART, g_app.hInstance, NULL);
    SendMessageW(chk, WM_SETFONT, (WPARAM)g_setFont, TRUE);
    if (g_app.config.auto_start)
        SendMessageW(chk, BM_SETCHECK, BST_CHECKED, 0);
    cy += 36;

    /* Section: Retention */
    lbl = CreateWindowExW(0, L"STATIC",
        L"\x5386\x53F2\x8BB0\x5F55\x4FDD\x7559\x65F6\x95F4:", /* 历史记录保留时间: */
        WS_CHILD | WS_VISIBLE, cx, cy, lw, 20, g_hwndSettings, NULL, g_app.hInstance, NULL);
    SendMessageW(lbl, WM_SETFONT, (WPARAM)g_setFont, TRUE);
    cy += 28;

    struct { int id; const wchar_t *text; int days; } radios[] = {
        { IDC_RETENTION_7,  L"7 \x5929",   7  },  /* 7 天 */
        { IDC_RETENTION_30, L"30 \x5929",  30 },  /* 30 天 */
        { IDC_RETENTION_90, L"90 \x5929",  90 },  /* 90 天 */
        { IDC_RETENTION_NONE, L"\x65E0\x9650\x5236", 0 }, /* 无限制 */
    };

    for (int i = 0; i < 4; i++) {
        DWORD style = WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON;
        if (i == 0) style |= WS_GROUP;
        HWND r = CreateWindowExW(0, L"BUTTON", radios[i].text, style,
            cx + 10, cy, lw - 10, 22, g_hwndSettings,
            (HMENU)(LONG_PTR)radios[i].id, g_app.hInstance, NULL);
        SendMessageW(r, WM_SETFONT, (WPARAM)g_setFont, TRUE);

        if (g_app.config.retention_days == radios[i].days)
            SendMessageW(r, BM_SETCHECK, BST_CHECKED, 0);
        cy += 26;
    }
    cy += 16;

    /* Clear history button */
    HWND clearBtn = CreateWindowExW(0, L"BUTTON",
        L"\x6E05\x9664\x6240\x6709\x5386\x53F2", /* 清除所有历史 */
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        cx, cy, lw, 32, g_hwndSettings, (HMENU)(LONG_PTR)IDC_CLEAR_HISTORY, g_app.hInstance, NULL);
    SendMessageW(clearBtn, WM_SETFONT, (WPARAM)g_setFont, TRUE);
    g_origBtnProc = (WNDPROC)SetWindowLongPtrW(clearBtn, GWLP_WNDPROC, (LONG_PTR)dark_btn_proc);
    cy += 48;

    /* Save / Cancel buttons */
    int btnW = (lw - 10) / 2;
    HWND saveBtn = CreateWindowExW(0, L"BUTTON",
        L"\x4FDD\x5B58", /* 保存 */
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        cx, cy, btnW, 32, g_hwndSettings, (HMENU)(LONG_PTR)IDC_SAVE, g_app.hInstance, NULL);
    SendMessageW(saveBtn, WM_SETFONT, (WPARAM)g_setFont, TRUE);
    g_origBtnProc = (WNDPROC)SetWindowLongPtrW(saveBtn, GWLP_WNDPROC, (LONG_PTR)dark_btn_proc);

    HWND cancelBtn = CreateWindowExW(0, L"BUTTON",
        L"\x53D6\x6D88", /* 取消 */
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        cx + btnW + 10, cy, btnW, 32, g_hwndSettings, (HMENU)(LONG_PTR)IDC_CANCEL, g_app.hInstance, NULL);
    SendMessageW(cancelBtn, WM_SETFONT, (WPARAM)g_setFont, TRUE);
    g_origBtnProc = (WNDPROC)SetWindowLongPtrW(cancelBtn, GWLP_WNDPROC, (LONG_PTR)dark_btn_proc);

    /* Init capture state */
    g_capturing = false;
    g_capture_mod = g_app.config.hotkey_modifiers;
    g_capture_vk = g_app.config.hotkey_vk;

    ShowWindow(g_hwndSettings, SW_SHOW);
    SetForegroundWindow(g_hwndSettings);
}

void settings_close(void) {
    if (g_hwndSettings) {
        DestroyWindow(g_hwndSettings);
        g_hwndSettings = NULL;
        g_app.hwndSettings = NULL;
    }
}
