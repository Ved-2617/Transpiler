/*
 * ================================================================
 *  util.h  —  STRING & DIAGNOSTIC UTILITIES
 *
 *  Tiny helpers used by every phase:
 *    addErr    — append a message to the global error list
 *    jsonEsc   — escape a C string for JSON output
 *    app       — safe bounded strcat
 *    irHasReturn — scan IR for any IR_RETURN node
 * ================================================================
 */
#ifndef UTIL_H
#define UTIL_H

/* Append a human-readable error message to errs[]. */
void addErr(const char *msg);

/*
 * Write `s` into `out` (capacity `len`) with JSON-special characters
 * escaped: " → \", \ → \\, newline → \n, etc.
 */
void jsonEsc(const char *s, char *out, int len);

/*
 * Bounded string-append: strncat `s` onto `buf` (total capacity `sz`),
 * never writing past the end.
 */
void app(char *buf, const char *s, int sz);

/* Return 1 if the IR list contains at least one IR_RETURN node. */
int irHasReturn(void);

#endif /* UTIL_H */
