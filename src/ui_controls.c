#include "app.h"

/* ──── Layout constants ──── */
#define SEARCH_LEFT   MARGIN
#define ICON_BTN_SIZE 32

static const wchar_t *tab_names[TAB_COUNT] = {
    L"\x5168\x90E8",     /* 全部 */
    L"\x6587\x672C",     /* 文本 */
    L"\x56FE\x7247",     /* 图片 */
    L"\x6587\x4EF6",     /* 文件 */
    L"\x6536\x85CF",     /* 收藏 */
};

static RECT search_rect, tab_rect, pin_rect, settings_rect;
static RECT tab_item_rects[TAB_COUNT];

/* ──── Draw a rounded rectangle ──── */
static void draw_rounded_rect(HDC hdc, RECT *rc, int radius, HBRUSH brush, HPEN pen) {
    HBRUSH old_brush = SelectObject(hdc, brush);
    HPEN old_pen = SelectObject(hdc, pen ? pen : GetStockObject(NULL_PEN));
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, radius, radius);
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
}

/* ──── Search bar ──── */
void ctrl_draw_search_bar(HDC hdc, RECT *parent_rc, const wchar_t *query, bool focused) {
    (void)focused;
    int w = parent_rc->right - parent_rc->left;
    search_rect.left = parent_rc->left + MARGIN;
    search_rect.top = parent_rc->top + SEARCH_TOP;
    search_rect.right = parent_rc->left + w - MARGIN - ICON_BTN_SIZE * 2 - 8;
    search_rect.bottom = search_rect.top + SEARCH_H;

    /* Pin icon rect */
    pin_rect.left = search_rect.right + 4;
    pin_rect.top = search_rect.top + 2;
    pin_rect.right = pin_rect.left + ICON_BTN_SIZE;
    pin_rect.bottom = pin_rect.top + ICON_BTN_SIZE;

    /* Settings icon rect */
    settings_rect.left = pin_rect.right + 4;
    settings_rect.top = search_rect.top + 2;
    settings_rect.right = settings_rect.left + ICON_BTN_SIZE;
    settings_rect.bottom = settings_rect.top + ICON_BTN_SIZE;

    /* Draw search bar background */
    draw_rounded_rect(hdc, &search_rect, 10, g_app.brush_search, NULL);

    /* Magnifying glass icon (simple circle + line) */
    HPEN old_pen = SelectObject(hdc, CreatePen(PS_SOLID, 2, COLOR_TEXT_SEC));
    HBRUSH old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    int iconX = search_rect.left + 12;
    int iconY = search_rect.top + 10;
    Ellipse(hdc, iconX, iconY, iconX + 12, iconY + 12);
    MoveToEx(hdc, iconX + 10, iconY + 10, NULL);
    LineTo(hdc, iconX + 16, iconY + 16);
    DeleteObject(SelectObject(hdc, old_pen));
    SelectObject(hdc, old_brush);

    /* Search text or placeholder */
    RECT text_rc = search_rect;
    text_rc.left += 36;
    text_rc.right -= 8;
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, g_app.font_normal);

    if (query && query[0]) {
        SetTextColor(hdc, COLOR_TEXT);
        DrawTextW(hdc, query, -1, &text_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else {
        SetTextColor(hdc, COLOR_TEXT_SEC);
        DrawTextW(hdc, L"\x641C\x7D22", -1, &text_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    /* Pin icon (simple drawing) */
    SelectObject(hdc, g_app.font_small);
    SetTextColor(hdc, g_app.is_pinned ? COLOR_ACCENT : COLOR_TEXT_SEC);
    RECT pin_text = pin_rect;
    DrawTextW(hdc, L"\x25C9", -1, &pin_text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* Settings gear icon */
    SetTextColor(hdc, COLOR_TEXT_SEC);
    RECT gear_text = settings_rect;
    DrawTextW(hdc, L"\x2699", -1, &gear_text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/* ──── Tab bar ──── */
void ctrl_draw_tab_bar(HDC hdc, RECT *parent_rc, TabIndex active) {
    int w = parent_rc->right - parent_rc->left;
    tab_rect.left = parent_rc->left + MARGIN;
    tab_rect.top = parent_rc->top + TAB_TOP;
    tab_rect.right = parent_rc->left + w - MARGIN;
    tab_rect.bottom = tab_rect.top + TAB_H;

    /* Draw bottom border line */
    HPEN old_pen = SelectObject(hdc, CreatePen(PS_SOLID, 1, COLOR_BORDER));
    MoveToEx(hdc, tab_rect.left, tab_rect.bottom, NULL);
    LineTo(hdc, tab_rect.right, tab_rect.bottom);
    DeleteObject(SelectObject(hdc, old_pen));

    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, g_app.font_normal);

    int x = tab_rect.left + 4;
    for (int i = 0; i < TAB_COUNT; i++) {
        SIZE sz;
        GetTextExtentPoint32W(hdc, tab_names[i], (int)wcslen(tab_names[i]), &sz);
        int tw = sz.cx + 16;

        tab_item_rects[i].left = x;
        tab_item_rects[i].top = tab_rect.top;
        tab_item_rects[i].right = x + tw;
        tab_item_rects[i].bottom = tab_rect.bottom;

        if (i == (int)active) {
            SetTextColor(hdc, COLOR_ACCENT);
            /* Draw active indicator line */
            HPEN accent_pen = CreatePen(PS_SOLID, 2, COLOR_ACCENT);
            HPEN saved = SelectObject(hdc, accent_pen);
            MoveToEx(hdc, x + 4, tab_rect.bottom - 1, NULL);
            LineTo(hdc, x + tw - 4, tab_rect.bottom - 1);
            DeleteObject(SelectObject(hdc, saved));
        } else {
            SetTextColor(hdc, COLOR_TEXT_SEC);
        }

        RECT trc = tab_item_rects[i];
        DrawTextW(hdc, tab_names[i], -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        x += tw + 4;
    }
}

/* ──── Item row ──── */
void ctrl_draw_item_row(HDC hdc, RECT *parent_rc, ClipItem *item, int index, bool hovered, bool copied) {
    int w = parent_rc->right - parent_rc->left;
    int y = parent_rc->top + LIST_TOP + (index * ITEM_HEIGHT) - g_app.scroll_offset;

    RECT row_rc;
    row_rc.left = parent_rc->left + MARGIN;
    row_rc.top = y;
    row_rc.right = parent_rc->left + w - MARGIN;
    row_rc.bottom = y + ITEM_HEIGHT - 4;

    /* Skip if offscreen */
    if (row_rc.bottom < LIST_TOP || row_rc.top > parent_rc->bottom) return;

    /* Background */
    HBRUSH bg = hovered ? g_app.brush_hover : g_app.brush_surface;
    draw_rounded_rect(hdc, &row_rc, 8, bg, NULL);

    /* Copied flash */
    if (copied) {
        HBRUSH flash = CreateSolidBrush(COLOR_COPIED);
        RECT flash_rc = row_rc;
        flash_rc.left = flash_rc.right - 60;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        SelectObject(hdc, g_app.font_small);
        DrawTextW(hdc, L"\x5DF2\x590D\x5236", -1, &flash_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DeleteObject(flash);
    }

    SetBkMode(hdc, TRANSPARENT);
    int content_left = row_rc.left + 8;

    /* Type icon / thumbnail */
    if (item->type == CLIP_IMAGE && item->thumb_cache) {
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP old_bmp = SelectObject(memDC, item->thumb_cache);
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        AlphaBlend(hdc, content_left, y + 8, THUMB_SIZE, THUMB_SIZE,
                   memDC, 0, 0, THUMB_SIZE, THUMB_SIZE, bf);
        SelectObject(memDC, old_bmp);
        DeleteDC(memDC);
        content_left += THUMB_SIZE + 8;
    } else if (item->type == CLIP_IMAGE) {
        /* No thumbnail yet - draw placeholder */
        RECT icon_rc = { content_left, y + 8, content_left + THUMB_SIZE, y + 8 + THUMB_SIZE };
        HBRUSH icon_bg = CreateSolidBrush(COLOR_SEARCH_BG);
        FillRect(hdc, &icon_rc, icon_bg);
        DeleteObject(icon_bg);
        SetTextColor(hdc, COLOR_TEXT_SEC);
        SelectObject(hdc, g_app.font_small);
        DrawTextW(hdc, L"\x56FE", -1, &icon_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        content_left += THUMB_SIZE + 8;
    } else {
        /* Type indicator */
        const wchar_t *type_icon;
        switch (item->type) {
            case CLIP_TEXT: type_icon = L"\x6587"; break; /* 文 */
            case CLIP_FILE: type_icon = L"\x4EF6"; break; /* 件 */
            default: type_icon = L"?"; break;
        }
        RECT icon_rc = { content_left, y + 12, content_left + 28, y + 40 };
        draw_rounded_rect(hdc, &icon_rc, 6, g_app.brush_search, NULL);
        SetTextColor(hdc, COLOR_ACCENT);
        SelectObject(hdc, g_app.font_normal);
        DrawTextW(hdc, type_icon, -1, &icon_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        content_left += 36;
    }

    /* Preview text */
    RECT text_rc;
    text_rc.left = content_left;
    text_rc.top = y + 6;
    text_rc.right = row_rc.right - 40;
    text_rc.bottom = y + ITEM_HEIGHT / 2 + 2;

    SetTextColor(hdc, COLOR_TEXT);
    SelectObject(hdc, g_app.font_normal);
    DrawTextW(hdc, item->preview ? item->preview : L"", -1, &text_rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    /* Type label + time */
    const wchar_t *type_label;
    switch (item->type) {
        case CLIP_TEXT:  type_label = L"\x6587\x672C"; break;
        case CLIP_IMAGE: type_label = L"\x56FE\x7247"; break;
        case CLIP_FILE:  type_label = L"\x6587\x4EF6"; break;
        default: type_label = L""; break;
    }

    wchar_t time_buf[64];
    utils_format_time(item->timestamp, time_buf, 64);

    wchar_t meta[128];
    wsprintfW(meta, L"%s \x00B7 %s", type_label, time_buf);

    RECT meta_rc;
    meta_rc.left = content_left;
    meta_rc.top = y + ITEM_HEIGHT / 2 - 2;
    meta_rc.right = row_rc.right - 40;
    meta_rc.bottom = row_rc.bottom - 4;

    SetTextColor(hdc, COLOR_TEXT_SEC);
    SelectObject(hdc, g_app.font_small);
    DrawTextW(hdc, meta, -1, &meta_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    /* Favorite star */
    if (item->is_favorite) {
        SetTextColor(hdc, COLOR_STAR);
        SelectObject(hdc, g_app.font_normal);
        RECT star_rc = { row_rc.right - 30, y + 4, row_rc.right - 4, y + 30 };
        DrawTextW(hdc, L"\x2605", -1, &star_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

/* ──── Scrollbar ──── */
void ctrl_draw_scrollbar(HDC hdc, RECT *parent_rc, int offset, int total_h, int visible_h) {
    if (total_h <= visible_h) return;

    int w = parent_rc->right - parent_rc->left;
    int track_top = LIST_TOP;
    int track_bottom = parent_rc->bottom - 4;
    int track_h = track_bottom - track_top;
    int track_left = parent_rc->left + w - MARGIN - 4;

    /* Track */
    HPEN null_pen = GetStockObject(NULL_PEN);
    HPEN old_pen = SelectObject(hdc, null_pen);
    HBRUSH track_brush = CreateSolidBrush(RGB(30, 30, 55));
    RECT track_rc = { track_left, track_top, track_left + 4, track_bottom };
    FillRect(hdc, &track_rc, track_brush);
    DeleteObject(track_brush);

    /* Thumb */
    float ratio = (float)visible_h / total_h;
    int thumb_h = (int)(track_h * ratio);
    if (thumb_h < 20) thumb_h = 20;
    int thumb_y = track_top + (int)((float)offset / total_h * track_h);

    HBRUSH thumb_brush = CreateSolidBrush(COLOR_TEXT_SEC);
    RECT thumb_rc = { track_left, thumb_y, track_left + 4, thumb_y + thumb_h };
    FillRect(hdc, &thumb_rc, thumb_brush);
    DeleteObject(thumb_brush);
    SelectObject(hdc, old_pen);
}

/* ──── Empty state ──── */
void ctrl_draw_empty_state(HDC hdc, RECT *rc, const wchar_t *text) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, COLOR_TEXT_SEC);
    SelectObject(hdc, g_app.font_normal);
    DrawTextW(hdc, text, -1, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

/* ──── Hit testing ──── */
bool ctrl_hit_search(int x, int y, RECT *parent_rc) {
    (void)parent_rc;
    return PtInRect(&search_rect, (POINT){ x, y });
}

bool ctrl_hit_pin(int x, int y, RECT *parent_rc) {
    (void)parent_rc;
    return PtInRect(&pin_rect, (POINT){ x, y });
}

bool ctrl_hit_settings(int x, int y, RECT *parent_rc) {
    (void)parent_rc;
    return PtInRect(&settings_rect, (POINT){ x, y });
}

TabIndex ctrl_hit_tab(int x, int y, RECT *parent_rc) {
    (void)parent_rc;
    POINT pt = { x, y };
    for (int i = 0; i < TAB_COUNT; i++) {
        if (PtInRect(&tab_item_rects[i], pt))
            return (TabIndex)i;
    }
    return -1;
}

int ctrl_hit_item(int x, int y, RECT *parent_rc, int scroll, int count) {
    int w = parent_rc->right - parent_rc->left;
    for (int i = 0; i < count; i++) {
        int iy = LIST_TOP + i * ITEM_HEIGHT - scroll;
        RECT row_rc = { parent_rc->left + MARGIN, iy,
                        parent_rc->left + w - MARGIN, iy + ITEM_HEIGHT - 4 };
        if (PtInRect(&row_rc, (POINT){ x, y }))
            return i;
    }
    return -1;
}

bool ctrl_hit_star(int x, int y, RECT *parent_rc, int item_index, int scroll) {
    int w = parent_rc->right - parent_rc->left;
    int iy = LIST_TOP + item_index * ITEM_HEIGHT - scroll;
    RECT star_rc = { parent_rc->left + w - MARGIN - 30, iy + 4,
                     parent_rc->left + w - MARGIN - 4, iy + 30 };
    return PtInRect(&star_rc, (POINT){ x, y });
}
