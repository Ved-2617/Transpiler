/*
 * ================================================================
 *  util.c  —  STRING & DIAGNOSTIC UTILITY IMPLEMENTATIONS
 * ================================================================
 */
#include <string.h>
#include "xpile.h"
#include "globals.h"
#include "util.h"

/* ── addErr ─────────────────────────────────────────────────────── */
void addErr(const char *msg) {
    if (errCount < MAX_ERRORS)
        strncpy(errs[errCount++], msg, MAX_ERROR_LEN - 1);
}

/* ── jsonEsc ─────────────────────────────────────────────────────── */
void jsonEsc(const char *s, char *out, int len) {
    int j = 0;
    for (int i = 0; s[i] && j < len - 2; i++) {
        char c = s[i];
        if      (c == '"')  { out[j++] = '\\'; out[j++] = '"';  }
        else if (c == '\\') { out[j++] = '\\'; out[j++] = '\\'; }
        else if (c == '\n') { out[j++] = '\\'; out[j++] = 'n';  }
        else if (c == '\r') { out[j++] = '\\'; out[j++] = 'r';  }
        else if (c == '\t') { out[j++] = '\\'; out[j++] = 't';  }
        else                { out[j++] = c; }
    }
    out[j] = '\0';
}

/* ── app ────────────────────────────────────────────────────────── */
void app(char *buf, const char *s, int sz) {
    int cur = (int)strlen(buf), rem = sz - cur - 1;
    if (rem > 0) strncat(buf, s, rem);
}

/* ── irHasReturn ────────────────────────────────────────────────── */
int irHasReturn(void) {
    for (int i = 0; i < irCount; i++)
        if (ir[i].type == IR_RETURN) return 1;
    return 0;
}
