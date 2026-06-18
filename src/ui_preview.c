#include "app.h"

static HWND g_hwndPreview = NULL;
static HBITMAP g_previewBmp = NULL;
static int g_imgW = 0, g_imgH = 0;
static wchar_t g_previewPath[MAX_PATH];

static LRESULT CALLBACK preview_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        /* Dark background */
        HBRUSH bg = CreateSolidBrush(COLOR_BG);
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        /* Draw image centered */
        if (g_previewBmp) {
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP old = SelectObject(memDC, g_previewBmp);
            int x = (rc.right - g_imgW) / 2;
            int y = (rc.bottom - g_imgH) / 2;
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            AlphaBlend(hdc, x, y, g_imgW, g_imgH, memDC, 0, 0, g_imgW, g_imgH, bf);
            SelectObject(memDC, old);
            DeleteDC(memDC);
        }

        /* Info text at bottom */
        if (g_previewPath[0]) {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, COLOR_TEXT_SEC);
            HFONT font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
            HFONT old_font = SelectObject(hdc, font);

            wchar_t info[512];
            wsprintfW(info, L"%s  (%dx%d)", g_previewPath, g_imgW, g_imgH);
            RECT info_rc = { 10, rc.bottom - 30, rc.right - 10, rc.bottom - 5 };
            DrawTextW(hdc, info, -1, &info_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(hdc, old_font);
            DeleteObject(font);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        DestroyWindow(hwnd);
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_DESTROY:
        if (g_previewBmp) {
            DeleteObject(g_previewBmp);
            g_previewBmp = NULL;
        }
        g_hwndPreview = NULL;
        g_app.hwndPreview = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void preview_show(const wchar_t *image_path) {
    if (!image_path) return;

    /* Close existing preview */
    preview_close();

    /* Load image */
    g_previewBmp = image_load_full(image_path, &g_imgW, &g_imgH);
    if (!g_previewBmp) return;

    wcsncpy(g_previewPath, image_path, MAX_PATH - 1);

    /* Register window class */
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = preview_wndproc;
    wc.hInstance = g_app.hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = APP_CLASS_PREVIEW;
    RegisterClassExW(&wc);

    /* Size window to fit image, capped at 80% screen */
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = g_imgW + 20;
    int wh = g_imgH + 60;
    if (ww > sw * 4 / 5) ww = sw * 4 / 5;
    if (wh > sh * 4 / 5) wh = sh * 4 / 5;

    int x = (sw - ww) / 2;
    int y = (sh - wh) / 2;

    g_hwndPreview = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        APP_CLASS_PREVIEW, L"ClipManager - Preview",
        WS_POPUP,
        x, y, ww, wh,
        g_app.hwndMain, NULL, g_app.hInstance, NULL);

    if (g_hwndPreview) {
        g_app.hwndPreview = g_hwndPreview;
        ShowWindow(g_hwndPreview, SW_SHOW);
        SetForegroundWindow(g_hwndPreview);
    }
}

void preview_close(void) {
    if (g_hwndPreview) {
        DestroyWindow(g_hwndPreview);
        g_hwndPreview = NULL;
        g_app.hwndPreview = NULL;
    }
    if (g_previewBmp) {
        DeleteObject(g_previewBmp);
        g_previewBmp = NULL;
    }
    g_previewPath[0] = L'\0';
}
