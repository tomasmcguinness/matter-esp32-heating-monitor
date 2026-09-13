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

bool history_path_for_date(uint8_t kind, const char *date, char *out, size_t out_len)
{
    char safe_date[16];
    if (!history_sanitize_token(date, safe_date, sizeof(safe_date))) {
        return false;
    }

    if (kind != KIND_HOME) {
        return false;
    }

    int written = snprintf(out, out_len, "%s/home-%s", SD_CARD_MOUNT_POINT, safe_date);
    return written > 0 && (size_t)written < out_len;
}

bool history_path_for(uint8_t kind, uint32_t base_ts, char *out, size_t out_len)
{
    char date[16];
    local_date_string(base_ts, date, sizeof(date));
    return history_path_for_date(kind, date, out, out_len);
}
