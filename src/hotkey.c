#include "app.h"

bool hotkey_register(HWND hwnd, UINT modifiers, UINT vk) {
    UnregisterHotKey(hwnd, HOTKEY_ID);
    if (modifiers == 0) return false; /* Must have at least one modifier */
    modifiers |= MOD_NOREPEAT;
    return RegisterHotKey(hwnd, HOTKEY_ID, modifiers, vk);
}

void hotkey_unregister(HWND hwnd) {
    UnregisterHotKey(hwnd, HOTKEY_ID);
}

/* Parse a hotkey string like "Ctrl+Alt+V" or "Win+Shift+A" into modifiers + vk */
bool hotkey_parse(const wchar_t *str, UINT *out_mod, UINT *out_vk) {
    if (!str || !out_mod || !out_vk) return false;
    *out_mod = 0;
    *out_vk = 0;

    /* Make a working copy */
    wchar_t buf[128];
    wcsncpy(buf, str, 127);
    buf[127] = L'\0';

    /* Tokenize by '+' */
    wchar_t *tokens[8] = { 0 };
    int count = 0;
    wchar_t *p = buf;
    tokens[count++] = p;
    while (*p && count < 8) {
        if (*p == L'+') {
            *p = L'\0';
            tokens[count++] = p + 1;
        }
        p++;
    }

    if (count < 2) return false; /* Need at least modifier + key */

    for (int i = 0; i < count - 1; i++) {
        wchar_t *t = tokens[i];
        if (_wcsicmp(t, L"Ctrl") == 0 || _wcsicmp(t, L"Control") == 0)
            *out_mod |= MOD_CONTROL;
        else if (_wcsicmp(t, L"Alt") == 0)
            *out_mod |= MOD_ALT;
        else if (_wcsicmp(t, L"Shift") == 0)
            *out_mod |= MOD_SHIFT;
        else if (_wcsicmp(t, L"Win") == 0)
            *out_mod |= MOD_WIN;
        else
            return false; /* Unknown modifier */
    }

    /* Last token is the key */
    wchar_t *key = tokens[count - 1];
    if (wcslen(key) == 1) {
        wchar_t ch = towupper(key[0]);
        if (ch >= L'A' && ch <= L'Z')
            *out_vk = (UINT)ch;
        else if (ch >= L'0' && ch <= L'9')
            *out_vk = (UINT)ch;
        else
            return false;
    } else if (_wcsicmp(key, L"Space") == 0) *out_vk = VK_SPACE;
    else if (_wcsicmp(key, L"Tab") == 0) *out_vk = VK_TAB;
    else if (_wcsicmp(key, L"Enter") == 0 || _wcsicmp(key, L"Return") == 0) *out_vk = VK_RETURN;
    else if (_wcsicmp(key, L"Esc") == 0 || _wcsicmp(key, L"Escape") == 0) *out_vk = VK_ESCAPE;
    else if (_wcsicmp(key, L"Insert") == 0) *out_vk = VK_INSERT;
    else if (_wcsicmp(key, L"Delete") == 0) *out_vk = VK_DELETE;
    else if (_wcsicmp(key, L"Home") == 0) *out_vk = VK_HOME;
    else if (_wcsicmp(key, L"End") == 0) *out_vk = VK_END;
    else if (_wcsicmp(key, L"PageUp") == 0) *out_vk = VK_PRIOR;
    else if (_wcsicmp(key, L"PageDown") == 0) *out_vk = VK_NEXT;
    else if (_wcsicmp(key, L"F1") == 0) *out_vk = VK_F1;
    else if (_wcsicmp(key, L"F2") == 0) *out_vk = VK_F2;
    else if (_wcsicmp(key, L"F3") == 0) *out_vk = VK_F3;
    else if (_wcsicmp(key, L"F4") == 0) *out_vk = VK_F4;
    else if (_wcsicmp(key, L"F5") == 0) *out_vk = VK_F5;
    else if (_wcsicmp(key, L"F6") == 0) *out_vk = VK_F6;
    else if (_wcsicmp(key, L"F7") == 0) *out_vk = VK_F7;
    else if (_wcsicmp(key, L"F8") == 0) *out_vk = VK_F8;
    else if (_wcsicmp(key, L"F9") == 0) *out_vk = VK_F9;
    else if (_wcsicmp(key, L"F10") == 0) *out_vk = VK_F10;
    else if (_wcsicmp(key, L"F11") == 0) *out_vk = VK_F11;
    else if (_wcsicmp(key, L"F12") == 0) *out_vk = VK_F12;
    else return false;

    return (*out_mod != 0 && *out_vk != 0);
}

void hotkey_to_string(UINT mod, UINT vk, wchar_t *out, size_t len) {
    (void)len;
    out[0] = L'\0';
    size_t pos = 0;

    if (mod & MOD_CONTROL) {
        wcscpy(out + pos, L"Ctrl+"); pos += 5;
    }
    if (mod & MOD_ALT) {
        wcscpy(out + pos, L"Alt+"); pos += 4;
    }
    if (mod & MOD_SHIFT) {
        wcscpy(out + pos, L"Shift+"); pos += 6;
    }
    if (mod & MOD_WIN) {
        wcscpy(out + pos, L"Win+"); pos += 4;
    }

    if (vk >= 'A' && vk <= 'Z') {
        out[pos++] = (wchar_t)vk;
        out[pos] = L'\0';
    } else if (vk >= '0' && vk <= '9') {
        out[pos++] = (wchar_t)vk;
        out[pos] = L'\0';
    } else if (vk == VK_SPACE)    wcscpy(out + pos, L"Space");
    else if (vk == VK_TAB)       wcscpy(out + pos, L"Tab");
    else if (vk == VK_RETURN)    wcscpy(out + pos, L"Enter");
    else if (vk == VK_ESCAPE)    wcscpy(out + pos, L"Esc");
    else if (vk == VK_INSERT)    wcscpy(out + pos, L"Insert");
    else if (vk == VK_DELETE)    wcscpy(out + pos, L"Delete");
    else if (vk == VK_HOME)      wcscpy(out + pos, L"Home");
    else if (vk == VK_END)       wcscpy(out + pos, L"End");
    else if (vk == VK_PRIOR)     wcscpy(out + pos, L"PageUp");
    else if (vk == VK_NEXT)      wcscpy(out + pos, L"PageDown");
    else if (vk >= VK_F1 && vk <= VK_F12) {
        wsprintfW(out + pos, L"F%d", vk - VK_F1 + 1);
    } else {
        wsprintfW(out + pos, L"VK_%d", vk);
    }
}
