#include "app.h"

void clipboard_init(HWND hwnd) {
    AddClipboardFormatListener(hwnd);
}

void clipboard_shutdown(void) {
    RemoveClipboardFormatListener(g_app.hwndMain);
}

static bool open_clipboard_retry(HWND owner) {
    for (int i = 0; i < 3; i++) {
        if (OpenClipboard(owner)) return true;
        Sleep(50 << i); /* 50, 100, 200 ms */
    }
    return false;
}

/* Extract text from clipboard. Returns malloc'd string or NULL. */
static wchar_t *clip_get_text(void) {
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        hData = GetClipboardData(CF_TEXT);
        if (!hData) return NULL;
        char *text = (char *)GlobalLock(hData);
        if (!text) return NULL;
        int len = MultiByteToWideChar(CP_ACP, 0, text, -1, NULL, 0);
        wchar_t *wtext = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
        if (wtext) MultiByteToWideChar(CP_ACP, 0, text, -1, wtext, len);
        GlobalUnlock(hData);
        return wtext;
    }
    wchar_t *text = (wchar_t *)GlobalLock(hData);
    if (!text) return NULL;
    size_t len = wcslen(text);
    wchar_t *copy = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (copy) wcscpy(copy, text);
    GlobalUnlock(hData);
    return copy;
}

/* Extract file paths from clipboard (CF_HDROP). Returns malloc'd string. */
static wchar_t *clip_get_files(void) {
    HANDLE hData = GetClipboardData(CF_HDROP);
    if (!hData) return NULL;
    HDROP hDrop = (HDROP)hData;

    /* Get total size needed */
    UINT size = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
    if (size == 0) return NULL;

    /* Get first file path for display purposes */
    wchar_t path[MAX_PATH];
    if (DragQueryFileW(hDrop, 0, path, MAX_PATH) == 0) return NULL;

    UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);

    /* Build a combined string of all file paths */
    wchar_t *combined = (wchar_t *)malloc(MAX_PATH * sizeof(wchar_t));
    if (!combined) return NULL;
    wcscpy(combined, path);

    if (fileCount > 1) {
        wchar_t extra[MAX_PATH];
        for (UINT i = 1; i < fileCount; i++) {
            if (DragQueryFileW(hDrop, i, extra, MAX_PATH)) {
                size_t cur_len = wcslen(combined);
                size_t add_len = wcslen(extra);
                wchar_t *newbuf = (wchar_t *)realloc(combined,
                    (cur_len + add_len + 3) * sizeof(wchar_t));
                if (newbuf) {
                    combined = newbuf;
                    wcscat(combined, L"\n");
                    wcscat(combined, extra);
                }
            }
        }
    }
    return combined;
}

static void handle_clip_text(wchar_t *text) {
    if (!text || text[0] == L'\0') { free(text); return; }

    /* Compute hash for dedup */
    char hash[65];
    utils_sha256(text, wcslen(text) * sizeof(wchar_t), hash);

    /* Check if duplicate exists in DB */
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_app.db,
        "SELECT id FROM clip_items WHERE data_hash=?;", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, hash, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t existing_id = sqlite3_column_int64(stmt, 0);
            sqlite3_finalize(stmt);
            /* Update timestamp of existing item */
            db_update_timestamp(existing_id, utils_current_time_ms());
            free(text);
            return;
        }
        sqlite3_finalize(stmt);
    }

    /* New item */
    ClipItem item = { 0 };
    item.type = CLIP_TEXT;
    item.content = text;
    item.timestamp = utils_current_time_ms();
    strncpy(item.data_hash, hash, 64);
    item.data_hash[64] = '\0';

    wchar_t preview[MAX_PREVIEW_LEN + 1];
    utils_truncate_text(text, MAX_PREVIEW_LEN, preview, MAX_PREVIEW_LEN + 1);
    item.preview = preview;

    db_insert_item(&item);

    /* Add to in-memory list */
    ClipItem *mem_item = items_add();
    if (mem_item) {
        *mem_item = item;
        mem_item->content = _wcsdup(text);
        mem_item->preview = _wcsdup(preview);
    }
}

static void handle_clip_image(void) {
    /* Generate file path */
    int64_t ts = utils_current_time_ms();
    wchar_t filename[256];
    wsprintfW(filename, L"%lld.png", (long long)ts);

    wchar_t full_path[MAX_PATH];
    wsprintfW(full_path, L"%s\\%s", g_app.images_path, filename);

    if (image_save_from_clipboard(full_path) != 0) return;

    /* Hash the file path as dedup key for images */
    char hash[65];
    /* Read first few KB of the file for hash */
    HANDLE hFile = CreateFileW(full_path, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    DWORD fileSize = GetFileSize(hFile, NULL);
    DWORD toRead = (fileSize > 65536) ? 65536 : fileSize;
    BYTE *buf = (BYTE *)malloc(toRead);
    DWORD read = 0;
    ReadFile(hFile, buf, toRead, &read, NULL);
    CloseHandle(hFile);

    if (buf) {
        utils_sha256(buf, read, hash);
        free(buf);
    } else {
        utils_sha256(full_path, wcslen(full_path) * sizeof(wchar_t), hash);
    }

    /* Check duplicate */
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_app.db,
        "SELECT id FROM clip_items WHERE data_hash=?;", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, hash, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t existing_id = sqlite3_column_int64(stmt, 0);
            sqlite3_finalize(stmt);
            db_update_timestamp(existing_id, ts);
            DeleteFileW(full_path); /* Remove duplicate file */
            return;
        }
        sqlite3_finalize(stmt);
    }

    ClipItem item = { 0 };
    item.type = CLIP_IMAGE;
    item.file_path = full_path;
    item.timestamp = ts;
    item.file_size = (int64_t)fileSize;
    memcpy(item.data_hash, hash, 65); /* Copy full hash including null terminator */

    wchar_t preview[MAX_PREVIEW_LEN + 1];
    int iw = 0, ih = 0;
    image_get_dimensions(full_path, &iw, &ih);
    wsprintfW(preview, L"\x56FE\x7247 %dx%d", iw, ih); /* 图片 WxH */
    item.preview = preview;

    db_insert_item(&item);

    ClipItem *mem_item = items_add();
    if (mem_item) {
        *mem_item = item;
        mem_item->file_path = _wcsdup(full_path);
        mem_item->preview = _wcsdup(preview);
    }
}

static void handle_clip_files(wchar_t *files) {
    if (!files) return;

    char hash[65];
    utils_sha256(files, wcslen(files) * sizeof(wchar_t), hash);

    /* Check duplicate */
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_app.db,
        "SELECT id FROM clip_items WHERE data_hash=?;", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, hash, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t existing_id = sqlite3_column_int64(stmt, 0);
            sqlite3_finalize(stmt);
            db_update_timestamp(existing_id, utils_current_time_ms());
            free(files);
            return;
        }
        sqlite3_finalize(stmt);
    }

    ClipItem item = { 0 };
    item.type = CLIP_FILE;
    item.content = files;
    item.file_path = files;
    item.timestamp = utils_current_time_ms();
    strncpy(item.data_hash, hash, 64);
    item.data_hash[64] = '\0';

    wchar_t preview[MAX_PREVIEW_LEN + 1];
    utils_truncate_text(files, MAX_PREVIEW_LEN, preview, MAX_PREVIEW_LEN + 1);
    item.preview = preview;

    db_insert_item(&item);

    ClipItem *mem_item = items_add();
    if (mem_item) {
        *mem_item = item;
        mem_item->content = _wcsdup(files);
        mem_item->file_path = _wcsdup(files);
        mem_item->preview = _wcsdup(preview);
    }
}

void clipboard_handle_update(void) {
    if (g_app.self_write) {
        g_app.self_write = false;
        return;
    }

    if (!open_clipboard_retry(g_app.hwndMain)) return;

    /* Check formats in priority order */
    if (IsClipboardFormatAvailable(CF_HDROP)) {
        wchar_t *files = clip_get_files();
        CloseClipboard();
        handle_clip_files(files);
    } else if (IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_BITMAP)) {
        /* Only save image if no text is available (prefer text over image) */
        if (!IsClipboardFormatAvailable(CF_UNICODETEXT) && !IsClipboardFormatAvailable(CF_TEXT)) {
            CloseClipboard();
            handle_clip_image();
        } else {
            /* Both image and text: save as text */
            wchar_t *text = clip_get_text();
            CloseClipboard();
            handle_clip_text(text);
        }
    } else if (IsClipboardFormatAvailable(CF_UNICODETEXT) || IsClipboardFormatAvailable(CF_TEXT)) {
        wchar_t *text = clip_get_text();
        CloseClipboard();
        handle_clip_text(text);
    } else {
        CloseClipboard();
    }

    /* Refresh popup if visible */
    if (g_app.popup_visible && g_app.hwndPopup) {
        popup_rebuild_filter();
        InvalidateRect(g_app.hwndPopup, NULL, TRUE);
    }
}

void clipboard_copy_item(ClipItem *item) {
    if (!item) return;
    if (!open_clipboard_retry(g_app.hwndPopup)) return;

    EmptyClipboard();

    switch (item->type) {
    case CLIP_TEXT:
        if (item->content) {
            size_t len = (wcslen(item->content) + 1) * sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
            if (hMem) {
                wchar_t *dst = (wchar_t *)GlobalLock(hMem);
                memcpy(dst, item->content, len);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
        break;

    case CLIP_IMAGE:
        if (item->file_path) {
            GpImage *gpImage = NULL;
            if (GdipLoadImageFromFile(item->file_path, &gpImage) == Ok && gpImage) {
                HBITMAP hbmp = NULL;
                GpBitmap *gpBmp = (GpBitmap *)gpImage;
                GdipCreateHBITMAPFromBitmap(gpBmp, &hbmp, 0);
                if (hbmp) {
                    /* Build DIB from HBITMAP */
                    BITMAP bm;
                    GetObject(hbmp, sizeof(bm), &bm);
                    HDC hdc = GetDC(NULL);
                    int bmpSize = (int)(((bm.bmWidth * 32 + 31) / 32) * 4 * bm.bmHeight);
                    BITMAPINFOHEADER bih = { 0 };
                    bih.biSize = sizeof(bih);
                    bih.biWidth = bm.bmWidth;
                    bih.biHeight = -bm.bmHeight;
                    bih.biPlanes = 1;
                    bih.biBitCount = 32;
                    bih.biCompression = BI_RGB;
                    bih.biSizeImage = bmpSize;

                    HGLOBAL hDib = GlobalAlloc(GMEM_MOVEABLE, sizeof(bih) + bmpSize);
                    if (hDib) {
                        BYTE *pDib = (BYTE *)GlobalLock(hDib);
                        memcpy(pDib, &bih, sizeof(bih));
                        GetDIBits(hdc, hbmp, 0, bm.bmHeight,
                                  pDib + sizeof(bih), (BITMAPINFO *)&bih, DIB_RGB_COLORS);
                        GlobalUnlock(hDib);
                        SetClipboardData(CF_DIB, hDib);
                    }
                    ReleaseDC(NULL, hdc);
                    DeleteObject(hbmp);
                }
                GdipDisposeImage(gpImage);
            }
        }
        break;

    case CLIP_FILE:
        /* For files, we can't easily put the actual file on clipboard,
           so we just copy the path as text */
        if (item->content) {
            size_t len = (wcslen(item->content) + 1) * sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
            if (hMem) {
                wchar_t *dst = (wchar_t *)GlobalLock(hMem);
                memcpy(dst, item->content, len);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
        break;
    }

    g_app.self_write = true;
    CloseClipboard();
}
