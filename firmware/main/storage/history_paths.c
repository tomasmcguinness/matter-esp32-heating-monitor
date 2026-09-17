#include "history_paths.h"

#include <stdio.h>
#include <string.h>

#include "history_format.h"
#include "time_sync.h"

bool history_sanitize_token(const char *tok, char *out, size_t out_len)
{
    if (!tok || !out) {
        return false;
    }

    size_t n = strlen(tok);
    if (n == 0 || n > 32 || n >= out_len) {
        return false;
    }

    for (size_t i = 0; i < n; i++) {
        char c  = tok[i];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
        out[i] = c;
    }
    out[n] = '\0';
    return true;
}

bool history_dir_for(uint8_t kind, uint8_t inst, char *out, size_t out_len)
{
    int written;

    switch (kind) {
    case KIND_ROOM:
        written = snprintf(out, out_len, "%s/room-%u", SD_CARD_MOUNT_POINT, (unsigned)inst);
        break;
    case KIND_RADIATOR:
        written = snprintf(out, out_len, "%s/rad-%u", SD_CARD_MOUNT_POINT, (unsigned)inst);
        break;
    default:
        // KIND_HOME lives directly on the mount point, and an unknown kind has no directory.
        return false;
    }

    return written > 0 && (size_t)written < out_len;
}

bool history_path_for_date(uint8_t kind, uint8_t inst, const char *date, char *out, size_t out_len)
{
    char safe_date[16];
    if (!history_sanitize_token(date, safe_date, sizeof(safe_date))) {
        return false;
    }

    if (kind == KIND_HOME) {
        int written = snprintf(out, out_len, "%s/home-%s", SD_CARD_MOUNT_POINT, safe_date);
        return written > 0 && (size_t)written < out_len;
    }

    char dir[HISTORY_PATH_MAX];
    if (!history_dir_for(kind, inst, dir, sizeof(dir))) {
        return false;
    }

    int written = snprintf(out, out_len, "%s/%s", dir, safe_date);
    return written > 0 && (size_t)written < out_len;
}

bool history_path_for(uint8_t kind, uint8_t inst, uint32_t base_ts, char *out, size_t out_len)
{
    char date[16];
    local_date_string(base_ts, date, sizeof(date));
    return history_path_for_date(kind, inst, date, out, out_len);
}
