#ifndef APP_H
#define APP_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "../third_party/sqlite3.h"

/* ──────────────────── Constants ──────────────────── */

#define APP_NAME            L"ClipManager"
#define APP_CLASS_MAIN      L"ClipManager_Main"
#define APP_CLASS_POPUP     L"ClipManager_Popup"
#define APP_CLASS_SETTINGS  L"ClipManager_Settings"
#define APP_CLASS_PREVIEW   L"ClipManager_Preview"

#define DEFAULT_POPUP_W     420
#define DEFAULT_POPUP_H     560
#define ITEM_HEIGHT         64
#define SEARCH_BAR_H        44
#define TAB_BAR_H           38
#define THUMB_SIZE          48
#define MAX_PREVIEW_LEN     200
#define MAX_SEARCH_LEN      256
#define MAX_ITEMS           5000

/* Layout constants (shared between ui_controls.c and ui_main.c) */
#define MARGIN              10
#define SEARCH_TOP          8
#define SEARCH_H            36
#define TAB_TOP             (SEARCH_TOP + SEARCH_H + 6)
#define TAB_H               32
#define LIST_TOP            (TAB_TOP + TAB_H + 4)

#define TRAY_ICON_ID        1
#define HOTKEY_ID           1
#define CLEANUP_TIMER_ID    1

/* Custom messages */
#define WM_TRAYICON         (WM_APP + 1)
#define WM_THUMB_LOADED     (WM_APP + 2)

/* ──────────────────── Colors ──────────────────── */

#define COLOR_BG            RGB(26, 26, 46)
#define COLOR_SURFACE       RGB(37, 37, 69)
#define COLOR_BORDER        RGB(45, 45, 74)
#define COLOR_TEXT          RGB(224, 224, 224)
#define COLOR_TEXT_SEC      RGB(136, 136, 160)
#define COLOR_ACCENT        RGB(74, 158, 255)
#define COLOR_SEARCH_BG     RGB(22, 22, 43)
#define COLOR_STAR          RGB(255, 215, 0)
#define COLOR_HOVER         RGB(40, 40, 75)
#define COLOR_COPIED        RGB(80, 200, 120)

/* ──────────────────── Enums ──────────────────── */

typedef enum { CLIP_TEXT = 0, CLIP_IMAGE = 1, CLIP_FILE = 2 } ClipType;

typedef enum {
    TAB_ALL = 0, TAB_TEXT, TAB_IMAGE, TAB_FILE, TAB_FAVORITES, TAB_COUNT
} TabIndex;

/* ──────────────────── Data Structures ──────────────────── */

typedef struct {
    int64_t   id;
    ClipType  type;
    wchar_t  *content;       /* text content (NULL for non-text) */
    wchar_t  *file_path;     /* image file path / original file path */
    wchar_t  *preview;       /* display text (~200 chars) */
    int64_t   timestamp;     /* unix ms */
    int       is_favorite;
    char      data_hash[65]; /* SHA-256 hex */
    int64_t   file_size;
    HBITMAP   thumb_cache;   /* lazy-loaded thumbnail */
} ClipItem;

typedef struct {
    bool      auto_start;
    wchar_t   hotkey_str[64];
    UINT      hotkey_modifiers;
    UINT      hotkey_vk;
    int       retention_days; /* 0 = unlimited */
    int       max_items;
    int       window_x, window_y;
    int       window_w, window_h;
    bool      is_pinned;
} Config;

typedef struct {
    HINSTANCE       hInstance;
    HWND            hwndMain;       /* hidden message window */
    HWND            hwndPopup;      /* clipboard history popup */
    HWND            hwndSettings;   /* settings window */
    HWND            hwndPreview;    /* image preview window */

    NOTIFYICONDATAW nid;
    bool            popup_visible;
    bool            is_pinned;

    /* hotkey */
    int             hotkey_id;
    UINT            hotkey_modifiers;
    UINT            hotkey_vk;

    /* data */
    ClipItem       *items;
    int             item_count;
    int             item_capacity;
    int            *filtered;      /* indices into items[] */
    int             filtered_count;

    /* UI state */
    TabIndex        active_tab;
    wchar_t         search_query[MAX_SEARCH_LEN];
    bool            search_focused;
    int             scroll_offset;
    int             hover_index;
    int             selected_index;
    int             copied_index;   /* flash "copied" indicator */
    DWORD           copied_time;

    /* cached GDI resources */
    ULONG_PTR       gdiplus_token;
    HFONT           font_normal;
    HFONT           font_bold;
    HFONT           font_small;
    HBRUSH          brush_bg;
    HBRUSH          brush_surface;
    HBRUSH          brush_hover;
    HBRUSH          brush_search;
    HPEN            pen_border;
    HPEN            pen_accent;

    /* back buffer */
    HDC             mem_dc;
    HBITMAP         mem_bmp;
    int             buf_w, buf_h;

    Config          config;
    sqlite3        *db;
    bool            self_write;     /* ignore own clipboard writes */
    wchar_t         app_data_path[MAX_PATH];
    wchar_t         images_path[MAX_PATH];
    wchar_t         ini_path[MAX_PATH];
    wchar_t         db_path[MAX_PATH];
} AppState;

/* Global app state (defined in main.c) */
extern AppState g_app;

/* ──────────────────── config.c ──────────────────── */
void config_load(Config *cfg);
void config_save(const Config *cfg);
bool config_set_autostart(bool enable);
bool config_get_autostart(void);

/* ──────────────────── utils.c ──────────────────── */
void     utils_get_appdata_path(wchar_t *out, const wchar_t *subdir);
void     utils_ensure_directory(const wchar_t *path);
void     utils_sha256(const void *data, size_t len, char *out_hex);
void     utils_format_time(int64_t epoch_ms, wchar_t *out, size_t out_len);
void     utils_truncate_text(const wchar_t *src, size_t max_chars, wchar_t *out, size_t out_len);
int64_t  utils_current_time_ms(void);
void     utils_log(const wchar_t *fmt, ...);
void     utils_init_fonts(void);
void     utils_destroy_fonts(void);
void     utils_create_brushes(void);
void     utils_destroy_brushes(void);

/* ──────────────────── db.c ──────────────────── */
int      db_open(const wchar_t *path);
void     db_close(void);
int      db_insert_item(ClipItem *item);
int      db_update_timestamp(int64_t id, int64_t new_ts);
int      db_load_items(int offset, int limit);
int      db_load_filtered(ClipType filter_type, bool favorites_only, const wchar_t *search);
void     db_toggle_favorite(int64_t id);
int      db_delete_item(int64_t id);
int      db_clear_all(void);
int      db_run_retention(int days);
int      db_get_count(void);

/* ──────────────────── image.c ──────────────────── */
void     image_init(void);
void     image_shutdown(void);
int      image_save_from_clipboard(const wchar_t *out_path);
HBITMAP  image_load_thumbnail(const wchar_t *path, int w, int h);
HBITMAP  image_load_full(const wchar_t *path, int *out_w, int *out_h);
int      image_get_dimensions(const wchar_t *path, int *w, int *h);

/* ──────────────────── clipboard.c ──────────────────── */
void     clipboard_init(HWND hwnd);
void     clipboard_shutdown(void);
void     clipboard_handle_update(void);
void     clipboard_copy_item(ClipItem *item);

/* ──────────────────── hotkey.c ──────────────────── */
bool     hotkey_register(HWND hwnd, UINT modifiers, UINT vk);
void     hotkey_unregister(HWND hwnd);
bool     hotkey_parse(const wchar_t *str, UINT *out_mod, UINT *out_vk);
void     hotkey_to_string(UINT mod, UINT vk, wchar_t *out, size_t len);

/* ──────────────────── ui_tray.c ──────────────────── */
void     tray_create(HWND hwnd);
void     tray_destroy(void);
void     tray_show_context_menu(void);
void     tray_handle_wmapp(WPARAM wp, LPARAM lp);
void     tray_handle_command(WPARAM wp);

/* ──────────────────── ui_main.c ──────────────────── */
void     popup_create(void);
void     popup_destroy(void);
void     popup_toggle(void);
void     popup_show(void);
void     popup_hide(void);
void     popup_rebuild_filter(void);
LRESULT CALLBACK popup_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

/* ──────────────────── ui_controls.c ──────────────────── */
void     ctrl_draw_search_bar(HDC hdc, RECT *rc, const wchar_t *query, bool focused);
void     ctrl_draw_tab_bar(HDC hdc, RECT *rc, TabIndex active);
void     ctrl_draw_item_row(HDC hdc, RECT *rc, ClipItem *item, int index, bool hovered, bool copied);
void     ctrl_draw_scrollbar(HDC hdc, RECT *rc, int offset, int total_h, int visible_h);
void     ctrl_draw_empty_state(HDC hdc, RECT *rc, const wchar_t *text);

/* Hit-testing helpers */
bool     ctrl_hit_search(int x, int y, RECT *rc);
bool     ctrl_hit_pin(int x, int y, RECT *rc);
bool     ctrl_hit_settings(int x, int y, RECT *rc);
TabIndex ctrl_hit_tab(int x, int y, RECT *rc);
int      ctrl_hit_item(int x, int y, RECT *rc, int scroll, int count);
bool     ctrl_hit_star(int x, int y, RECT *rc, int item_index, int scroll);

/* ──────────────────── ui_preview.c ──────────────────── */
void     preview_show(const wchar_t *image_path);
void     preview_close(void);

/* ──────────────────── ui_settings.c ──────────────────── */
void     settings_show(void);
void     settings_close(void);

/* ──────────────────── Item helpers ──────────────────── */
void     item_free(ClipItem *item);
void     items_clear(void);
ClipItem* items_add(void);

#endif /* APP_H */
