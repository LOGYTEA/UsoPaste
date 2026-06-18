#include "app.h"

static const wchar_t *RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

void config_load(Config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->window_w = DEFAULT_POPUP_W;
    cfg->window_h = DEFAULT_POPUP_H;
    cfg->max_items = MAX_ITEMS;
    cfg->hotkey_modifiers = MOD_CONTROL | MOD_ALT;
    cfg->hotkey_vk = 'V';
    wcscpy(cfg->hotkey_str, L"Ctrl+Alt+V");

    wchar_t ini[MAX_PATH];
    utils_get_appdata_path(ini, L"config.ini");

    cfg->auto_start = (bool)GetPrivateProfileIntW(L"General", L"AutoStart", 0, ini);
    cfg->retention_days = GetPrivateProfileIntW(L"General", L"RetentionDays", 0, ini);
    cfg->max_items = GetPrivateProfileIntW(L"General", L"MaxItems", MAX_ITEMS, ini);
    cfg->is_pinned = (bool)GetPrivateProfileIntW(L"General", L"IsPinned", 0, ini);
    cfg->window_x = GetPrivateProfileIntW(L"Window", L"X", -1, ini);
    cfg->window_y = GetPrivateProfileIntW(L"Window", L"Y", -1, ini);
    cfg->window_w = GetPrivateProfileIntW(L"Window", L"W", DEFAULT_POPUP_W, ini);
    cfg->window_h = GetPrivateProfileIntW(L"Window", L"H", DEFAULT_POPUP_H, ini);

    UINT mod = (UINT)GetPrivateProfileIntW(L"Hotkey", L"Modifiers", MOD_CONTROL | MOD_ALT, ini);
    UINT vk  = (UINT)GetPrivateProfileIntW(L"Hotkey", L"VK", 'V', ini);
    cfg->hotkey_modifiers = mod;
    cfg->hotkey_vk = vk;
    hotkey_to_string(mod, vk, cfg->hotkey_str, 64);
}

void config_save(const Config *cfg) {
    wchar_t ini[MAX_PATH];
    utils_get_appdata_path(ini, L"config.ini");

    wchar_t buf[32];
    WritePrivateProfileStringW(L"General", L"AutoStart",
        cfg->auto_start ? L"1" : L"0", ini);

    wsprintfW(buf, L"%d", cfg->retention_days);
    WritePrivateProfileStringW(L"General", L"RetentionDays", buf, ini);

    wsprintfW(buf, L"%d", cfg->max_items);
    WritePrivateProfileStringW(L"General", L"MaxItems", buf, ini);

    WritePrivateProfileStringW(L"General", L"IsPinned",
        cfg->is_pinned ? L"1" : L"0", ini);

    wsprintfW(buf, L"%d", cfg->window_x);
    WritePrivateProfileStringW(L"Window", L"X", buf, ini);
    wsprintfW(buf, L"%d", cfg->window_y);
    WritePrivateProfileStringW(L"Window", L"Y", buf, ini);
    wsprintfW(buf, L"%d", cfg->window_w);
    WritePrivateProfileStringW(L"Window", L"W", buf, ini);
    wsprintfW(buf, L"%d", cfg->window_h);
    WritePrivateProfileStringW(L"Window", L"H", buf, ini);

    wsprintfW(buf, L"%u", cfg->hotkey_modifiers);
    WritePrivateProfileStringW(L"Hotkey", L"Modifiers", buf, ini);
    wsprintfW(buf, L"%u", cfg->hotkey_vk);
    WritePrivateProfileStringW(L"Hotkey", L"VK", buf, ini);
}

bool config_set_autostart(bool enable) {
    HKEY hkey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0,
                      KEY_SET_VALUE | KEY_QUERY_VALUE, &hkey) != ERROR_SUCCESS)
        return false;

    bool ok = false;
    if (enable) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        ok = (RegSetValueExW(hkey, APP_NAME, 0, REG_SZ,
            (const BYTE *)exe, (DWORD)((wcslen(exe) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
    } else {
        RegDeleteValueW(hkey, APP_NAME);
        ok = true;
    }
    RegCloseKey(hkey);
    return ok;
}

bool config_get_autostart(void) {
    HKEY hkey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0,
                      KEY_QUERY_VALUE, &hkey) != ERROR_SUCCESS)
        return false;
    bool exists = (RegQueryValueExW(hkey, APP_NAME, NULL, NULL, NULL, NULL) == ERROR_SUCCESS);
    RegCloseKey(hkey);
    return exists;
}
