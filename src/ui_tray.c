#include "app.h"

enum { IDM_SHOW = 100, IDM_SETTINGS, IDM_RESTART, IDM_EXIT };

void tray_create(HWND hwnd) {
    memset(&g_app.nid, 0, sizeof(g_app.nid));
    g_app.nid.cbSize = sizeof(g_app.nid);
    g_app.nid.hWnd = hwnd;
    g_app.nid.uID = TRAY_ICON_ID;
    g_app.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_app.nid.uCallbackMessage = WM_TRAYICON;
    g_app.nid.uVersion = NOTIFYICON_VERSION_4;

    /* Use default application icon */
    g_app.nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);

    wcscpy(g_app.nid.szTip, L"ClipManager");

    Shell_NotifyIconW(NIM_ADD, &g_app.nid);
    Shell_NotifyIconW(NIM_SETVERSION, &g_app.nid);
}

void tray_destroy(void) {
    Shell_NotifyIconW(NIM_DELETE, &g_app.nid);
}

void tray_show_context_menu(void) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_SHOW, L"\x663E\x793A\x526A\x8D34\x677F"); /* 显示剪贴板 */
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_SETTINGS, L"\x8BBE\x7F6E"); /* 设置 */
    AppendMenuW(hMenu, MF_STRING, IDM_RESTART, L"\x91CD\x542F"); /* 重启 */
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"\x9000\x51FA"); /* 退出 */

    SetForegroundWindow(g_app.hwndMain);
    TrackPopupMenuEx(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, g_app.hwndMain, NULL);
    PostMessageW(g_app.hwndMain, WM_NULL, 0, 0); /* fix focus bug */
    DestroyMenu(hMenu);
}

/* Called from main.c message handler */
void tray_handle_wmapp(WPARAM wp, LPARAM lp) {
    (void)wp;
    /* With NOTIFYICON_VERSION_4: LOWORD(lp) is the actual message */
    UINT actual_msg = LOWORD(lp);

    switch (actual_msg) {
    case NIN_SELECT:       /* left single click */
    case WM_LBUTTONUP:
        popup_toggle();
        break;
    case WM_CONTEXTMENU:   /* right click */
    case WM_RBUTTONUP:
    case WM_LBUTTONDBLCLK:
        tray_show_context_menu();
        break;
    }
}

/* Called from main.c for WM_COMMAND from tray menu */
void tray_handle_command(WPARAM wp) {
    switch (LOWORD(wp)) {
    case IDM_SHOW:
        popup_toggle();
        break;
    case IDM_SETTINGS:
        settings_show();
        break;
    case IDM_RESTART: {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        ShellExecuteW(NULL, L"open", exe, NULL, NULL, SW_SHOW);
        PostQuitMessage(0);
        break;
    }
    case IDM_EXIT:
        PostQuitMessage(0);
        break;
    }
}
