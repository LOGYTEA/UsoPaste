#include "app.h"

#define SCHEMA_SQL \
    "CREATE TABLE IF NOT EXISTS clip_items (" \
    "  id          INTEGER PRIMARY KEY AUTOINCREMENT," \
    "  type        INTEGER NOT NULL," \
    "  content     TEXT," \
    "  file_path   TEXT," \
    "  preview     TEXT," \
    "  timestamp   INTEGER NOT NULL," \
    "  is_favorite INTEGER DEFAULT 0," \
    "  data_hash   TEXT NOT NULL UNIQUE," \
    "  file_size   INTEGER DEFAULT 0" \
    ");" \
    "CREATE INDEX IF NOT EXISTS idx_ts ON clip_items(timestamp DESC);" \
    "CREATE INDEX IF NOT EXISTS idx_hash ON clip_items(data_hash);"

static sqlite3_stmt *stmt_insert = NULL;
static sqlite3_stmt *stmt_update_ts = NULL;

static char *w_to_utf8(const wchar_t *w) {
    if (!w) return NULL;
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *buf = (char *)malloc(len + 1);
    if (buf) WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, len, NULL, NULL);
    return buf;
}

static wchar_t *utf8_to_w(const char *utf8) {
    if (!utf8) return NULL;
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    wchar_t *buf = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (buf) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, buf, len);
    return buf;
}

int db_open(const wchar_t *path) {
    char *utf8_path = w_to_utf8(path);
    if (!utf8_path) return -1;
    int rc = sqlite3_open(utf8_path, &g_app.db);
    free(utf8_path);
    if (rc != SQLITE_OK) return rc;

    sqlite3_exec(g_app.db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(g_app.db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);

    char *errmsg = NULL;
    rc = sqlite3_exec(g_app.db, SCHEMA_SQL, NULL, NULL, &errmsg);
    if (errmsg) { sqlite3_free(errmsg); }

    /* Prepare commonly-used statements */
    sqlite3_prepare_v2(g_app.db,
        "INSERT OR IGNORE INTO clip_items (type,content,file_path,preview,timestamp,is_favorite,data_hash,file_size) "
        "VALUES (?,?,?,?,?,?,?,?);", -1, &stmt_insert, NULL);
    sqlite3_prepare_v2(g_app.db,
        "UPDATE clip_items SET timestamp=? WHERE id=?;", -1, &stmt_update_ts, NULL);

    return rc;
}

void db_close(void) {
    if (stmt_insert) { sqlite3_finalize(stmt_insert); stmt_insert = NULL; }
    if (stmt_update_ts) { sqlite3_finalize(stmt_update_ts); stmt_update_ts = NULL; }
    if (g_app.db) { sqlite3_close(g_app.db); g_app.db = NULL; }
}

int db_insert_item(ClipItem *item) {
    if (!g_app.db || !stmt_insert) return -1;

    sqlite3_reset(stmt_insert);
    sqlite3_bind_int(stmt_insert, 1, (int)item->type);

    char *u_content = w_to_utf8(item->content);
    char *u_filepath = w_to_utf8(item->file_path);
    char *u_preview = w_to_utf8(item->preview);

    sqlite3_bind_text(stmt_insert, 2, u_content, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt_insert, 3, u_filepath, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt_insert, 4, u_preview, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt_insert, 5, item->timestamp);
    sqlite3_bind_int(stmt_insert, 6, item->is_favorite);
    sqlite3_bind_text(stmt_insert, 7, item->data_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt_insert, 8, item->file_size);

    int rc = sqlite3_step(stmt_insert);

    free(u_content);
    free(u_filepath);
    free(u_preview);

    if (rc == SQLITE_DONE) {
        item->id = sqlite3_last_insert_rowid(g_app.db);
        return 0;
    }
    return -1;
}

int db_update_timestamp(int64_t id, int64_t new_ts) {
    if (!g_app.db || !stmt_update_ts) return -1;
    sqlite3_reset(stmt_update_ts);
    sqlite3_bind_int64(stmt_update_ts, 1, new_ts);
    sqlite3_bind_int64(stmt_update_ts, 2, id);
    int rc = sqlite3_step(stmt_update_ts);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

/* Load items from DB into g_app.items, appending to existing. Returns count loaded. */
int db_load_items(int offset, int limit) {
    if (!g_app.db) return 0;

    char sql[256];
    wsprintfA(sql, "SELECT id,type,content,file_path,preview,timestamp,is_favorite,data_hash,file_size "
                   "FROM clip_items ORDER BY timestamp DESC LIMIT %d OFFSET %d;", limit, offset);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_app.db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && g_app.item_count < g_app.item_capacity) {
        ClipItem *it = &g_app.items[g_app.item_count];
        memset(it, 0, sizeof(*it));

        it->id = sqlite3_column_int64(stmt, 0);
        it->type = (ClipType)sqlite3_column_int(stmt, 1);

        const char *c = (const char *)sqlite3_column_text(stmt, 2);
        it->content = utf8_to_w(c);

        c = (const char *)sqlite3_column_text(stmt, 3);
        it->file_path = utf8_to_w(c);

        c = (const char *)sqlite3_column_text(stmt, 4);
        it->preview = utf8_to_w(c);

        it->timestamp = sqlite3_column_int64(stmt, 5);
        it->is_favorite = sqlite3_column_int(stmt, 6);

        c = (const char *)sqlite3_column_text(stmt, 7);
        if (c) strncpy(it->data_hash, c, 64);
        it->data_hash[64] = '\0';

        it->file_size = sqlite3_column_int64(stmt, 8);
        it->thumb_cache = NULL;

        g_app.item_count++;
        count++;
    }
    sqlite3_finalize(stmt);
    return count;
}

/* Load filtered items by type/search. Rebuilds g_app.filtered array. Returns count. */
int db_load_filtered(ClipType filter_type, bool favorites_only, const wchar_t *search) {
    if (!g_app.db) return 0;

    /* Build WHERE clause */
    char where[1024] = "WHERE 1=1";
    if (favorites_only) {
        strcat(where, " AND is_favorite=1");
    } else {
        switch (filter_type) {
            case CLIP_TEXT:  strcat(where, " AND type=0"); break;
            case CLIP_IMAGE: strcat(where, " AND type=1"); break;
            case CLIP_FILE:  strcat(where, " AND type=2"); break;
            default: break; /* TAB_ALL: no type filter */
        }
    }

    char *u_search = NULL;
    if (search && search[0]) {
        u_search = w_to_utf8(search);
        /* Escape single quotes */
        char safe[512] = " AND (content LIKE '%";
        strcat(safe, u_search);
        strcat(safe, "%' OR preview LIKE '%");
        strcat(safe, u_search);
        strcat(safe, "%' OR file_path LIKE '%");
        strcat(safe, u_search);
        strcat(safe, "%')");
        strcat(where, safe);
        free(u_search);
    }

    char sql[2048];
    wsprintfA(sql,
        "SELECT id,type,content,file_path,preview,timestamp,is_favorite,data_hash,file_size "
        "FROM clip_items %s ORDER BY timestamp DESC LIMIT %d;",
        where, MAX_ITEMS);

    /* Clear old items and reload */
    items_clear();

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_app.db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;

    while (sqlite3_step(stmt) == SQLITE_ROW && g_app.item_count < g_app.item_capacity) {
        ClipItem *it = &g_app.items[g_app.item_count];
        memset(it, 0, sizeof(*it));

        it->id = sqlite3_column_int64(stmt, 0);
        it->type = (ClipType)sqlite3_column_int(stmt, 1);

        const char *c = (const char *)sqlite3_column_text(stmt, 2);
        it->content = utf8_to_w(c);

        c = (const char *)sqlite3_column_text(stmt, 3);
        it->file_path = utf8_to_w(c);

        c = (const char *)sqlite3_column_text(stmt, 4);
        it->preview = utf8_to_w(c);

        it->timestamp = sqlite3_column_int64(stmt, 5);
        it->is_favorite = sqlite3_column_int(stmt, 6);

        c = (const char *)sqlite3_column_text(stmt, 7);
        if (c) strncpy(it->data_hash, c, 64);
        it->data_hash[64] = '\0';

        it->file_size = sqlite3_column_int64(stmt, 8);
        it->thumb_cache = NULL;

        g_app.item_count++;
    }
    sqlite3_finalize(stmt);

    /* Rebuild filtered indices */
    free(g_app.filtered);
    g_app.filtered = (int *)malloc(sizeof(int) * (size_t)g_app.item_count);
    g_app.filtered_count = g_app.item_count;
    for (int i = 0; i < g_app.item_count; i++)
        g_app.filtered[i] = i;

    return g_app.filtered_count;
}

void db_toggle_favorite(int64_t id) {
    if (!g_app.db) return;
    char sql[128];
    wsprintfA(sql, "UPDATE clip_items SET is_favorite = NOT is_favorite WHERE id=%lld;", (long long)id);
    sqlite3_exec(g_app.db, sql, NULL, NULL, NULL);
}

int db_delete_item(int64_t id) {
    if (!g_app.db) return -1;

    /* Get file path before deleting to clean up image files */
    char sql[128];
    wsprintfA(sql, "SELECT type, file_path FROM clip_items WHERE id=%lld;", (long long)id);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_app.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int type = sqlite3_column_int(stmt, 0);
            if (type == CLIP_IMAGE) {
                const char *fp = (const char *)sqlite3_column_text(stmt, 1);
                if (fp) {
                    wchar_t wpath[MAX_PATH];
                    MultiByteToWideChar(CP_UTF8, 0, fp, -1, wpath, MAX_PATH);
                    DeleteFileW(wpath);
                }
            }
        }
        sqlite3_finalize(stmt);
    }

    wsprintfA(sql, "DELETE FROM clip_items WHERE id=%lld;", (long long)id);
    return sqlite3_exec(g_app.db, sql, NULL, NULL, NULL);
}

int db_clear_all(void) {
    if (!g_app.db) return -1;

    /* Delete image files for non-favorite items */
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_app.db,
        "SELECT file_path FROM clip_items WHERE type=1 AND is_favorite=0;",
        -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *fp = (const char *)sqlite3_column_text(stmt, 0);
            if (fp) {
                wchar_t wpath[MAX_PATH];
                MultiByteToWideChar(CP_UTF8, 0, fp, -1, wpath, MAX_PATH);
                DeleteFileW(wpath);
            }
        }
        sqlite3_finalize(stmt);
    }

    return sqlite3_exec(g_app.db,
        "DELETE FROM clip_items WHERE is_favorite=0;", NULL, NULL, NULL);
}

int db_run_retention(int days) {
    if (!g_app.db || days <= 0) return 0;

    int64_t cutoff = utils_current_time_ms() - (int64_t)days * 86400000LL;

    /* Delete image files first */
    char sql[256];
    wsprintfA(sql,
        "SELECT file_path FROM clip_items WHERE type=1 AND is_favorite=0 AND timestamp < %lld;",
        (long long)cutoff);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_app.db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *fp = (const char *)sqlite3_column_text(stmt, 0);
            if (fp) {
                wchar_t wpath[MAX_PATH];
                MultiByteToWideChar(CP_UTF8, 0, fp, -1, wpath, MAX_PATH);
                DeleteFileW(wpath);
            }
        }
        sqlite3_finalize(stmt);
    }

    wsprintfA(sql,
        "DELETE FROM clip_items WHERE is_favorite=0 AND timestamp < %lld;",
        (long long)cutoff);
    return sqlite3_exec(g_app.db, sql, NULL, NULL, NULL);
}

int db_get_count(void) {
    if (!g_app.db) return 0;
    sqlite3_stmt *stmt = NULL;
    int count = 0;
    if (sqlite3_prepare_v2(g_app.db, "SELECT COUNT(*) FROM clip_items;", -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return count;
}
