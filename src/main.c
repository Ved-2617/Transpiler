/*
 * ================================================================
 *  main.c  —  ENTRY POINT
 *
 *  stdin protocol (identical to the single-file version):
 *    Line 1 : language tag  —  python | c | cpp | java | custom
 *    Lines 2+: actual source code
 *
 *  Pipeline:
 *    1. Read all of stdin into raw[]
 *    2. Split off the language tag (first line) → srcLang
 *    3. Lex the source body → toks[], tokCount
 *    4. Parse with the language-specific front-end → ir[], irCount
 *    5. Serialize everything to JSON on stdout → outputJSON()
 *
 *  Error handling:
 *    Any phase can call addErr().  Errors are collected in errs[]
 *    and emitted inside the JSON "errors" array.  The process always
 *    exits with code 0 so the Python server can always parse the
 *    JSON response; a non-zero exit is treated as a hard crash.
 * ================================================================
 */
#include <stdio.h>
#include <string.h>
#include "xpile.h"
#include "globals.h"
#include "util.h"
#include "lexer.h"
#include "parser.h"
#include "output.h"

int main(void) {
    /* ── 1. Read stdin ───────────────────────────────────────────── */
    static char raw[MAX_SOURCE];
    int n = (int)fread(raw, 1, MAX_SOURCE - 1, stdin);
    raw[n] = '\0';

    if (n == 0) {
        printf("{\"tokens\":[],\"ir\":[],"
               "\"python\":\"\",\"c\":\"\",\"cpp\":\"\",\"java\":\"\","
               "\"srclang\":\"?\",\"errors\":[\"Empty input\"]}\n");
        return 0;
    }

    /* ── 2. Extract language tag from first line ─────────────────── */
    const char *src = raw;
    char *nl = strchr(raw, '\n');
    if (nl) {
        int tl = (int)(nl - raw);
        if (tl > 14) tl = 14;
        strncpy(srcLang, raw, tl);
        srcLang[tl] = '\0';
        /* trim Windows-style \r if present */
        if (tl > 0 && srcLang[tl-1] == '\r') srcLang[tl-1] = '\0';
        src = nl + 1;
    }

    /* ── 3. Lex ──────────────────────────────────────────────────── */
    lexAll(src);

    /* ── 4. Parse ────────────────────────────────────────────────── */
    if      (!strcmp(srcLang, "c"))    parseC();
    else if (!strcmp(srcLang, "cpp"))  parseCpp();
    else if (!strcmp(srcLang, "java")) parseJava();
    else                               parsePython(); /* python / custom */

    /* ── 5. Serialize ────────────────────────────────────────────── */
    outputJSON();
    return 0;
}
