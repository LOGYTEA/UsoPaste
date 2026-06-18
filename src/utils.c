#include "app.h"
#include <bcrypt.h>
#include <stdarg.h>

#ifdef _MSC_VER
#pragma comment(lib, "bcrypt.lib")
#endif

static FILE *g_log_file = NULL;

void utils_get_appdata_path(wchar_t *out, const wchar_t *subdir) {
    wchar_t base[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, base);
    if (subdir && subdir[0])
        wsprintfW(out, L"%s\\%s\\%s", base, APP_NAME, subdir);
    else
        wsprintfW(out, L"%s\\%s", base, APP_NAME);
}

void utils_ensure_directory(const wchar_t *path) {
    CreateDirectoryW(path, NULL);
}

/* ──── SHA-256 via Windows CNG ──── */
void utils_sha256(const void *data, size_t len, char *out_hex) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    UCHAR hash[32];
    DWORD hash_len = 0, cb_data = 0;

    out_hex[0] = '\0';

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0)
        return;
    if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_len,
                          sizeof(hash_len), &cb_data, 0) != 0 || hash_len != 32) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return;
    }
    if (BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return;
    }
    if (BCryptHashData(hHash, (PUCHAR)data, (ULONG)len, 0) != 0) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return;
    }
    if (BCryptFinishHash(hHash, hash, 32, 0) != 0) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return;
    }
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    for (int i = 0; i < 32; i++)
        wsprintfA(out_hex + i * 2, "%02x", hash[i]);
    out_hex[64] = '\0';
}

/* ──── Time formatting (Chinese relative) ──── */
void utils_format_time(int64_t epoch_ms, wchar_t *out, size_t out_len) {
    int64_t now = utils_current_time_ms();
    int64_t diff = (now - epoch_ms) / 1000; /* seconds */

    if (diff < 60) {
        wcsncpy(out, L"\x521A\x521A", out_len); /* 刚刚 */
    } else if (diff < 3600) {
        wsprintfW(out, L"%d\x5206\x949F\x524D", (int)(diff / 60)); /* N分钟前 */
    } else if (diff < 86400) {
        wsprintfW(out, L"%d\x5C0F\x65F6\x524D", (int)(diff / 3600)); /* N小时前 */
    } else if (diff < 172800) {
        wcsncpy(out, L"\x6628\x5929", out_len); /* 昨天 */
    } else if (diff < 604800) {
        wsprintfW(out, L"%d\x5929\x524D", (int)(diff / 86400)); /* N天前 */
    } else {
        /* YYYY-MM-DD */
        FILETIME ft;
        ULARGE_INTEGER uli;
        uli.QuadPart = (ULONGLONG)(epoch_ms * 10000 + 116444736000000000LL);
        ft.dwLowDateTime = uli.LowPart;
        ft.dwHighDateTime = uli.HighPart;
        SYSTEMTIME st;
        FileTimeToSystemTime(&ft, &st);
        wsprintfW(out, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    }
}

void utils_truncate_text(const wchar_t *src, size_t max_chars, wchar_t *out, size_t out_len) {
    if (!src) { out[0] = L'\0'; return; }
    size_t slen = wcslen(src);
    /* Replace newlines with spaces for display */
    size_t j = 0;
    for (size_t i = 0; i < slen && j < max_chars && j < out_len - 1; i++) {
        if (src[i] == L'\n' || src[i] == L'\r')
            out[j++] = L' ';
        else
            out[j++] = src[i];
    }
    if (slen > max_chars && j >= max_chars) {
        out[j - 1] = L'.';
        if (j >= 2) out[j - 2] = L'.';
        if (j >= 3) out[j - 3] = L'.';
    }
    out[j] = L'\0';
}

int64_t utils_current_time_ms(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    /* FILETIME is 100-nanosecond intervals since 1601-01-01 */
    /* Unix epoch is 116444736000000000 * 100ns after 1601-01-01 */
    int64_t unix_ms = (int64_t)((uli.QuadPart - 116444736000000000ULL) / 10000);
    return unix_ms;
}

void utils_log(const wchar_t *fmt, ...) {
    if (!g_log_file) {
        wchar_t path[MAX_PATH];
        utils_get_appdata_path(path, L"debug.log");
        g_log_file = _wfopen(path, L"a");
        if (!g_log_file) return;
    }
    va_list args;
    va_start(args, fmt);
    vfwprintf(g_log_file, fmt, args);
    va_end(args);
    fwprintf(g_log_file, L"\n");
    fflush(g_log_file);
}

void utils_init_fonts(void) {
    g_app.font_normal = CreateFontW(
        -MulDiv(10, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (!g_app.font_normal)
        g_app.font_normal = CreateFontW(
            -MulDiv(10, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72),
            0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

    g_app.font_bold = CreateFontW(
        -MulDiv(10, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72),
        0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    g_app.font_small = CreateFontW(
        -MulDiv(8, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void utils_destroy_fonts(void) {
    if (g_app.font_normal) { DeleteObject(g_app.font_normal); g_app.font_normal = NULL; }
    if (g_app.font_bold)   { DeleteObject(g_app.font_bold);   g_app.font_bold = NULL; }
    if (g_app.font_small)  { DeleteObject(g_app.font_small);  g_app.font_small = NULL; }
}

void utils_create_brushes(void) {
    g_app.brush_bg      = CreateSolidBrush(COLOR_BG);
    g_app.brush_surface = CreateSolidBrush(COLOR_SURFACE);
    g_app.brush_hover   = CreateSolidBrush(COLOR_HOVER);
    g_app.brush_search  = CreateSolidBrush(COLOR_SEARCH_BG);
    g_app.pen_border    = CreatePen(PS_SOLID, 1, COLOR_BORDER);
    g_app.pen_accent    = CreatePen(PS_SOLID, 2, COLOR_ACCENT);
}

void utils_destroy_brushes(void) {
    if (g_app.brush_bg)      { DeleteObject(g_app.brush_bg);      g_app.brush_bg = NULL; }
    if (g_app.brush_surface) { DeleteObject(g_app.brush_surface); g_app.brush_surface = NULL; }
    if (g_app.brush_hover)   { DeleteObject(g_app.brush_hover);   g_app.brush_hover = NULL; }
    if (g_app.brush_search)  { DeleteObject(g_app.brush_search);  g_app.brush_search = NULL; }
    if (g_app.pen_border)    { DeleteObject(g_app.pen_border);    g_app.pen_border = NULL; }
    if (g_app.pen_accent)    { DeleteObject(g_app.pen_accent);    g_app.pen_accent = NULL; }
    if (g_app.mem_dc)        { DeleteDC(g_app.mem_dc);            g_app.mem_dc = NULL; }
    if (g_app.mem_bmp)       { DeleteObject(g_app.mem_bmp);       g_app.mem_bmp = NULL; }
    if (g_log_file)          { fclose(g_log_file);                g_log_file = NULL; }
}
