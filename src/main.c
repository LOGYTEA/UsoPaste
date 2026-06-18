#include "app.h"

AppState g_app = { 0 };

/* ──── Item management ──── */
void item_free(ClipItem *item) {
    if (!item) return;
    if (item->content)   { free(item->content); item->content = NULL; }
    if (item->file_path) { free(item->file_path); item->file_path = NULL; }
    if (item->preview)   { free(item->preview); item->preview = NULL; }
    if (item->thumb_cache) { DeleteObject(item->thumb_cache); item->thumb_cache = NULL; }
}

void items_clear(void) {
    for (int i = 0; i < g_app.item_count; i++)
        item_free(&g_app.items[i]);
    g_app.item_count = 0;
}

ClipItem* items_add(void) {
    if (g_app.item_count >= g_app.item_capacity) {
        int new_cap = g_app.item_capacity == 0 ? 256 : g_app.item_capacity * 2;
        if (new_cap > MAX_ITEMS) new_cap = MAX_ITEMS;
        if (g_app.item_count >= new_cap) return NULL;
        ClipItem *new_items = (ClipItem *)realloc(g_app.items, sizeof(ClipItem) * (size_t)new_cap);
        if (!new_items) return NULL;
        g_app.items = new_items;
        g_app.item_capacity = new_cap;
    }
    memset(&g_app.items[g_app.item_count], 0, sizeof(ClipItem));
    return &g_app.items[g_app.item_count];
}

/* ──── Hidden main window WndProc ──── */
static LRESULT CALLBACK main_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    /* Forward tray and hotkey to handlers */
    switch (msg) {
    case WM_CLIPBOARDUPDATE:
        clipboard_handle_update();
        return 0;

    case WM_HOTKEY:
        if (wp == HOTKEY_ID) {
            popup_toggle();
            return 0;
        }
        break;

    case WM_TRAYICON:
        tray_handle_wmapp(wp, lp);
        return 0;

    case WM_COMMAND:
        tray_handle_command(wp);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* External declarations for tray handler (defined in ui_tray.c) */
extern void tray_handle_wmapp(WPARAM wp, LPARAM lp);
extern void tray_handle_command(WPARAM wp);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR cmdLine, int nCmdShow) {
    (void)hPrev; (void)cmdLine; (void)nCmdShow;

    memset(&g_app, 0, sizeof(g_app));
    g_app.hInstance = hInstance;
    g_app.hotkey_id = HOTKEY_ID;

    /* DPI awareness */
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    /* Initialize GDI+ */
    image_init();

    /* Initialize fonts and brushes */
    utils_init_fonts();
    utils_create_brushes();

    /* Set up paths */
    utils_get_appdata_path(g_app.app_data_path, NULL);
    utils_ensure_directory(g_app.app_data_path);
    wsprintfW(g_app.images_path, L"%s\\images", g_app.app_data_path);
    utils_ensure_directory(g_app.images_path);
    wsprintfW(g_app.db_path, L"%s\\clipboard.db", g_app.app_data_path);
    wsprintfW(g_app.ini_path, L"%s\\config.ini", g_app.app_data_path);

    /* Load configuration */
    config_load(&g_app.config);
    g_app.is_pinned = g_app.config.is_pinned;

    /* Open database */
    if (db_open(g_app.db_path) != 0) {
        MessageBoxW(NULL, L"\x65E0\x6CD5\x6253\x5F00\x6570\x636E\x5E93",
                    APP_NAME, MB_OK | MB_ICONERROR);
        return 1;
    }

    /* Run retention cleanup on startup */
    if (g_app.config.retention_days > 0)
        db_run_retention(g_app.config.retention_days);

    /* Load initial items */
    g_app.items = (ClipItem *)calloc(256, sizeof(ClipItem));
    g_app.item_capacity = 256;
    g_app.item_count = 0;
    db_load_items(0, 200);

    g_app.filtered = (int *)malloc(sizeof(int) * (size_t)g_app.item_count);
    g_app.filtered_count = g_app.item_count;
    for (int i = 0; i < g_app.item_count; i++)
        g_app.filtered[i] = i;

    /* Register main window class */
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = main_wndproc;
    wc.hInstance = hInstance;
    wc.lpszClassName = APP_CLASS_MAIN;
    RegisterClassExW(&wc);

    /* Create hidden message-only window */
    g_app.hwndMain = CreateWindowExW(0, APP_CLASS_MAIN, APP_NAME,
        0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);

    if (!g_app.hwndMain) {
        MessageBoxW(NULL, L"\x65E0\x6CD5\x521B\x5EFA\x7A97\x53E3",
                    APP_NAME, MB_OK | MB_ICONERROR);
        return 1;
    }

    /* Initialize clipboard listener */
    clipboard_init(g_app.hwndMain);

    /* Register hotkey */
    hotkey_register(g_app.hwndMain, g_app.config.hotkey_modifiers, g_app.config.hotkey_vk);

    /* Create system tray icon */
    tray_create(g_app.hwndMain);

    /* Create popup window (hidden) */
    popup_create();

    /* Set periodic cleanup timer (every hour) */
    SetTimer(g_app.hwndMain, CLEANUP_TIMER_ID, 3600000, NULL);

    /* Enter message loop */
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);

        /* Handle periodic cleanup timer */
        if (msg.message == WM_TIMER && msg.wParam == CLEANUP_TIMER_ID) {
            if (g_app.config.retention_days > 0)
                db_run_retention(g_app.config.retention_days);
        }
    }

    /* Cleanup */
    config_save(&g_app.config);

    clipboard_shutdown();
    hotkey_unregister(g_app.hwndMain);
    tray_destroy();
    preview_close();
    popup_destroy();

    items_clear();
    free(g_app.items);
    free(g_app.filtered);

    db_close();

    utils_destroy_fonts();
    utils_destroy_brushes();
    image_shutdown();

    return (int)msg.wParam;
}
