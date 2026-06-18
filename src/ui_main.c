#include "app.h"
#include <gdiplus/gdiplusflat.h>

static WNDCLASSEXW g_popupClass;

void popup_create(void) {
    g_popupClass.cbSize = sizeof(g_popupClass);
    g_popupClass.style = CS_HREDRAW | CS_VREDRAW;
    g_popupClass.lpfnWndProc = popup_wndproc;
    g_popupClass.hInstance = g_app.hInstance;
    g_popupClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    g_popupClass.lpszClassName = APP_CLASS_POPUP;
    RegisterClassExW(&g_popupClass);

    int x = g_app.config.window_x;
    int y = g_app.config.window_y;
    int w = g_app.config.window_w;
    int h = g_app.config.window_h;

    /* Default position: center of screen */
    if (x < 0 || y < 0) {
        x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    }

    g_app.hwndPopup = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        APP_CLASS_POPUP, APP_NAME,
        WS_POPUP | WS_CLIPCHILDREN,
        x, y, w, h,
        g_app.hwndMain, NULL, g_app.hInstance, NULL);

    /* Rounded corners on Windows 11 */
    int cornerPref = 2; /* DWMWCP_ROUND */
    DwmSetWindowAttribute(g_app.hwndPopup, 33, &cornerPref, sizeof(cornerPref));
}

void popup_destroy(void) {
    if (g_app.hwndPopup) {
        /* Save window position */
        RECT rc;
        GetWindowRect(g_app.hwndPopup, &rc);
        g_app.config.window_x = rc.left;
        g_app.config.window_y = rc.top;
        g_app.config.window_w = rc.right - rc.left;
        g_app.config.window_h = rc.bottom - rc.top;

        DestroyWindow(g_app.hwndPopup);
        g_app.hwndPopup = NULL;
    }
}

void popup_toggle(void) {
    if (g_app.popup_visible)
        popup_hide();
    else
        popup_show();
}

void popup_show(void) {
    if (!g_app.hwndPopup) return;

    /* Position near cursor or tray */
    POINT pt;
    GetCursorPos(&pt);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int w = g_app.config.window_w;
    int h = g_app.config.window_h;
    int x = pt.x - w / 2;
    int y = pt.y + 20;

    /* Keep on screen */
    if (x < 10) x = 10;
    if (x + w > sw - 10) x = sw - w - 10;
    if (y + h > sh - 10) y = pt.y - h - 20;
    if (y < 10) y = 10;

    SetWindowPos(g_app.hwndPopup, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
    ShowWindow(g_app.hwndPopup, SW_SHOW);
    SetForegroundWindow(g_app.hwndPopup);
    SetFocus(g_app.hwndPopup);
    g_app.popup_visible = true;

    /* Refresh data */
    popup_rebuild_filter();
    InvalidateRect(g_app.hwndPopup, NULL, TRUE);
}

void popup_hide(void) {
    if (!g_app.hwndPopup) return;
    ShowWindow(g_app.hwndPopup, SW_HIDE);
    g_app.popup_visible = false;

    /* Save position */
    RECT rc;
    GetWindowRect(g_app.hwndPopup, &rc);
    g_app.config.window_x = rc.left;
    g_app.config.window_y = rc.top;
}

void popup_rebuild_filter(void) {
    bool fav_only = (g_app.active_tab == TAB_FAVORITES);
    ClipType filter = (ClipType)g_app.active_tab;
    if (fav_only) filter = CLIP_TEXT; /* doesn't matter, will use fav_only flag */

    db_load_filtered(filter, fav_only,
                     g_app.search_query[0] ? g_app.search_query : NULL);

    g_app.scroll_offset = 0;
    g_app.hover_index = -1;
    g_app.selected_index = -1;
}

/* ──── Popup WndProc ──── */
LRESULT CALLBACK popup_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1; /* prevent flicker */

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right, h = rc.bottom;

        /* Ensure back buffer */
        if (g_app.buf_w != w || g_app.buf_h != h) {
            if (g_app.mem_dc) DeleteDC(g_app.mem_dc);
            if (g_app.mem_bmp) DeleteObject(g_app.mem_bmp);
            g_app.mem_dc = CreateCompatibleDC(hdc);
            g_app.mem_bmp = CreateCompatibleBitmap(hdc, w, h);
            SelectObject(g_app.mem_dc, g_app.mem_bmp);
            g_app.buf_w = w;
            g_app.buf_h = h;
        }

        HDC memDC = g_app.mem_dc;

        /* Clear background */
        FillRect(memDC, &rc, g_app.brush_bg);

        /* Draw search bar */
        ctrl_draw_search_bar(memDC, &rc, g_app.search_query, g_app.search_focused);

        /* Draw tab bar */
        ctrl_draw_tab_bar(memDC, &rc, g_app.active_tab);

        /* Draw items */
        if (g_app.filtered_count == 0) {
            RECT empty_rc = rc;
            empty_rc.top += 120;
            ctrl_draw_empty_state(memDC, &empty_rc,
                L"\x6CA1\x6709\x526A\x8D34\x677F\x8BB0\x5F55"); /* 没有剪贴板记录 */
        } else {
            /* Virtual list: only draw visible items */
            int list_area_h = h - LIST_TOP - 4;
            int first_visible = g_app.scroll_offset / ITEM_HEIGHT;
            int visible_count = list_area_h / ITEM_HEIGHT + 2;

            /* Trigger thumbnail loading for visible items */
            for (int i = first_visible; i < first_visible + visible_count && i < g_app.filtered_count; i++) {
                if (i >= g_app.item_count) break;
                int idx = g_app.filtered[i];
                ClipItem *it = &g_app.items[idx];
                if (it->type == CLIP_IMAGE && !it->thumb_cache && it->file_path) {
                    it->thumb_cache = image_load_thumbnail(it->file_path, THUMB_SIZE, THUMB_SIZE);
                }
            }

            for (int i = first_visible; i < first_visible + visible_count && i < g_app.filtered_count; i++) {
                if (i >= g_app.item_count) break;
                int idx = g_app.filtered[i];
                bool hovered = (i == g_app.hover_index);
                bool copied = (i == g_app.copied_index &&
                               (GetTickCount() - g_app.copied_time) < 1500);
                ctrl_draw_item_row(memDC, &rc, &g_app.items[idx], i, hovered, copied);
            }

            /* Scrollbar */
            int total_h = g_app.filtered_count * ITEM_HEIGHT;
            ctrl_draw_scrollbar(memDC, &rc, g_app.scroll_offset, total_h, list_area_h);
        }

        /* Blit to screen */
        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp);
        int y = GET_Y_LPARAM(lp);
        RECT rc;
        GetClientRect(hwnd, &rc);

        int old_hover = g_app.hover_index;
        g_app.hover_index = ctrl_hit_item(x, y, &rc, g_app.scroll_offset, g_app.filtered_count);

        if (old_hover != g_app.hover_index)
            InvalidateRect(hwnd, NULL, FALSE);

        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_app.hover_index != -1) {
            g_app.hover_index = -1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp);
        int y = GET_Y_LPARAM(lp);
        RECT rc;
        GetClientRect(hwnd, &rc);

        /* Check search bar click */
        if (ctrl_hit_search(x, y, &rc)) {
            g_app.search_focused = true;
            SetFocus(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        /* Check pin click */
        if (ctrl_hit_pin(x, y, &rc)) {
            g_app.is_pinned = !g_app.is_pinned;
            g_app.config.is_pinned = g_app.is_pinned;
            config_save(&g_app.config);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        /* Check settings click */
        if (ctrl_hit_settings(x, y, &rc)) {
            settings_show();
            return 0;
        }

        /* Check tab click */
        TabIndex tab = ctrl_hit_tab(x, y, &rc);
        if (tab >= 0 && tab < TAB_COUNT) {
            g_app.active_tab = tab;
            popup_rebuild_filter();
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }

        /* Check item click */
        int item_idx = ctrl_hit_item(x, y, &rc, g_app.scroll_offset, g_app.filtered_count);
        if (item_idx >= 0 && item_idx < g_app.filtered_count) {
            int real_idx = g_app.filtered[item_idx];

            /* Check star click */
            if (ctrl_hit_star(x, y, &rc, item_idx, g_app.scroll_offset)) {
                db_toggle_favorite(g_app.items[real_idx].id);
                g_app.items[real_idx].is_favorite = !g_app.items[real_idx].is_favorite;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            /* Copy item to clipboard */
            clipboard_copy_item(&g_app.items[real_idx]);
            g_app.copied_index = item_idx;
            g_app.copied_time = GetTickCount();
            InvalidateRect(hwnd, NULL, FALSE);

            /* Hide popup after copying (unless pinned) */
            if (!g_app.is_pinned) {
                SetTimer(hwnd, 999, 800, NULL);
            }
        }

        g_app.search_focused = false;
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lp);
        int y = GET_Y_LPARAM(lp);
        RECT rc;
        GetClientRect(hwnd, &rc);

        int item_idx = ctrl_hit_item(x, y, &rc, g_app.scroll_offset, g_app.filtered_count);
        if (item_idx >= 0 && item_idx < g_app.filtered_count) {
            int real_idx = g_app.filtered[item_idx];
            ClipItem *it = &g_app.items[real_idx];
            if (it->type == CLIP_IMAGE && it->file_path) {
                preview_show(it->file_path);
            }
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int list_h = rc.bottom - LIST_TOP;
        int total_h = g_app.filtered_count * ITEM_HEIGHT;

        g_app.scroll_offset -= delta / 3;
        if (g_app.scroll_offset < 0) g_app.scroll_offset = 0;
        int max_scroll = total_h - list_h;
        if (max_scroll < 0) max_scroll = 0;
        if (g_app.scroll_offset > max_scroll) g_app.scroll_offset = max_scroll;

        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_KEYDOWN: {
        if (wp == VK_ESCAPE) {
            if (g_app.search_focused && g_app.search_query[0]) {
                g_app.search_query[0] = L'\0';
                popup_rebuild_filter();
                InvalidateRect(hwnd, NULL, TRUE);
            } else {
                popup_hide();
            }
            return 0;
        }
        if (wp == VK_BACK && g_app.search_focused) {
            size_t len = wcslen(g_app.search_query);
            if (len > 0) {
                g_app.search_query[len - 1] = L'\0';
                popup_rebuild_filter();
                InvalidateRect(hwnd, NULL, TRUE);
            }
            return 0;
        }
        return 0;
    }

    case WM_CHAR: {
        if (g_app.search_focused && wp >= 32 && wp != 127) {
            size_t len = wcslen(g_app.search_query);
            if (len < MAX_SEARCH_LEN - 1) {
                g_app.search_query[len] = (wchar_t)wp;
                g_app.search_query[len + 1] = L'\0';
                popup_rebuild_filter();
                InvalidateRect(hwnd, NULL, TRUE);
            }
            return 0;
        }
        return 0;
    }

    case WM_TIMER:
        if (wp == 999) {
            KillTimer(hwnd, 999);
            if (!g_app.is_pinned) popup_hide();
            return 0;
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && !g_app.is_pinned) {
            popup_hide();
        }
        return 0;

    case WM_SIZE:
        g_app.buf_w = 0; /* force back buffer recreate */
        g_app.buf_h = 0;
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;

    case WM_DESTROY:
        if (g_app.mem_dc) { DeleteDC(g_app.mem_dc); g_app.mem_dc = NULL; }
        if (g_app.mem_bmp) { DeleteObject(g_app.mem_bmp); g_app.mem_bmp = NULL; }
        g_app.buf_w = 0;
        g_app.buf_h = 0;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
